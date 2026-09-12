#include "ptx_resolved_ir_private.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <string_view>
#include <type_traits>

#include <ptx_frontend/base/ptx_special_register.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

namespace ptx_frontend::resolved_ir {
using check_end::OperandPresence;
using check_end::OperandSyntaxShape;
using check_end::ResolvedFieldDescriptor;
using check_end::ResolvedInstructionDescriptor;
using check_end::ResolvedModifierBindingDescriptor;
using check_end::ResolvedModifierDefaultKind;
using check_end::ResolvedOperandBindingDescriptor;
using check_end::ResolvedOperandLayoutDescriptor;
using check_end::ResolvedValueKind;
using check_end::ResolvedVariantDescriptor;
using check_end::SyntaxInstructionDescriptor;
using check_end::SyntaxModifierDescriptor;
using check_end::SyntaxOperandLayoutDescriptor;
using check_end::SyntaxOperandSlotDescriptor;
using check_end::SyntaxVariantDescriptor;

namespace detail {
/** Test whether a generated mov-source shape admits the resolved shape. */
static bool allows_shape(checker::OperandShape allowed,
                         checker::OperandShape actual) {
  using Underlying = std::underlying_type_t<checker::OperandShape>;
  return (static_cast<Underlying>(allowed) & static_cast<Underlying>(actual)) !=
         0;
}

struct ParsedNumberedRegister {
  std::string_view prefix;
  uint32_t index;
};

std::expected<ParsedNumberedRegister, ResolveDiagnostic>
parse_numbered_register(const syntax_ast::AstIdentifierRef& identifier,
                        SourceRange diagnostic_range,
                        std::string_view expected_description) {
  const std::string_view spelling = identifier.syntax.text;
  size_t digit_begin = spelling.size();
  while (digit_begin > 0 &&
         std::isdigit(static_cast<unsigned char>(spelling[digit_begin - 1]))) {
    --digit_begin;
  }
  if (spelling.size() < 3 || spelling.front() != '%' ||
      digit_begin == spelling.size() || digit_begin == 1) {
    return std::unexpected(ResolveDiagnostic{
        .range = diagnostic_range,
        .message = fmt::format("Expected {}, got '{}'.", expected_description,
                               spelling),
    });
  }

  uint32_t index = 0;
  const char* first = spelling.data() + digit_begin;
  const char* last = spelling.data() + spelling.size();
  const auto [end, error] = std::from_chars(first, last, index);
  if (error != std::errc{} || end != last) {
    return std::unexpected(ResolveDiagnostic{
        .range = diagnostic_range,
        .message =
            fmt::format("Register '{}' has an invalid numeric ID.", spelling),
    });
  }
  return ParsedNumberedRegister{
      .prefix = spelling.substr(1, digit_begin - 1),
      .index = index,
  };
}

std::optional<uint32_t> numbered_register_index(std::string_view spelling) {
  size_t digit_begin = spelling.size();
  while (digit_begin > 0 &&
         std::isdigit(static_cast<unsigned char>(spelling[digit_begin - 1]))) {
    --digit_begin;
  }
  if (digit_begin == spelling.size())
    return std::nullopt;

  uint32_t index = 0;
  const auto [end, error] = std::from_chars(
      spelling.data() + digit_begin, spelling.data() + spelling.size(), index);
  if (error != std::errc{} || end != spelling.data() + spelling.size())
    return std::nullopt;
  return index;
}

std::expected<ResolvedRegisterRef, ResolveDiagnostic> resolve_bound_register(
    const syntax_ast::AstIdentifierRef& identifier,
    ResolvedRegisterClass register_class, const ResolveContext& context,
    SourceRange range,
    std::optional<uint8_t> required_vector_width = std::nullopt) {
  if (binding::isSpecialRegister(identifier.syntax.text)) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Special register '{}' is not supported by this resolved operand.",
            identifier.syntax.text),
    });
  }
  const auto lookup =
      context.symbols.lookup(context.scope, identifier.syntax.text);
  if (!lookup) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message =
            fmt::format("Unresolved register '{}'.", identifier.syntax.text),
    });
  }

  const binding::Symbol& symbol = context.symbols.symbol(lookup->symbol);
  const bool register_valued_role =
      symbol.kind == binding::SymbolKind::Variable ||
      symbol.kind == binding::SymbolKind::InputParameter ||
      symbol.kind == binding::SymbolKind::ReturnParameter;
  if (!register_valued_role ||
      symbol.state_space != syntax_ast::AstStateSpace::Register) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format("Symbol '{}' is not a .reg variable.",
                               identifier.syntax.text),
    });
  }
  if (!symbol.type) {
    throw ResolveException(fmt::format(
        "Register symbol '{}' has no declaration type.", symbol.name));
  }
  const auto declared_type = scalar_type_from_ptx_name(*symbol.type);
  if (!declared_type) {
    return std::unexpected(ResolveDiagnostic{
        .range = symbol.declaration_range,
        .message =
            fmt::format("Register '{}' has unsupported declared type '{}'.",
                        symbol.name, *symbol.type),
    });
  }

  const bool is_predicate = *declared_type == ScalarType::Pred;
  if (register_class == ResolvedRegisterClass::Predicate && !is_predicate) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Expected a predicate register, but '{}' is declared '{}'.",
            identifier.syntax.text, *symbol.type),
    });
  }
  if (register_class == ResolvedRegisterClass::General && is_predicate) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Expected a non-predicate register, but '{}' is declared '.pred'.",
            identifier.syntax.text),
    });
  }
  if (required_vector_width) {
    if (symbol.vector_width != required_vector_width) {
      return std::unexpected(ResolveDiagnostic{
          .range = range,
          .message =
              fmt::format("Expected a .reg .v{} register, but '{}' is not one.",
                          *required_vector_width, identifier.syntax.text),
      });
    }
  } else if (symbol.vector_width) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Vector register '{}' is not supported by this resolved operand.",
            identifier.syntax.text),
    });
  }

  return ResolvedRegisterRef{
      .spelling = identifier.syntax.text,
      .register_class = register_class,
      .index = numbered_register_index(identifier.syntax.text),
      .symbol_id = lookup->symbol,
      .parameterized_index = lookup->parameterized_index,
      .declared_type = *declared_type,
      .vector_width = symbol.vector_width,
  };
}

std::expected<WithLocs<ResolvedRegisterRef>, ResolveDiagnostic>
resolve_register(const syntax_ast::AstOperand& operand,
                 const ResolveContext* context) {
  const auto* identifier = std::get_if<syntax_ast::AstIdentifierRef>(&operand);
  if (identifier == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a register operand.",
    });
  }

  if (context != nullptr) {
    auto value =
        resolve_bound_register(*identifier, ResolvedRegisterClass::General,
                               *context, identifier->syntax.range);
    if (!value)
      return std::unexpected(value.error());
    return WithLocs<ResolvedRegisterRef>{std::move(*value),
                                         identifier->syntax.range};
  }

  const auto parsed = parse_numbered_register(
      *identifier, identifier->syntax.range, "a numbered register");
  if (!parsed)
    return std::unexpected(parsed.error());
  if (parsed->prefix == "p") {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier->syntax.range,
        .message = fmt::format("Expected a non-predicate register, got '{}'.",
                               identifier->syntax.text),
    });
  }
  return WithLocs<ResolvedRegisterRef>{
      ResolvedRegisterRef{
          .spelling = identifier->syntax.text,
          .register_class = ResolvedRegisterClass::General,
          .index = parsed->index,
      },
      identifier->syntax.range};
}

std::expected<WithLocs<ResolvedVectorRegisterRef>, ResolveDiagnostic>
resolve_vector_register(const syntax_ast::AstOperand& operand,
                        const ResolveContext* context) {
  const auto* identifier = std::get_if<syntax_ast::AstIdentifierRef>(&operand);
  if (identifier == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a vector register operand.",
    });
  }
  if (context == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier->syntax.range,
        .message = "A vector register operand requires a module declaration.",
    });
  }
  auto value =
      resolve_bound_register(*identifier, ResolvedRegisterClass::General,
                             *context, identifier->syntax.range, 4);
  if (!value)
    return std::unexpected(value.error());
  return WithLocs<ResolvedVectorRegisterRef>{
      ResolvedVectorRegisterRef{.register_ref = std::move(*value)},
      identifier->syntax.range};
}

std::expected<WithLocs<ResolvedPredicate>, ResolveDiagnostic>
resolve_predicate_identifier(const syntax_ast::AstIdentifierRef& identifier,
                             bool negated, SourceRange range,
                             const ResolveContext* context) {
  ResolvedRegisterRef register_ref{
      .register_class = ResolvedRegisterClass::Predicate,
  };
  if (context != nullptr) {
    auto value = resolve_bound_register(
        identifier, ResolvedRegisterClass::Predicate, *context, range);
    if (!value)
      return std::unexpected(value.error());
    register_ref = std::move(*value);
  } else {
    const auto parsed = parse_numbered_register(
        identifier, range, "a numbered predicate register");
    if (!parsed)
      return std::unexpected(parsed.error());
    if (parsed->prefix != "p") {
      return std::unexpected(ResolveDiagnostic{
          .range = range,
          .message = fmt::format("Expected a predicate register, got '{}'.",
                                 identifier.syntax.text),
      });
    }
    register_ref = ResolvedRegisterRef{
        .spelling = identifier.syntax.text,
        .register_class = ResolvedRegisterClass::Predicate,
        .index = parsed->index,
    };
  }

  return WithLocs<ResolvedPredicate>{
      ResolvedPredicate{.register_ref = std::move(register_ref),
                        .negated = negated},
      range};
}

std::expected<WithLocs<ResolvedPredicate>, ResolveDiagnostic> resolve_predicate(
    const syntax_ast::AstOperand& operand, const ResolveContext* context) {
  if (const auto* plain = std::get_if<syntax_ast::AstIdentifierRef>(&operand)) {
    return resolve_predicate_identifier(*plain, false, plain->syntax.range,
                                        context);
  }
  if (const auto* predicate =
          std::get_if<syntax_ast::AstPredicateOperand>(&operand)) {
    return resolve_predicate_identifier(predicate->name, predicate->negated,
                                        predicate->range, context);
  }
  return std::unexpected(ResolveDiagnostic{
      .range = syntax_ast::sourceRange(operand),
      .message = "Expected a predicate operand.",
  });
}

std::expected<WithLocs<ResolvedShflSyncDestination>, ResolveDiagnostic>
resolve_shfl_destination(const syntax_ast::AstOperand& operand,
                         bool allow_destination_sink, bool allow_predicate_sink,
                         const ResolveContext* context) {
  const auto* pair =
      std::get_if<syntax_ast::AstRegisterPredicatePair>(&operand);
  if (pair == nullptr) {
    return std::unexpected(
        ResolveDiagnostic{.range = syntax_ast::sourceRange(operand),
                          .message = "Expected d|p destination."});
  }
  std::optional<WithLoc<ResolvedRegisterRef>> data;
  if (pair->dst.syntax.text == "_") {
    if (!allow_destination_sink) {
      return std::unexpected(ResolveDiagnostic{
          .range = pair->dst.syntax.range,
          .message = "The '_' sink is not allowed in this d|p destination.",
      });
    }
  } else {
    auto resolved_data =
        resolve_register(syntax_ast::AstOperand{pair->dst}, context);
    if (!resolved_data)
      return std::unexpected(resolved_data.error());
    data = WithLoc<ResolvedRegisterRef>{std::move(resolved_data->value),
                                        pair->dst.syntax.range};
  }
  std::optional<WithLoc<ResolvedPredicate>> predicate;
  if (pair->predicate.syntax.text == "_") {
    if (!allow_predicate_sink) {
      return std::unexpected(ResolveDiagnostic{
          .range = pair->predicate.syntax.range,
          .message = "The '_' sink is not allowed as the predicate half of "
                     "this d|p destination.",
      });
    }
  } else {
    auto resolved_predicate = resolve_predicate_identifier(
        pair->predicate, false, pair->predicate.syntax.range, context);
    if (!resolved_predicate)
      return std::unexpected(resolved_predicate.error());
    predicate = WithLoc<ResolvedPredicate>{std::move(resolved_predicate->value),
                                           pair->predicate.syntax.range};
  }
  if (!data && !predicate) {
    return std::unexpected(ResolveDiagnostic{
        .range = pair->range,
        .message = "A d|p destination must retain a data or predicate output.",
    });
  }
  WithLocs<ResolvedShflSyncDestination> result{
      ResolvedShflSyncDestination{.data = std::move(data),
                                  .predicate = std::move(predicate)},
      pair->range};
  result.locs = {pair->dst.syntax.range, pair->predicate.syntax.range};
  return result;
}

std::expected<WithLocs<ResolvedPredicatePair>, ResolveDiagnostic>
resolve_predicate_pair(const syntax_ast::AstOperand& operand,
                       const ResolveContext* context) {
  const auto* pair =
      std::get_if<syntax_ast::AstRegisterPredicatePair>(&operand);
  if (pair == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a predicate-pair operand.",
    });
  }
  auto first = resolve_predicate_identifier(pair->dst, false,
                                            pair->dst.syntax.range, context);
  if (!first)
    return std::unexpected(first.error());
  auto second = resolve_predicate_identifier(
      pair->predicate, false, pair->predicate.syntax.range, context);
  if (!second)
    return std::unexpected(second.error());
  WithLocs<ResolvedPredicatePair> result{
      ResolvedPredicatePair{.first = std::move(first->value),
                            .second = std::move(second->value)},
      pair->range};
  result.locs = {pair->dst.syntax.range, pair->predicate.syntax.range};
  return result;
}

std::expected<WithLocs<ResolvedBranchTarget>, ResolveDiagnostic>
resolve_branch_target(const syntax_ast::AstOperand& operand,
                      const ResolveContext* context) {
  const auto* target = std::get_if<syntax_ast::AstBranchTarget>(&operand);
  if (target == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a direct branch target.",
    });
  }

  ResolvedBranchTarget resolved{.spelling = target->name.syntax.text};
  if (context != nullptr) {
    const binding::ScopeId function_scope =
        context->function_scope.value_or(context->scope);
    const auto lookup =
        context->symbols.lookup(context->scope, target->name.syntax.text);
    if (!lookup) {
      return std::unexpected(ResolveDiagnostic{
          .range = target->range,
          .message = fmt::format("Unresolved branch target '{}'.",
                                 target->name.syntax.text),
      });
    }
    const binding::Symbol& symbol = context->symbols.symbol(lookup->symbol);
    if (symbol.kind != binding::SymbolKind::Label ||
        symbol.scope != function_scope) {
      return std::unexpected(ResolveDiagnostic{
          .range = target->range,
          .message = fmt::format(
              "Branch target '{}' must name a label in the current function.",
              target->name.syntax.text),
      });
    }
    resolved.symbol_id = lookup->symbol;
  }

  return WithLocs<ResolvedBranchTarget>{std::move(resolved), target->range};
}

std::expected<WithLocs<ResolvedBranchTargetSet>, ResolveDiagnostic>
resolve_branch_target_set(const syntax_ast::AstOperand& operand,
                          const ResolveContext* context) {
  const auto* target_set =
      std::get_if<syntax_ast::AstBranchTargetSet>(&operand);
  if (target_set == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a brx.idx branch target list.",
    });
  }

  ResolvedBranchTargetSet resolved{.spelling = target_set->name.syntax.text};
  if (context != nullptr) {
    const binding::ScopeId function_scope =
        context->function_scope.value_or(context->scope);
    const auto lookup =
        context->symbols.lookup(context->scope, target_set->name.syntax.text);
    if (!lookup) {
      return std::unexpected(ResolveDiagnostic{
          .range = target_set->range,
          .message = fmt::format("Unresolved brx.idx branch target list '{}'.",
                                 target_set->name.syntax.text),
      });
    }
    const binding::Symbol& symbol = context->symbols.symbol(lookup->symbol);
    if (symbol.kind != binding::SymbolKind::BranchTargetSet ||
        symbol.scope != function_scope) {
      return std::unexpected(ResolveDiagnostic{
          .range = target_set->range,
          .message = fmt::format(
              "brx.idx branch target list '{}' must name a function-local "
              ".branchtargets declaration.",
              target_set->name.syntax.text),
      });
    }
    resolved.symbol_id = symbol.id;
  }
  return WithLocs<ResolvedBranchTargetSet>{std::move(resolved),
                                           target_set->range};
}

std::expected<ResolvedCallParameterRef, ResolveDiagnostic>
resolve_call_parameter(const syntax_ast::AstIdentifierRef& identifier,
                       const ResolveContext* context) {
  ResolvedCallParameterRef resolved{.spelling = identifier.syntax.text};
  if (context == nullptr)
    return resolved;

  const auto lookup =
      context->symbols.lookup(context->scope, identifier.syntax.text);
  if (!lookup) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier.syntax.range,
        .message = fmt::format("Unresolved call parameter '{}'.",
                               identifier.syntax.text),
    });
  }
  const binding::Symbol& symbol = context->symbols.symbol(lookup->symbol);
  const bool parameter_or_variable =
      symbol.kind == binding::SymbolKind::Variable ||
      symbol.kind == binding::SymbolKind::InputParameter ||
      symbol.kind == binding::SymbolKind::ReturnParameter ||
      symbol.kind == binding::SymbolKind::CallParameter;
  const bool allowed_space =
      symbol.state_space &&
      (*symbol.state_space == syntax_ast::AstStateSpace::Register ||
       *symbol.state_space == syntax_ast::AstStateSpace::Parameter);
  if (!parameter_or_variable || !allowed_space) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier.syntax.range,
        .message = fmt::format("Call parameter '{}' must name a .reg or "
                               ".param variable.",
                               identifier.syntax.text),
    });
  }
  if (*symbol.state_space == syntax_ast::AstStateSpace::Parameter &&
      (symbol.kind == binding::SymbolKind::InputParameter ||
       symbol.kind == binding::SymbolKind::ReturnParameter)) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier.syntax.range,
        .message = fmt::format(
            "Formal .param parameter '{}' cannot be used directly as a call "
            "argument; use a register or body-local .param object.",
            identifier.syntax.text),
    });
  }
  resolved.symbol_id = lookup->symbol;
  resolved.parameterized_index = lookup->parameterized_index;
  resolved.state_space = symbol.state_space;
  if (!symbol.type) {
    throw ResolveException(fmt::format(
        "Call parameter symbol '{}' has no declaration type.", symbol.name));
  }
  const auto declared_type = scalar_type_from_ptx_name(*symbol.type);
  if (!declared_type) {
    return std::unexpected(ResolveDiagnostic{
        .range = symbol.declaration_range,
        .message = fmt::format(
            "Call parameter '{}' has unsupported declared type '{}'.",
            symbol.name, *symbol.type),
    });
  }
  resolved.declared_type = *declared_type;
  return resolved;
}

std::expected<WithLocs<ResolvedFunctionRef>, ResolveDiagnostic>
resolve_direct_call_target(const syntax_ast::AstOperand& operand,
                           const ResolveContext* context) {
  const auto* target = std::get_if<syntax_ast::AstCallTarget>(&operand);
  if (target == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a direct call target.",
    });
  }

  ResolvedFunctionRef resolved{.spelling = target->name.syntax.text};
  if (context != nullptr) {
    const auto lookup =
        context->symbols.lookup(context->scope, target->name.syntax.text);
    if (!lookup) {
      return std::unexpected(ResolveDiagnostic{
          .range = target->range,
          .message = fmt::format("Unresolved call target '{}'.",
                                 target->name.syntax.text),
      });
    }
    const binding::Symbol& symbol = context->symbols.symbol(lookup->symbol);
    if (symbol.kind != binding::SymbolKind::Function) {
      const bool is_register =
          symbol.kind == binding::SymbolKind::Variable &&
          symbol.state_space == syntax_ast::AstStateSpace::Register;
      return std::unexpected(ResolveDiagnostic{
          .range = target->range,
          .message =
              is_register
                  ? "Indirect call register targets require a function-local "
                    ".callprototype or .calltargets metadata operand."
                  : fmt::format("Call target '{}' must name a function.",
                                target->name.syntax.text),
      });
    }
    if (symbol.function_is_entry) {
      return std::unexpected(ResolveDiagnostic{
          .range = target->range,
          .message = fmt::format("Direct call target '{}' must name a device "
                                 ".func, not an .entry.",
                                 target->name.syntax.text),
      });
    }
    resolved.symbol_id = symbol.id;
    resolved.is_entry = symbol.function_is_entry;
  }
  return WithLocs<ResolvedFunctionRef>{std::move(resolved), target->range};
}

std::expected<WithLocs<ResolvedIndirectCallee>, ResolveDiagnostic>
resolve_indirect_callee(const syntax_ast::AstOperand& operand,
                        const ResolveContext* context) {
  if (const auto* target = std::get_if<syntax_ast::AstCallTarget>(&operand)) {
    auto register_ref =
        resolve_register(syntax_ast::AstOperand{target->name}, context);
    if (!register_ref)
      return std::unexpected(register_ref.error());
    return WithLocs<ResolvedIndirectCallee>{
        ResolvedIndirectCallee{std::move(register_ref->value)}, target->range};
  }

  const auto* metadata = std::get_if<syntax_ast::AstCallTargetSet>(&operand);
  if (metadata == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message =
            "Expected an indirect call register target or metadata label.",
    });
  }

  ResolvedIndirectMetadataRef resolved{.spelling = metadata->name.syntax.text};
  if (context != nullptr) {
    const binding::ScopeId function_scope =
        context->function_scope.value_or(context->scope);
    const auto lookup =
        context->symbols.lookup(context->scope, metadata->name.syntax.text);
    if (!lookup) {
      return std::unexpected(ResolveDiagnostic{
          .range = metadata->range,
          .message = fmt::format("Unresolved indirect call metadata '{}'.",
                                 metadata->name.syntax.text),
      });
    }
    const binding::Symbol& symbol = context->symbols.symbol(lookup->symbol);
    if (lookup->parameterized_index ||
        symbol.kind == binding::SymbolKind::Variable) {
      return std::unexpected(ResolveDiagnostic{
          .range = metadata->range,
          .message = "Indirect call metadata variables and call-table arrays "
                     "are not supported.",
      });
    }
    if (symbol.scope != function_scope ||
        (symbol.kind != binding::SymbolKind::CallPrototype &&
         symbol.kind != binding::SymbolKind::CallTargetSet)) {
      return std::unexpected(ResolveDiagnostic{
          .range = metadata->range,
          .message = fmt::format(
              "Indirect call metadata '{}' must name a function-local "
              ".callprototype or .calltargets declaration.",
              metadata->name.syntax.text),
      });
    }
    resolved.symbol_id = symbol.id;
    resolved.declaration_kind = symbol.kind;
  }
  return WithLocs<ResolvedIndirectCallee>{
      ResolvedIndirectCallee{std::move(resolved)}, metadata->range};
}

std::expected<WithLocs<ResolvedCallParameterRef>, ResolveDiagnostic>
resolve_call_return_parameter(const syntax_ast::AstOperand& operand,
                              const ResolveContext* context) {
  const auto* group = std::get_if<syntax_ast::AstCallParameterList>(&operand);
  if (group == nullptr ||
      group->kind != syntax_ast::AstCallParameterListKind::Return ||
      group->parameters.size() != 1) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a single call return parameter.",
    });
  }
  const auto* identifier =
      std::get_if<syntax_ast::AstIdentifierRef>(&group->parameters.front());
  if (identifier == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = group->range,
        .message = "A call return parameter must be a .reg or .param variable.",
    });
  }
  auto resolved = resolve_call_parameter(*identifier, context);
  if (!resolved)
    return std::unexpected(resolved.error());
  return WithLocs<ResolvedCallParameterRef>{std::move(*resolved), group->range};
}

std::expected<WithLocs<ResolvedCallArguments>, ResolveDiagnostic>
resolve_call_arguments(const syntax_ast::AstOperand& operand,
                       const ResolveContext* context) {
  const auto* group = std::get_if<syntax_ast::AstCallParameterList>(&operand);
  if (group == nullptr ||
      group->kind != syntax_ast::AstCallParameterListKind::Input) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a call input parameter group.",
    });
  }

  ResolvedCallArguments resolved;
  resolved.values.reserve(group->parameters.size());
  for (const auto& parameter : group->parameters) {
    if (const auto* identifier =
            std::get_if<syntax_ast::AstIdentifierRef>(&parameter)) {
      auto value = resolve_call_parameter(*identifier, context);
      if (!value)
        return std::unexpected(value.error());
      resolved.values.emplace_back(ResolvedCallArgument{std::move(*value)},
                                   identifier->syntax.range);
      continue;
    }
    const auto& immediate = std::get<syntax_ast::AstImmediate>(parameter);
    resolved.values.emplace_back(
        ResolvedCallArgument{ResolvedCallLiteral{
            .spelling = immediate.syntax.text, .kind = immediate.kind}},
        immediate.syntax.range);
  }
  return WithLocs<ResolvedCallArguments>{std::move(resolved), group->range};
}

std::expected<WithLocs<ResolvedSpecialRegisterRef>, ResolveDiagnostic>
resolve_special_register(const syntax_ast::AstOperand& operand) {
  std::string spelling;
  SourceRange range;
  std::optional<base::VectorComponent> component;
  if (const auto* identifier =
          std::get_if<syntax_ast::AstIdentifierRef>(&operand)) {
    spelling = identifier->syntax.text;
    range = identifier->syntax.range;
  } else if (const auto* member =
                 std::get_if<syntax_ast::AstVectorMember>(&operand)) {
    spelling = member->base.syntax.text + member->selector.text;
    range = member->range;
    switch (member->selector.text.back()) {
      case 'x':
        component = base::VectorComponent::X;
        break;
      case 'y':
        component = base::VectorComponent::Y;
        break;
      case 'z':
        component = base::VectorComponent::Z;
        break;
      default:
        break;
    }
  } else {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a special-register operand.",
    });
  }

  auto info = base::lookup(spelling);
  if (!info) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format("Unknown special register '{}'.", spelling),
    });
  }
  if (info->vector_width != 1) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Special register '{}' is a vector; select a scalar component.",
            spelling),
    });
  }
  return WithLocs<ResolvedSpecialRegisterRef>{
      ResolvedSpecialRegisterRef{.spelling = std::move(spelling),
                                 .id = info->id,
                                 .component = component},
      range};
}

std::expected<WithLocs<ResolvedPredicateSource>, ResolveDiagnostic>
resolve_predicate_source(const syntax_ast::AstOperand& operand,
                         const ResolveContext* context) {
  const auto range = syntax_ast::sourceRange(operand);
  if (std::holds_alternative<syntax_ast::AstIdentifierRef>(operand) ||
      std::holds_alternative<syntax_ast::AstVectorMember>(operand)) {
    auto special = resolve_special_register(operand);
    if (special) {
      if (base::metadata(special->value.id).element_type !=
          base::ScalarType::Pred) {
        return std::unexpected(ResolveDiagnostic{
            .range = range,
            .message =
                fmt::format("Expected a predicate special register, got '{}'.",
                            special->value.spelling),
        });
      }
      return WithLocs<ResolvedPredicateSource>{
          ResolvedPredicateSource{std::move(special->value)}, range};
    }
    const auto* identifier =
        std::get_if<syntax_ast::AstIdentifierRef>(&operand);
    if (identifier == nullptr || base::lookup(identifier->syntax.text))
      return std::unexpected(special.error());
  }
  auto predicate = resolve_predicate(operand, context);
  if (!predicate)
    return std::unexpected(predicate.error());
  return WithLocs<ResolvedPredicateSource>{
      ResolvedPredicateSource{std::move(predicate->value)}, range};
}

std::expected<WithLocs<ResolvedVectorSpecialRegisterRef>, ResolveDiagnostic>
resolve_vector_special_register(const syntax_ast::AstOperand& operand) {
  const auto* identifier = std::get_if<syntax_ast::AstIdentifierRef>(&operand);
  if (identifier == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a vector special-register base operand.",
    });
  }
  const auto info = base::lookup(identifier->syntax.text);
  if (!info || info->vector_width != 4) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier->syntax.range,
        .message =
            fmt::format("Expected a four-component special register, got '{}'.",
                        identifier->syntax.text),
    });
  }
  return WithLocs<ResolvedVectorSpecialRegisterRef>{
      ResolvedVectorSpecialRegisterRef{.spelling = identifier->syntax.text,
                                       .id = info->id},
      identifier->syntax.range};
}

/** Select whether an address consumer imposes memory-offset source limits. */
enum class AddressImmediateDomain : uint8_t { General, MemoryOperand };

/**
 * Resolve an address offset while retaining its signed-64-bit IR magnitude.
 *
 * Bracketed memory addresses additionally constrain the effective signed
 * offset, after combining the address operator with a lexical literal sign,
 * to the PTX signed-32-bit source domain.  Other consumers retain their
 * wider relocation-addend contract.
 */
std::expected<std::optional<ResolvedAddressOffset>, ResolveDiagnostic>
resolve_address_offset(
    const syntax_ast::AstAddress& address,
    AddressImmediateDomain domain = AddressImmediateDomain::General) {
  if (!address.offset)
    return std::nullopt;

  const bool memory_operand = domain == AddressImmediateDomain::MemoryOperand;
  auto value =
      resolve_immediate_value(address.offset->magnitude, ScalarType::S64, true);
  if (!value)
    return std::unexpected(value.error());
  const bool subtract = address.offset->operation ==
                        syntax_ast::AstAddressOffset::Operator::Subtract;
  if (memory_operand) {
    const uint64_t source_bits =
        value->integer_source_bits.value_or(value->bits);
    const uint64_t magnitude =
        value->is_negative ? uint64_t{0} - source_bits : source_bits;
    const bool effective_negative = subtract != value->is_negative;
    const uint64_t maximum_magnitude =
        effective_negative ? uint64_t{1} << 31 : (uint64_t{1} << 31) - 1;
    if (magnitude > maximum_magnitude) {
      return std::unexpected(ResolveDiagnostic{
          .range = address.offset->magnitude.syntax.range,
          .message = fmt::format(
              "Address offset magnitude '{}' is outside the signed 32-bit "
              "range for its '{}' operator.",
              address.offset->magnitude.syntax.text,
              effective_negative ? "-" : "+"),
      });
    }
    // ResolvedAddressOffset retains the spelling's operation separately, so
    // retain its magnitude representation while validating the signed PTX domain.
  }
  return ResolvedAddressOffset{
      .operation = subtract ? ResolvedAddressOffsetOperator::Subtract
                            : ResolvedAddressOffsetOperator::Add,
      .value = std::move(*value),
  };
}

/** Return whether a declared register can hold a PTX address value. */
bool is_address_register_type(ScalarType type) {
  const auto kind = scalar_kind(type);
  return (kind == base::ScalarKind::Unsigned ||
          kind == base::ScalarKind::Signed || kind == base::ScalarKind::Bit) &&
         scalar_size_of(type) <= sizeof(uint64_t);
}

/** Reject a bound address base whose declaration cannot represent an address. */
std::expected<void, ResolveDiagnostic> check_address_register_type(
    const ResolvedRegisterRef& register_ref, SourceRange range) {
  if (register_ref.declared_type &&
      is_address_register_type(*register_ref.declared_type))
    return {};
  return std::unexpected(ResolveDiagnostic{
      .range = range,
      .message = fmt::format(
          "Address register '{}' has invalid declared type '{}'; expected an "
          "integer or bit-size type no wider than 64 bits.",
          register_ref.spelling,
          register_ref.declared_type ? to_string(*register_ref.declared_type)
                                     : "unknown"),
  });
}

enum class FormalParameterAddressPolicy : uint8_t {
  Reject,
  PreserveParameterSpace,
  MaterializeDeviceParameter,
};

bool is_addressable_data_symbol(const binding::Symbol& symbol,
                                FormalParameterAddressPolicy parameter_policy) {
  if (symbol.kind == binding::SymbolKind::Variable) {
    return symbol.state_space == syntax_ast::AstStateSpace::Local ||
           symbol.state_space == syntax_ast::AstStateSpace::Shared ||
           symbol.state_space == syntax_ast::AstStateSpace::Global ||
           symbol.state_space == syntax_ast::AstStateSpace::Constant;
  }
  return parameter_policy != FormalParameterAddressPolicy::Reject &&
         (symbol.kind == binding::SymbolKind::InputParameter ||
          symbol.kind == binding::SymbolKind::ReturnParameter ||
          (parameter_policy ==
               FormalParameterAddressPolicy::PreserveParameterSpace &&
           symbol.kind == binding::SymbolKind::CallParameter)) &&
         symbol.state_space == syntax_ast::AstStateSpace::Parameter;
}

std::expected<ResolvedSymbolRef, ResolveDiagnostic> resolve_data_symbol(
    const syntax_ast::AstIdentifierRef& identifier,
    const ResolveContext* context,
    FormalParameterAddressPolicy parameter_policy) {
  ResolvedSymbolRef resolved{.spelling = identifier.syntax.text};
  if (context == nullptr)
    return resolved;

  if (binding::isSpecialRegister(identifier.syntax.text)) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier.syntax.range,
        .message = fmt::format("Special register '{}' is not a data symbol.",
                               identifier.syntax.text),
    });
  }
  const auto lookup =
      context->symbols.lookup(context->scope, identifier.syntax.text);
  if (!lookup) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier.syntax.range,
        .message =
            fmt::format("Unresolved data symbol '{}'.", identifier.syntax.text),
    });
  }
  const binding::Symbol& symbol = context->symbols.symbol(lookup->symbol);
  if (!is_addressable_data_symbol(symbol, parameter_policy)) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier.syntax.range,
        .message = fmt::format("Symbol '{}' is not an addressable data symbol.",
                               identifier.syntax.text),
    });
  }

  resolved.symbol_id = lookup->symbol;
  resolved.parameterized_index = lookup->parameterized_index;
  resolved.declaration_kind = symbol.kind;
  resolved.declaration_state_space = symbol.state_space;
  resolved.address_state_space = symbol.state_space;
  if (symbol.kind == binding::SymbolKind::InputParameter ||
      symbol.kind == binding::SymbolKind::ReturnParameter) {
    // A direct formal-parameter memory address stays in .param.  Only mov
    // address-taking materializes a device-function parameter on the stack and
    // consequently changes the produced address to .local.
    if (parameter_policy ==
            FormalParameterAddressPolicy::MaterializeDeviceParameter &&
        !context->function_is_entry) {
      resolved.address_state_space = syntax_ast::AstStateSpace::Local;
      // Device-function parameters require PTX 2.0 and sm_20.  PTX raised the
      // minimum for taking a return-parameter address to 6.0.
      resolved.address_availability = checker::AvailabilityDescriptor{
          .minimum_ptx_version =
              symbol.kind == binding::SymbolKind::ReturnParameter
                  ? checker::PtxVersion{6, 0}
                  : checker::PtxVersion{2, 0},
          .minimum_sm_version = 20,
      };
    }
  }
  if (symbol.type)
    resolved.declared_type = scalar_type_from_ptx_name(*symbol.type);
  resolved.address_alignment = symbol.address_alignment;
  return resolved;
}

std::expected<WithLocs<ResolvedSymbolRef>, ResolveDiagnostic> resolve_symbol(
    const syntax_ast::AstOperand& operand, const ResolveContext* context) {
  const auto* identifier = std::get_if<syntax_ast::AstIdentifierRef>(&operand);
  if (identifier == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a data-symbol operand.",
    });
  }
  auto resolved = resolve_data_symbol(*identifier, context,
                                      FormalParameterAddressPolicy::Reject);
  if (!resolved)
    return std::unexpected(resolved.error());
  return WithLocs<ResolvedSymbolRef>{std::move(*resolved),
                                     identifier->syntax.range};
}

std::expected<WithLocs<ResolvedAddress>, ResolveDiagnostic> resolve_address(
    const syntax_ast::AstOperand& operand, const ResolveContext* context) {
  const auto* address = std::get_if<syntax_ast::AstAddress>(&operand);
  if (address == nullptr || !address->bracketed) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a bracketed address operand.",
    });
  }

  std::optional<ResolvedAddressBase> base;
  if (const auto* identifier =
          std::get_if<syntax_ast::AstIdentifierRef>(&address->base)) {
    if (context != nullptr) {
      const auto lookup =
          context->symbols.lookup(context->scope, identifier->syntax.text);
      if (lookup &&
          context->symbols.symbol(lookup->symbol).kind ==
              binding::SymbolKind::Variable &&
          context->symbols.symbol(lookup->symbol).state_space ==
              syntax_ast::AstStateSpace::Register) {
        auto register_ref =
            resolve_bound_register(*identifier, ResolvedRegisterClass::General,
                                   *context, identifier->syntax.range);
        if (!register_ref)
          return std::unexpected(register_ref.error());
        if (auto type_check = check_address_register_type(
                *register_ref, identifier->syntax.range);
            !type_check)
          return std::unexpected(type_check.error());
        base = std::move(*register_ref);
      } else {
        auto symbol = resolve_data_symbol(
            *identifier, context,
            FormalParameterAddressPolicy::PreserveParameterSpace);
        if (!symbol)
          return std::unexpected(symbol.error());
        base = std::move(*symbol);
      }
    } else if (identifier->syntax.text.starts_with('%')) {
      const syntax_ast::AstOperand base_operand = *identifier;
      auto register_ref = resolve_register(base_operand, nullptr);
      if (!register_ref)
        return std::unexpected(register_ref.error());
      base = std::move(register_ref->value);
    } else {
      auto symbol = resolve_data_symbol(
          *identifier, nullptr,
          FormalParameterAddressPolicy::PreserveParameterSpace);
      base = std::move(*symbol);
    }
  } else {
    const auto& immediate = std::get<syntax_ast::AstImmediate>(address->base);
    auto immediate_base =
        resolve_immediate_value(immediate, ScalarType::U32, true);
    if (!immediate_base)
      return std::unexpected(immediate_base.error());
    if (immediate_base->is_negative) {
      return std::unexpected(ResolveDiagnostic{
          .range = immediate.syntax.range,
          .message = fmt::format(
              "Immediate address '{}' must be an unsigned 32-bit value.",
              immediate.syntax.text),
      });
    }
    base = std::move(*immediate_base);
  }

  auto offset =
      resolve_address_offset(*address, AddressImmediateDomain::MemoryOperand);
  if (!offset)
    return std::unexpected(offset.error());

  return WithLocs<ResolvedAddress>{
      ResolvedAddress{
          .base = std::move(*base),
          .offset = std::move(*offset),
          .enclosing_function_kind =
              context == nullptr           ? EnclosingFunctionKind::Unknown
              : context->function_is_entry ? EnclosingFunctionKind::Entry
                                           : EnclosingFunctionKind::Device,
      },
      address->range};
}

std::expected<WithLocs<RegOrImm>, ResolveDiagnostic> resolve_reg_or_imm(
    const syntax_ast::AstOperand& operand, ScalarType type,
    const ResolveContext* context, bool require_target_range = false) {
  if (const auto* identifier =
          std::get_if<syntax_ast::AstIdentifierRef>(&operand)) {
    auto register_ref = resolve_register(operand, context);
    if (!register_ref)
      return std::unexpected(register_ref.error());
    return WithLocs<RegOrImm>{RegOrImm{register_ref->value},
                              identifier->syntax.range};
  }
  if (const auto* immediate = std::get_if<syntax_ast::AstImmediate>(&operand)) {
    auto value =
        detail::resolve_immediate_value(*immediate, type, require_target_range);
    if (!value)
      return std::unexpected(value.error());
    return WithLocs<RegOrImm>{RegOrImm{*value}, immediate->syntax.range};
  }
  return std::unexpected(ResolveDiagnostic{
      .range = syntax_ast::sourceRange(operand),
      .message = "Expected a register or immediate operand.",
  });
}

std::expected<ScalarType, ResolveDiagnostic> type_for_operand(
    const ResolvedOperandBindingDescriptor& binding,
    const ResolvedInstructionFields& fields, const SourceRange& range);

std::expected<WithLocs<ResolvedRegisterVector>, ResolveDiagnostic>
resolve_reg_vector(const syntax_ast::AstOperand& operand,
                   ScalarType instruction_type,
                   std::span<const uint8_t> allowed_arities,
                   std::optional<uint8_t> required_arity,
                   checker::VectorTypePolicy vector_type_policy,
                   base::ScalarTypeSizePolicy register_width_policy,
                   bool allow_sink, size_t sink_payload_bits,
                   const ResolveContext* context) {
  const auto* vector = std::get_if<syntax_ast::AstVectorPack>(&operand);
  if (vector == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a vector-pack operand.",
    });
  }
  if (vector_type_policy == checker::VectorTypePolicy::Aggregate &&
      scalar_kind(instruction_type) != base::ScalarKind::Bit) {
    return std::unexpected(ResolveDiagnostic{
        .range = vector->range,
        .message = "A vector mov requires a bit-size instruction type.",
    });
  }

  const size_t arity = vector->elements.size();
  if (required_arity && arity != *required_arity) {
    return std::unexpected(ResolveDiagnostic{
        .range = vector->range,
        .message = fmt::format("This vector operand requires {} elements.",
                               *required_arity),
    });
  }
  const size_t vector_payload_bits =
      (vector_type_policy == checker::VectorTypePolicy::Aggregate
           ? scalar_size_of(instruction_type)
           : arity * scalar_size_of(instruction_type)) *
      8u;
  if (vector_payload_bits > checker::kMaxRegisterVectorPayloadBits) {
    return std::unexpected(ResolveDiagnostic{
        .range = vector->range,
        .message = fmt::format("This vector operand's payload width ({} bits) "
                               "exceeds the supported "
                               "{} bit limit.",
                               vector_payload_bits,
                               checker::kMaxRegisterVectorPayloadBits),
    });
  }
  if (!required_arity &&
      std::ranges::find(allowed_arities, arity) == allowed_arities.end()) {
    return std::unexpected(ResolveDiagnostic{
        .range = vector->range,
        .message =
            vector_type_policy == checker::VectorTypePolicy::Aggregate
                ? "A vector mov requires two or four elements."
                : "A vector operand requires two, four, or eight elements.",
    });
  }
  size_t element_bytes = scalar_size_of(instruction_type);
  if (vector_type_policy == checker::VectorTypePolicy::Aggregate) {
    const size_t instruction_bytes = scalar_size_of(instruction_type);
    if (instruction_bytes % arity != 0 || instruction_bytes / arity == 0) {
      return std::unexpected(ResolveDiagnostic{
          .range = vector->range,
          .message = "Vector mov elements must be at least eight bits wide.",
      });
    }
    element_bytes = instruction_bytes / arity;
  }

  ResolvedRegisterVector result;
  result.elements.reserve(arity);
  std::vector<SourceRange> locations;
  locations.reserve(arity);
  size_t sink_count = 0;
  for (const auto& element : vector->elements) {
    const auto* identifier =
        std::get_if<syntax_ast::AstIdentifierRef>(&element);
    if (identifier == nullptr) {
      return std::unexpected(ResolveDiagnostic{
          .range = std::get<syntax_ast::AstImmediate>(element).syntax.range,
          .message =
              "A register-vector element must be a register or '_' sink.",
      });
    }
    locations.push_back(identifier->syntax.range);
    if (identifier->syntax.text == "_") {
      if (!allow_sink) {
        return std::unexpected(ResolveDiagnostic{
            .range = identifier->syntax.range,
            .message = "The '_' sink is allowed only in a destination vector.",
        });
      }
      if (sink_payload_bits != 0 && vector_payload_bits != sink_payload_bits) {
        return std::unexpected(ResolveDiagnostic{
            .range = identifier->syntax.range,
            .message = fmt::format(
                "The '_' sink requires an exact {}-bit vector payload.",
                sink_payload_bits),
        });
      }
      ++sink_count;
      result.elements.emplace_back(std::nullopt);
      continue;
    }

    syntax_ast::AstOperand register_operand{*identifier};
    auto register_ref = resolve_register(register_operand, context);
    if (!register_ref)
      return std::unexpected(register_ref.error());
    if (register_ref->value.declared_type) {
      const auto declared_type = *register_ref->value.declared_type;
      const bool type_mismatch =
          vector_type_policy == checker::VectorTypePolicy::Aggregate
              ? scalar_size_of(declared_type) != element_bytes
              : !scalar_types_compatible(declared_type, instruction_type,
                                         register_width_policy);
      if (type_mismatch) {
        return std::unexpected(ResolveDiagnostic{
            .range = identifier->syntax.range,
            .message = fmt::format(
                "Vector element '{}' has type '{}' incompatible with this "
                "instruction.",
                identifier->syntax.text, to_string(declared_type)),
        });
      }
    }
    result.elements.emplace_back(std::move(register_ref->value));
  }
  if (sink_count == arity) {
    return std::unexpected(ResolveDiagnostic{
        .range = vector->range,
        .message = "A vector must contain at least one register.",
    });
  }
  WithLocs<ResolvedRegisterVector> resolved{std::move(result)};
  resolved.locs = std::move(locations);
  return resolved;
}

std::expected<WithLocs<ResolvedRegisterVector>, ResolveDiagnostic>
resolve_modern_register_vector(
    const syntax_ast::AstOperand& operand,
    const check_end::ResolvedOperandBindingDescriptor& binding,
    const ResolveContext* context) {
  const auto* vector = std::get_if<syntax_ast::AstVectorPack>(&operand);
  if (vector == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a vector-pack operand.",
    });
  }
  if (vector->elements.size() < binding.minimum_elements ||
      vector->elements.size() > binding.maximum_elements) {
    return std::unexpected(ResolveDiagnostic{
        .range = vector->range,
        .message =
            fmt::format("Vector operand requires {} to {} elements.",
                        binding.minimum_elements, binding.maximum_elements),
    });
  }

  ResolvedRegisterVector result;
  result.elements.reserve(vector->elements.size());
  std::vector<SourceRange> locations;
  locations.reserve(vector->elements.size());
  for (const auto& element : vector->elements) {
    const auto* identifier =
        std::get_if<syntax_ast::AstIdentifierRef>(&element);
    if (identifier == nullptr) {
      return std::unexpected(ResolveDiagnostic{
          .range = std::get<syntax_ast::AstImmediate>(element).syntax.range,
          .message =
              "A matrix fragment element must be a register or '_' sink.",
      });
    }
    locations.push_back(identifier->syntax.range);
    if (identifier->syntax.text == "_") {
      result.elements.emplace_back(std::nullopt);
      continue;
    }
    syntax_ast::AstOperand register_operand{*identifier};
    auto register_ref = resolve_register(register_operand, context);
    if (!register_ref)
      return std::unexpected(register_ref.error());
    result.elements.emplace_back(std::move(register_ref->value));
  }
  WithLocs<ResolvedRegisterVector> resolved{std::move(result)};
  resolved.locs = std::move(locations);
  return resolved;
}

std::expected<WithLocs<ResolvedTensorCoordinate>, ResolveDiagnostic>
resolve_tensor_coordinate(
    const syntax_ast::AstOperand& operand,
    const check_end::ResolvedOperandBindingDescriptor& binding,
    const ResolvedInstructionFields& fields, const ResolveContext* context) {
  const auto* vector = std::get_if<syntax_ast::AstVectorPack>(&operand);
  if (vector == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a vector-pack operand.",
    });
  }
  if (vector->elements.size() < binding.minimum_elements ||
      vector->elements.size() > binding.maximum_elements) {
    return std::unexpected(ResolveDiagnostic{
        .range = vector->range,
        .message =
            fmt::format("Vector operand requires {} to {} elements.",
                        binding.minimum_elements, binding.maximum_elements),
    });
  }

  ResolvedTensorCoordinate result;
  result.elements.reserve(vector->elements.size());
  std::vector<SourceRange> locations;
  locations.reserve(vector->elements.size());
  std::optional<ScalarType> immediate_type;
  for (const auto& element : vector->elements) {
    if (const auto* identifier =
            std::get_if<syntax_ast::AstIdentifierRef>(&element)) {
      syntax_ast::AstOperand register_operand{*identifier};
      auto register_ref = resolve_register(register_operand, context);
      if (!register_ref)
        return std::unexpected(register_ref.error());
      locations.push_back(identifier->syntax.range);
      result.elements.emplace_back(std::move(register_ref->value));
      continue;
    }

    const auto& immediate = std::get<syntax_ast::AstImmediate>(element);
    if (!immediate_type) {
      auto type = type_for_operand(binding, fields, immediate.syntax.range);
      if (!type)
        return std::unexpected(type.error());
      if (*type == ScalarType::Invalid) {
        return std::unexpected(ResolveDiagnostic{
            .range = immediate.syntax.range,
            .message =
                "Tensor coordinate immediates require an operand scalar type.",
        });
      }
      immediate_type = *type;
    }
    auto value = detail::resolve_immediate_value(
        immediate, *immediate_type,
        binding.immediate_conversion_policy ==
            checker::ImmediateConversionPolicy::RequireTargetRange);
    if (!value)
      return std::unexpected(value.error());
    locations.push_back(immediate.syntax.range);
    result.elements.emplace_back(std::move(*value));
  }
  WithLocs<ResolvedTensorCoordinate> resolved{std::move(result)};
  resolved.locs = std::move(locations);
  return resolved;
}

std::expected<WithLocs<ResolvedMovSource>, ResolveDiagnostic>
resolve_mov_source(const syntax_ast::AstOperand& operand, ScalarType type,
                   checker::OperandShape allowed_shapes,
                   bool allow_function_symbol, const ResolveContext* context) {
  if (type == ScalarType::B128) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "The .b128 mov type is available only for vector pack or "
                   "unpack forms.",
    });
  }
  const auto reject_shape =
      [&](checker::OperandShape shape,
          SourceRange range) -> std::optional<ResolveDiagnostic> {
    if (allows_shape(allowed_shapes, shape))
      return std::nullopt;
    return ResolveDiagnostic{
        .range = range,
        .message =
            "This mov variant does not accept the resolved source "
            "operand shape.",
    };
  };
  const auto reject_address_type =
      [&](SourceRange range,
          bool function_address = false) -> std::optional<ResolveDiagnostic> {
    using base::ScalarKind;
    const ScalarKind kind = scalar_kind(type);
    const uint8_t width = scalar_size_of(type);
    const bool integer_address =
        kind == ScalarKind::Unsigned || kind == ScalarKind::Signed;
    const bool data_address = integer_address || kind == ScalarKind::Bit;
    const bool address_width = width == 4 || width == 8;
    if (address_width && (function_address ? integer_address : data_address))
      return std::nullopt;
    return ResolveDiagnostic{
        .range = range,
        .message = function_address
                       ? "A function address requires a 32-bit or 64-bit "
                         "integer mov type."
                       : "A data address requires a 32-bit or 64-bit integer "
                         "or bit-size mov type.",
    };
  };

  if (const auto* immediate = std::get_if<syntax_ast::AstImmediate>(&operand)) {
    if (auto rejected = reject_shape(checker::OperandShape::Immediate,
                                     immediate->syntax.range)) {
      return std::unexpected(std::move(*rejected));
    }
    auto value = detail::resolve_immediate_value(*immediate, type);
    if (!value)
      return std::unexpected(value.error());
    return WithLocs<ResolvedMovSource>{ResolvedMovSource{std::move(*value)},
                                       immediate->syntax.range};
  }

  if (std::holds_alternative<syntax_ast::AstVectorMember>(operand)) {
    if (auto rejected = reject_shape(checker::OperandShape::SpecialRegister,
                                     syntax_ast::sourceRange(operand))) {
      return std::unexpected(std::move(*rejected));
    }
    auto value = resolve_special_register(operand);
    if (!value)
      return std::unexpected(value.error());
    WithLocs<ResolvedMovSource> resolved{
        ResolvedMovSource{std::move(value->value)}};
    resolved.locs = std::move(value->locs);
    return resolved;
  }

  if (const auto* address = std::get_if<syntax_ast::AstAddress>(&operand)) {
    if (auto rejected =
            reject_shape(checker::OperandShape::Address, address->range)) {
      return std::unexpected(std::move(*rejected));
    }
    if (address->bracketed) {
      return std::unexpected(ResolveDiagnostic{
          .range = address->range,
          .message = "Expected an unbracketed symbol-address operand.",
      });
    }
    if (auto rejected = reject_address_type(address->range))
      return std::unexpected(std::move(*rejected));
    const auto* identifier =
        std::get_if<syntax_ast::AstIdentifierRef>(&address->base);
    if (identifier == nullptr) {
      return std::unexpected(ResolveDiagnostic{
          .range = address->range,
          .message = "A mov address expression must use a data-symbol base.",
      });
    }
    auto symbol = resolve_data_symbol(
        *identifier, context,
        FormalParameterAddressPolicy::MaterializeDeviceParameter);
    if (!symbol)
      return std::unexpected(symbol.error());
    auto offset = resolve_address_offset(*address);
    if (!offset)
      return std::unexpected(offset.error());
    ResolvedAddress value{
        .base = std::move(*symbol),
        .offset = std::move(*offset),
    };
    return WithLocs<ResolvedMovSource>{ResolvedMovSource{std::move(value)},
                                       address->range};
  }

  const auto* identifier = std::get_if<syntax_ast::AstIdentifierRef>(&operand);
  if (identifier == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a scalar mov source operand.",
    });
  }

  if (base::lookup(identifier->syntax.text)) {
    if (auto rejected = reject_shape(checker::OperandShape::SpecialRegister,
                                     identifier->syntax.range)) {
      return std::unexpected(std::move(*rejected));
    }
    auto value = resolve_special_register(operand);
    if (!value)
      return std::unexpected(value.error());
    WithLocs<ResolvedMovSource> resolved{
        ResolvedMovSource{std::move(value->value)}};
    resolved.locs = std::move(value->locs);
    return resolved;
  }

  bool is_register = identifier->syntax.text.starts_with('%');
  if (context != nullptr) {
    const auto lookup =
        context->symbols.lookup(context->scope, identifier->syntax.text);
    if (lookup) {
      const binding::Symbol& symbol = context->symbols.symbol(lookup->symbol);
      if (symbol.kind == binding::SymbolKind::Function) {
        if (!allow_function_symbol) {
          return std::unexpected(ResolveDiagnostic{
              .range = identifier->syntax.range,
              .message = "This operand does not accept a function symbol.",
          });
        }
        if (auto rejected = reject_shape(checker::OperandShape::Symbol,
                                         identifier->syntax.range)) {
          return std::unexpected(std::move(*rejected));
        }
        // PTX accepts signed function-address moves with a warning. The
        // frontend has no warning channel yet, so they remain successful.
        if (auto rejected =
                reject_address_type(identifier->syntax.range, true)) {
          return std::unexpected(std::move(*rejected));
        }
        ResolvedFunctionRef function{
            .spelling = identifier->syntax.text,
            .symbol_id = symbol.id,
            .is_entry = symbol.function_is_entry,
        };
        if (function.is_entry) {
          function.address_availability = checker::AvailabilityDescriptor{
              .minimum_ptx_version = {3, 1},
              .minimum_sm_version = 35,
          };
        }
        return WithLocs<ResolvedMovSource>{
            ResolvedMovSource{std::move(function)}, identifier->syntax.range};
      }
      is_register = symbol.kind == binding::SymbolKind::Variable &&
                    symbol.state_space == syntax_ast::AstStateSpace::Register;
    }
  }

  if (is_register) {
    if (auto rejected = reject_shape(checker::OperandShape::Register,
                                     identifier->syntax.range)) {
      return std::unexpected(std::move(*rejected));
    }
    auto value = resolve_register(operand, context);
    if (!value)
      return std::unexpected(value.error());
    WithLocs<ResolvedMovSource> resolved{
        ResolvedMovSource{std::move(value->value)}};
    resolved.locs = std::move(value->locs);
    return resolved;
  }

  if (auto rejected = reject_shape(checker::OperandShape::Symbol,
                                   identifier->syntax.range)) {
    return std::unexpected(std::move(*rejected));
  }
  if (auto rejected = reject_address_type(identifier->syntax.range))
    return std::unexpected(std::move(*rejected));
  auto value = resolve_data_symbol(
      *identifier, context,
      FormalParameterAddressPolicy::MaterializeDeviceParameter);
  if (!value)
    return std::unexpected(value.error());
  return WithLocs<ResolvedMovSource>{ResolvedMovSource{std::move(*value)},
                                     identifier->syntax.range};
}

std::expected<ScalarType, ResolveDiagnostic> type_for_operand(
    const ResolvedOperandBindingDescriptor& binding,
    const ResolvedInstructionFields& fields, const SourceRange& range) {
  const auto& expression = binding.type_expression;
  if (expression.kind == checker::OperandTypeExpressionKind::None)
    return ScalarType::Invalid;
  if (expression.kind == checker::OperandTypeExpressionKind::FixedScalar)
    return expression.fixed_scalar_type;
  if (expression.kind != checker::OperandTypeExpressionKind::ModifierField ||
      expression.modifier_field_id.empty()) {
    throw ResolveException(
        fmt::format("Resolved operand field '{}' has an invalid type "
                    "expression descriptor.",
                    binding.target_field_id));
  }

  const std::string_view field_id = expression.modifier_field_id;
  const auto it = fields.modifiers.find(std::string(field_id));
  if (it == fields.modifiers.end()) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format("Operand '{}' requires modifier '{}'.",
                               binding.target_field_id, field_id),
    });
  }
  if (const auto* type = std::get_if<WithLocs<ScalarType>>(&it->second))
    return type->value;
  throw ResolveException(fmt::format(
      "Operand '{}' expects modifier '{}' to resolve as ScalarType.",
      binding.target_field_id, field_id));
}

std::expected<std::optional<uint8_t>, ResolveDiagnostic>
vector_arity_for_operand(const ResolvedOperandBindingDescriptor& binding,
                         const ResolvedInstructionFields& fields,
                         const SourceRange& range) {
  if (binding.vector_arity_modifier_field_id.empty())
    return std::nullopt;

  const std::string_view field_id = binding.vector_arity_modifier_field_id;
  const auto it = fields.modifiers.find(std::string(field_id));
  if (it == fields.modifiers.end()) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format("Operand '{}' requires vector arity field '{}'.",
                               binding.target_field_id, field_id),
    });
  }
  if (const auto* arity = std::get_if<WithLocs<VectorArity>>(&it->second))
    return vector_arity_count(arity->value);
  throw ResolveException(fmt::format(
      "Operand '{}' expects modifier '{}' to resolve as VectorArity.",
      binding.target_field_id, field_id));
}

std::expected<ResolvedFieldValue, ResolveDiagnostic> resolve_operand_value(
    const ResolvedFieldDescriptor& field,
    const ResolvedOperandBindingDescriptor& binding,
    const syntax_ast::AstOperand& operand,
    const ResolvedInstructionFields& fields, const ResolveContext* context) {
  switch (field.value_kind) {
    case ResolvedValueKind::Register: {
      auto value = resolve_register(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::MbarrierStateToken: {
      const bool is_sink =
          std::get_if<syntax_ast::AstIdentifierRef>(&operand) != nullptr &&
          std::get<syntax_ast::AstIdentifierRef>(operand).syntax.text == "_";
      if (is_sink) {
        if (binding.mbarrier_state_token_form ==
            checker::MbarrierStateTokenForm::Register) {
          return std::unexpected(
              ResolveDiagnostic{.range = syntax_ast::sourceRange(operand),
                                .message = "The '_' sink is not allowed for "
                                           "this mbarrier state token."});
        }
        return ResolvedFieldValue{WithLocs<ResolvedMbarrierStateToken>{
            ResolvedMbarrierStateToken{.register_ref = std::nullopt},
            syntax_ast::sourceRange(operand)}};
      }
      if (binding.mbarrier_state_token_form ==
          checker::MbarrierStateTokenForm::Sink) {
        return std::unexpected(ResolveDiagnostic{
            .range = syntax_ast::sourceRange(operand),
            .message = "This mbarrier state token requires the '_' sink."});
      }
      auto value = resolve_register(operand, context);
      if (!value)
        return std::unexpected(value.error());
      WithLocs<ResolvedMbarrierStateToken> token{
          ResolvedMbarrierStateToken{.register_ref = std::move(value->value)}};
      token.locs = std::move(value->locs);
      return ResolvedFieldValue{std::move(token)};
    }
    case ResolvedValueKind::RegisterOrSink: {
      if (const auto* identifier =
              std::get_if<syntax_ast::AstIdentifierRef>(&operand);
          identifier != nullptr && identifier->syntax.text == "_") {
        return ResolvedFieldValue{WithLocs<ResolvedRegisterOrSink>{
            ResolvedRegisterOrSink{.register_ref = std::nullopt},
            identifier->syntax.range}};
      }
      auto value = resolve_register(operand, context);
      if (!value)
        return std::unexpected(value.error());
      WithLocs<ResolvedRegisterOrSink> destination{
          ResolvedRegisterOrSink{.register_ref = std::move(value->value)}};
      destination.locs = std::move(value->locs);
      return ResolvedFieldValue{std::move(destination)};
    }
    case ResolvedValueKind::Predicate: {
      auto value = resolve_predicate(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::PredicateSource: {
      auto value = resolve_predicate_source(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Immediate: {
      const auto* immediate = std::get_if<syntax_ast::AstImmediate>(&operand);
      if (immediate == nullptr) {
        return std::unexpected(ResolveDiagnostic{
            .range = syntax_ast::sourceRange(operand),
            .message = "Expected an immediate operand.",
        });
      }
      const auto type =
          type_for_operand(binding, fields, immediate->syntax.range);
      if (!type)
        return std::unexpected(type.error());
      auto value = resolve_immediate_value(
          *immediate, *type,
          binding.immediate_conversion_policy ==
              checker::ImmediateConversionPolicy::RequireTargetRange);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{WithLocs<ResolvedImmediate>{
          std::move(*value), immediate->syntax.range}};
    }
    case ResolvedValueKind::RegOrImm: {
      const auto type =
          type_for_operand(binding, fields, syntax_ast::sourceRange(operand));
      if (!type)
        return std::unexpected(type.error());
      auto value = resolve_reg_or_imm(
          operand, *type, context,
          binding.immediate_conversion_policy ==
              checker::ImmediateConversionPolicy::RequireTargetRange);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::ShflDestination: {
      auto value =
          resolve_shfl_destination(operand, binding.allow_destination_sink,
                                   binding.allow_predicate_sink, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::PredicatePair: {
      auto value = resolve_predicate_pair(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::MovSource: {
      const auto type =
          type_for_operand(binding, fields, syntax_ast::sourceRange(operand));
      if (!type)
        return std::unexpected(type.error());
      auto value = resolve_mov_source(operand, *type, binding.allowed_shapes,
                                      binding.allow_function_symbol, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::VectorRegister: {
      auto value = resolve_vector_register(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::VectorSpecialRegister: {
      auto value = resolve_vector_special_register(operand);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::BranchTarget: {
      auto value = resolve_branch_target(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::BranchTargetSet: {
      auto value = resolve_branch_target_set(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::SpecialRegister: {
      auto value = resolve_special_register(operand);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Symbol: {
      auto value = resolve_symbol(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Address: {
      auto value = resolve_address(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::RegisterVector: {
      if (binding.minimum_elements != 0) {
        auto value = resolve_modern_register_vector(operand, binding, context);
        if (!value)
          return std::unexpected(value.error());
        return ResolvedFieldValue{std::move(*value)};
      }
      const auto type =
          type_for_operand(binding, fields, syntax_ast::sourceRange(operand));
      if (!type)
        return std::unexpected(type.error());
      const auto arity = vector_arity_for_operand(
          binding, fields, syntax_ast::sourceRange(operand));
      if (!arity)
        return std::unexpected(arity.error());
      auto value = resolve_reg_vector(
          operand, *type, binding.allowed_vector_arities, *arity,
          binding.vector_type_policy, binding.register_width_policy,
          binding.allow_vector_sink, binding.vector_sink_payload_bits, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::TensorCoordinate: {
      auto value = resolve_tensor_coordinate(operand, binding, fields, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::DirectCallTarget: {
      auto value = resolve_direct_call_target(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::IndirectCallee: {
      auto value = resolve_indirect_callee(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::CallReturnParameter: {
      auto value = resolve_call_return_parameter(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::CallArguments: {
      auto value = resolve_call_arguments(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Bool:
    case ResolvedValueKind::ScalarType:
    case ResolvedValueKind::RoundingMode:
    case ResolvedValueKind::ComparisonOperator:
    case ResolvedValueKind::BooleanOperator:
    case ResolvedValueKind::CacheOperator:
    case ResolvedValueKind::EvictionPriority:
    case ResolvedValueKind::MemoryConsistency:
    case ResolvedValueKind::MemoryScope:
    case ResolvedValueKind::VectorArity:
    case ResolvedValueKind::MemoryStateSpace:
    case ResolvedValueKind::MbarrierPhaseType:
    case ResolvedValueKind::MbarrierLayout:
    case ResolvedValueKind::AsyncProxyKind:
    case ResolvedValueKind::ProxyKindPair:
      throw ResolveException(fmt::format(
          "Operand slot '{}' has a non-operand resolved value kind.",
          field.field_id));
  }
  throw ResolveException("Unknown ResolvedValueKind.");
}

}  // namespace detail

checker::AvailabilityDescriptor special_register_availability(
    const base::Info& info) {
  checker::AvailabilityDescriptor availability{
      .minimum_ptx_version = {info.minimum_ptx_major, info.minimum_ptx_minor},
      .minimum_sm_version = info.minimum_sm,
  };
  if (info.required_capability.empty())
    return availability;

  availability.any_of[0] = checker::AvailabilityClause{
      .minimum_ptx_version = {info.minimum_ptx_major, info.minimum_ptx_minor},
      .minimum_sm_version = info.minimum_sm,
      .capabilities = {info.required_capability},
      .capability_count = 1,
  };
  availability.any_of_count = 1;
  return availability;
}

}  // namespace ptx_frontend::resolved_ir

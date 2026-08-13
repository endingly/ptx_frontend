#include "ptx_ir/syntax/ptx_syntax_lower.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ptx_frontend {
namespace {

syntax_ast::AstSyntax leafSyntax(const syntax_cst::CstFile& cst,
                                 syntax_cst::TokenId id) {
  const PtxToken& token = cst.token(id);
  return syntax_ast::AstSyntax{token.text, token.range};
}

syntax_ast::AstImmediateKind immediateKind(TokenKind kind) {
  switch (kind) {
    case TokenKind::Decimal:
      return syntax_ast::AstImmediateKind::DecimalInteger;
    case TokenKind::Hex:
      return syntax_ast::AstImmediateKind::HexInteger;
    case TokenKind::F32Hex:
      return syntax_ast::AstImmediateKind::F32Hex;
    case TokenKind::F64Hex:
      return syntax_ast::AstImmediateKind::F64Hex;
    case TokenKind::F64:
      return syntax_ast::AstImmediateKind::DecimalFloat;
    default:
      throw std::logic_error("CST immediate contains a non-literal token");
  }
}

syntax_ast::AstImmediate lowerImmediate(
    const syntax_cst::CstFile& cst, const syntax_cst::CstImmediate& immediate) {
  std::string text;
  if (immediate.sign)
    text += cst.token(*immediate.sign).text;
  text += cst.token(immediate.literal).text;
  return syntax_ast::AstImmediate{
      {std::move(text), cst.sourceRange(immediate.token_range)},
      immediateKind(cst.token(immediate.literal).kind)};
}

syntax_ast::AstIdentifierRef lowerIdentifier(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstIdentifier& identifier) {
  return syntax_ast::AstIdentifierRef{leafSyntax(cst, identifier.token)};
}

syntax_ast::AstVectorElement lowerVectorElement(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstVectorElement& element) {
  return std::visit(
      [&cst](const auto& value) -> syntax_ast::AstVectorElement {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_cst::CstIdentifier>)
          return lowerIdentifier(cst, value);
        else
          return lowerImmediate(cst, value);
      },
      element);
}

syntax_ast::AstOperand lowerOperand(const syntax_cst::CstFile& cst,
                                    const syntax_cst::CstOperand& operand) {
  return std::visit(
      [&cst](const auto& value) -> syntax_ast::AstOperand {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_cst::CstIdentifier>) {
          return lowerIdentifier(cst, value);
        } else if constexpr (std::same_as<Value,
                                          syntax_cst::CstPredicateOperand>) {
          return syntax_ast::AstPredicateOperand{
              value.exclamation_token.has_value(),
              lowerIdentifier(cst, {value.name}),
              cst.sourceRange(value.token_range)};
        } else if constexpr (std::same_as<Value, syntax_cst::CstImmediate>) {
          return lowerImmediate(cst, value);
        } else if constexpr (std::same_as<Value, syntax_cst::CstAddress>) {
          auto base = std::visit(
              [&cst](const auto& item)
                  -> std::variant<syntax_ast::AstIdentifierRef,
                                  syntax_ast::AstImmediate> {
                using Item = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::same_as<Item, syntax_cst::CstIdentifier>)
                  return lowerIdentifier(cst, item);
                else
                  return lowerImmediate(cst, item);
              },
              value.base);
          std::optional<syntax_ast::AstAddressOffset> offset;
          if (value.offset) {
            const TokenKind operator_kind =
                cst.token(value.offset->operator_token).kind;
            offset = syntax_ast::AstAddressOffset{
                operator_kind == TokenKind::Minus
                    ? syntax_ast::AstAddressOffset::Operator::Subtract
                    : syntax_ast::AstAddressOffset::Operator::Add,
                lowerImmediate(cst, value.offset->magnitude),
                cst.sourceRange(value.offset->token_range)};
          }
          return syntax_ast::AstAddress{std::move(base), std::move(offset),
                                        value.left_bracket.has_value(),
                                        cst.sourceRange(value.token_range)};
        } else if constexpr (std::same_as<Value, syntax_cst::CstVectorMember>) {
          return syntax_ast::AstVectorMember{
              lowerIdentifier(cst, value.base), leafSyntax(cst, value.selector),
              cst.sourceRange(value.token_range)};
        } else {
          std::vector<syntax_ast::AstVectorElement> elements;
          elements.reserve(value.elements.size());
          for (const auto& element : value.elements)
            elements.push_back(lowerVectorElement(cst, element));
          return syntax_ast::AstVectorPack{std::move(elements),
                                           cst.sourceRange(value.token_range)};
        }
      },
      operand);
}

syntax_ast::AstInstruction lowerInstructionNode(
    const syntax_cst::CstFile& cst, const syntax_cst::CstInstruction& root) {
  syntax_ast::AstInstruction ast{
      .opcode = syntax_ast::AstOpcode{leafSyntax(cst, root.opcode)},
      .modifiers = {},
      .operands = {},
      .predicate = std::nullopt,
      .range = cst.sourceRange(root.token_range),
  };

  ast.modifiers.reserve(root.modifiers.size());
  for (const auto modifier : root.modifiers)
    ast.modifiers.push_back({leafSyntax(cst, modifier)});

  ast.operands.reserve(root.operands.size());
  for (const auto& operand : root.operands)
    ast.operands.push_back(lowerOperand(cst, operand.operand));

  if (root.predicate) {
    ast.predicate =
        syntax_ast::AstPredicate{root.predicate->exclamation_token.has_value(),
                                 lowerIdentifier(cst, {root.predicate->name}),
                                 cst.sourceRange(root.predicate->token_range)};
  }
  return ast;
}

syntax_ast::AstStateSpace lowerStateSpace(TokenKind kind) {
  switch (kind) {
    case TokenKind::DotReg:
      return syntax_ast::AstStateSpace::Register;
    case TokenKind::DotParam:
      return syntax_ast::AstStateSpace::Parameter;
    case TokenKind::DotLocal:
      return syntax_ast::AstStateSpace::Local;
    case TokenKind::DotShared:
      return syntax_ast::AstStateSpace::Shared;
    case TokenKind::DotGlobal:
      return syntax_ast::AstStateSpace::Global;
    case TokenKind::DotConst:
      return syntax_ast::AstStateSpace::Constant;
    default:
      throw std::logic_error("CST variable contains an invalid state space");
  }
}

syntax_ast::AstVariableDeclaration lowerVariableDeclaration(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstVariableDeclaration& declaration) {
  std::vector<syntax_ast::AstSyntax> qualifiers;
  qualifiers.reserve(declaration.qualifiers.size());
  for (const auto qualifier : declaration.qualifiers)
    qualifiers.push_back(leafSyntax(cst, qualifier));

  std::optional<syntax_ast::AstSyntax> alignment;
  if (declaration.alignment)
    alignment = leafSyntax(cst, *declaration.alignment);
  std::optional<syntax_ast::AstSyntax> vector_type;
  if (declaration.vector_type)
    vector_type = leafSyntax(cst, *declaration.vector_type);

  std::vector<syntax_ast::AstVariableDeclarator> declarators;
  declarators.reserve(declaration.declarators.size());
  for (const auto& declarator : declaration.declarators) {
    std::optional<syntax_ast::AstSyntax> register_count;
    if (declarator.register_count)
      register_count = leafSyntax(cst, *declarator.register_count);

    std::vector<syntax_ast::AstArrayDimension> dimensions;
    dimensions.reserve(declarator.array_dimensions.size());
    for (const auto& dimension : declarator.array_dimensions) {
      std::vector<syntax_ast::AstSyntax> size_tokens;
      size_tokens.reserve(dimension.size_tokens.size());
      for (const auto size_token : dimension.size_tokens)
        size_tokens.push_back(leafSyntax(cst, size_token));
      dimensions.push_back(syntax_ast::AstArrayDimension{
          std::move(size_tokens), cst.sourceRange(dimension.token_range)});
    }

    declarators.push_back(syntax_ast::AstVariableDeclarator{
        .name = lowerIdentifier(cst, {declarator.name}),
        .register_count = std::move(register_count),
        .array_dimensions = std::move(dimensions),
        .range = cst.sourceRange(declarator.token_range),
    });
  }

  return syntax_ast::AstVariableDeclaration{
      .qualifiers = std::move(qualifiers),
      .state_space = lowerStateSpace(cst.token(declaration.state_space).kind),
      .alignment = std::move(alignment),
      .vector_type = std::move(vector_type),
      .type = leafSyntax(cst, declaration.type),
      .declarators = std::move(declarators),
      .range = cst.sourceRange(declaration.token_range),
  };
}

syntax_ast::AstFunctionParameter lowerFunctionParameter(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstFunctionParameter& parameter) {
  std::optional<syntax_ast::AstSyntax> alignment;
  if (parameter.alignment)
    alignment = leafSyntax(cst, *parameter.alignment);
  std::optional<syntax_ast::AstSyntax> pointer_space;
  if (parameter.pointer_space)
    pointer_space = leafSyntax(cst, *parameter.pointer_space);
  std::optional<syntax_ast::AstSyntax> pointer_alignment;
  if (parameter.pointer_alignment)
    pointer_alignment = leafSyntax(cst, *parameter.pointer_alignment);
  std::optional<syntax_ast::AstSyntax> array_size;
  if (parameter.array_size)
    array_size = leafSyntax(cst, *parameter.array_size);

  return syntax_ast::AstFunctionParameter{
      .state_space = lowerStateSpace(cst.token(parameter.state_space).kind),
      .alignment = std::move(alignment),
      .type = leafSyntax(cst, parameter.type),
      .is_pointer = parameter.pointer_directive.has_value(),
      .pointer_space = std::move(pointer_space),
      .pointer_alignment = std::move(pointer_alignment),
      .name = lowerIdentifier(cst, {parameter.name}),
      .is_array = parameter.left_bracket.has_value(),
      .array_size = std::move(array_size),
      .range = cst.sourceRange(parameter.token_range),
  };
}

}  // namespace

std::expected<syntax_ast::AstInstruction, AstLowerDiagnostic>
lowerSyntaxInstruction(const syntax_cst::CstFile& cst) {
  const auto* root = cst.instruction();
  if (root == nullptr) {
    return std::unexpected(AstLowerDiagnostic{
        .range = {},
        .message = "expected an instruction-fragment CST root",
    });
  }
  return lowerInstructionNode(cst, *root);
}

std::expected<syntax_ast::AstModule, AstLowerDiagnostic> lowerSyntaxModule(
    const syntax_cst::CstFile& cst) {
  const auto* root = cst.module();
  if (root == nullptr) {
    return std::unexpected(AstLowerDiagnostic{
        .range = {},
        .message = "expected a module CST root",
    });
  }

  syntax_ast::AstModule ast{
      .items = {},
      .range = cst.sourceRange(root->token_range),
  };
  ast.items.reserve(root->items.size());

  for (const auto& item : root->items) {
    if (const auto* directive =
            std::get_if<syntax_cst::CstModuleDirective>(&item)) {
      const SourceRange range = cst.sourceRange(directive->token_range);
      switch (cst.token(directive->keyword).kind) {
        case TokenKind::DotVersion:
          ast.items.emplace_back(syntax_ast::AstVersionDirective{
              leafSyntax(cst, directive->arguments.at(0)), range});
          break;
        case TokenKind::DotTarget: {
          std::vector<syntax_ast::AstSyntax> targets;
          targets.reserve(directive->arguments.size());
          for (const auto argument : directive->arguments)
            targets.push_back(leafSyntax(cst, argument));
          ast.items.emplace_back(
              syntax_ast::AstTargetDirective{std::move(targets), range});
          break;
        }
        case TokenKind::DotAddressSize:
          ast.items.emplace_back(syntax_ast::AstAddressSizeDirective{
              leafSyntax(cst, directive->arguments.at(0)), range});
          break;
        default:
          return std::unexpected(AstLowerDiagnostic{
              .range = cst.token(directive->keyword).range,
              .message = "unsupported module directive in CST",
          });
      }
      continue;
    }

    if (const auto* declaration =
            std::get_if<syntax_cst::CstVariableDeclaration>(&item)) {
      ast.items.emplace_back(lowerVariableDeclaration(cst, *declaration));
      continue;
    }

    const auto& function = std::get<syntax_cst::CstFunction>(item);
    syntax_ast::AstFunction lowered{
        .is_entry = cst.token(function.directive).kind == TokenKind::DotEntry,
        .is_prototype = function.terminator.has_value(),
        .is_noreturn = function.noreturn_directive.has_value(),
        .qualifiers = {},
        .name = lowerIdentifier(cst, {function.name}),
        .return_parameters = {},
        .parameters = {},
        .body = {},
        .range = cst.sourceRange(function.token_range),
    };
    lowered.qualifiers.reserve(function.qualifiers.size());
    for (const auto qualifier : function.qualifiers)
      lowered.qualifiers.push_back(leafSyntax(cst, qualifier));
    if (function.return_parameters) {
      lowered.return_parameters.reserve(
          function.return_parameters->parameters.size());
      for (const auto& parameter : function.return_parameters->parameters)
        lowered.return_parameters.push_back(
            lowerFunctionParameter(cst, parameter));
    }
    if (function.parameters) {
      lowered.parameters.reserve(function.parameters->parameters.size());
      for (const auto& parameter : function.parameters->parameters)
        lowered.parameters.push_back(lowerFunctionParameter(cst, parameter));
    }
    lowered.body.reserve(function.body.size());
    for (const auto& body_item : function.body) {
      if (const auto* declaration =
              std::get_if<syntax_cst::CstVariableDeclaration>(&body_item)) {
        lowered.body.emplace_back(lowerVariableDeclaration(cst, *declaration));
      } else if (const auto* label =
                     std::get_if<syntax_cst::CstLabel>(&body_item)) {
        lowered.body.emplace_back(
            syntax_ast::AstLabel{lowerIdentifier(cst, {label->name}),
                                 cst.sourceRange(label->token_range)});
      } else {
        lowered.body.emplace_back(lowerInstructionNode(
            cst, std::get<syntax_cst::CstInstruction>(body_item)));
      }
    }
    ast.items.emplace_back(std::move(lowered));
  }
  return ast;
}

}  // namespace ptx_frontend

#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::resolved_ir::detail {
namespace source_identity {

/** Dependent false value used to reject future unencoded AST alternatives. */
template <typename>
inline constexpr bool always_false_v = false;

/** Append a length-prefixed atom so adjacent syntax values remain unambiguous. */
inline void atom(std::string& output, std::string_view value) {
  output += std::to_string(value.size());
  output.push_back(':');
  output.append(value);
}

/** Append a structural tag that cannot collide with a source spelling. */
inline void tag(std::string& output, std::string_view value) {
  output.push_back('[');
  atom(output, value);
  output.push_back(']');
}

/** Encode one integral enum or Boolean value without using its source range. */
template <typename Value>
  requires(std::integral<Value> || std::is_enum_v<Value>)
void number(std::string& output, Value value) {
  atom(output, std::to_string(static_cast<uint64_t>(value)));
}

/** Encode optional syntax while retaining the distinction from an empty spelling. */
inline void optional_syntax(std::string& output,
                            const std::optional<syntax_ast::AstSyntax>& value) {
  tag(output, value ? "some" : "none");
  if (value)
    atom(output, value->text);
}

/** Encode one resolution-significant declaration attribute and its values. */
inline void attribute(std::string& output,
                      const syntax_ast::AstAttribute& value) {
  tag(output, "attribute");
  number(output, value.kind);
  number(output, value.values.size());
  for (const auto& member : value.values)
    atom(output, member.text);
}

/** Encode source-order declaration attributes that affect resolved metadata. */
inline void attributes(std::string& output,
                       const std::vector<syntax_ast::AstAttribute>& values) {
  tag(output, "attributes");
  number(output, values.size());
  for (const auto& value : values)
    attribute(output, value);
}

/** Encode an optional ABI suffix without its source location. */
inline void optional_abi_suffix(
    std::string& output,
    const std::optional<syntax_ast::AstCallPrototypeAbiSuffix>& value) {
  tag(output, value ? "abi-present" : "abi-none");
  if (value) {
    atom(output, value->directive.text);
    atom(output, value->count.text);
  }
}

/** Encode one identifier reference by its spelling only. */
inline void identifier(std::string& output,
                       const syntax_ast::AstIdentifierRef& value) {
  tag(output, "identifier");
  atom(output, value.syntax.text);
}

/** Encode one immediate spelling and its parsed lexical category. */
inline void immediate(std::string& output,
                      const syntax_ast::AstImmediate& value) {
  tag(output, "immediate");
  number(output, value.kind);
  atom(output, value.syntax.text);
}

/** Encode one constant expression recursively, excluding all locations. */
inline void constant_expression(std::string& output,
                                const syntax_ast::AstConstantExpression& value);

/** Encode an optional array extent without conflating it with an empty extent. */
inline void optional_constant_expression(
    std::string& output,
    const std::optional<syntax_ast::AstConstantExpression>& value) {
  tag(output, value ? "some-expression" : "no-expression");
  if (value)
    constant_expression(output, *value);
}

/** Encode one initializer expression or nested initializer list recursively. */
inline void initializer(std::string& output,
                        const syntax_ast::AstInitializer& value);

/** Encode a constant expression node using a separate tag for every grammar form. */
inline void constant_expression(
    std::string& output, const syntax_ast::AstConstantExpression& value) {
  std::visit(
      [&output](const auto& node) {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, syntax_ast::AstConstantLiteral>) {
          tag(output, "constant-literal");
          immediate(output, node.value);
        } else if constexpr (std::same_as<Node,
                                          syntax_ast::AstConstantSymbol>) {
          tag(output, "constant-symbol");
          identifier(output, node.name);
        } else if constexpr (std::same_as<
                                 Node, syntax_ast::AstConstantParenthesized>) {
          tag(output, "constant-parenthesized");
          constant_expression(output, *node.expression);
        } else if constexpr (std::same_as<Node, syntax_ast::AstConstantCall>) {
          tag(output, "constant-call");
          constant_expression(output, *node.callee);
          constant_expression(output, *node.argument);
        } else if constexpr (std::same_as<Node, syntax_ast::AstConstantCast>) {
          tag(output, "constant-cast");
          atom(output, node.type.text);
          constant_expression(output, *node.operand);
        } else if constexpr (std::same_as<Node, syntax_ast::AstConstantUnary>) {
          tag(output, "constant-unary");
          number(output, node.operation);
          constant_expression(output, *node.operand);
        } else if constexpr (std::same_as<Node,
                                          syntax_ast::AstConstantBinary>) {
          tag(output, "constant-binary");
          constant_expression(output, *node.left);
          number(output, node.operation);
          constant_expression(output, *node.right);
        } else if constexpr (std::same_as<Node,
                                          syntax_ast::AstConstantConditional>) {
          tag(output, "constant-conditional");
          constant_expression(output, *node.condition);
          constant_expression(output, *node.true_expression);
          constant_expression(output, *node.false_expression);
        } else {
          static_assert(always_false_v<Node>);
        }
      },
      value.node);
}

/** Encode an initializer tree while preserving list element boundaries. */
inline void initializer(std::string& output,
                        const syntax_ast::AstInitializer& value) {
  std::visit(
      [&output](const auto& node) {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, syntax_ast::AstConstantExpression>) {
          tag(output, "initializer-expression");
          constant_expression(output, node);
        } else if constexpr (std::same_as<Node,
                                          syntax_ast::AstInitializerList>) {
          tag(output, "initializer-list");
          number(output, node.elements.size());
          for (const auto& element : node.elements)
            initializer(output, element);
        } else {
          static_assert(always_false_v<Node>);
        }
      },
      value.value);
}

/** Encode one variable declarator including shape and initializer syntax. */
inline void variable_declarator(
    std::string& output, const syntax_ast::AstVariableDeclarator& value) {
  tag(output, "declarator");
  identifier(output, value.name);
  optional_syntax(output, value.parameterized_count);
  number(output, value.array_dimensions.size());
  for (const auto& dimension : value.array_dimensions)
    optional_constant_expression(output, dimension.size);
  tag(output, value.initializer ? "initializer-present" : "initializer-none");
  if (value.initializer)
    initializer(output, *value.initializer);
}

/** Encode one local declaration including resolved metadata attributes. */
inline void variable_declaration(
    std::string& output, const syntax_ast::AstVariableDeclaration& value) {
  tag(output, "variable-declaration");
  number(output, value.qualifiers.size());
  for (const auto& qualifier : value.qualifiers)
    atom(output, qualifier.text);
  number(output, value.state_space);
  attributes(output, value.attributes);
  optional_syntax(output, value.alignment);
  optional_syntax(output, value.vector_type);
  atom(output, value.type.text);
  number(output, value.declarators.size());
  for (const auto& declarator : value.declarators)
    variable_declarator(output, declarator);
}

/** Encode a function or call-prototype parameter with all ABI-relevant spelling. */
inline void parameter(std::string& output,
                      const syntax_ast::AstFunctionParameter& value) {
  tag(output, "parameter");
  number(output, value.state_space);
  optional_syntax(output, value.alignment);
  atom(output, value.type.text);
  number(output, value.is_pointer);
  optional_syntax(output, value.pointer_space);
  optional_syntax(output, value.pointer_alignment);
  identifier(output, value.name);
  number(output, value.is_array);
  optional_constant_expression(output, value.array_size);
}

/** Encode one operand and its alternative-specific semantic spelling. */
inline void operand(std::string& output, const syntax_ast::AstOperand& value) {
  std::visit(
      [&output](const auto& node) {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, syntax_ast::AstIdentifierRef>) {
          tag(output, "operand-identifier");
          identifier(output, node);
        } else if constexpr (std::same_as<Node,
                                          syntax_ast::AstPredicateOperand>) {
          tag(output, "operand-predicate");
          number(output, node.negated);
          identifier(output, node.name);
        } else if constexpr (std::same_as<Node, syntax_ast::AstImmediate>) {
          tag(output, "operand-immediate");
          immediate(output, node);
        } else if constexpr (std::same_as<Node, syntax_ast::AstAddress>) {
          tag(output, "operand-address");
          number(output, node.bracketed);
          std::visit([&output](const auto& base) { operand(output, base); },
                     node.base);
          tag(output, node.offset ? "offset-present" : "offset-none");
          if (node.offset) {
            number(output, node.offset->operation);
            immediate(output, node.offset->magnitude);
          }
        } else if constexpr (std::same_as<Node, syntax_ast::AstVectorMember>) {
          tag(output, "operand-vector-member");
          identifier(output, node.base);
          atom(output, node.selector.text);
        } else if constexpr (std::same_as<Node, syntax_ast::AstVectorPack>) {
          tag(output, "operand-vector-pack");
          number(output, node.elements.size());
          for (const auto& element : node.elements)
            std::visit([&output](const auto& item) { operand(output, item); },
                       element);
        } else if constexpr (std::same_as<Node,
                                          syntax_ast::AstCallParameterList>) {
          tag(output, "operand-call-parameters");
          number(output, node.kind);
          number(output, node.parameters.size());
          for (const auto& parameter : node.parameters)
            std::visit([&output](const auto& item) { operand(output, item); },
                       parameter);
        } else if constexpr (std::same_as<Node, syntax_ast::AstCallTarget>) {
          tag(output, "operand-call-target");
          identifier(output, node.name);
        } else if constexpr (std::same_as<Node, syntax_ast::AstCallTargetSet>) {
          tag(output, "operand-call-target-set");
          identifier(output, node.name);
        } else if constexpr (std::same_as<Node, syntax_ast::AstBranchTarget>) {
          tag(output, "operand-branch-target");
          identifier(output, node.name);
        } else if constexpr (std::same_as<Node,
                                          syntax_ast::AstBranchTargetSet>) {
          tag(output, "operand-branch-target-set");
          identifier(output, node.name);
        } else if constexpr (std::same_as<
                                 Node, syntax_ast::AstRegisterPredicatePair>) {
          tag(output, "operand-register-predicate-pair");
          identifier(output, node.dst);
          identifier(output, node.predicate);
        } else {
          static_assert(always_false_v<Node>);
        }
      },
      value);
}

/** Encode one instruction opcode, modifiers, operands, and predicate. */
inline void instruction(std::string& output,
                        const syntax_ast::AstInstruction& value) {
  tag(output, "instruction");
  atom(output, value.opcode.syntax.text);
  number(output, value.modifiers.size());
  for (const auto& modifier : value.modifiers)
    atom(output, modifier.syntax.text);
  number(output, value.operands.size());
  for (const auto& value_operand : value.operands)
    operand(output, value_operand);
  tag(output, value.predicate ? "predicate-present" : "predicate-none");
  if (value.predicate) {
    number(output, value.predicate->negated);
    identifier(output, value.predicate->name);
  }
}

/** Encode a local call prototype including availability-checked suffixes. */
inline void call_prototype(std::string& output,
                           const syntax_ast::AstCallPrototype& value) {
  tag(output, "call-prototype");
  identifier(output, value.label);
  number(output, value.return_parameters.size());
  for (const auto& parameter_value : value.return_parameters)
    parameter(output, parameter_value);
  identifier(output, value.sink);
  number(output, value.parameters.size());
  for (const auto& parameter_value : value.parameters)
    parameter(output, parameter_value);
  optional_syntax(output, value.noreturn_directive);
  optional_abi_suffix(output, value.abi_preserve);
  optional_abi_suffix(output, value.abi_preserve_control);
}

/** Encode a call target set and retain source target order. */
inline void call_targets(std::string& output,
                         const syntax_ast::AstCallTargets& value) {
  tag(output, "call-targets");
  identifier(output, value.label);
  number(output, value.targets.size());
  for (const auto& target : value.targets)
    identifier(output, target);
}

/** Encode a branch target set and optional compact target counts. */
inline void branch_targets(std::string& output,
                           const syntax_ast::AstBranchTargets& value) {
  tag(output, "branch-targets");
  identifier(output, value.label);
  number(output, value.targets.size());
  for (const auto& target : value.targets) {
    identifier(output, target.name);
    optional_syntax(output, target.count);
  }
}

/** Encode one nested body, excluding `.loc` and pragma directives by contract. */
inline void function_body(
    std::string& output,
    const std::vector<syntax_ast::AstFunctionBodyItem>& body) {
  tag(output, "body");
  for (const auto& item : body) {
    std::visit(
        [&output](const auto& node) {
          using Node = std::remove_cvref_t<decltype(node)>;
          if constexpr (std::same_as<Node,
                                     syntax_ast::AstVariableDeclaration>) {
            variable_declaration(output, node);
          } else if constexpr (std::same_as<Node, syntax_ast::AstLabel>) {
            tag(output, "label");
            identifier(output, node.name);
          } else if constexpr (std::same_as<Node,
                                            syntax_ast::AstCallPrototype>) {
            call_prototype(output, node);
          } else if constexpr (std::same_as<Node, syntax_ast::AstCallTargets>) {
            call_targets(output, node);
          } else if constexpr (std::same_as<Node,
                                            syntax_ast::AstBranchTargets>) {
            branch_targets(output, node);
          } else if constexpr (std::same_as<Node, std::unique_ptr<
                                                      syntax_ast::AstBlock>>) {
            tag(output, "block");
            if (node)
              function_body(output, node->body);
            else
              tag(output, "null-block");
          } else if constexpr (std::same_as<Node, syntax_ast::AstInstruction>) {
            instruction(output, node);
          } else if constexpr (std::same_as<Node,
                                            syntax_ast::AstLocDirective> ||
                               std::same_as<Node, syntax_ast::AstPragma>) {
            // Debug locations and opaque pragmas are intentionally neutral.
          } else {
            static_assert(always_false_v<Node>);
          }
        },
        item);
  }
  tag(output, "body-end");
}

}  // namespace source_identity

/** Return a range-independent canonical identity for one function declaration. */
inline std::string function_source_identity(
    const syntax_ast::AstFunction& function) {
  std::string output;
  source_identity::tag(output, "function");
  source_identity::number(output, function.is_entry);
  source_identity::number(output, function.is_prototype);
  source_identity::number(output, function.is_noreturn);
  source_identity::number(output, function.qualifiers.size());
  for (const auto& qualifier : function.qualifiers)
    source_identity::atom(output, qualifier.text);
  source_identity::attributes(output, function.attributes);
  source_identity::identifier(output, function.name);
  source_identity::number(output, function.return_parameters.size());
  for (const auto& parameter : function.return_parameters)
    source_identity::parameter(output, parameter);
  source_identity::number(output, function.parameters.size());
  for (const auto& parameter : function.parameters)
    source_identity::parameter(output, parameter);
  source_identity::optional_syntax(output, function.noreturn_directive);
  source_identity::optional_abi_suffix(output, function.abi_preserve);
  source_identity::optional_abi_suffix(output, function.abi_preserve_control);
  source_identity::optional_syntax(output, function.blocks_are_clusters);
  source_identity::tag(
      output, function.language ? "language-present" : "language-none");
  if (function.language) {
    source_identity::number(output, function.language->values.size());
    for (const auto& value : function.language->values)
      source_identity::atom(output, value.text);
  }
  source_identity::tag(output, "resources");
  source_identity::number(output, function.resources.size());
  for (const auto& resource : function.resources) {
    source_identity::number(output, resource.kind);
    source_identity::number(output, resource.values.size());
    for (const auto& value : resource.values)
      source_identity::atom(output, value.text);
  }
  source_identity::function_body(output, function.body);
  return output;
}

/** Return a range-independent identity for resolution-significant module content. */
inline std::string module_source_identity(const syntax_ast::AstModule& module) {
  std::string output;
  std::vector<std::string> function_identities;
  source_identity::tag(output, "module");
  for (const auto& item : module.items) {
    std::visit(
        [&output, &function_identities](const auto& node) {
          using Node = std::remove_cvref_t<decltype(node)>;
          if constexpr (std::same_as<Node, syntax_ast::AstVersionDirective> ||
                        std::same_as<Node, syntax_ast::AstTargetDirective> ||
                        std::same_as<Node, syntax_ast::AstFileDirective> ||
                        std::same_as<Node, syntax_ast::AstSectionDirective> ||
                        std::same_as<Node, syntax_ast::AstPragma>) {
            // Context/debug directives do not alter resolution-owned content.
          } else if constexpr (std::same_as<
                                   Node, syntax_ast::AstAddressSizeDirective>) {
            source_identity::tag(output, "address-size");
            source_identity::atom(output, node.bit_width.text);
          } else if constexpr (std::same_as<
                                   Node, syntax_ast::AstVariableDeclaration>) {
            source_identity::variable_declaration(output, node);
          } else if constexpr (std::same_as<Node,
                                            syntax_ast::AstAliasDirective>) {
            source_identity::tag(output, "alias");
            source_identity::identifier(output, node.alias);
            source_identity::identifier(output, node.aliasee);
          } else if constexpr (std::same_as<Node, syntax_ast::AstFunction>) {
            function_identities.push_back(function_source_identity(node));
          } else {
            static_assert(source_identity::always_false_v<Node>);
          }
        },
        item);
  }
  std::ranges::sort(function_identities);
  source_identity::tag(output, "functions");
  source_identity::number(output, function_identities.size());
  for (const std::string& identity : function_identities)
    source_identity::atom(output, identity);
  return output;
}

}  // namespace ptx_frontend::resolved_ir::detail

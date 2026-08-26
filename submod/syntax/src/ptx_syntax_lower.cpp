#include <ptx_frontend/syntax/ptx_syntax_lower.hpp>

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
    case TokenKind::WarpSz:
      return syntax_ast::AstImmediateKind::WarpSize;
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

syntax_ast::AstConstantUnaryOperator lowerConstantUnaryOperator(
    TokenKind kind) {
  using Operator = syntax_ast::AstConstantUnaryOperator;
  switch (kind) {
    case TokenKind::Plus:
      return Operator::Plus;
    case TokenKind::Minus:
      return Operator::Minus;
    case TokenKind::Exclamation:
      return Operator::LogicalNot;
    case TokenKind::Tilde:
      return Operator::BitwiseNot;
    default:
      throw std::logic_error(
          "CST constant expression has invalid unary operator");
  }
}

syntax_ast::AstConstantBinaryOperator lowerConstantBinaryOperator(
    TokenKind kind) {
  using Operator = syntax_ast::AstConstantBinaryOperator;
  switch (kind) {
    case TokenKind::Star:
      return Operator::Multiply;
    case TokenKind::Slash:
      return Operator::Divide;
    case TokenKind::Percent:
      return Operator::Remainder;
    case TokenKind::Plus:
      return Operator::Add;
    case TokenKind::Minus:
      return Operator::Subtract;
    case TokenKind::ShiftLeft:
      return Operator::ShiftLeft;
    case TokenKind::ShiftRight:
      return Operator::ShiftRight;
    case TokenKind::Lt:
      return Operator::Less;
    case TokenKind::LtEq:
      return Operator::LessEqual;
    case TokenKind::Gt:
      return Operator::Greater;
    case TokenKind::GtEq:
      return Operator::GreaterEqual;
    case TokenKind::EqEq:
      return Operator::Equal;
    case TokenKind::NotEq:
      return Operator::NotEqual;
    case TokenKind::Amp:
      return Operator::BitwiseAnd;
    case TokenKind::Caret:
      return Operator::BitwiseXor;
    case TokenKind::Pipe:
      return Operator::BitwiseOr;
    case TokenKind::AmpAmp:
      return Operator::LogicalAnd;
    case TokenKind::PipePipe:
      return Operator::LogicalOr;
    default:
      throw std::logic_error(
          "CST constant expression has invalid binary operator");
  }
}

syntax_ast::AstConstantExpression lowerConstantExpression(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstConstantExpression& expression) {
  using namespace syntax_ast;
  AstConstantExpressionNode node = std::visit(
      [&cst](const auto& value) -> AstConstantExpressionNode {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_cst::CstConstantLiteral>) {
          const PtxToken& literal = cst.token(value.literal);
          return AstConstantLiteral{AstImmediate{leafSyntax(cst, value.literal),
                                                 immediateKind(literal.kind)}};
        } else if constexpr (std::same_as<Value,
                                          syntax_cst::CstConstantSymbol>) {
          return AstConstantSymbol{lowerIdentifier(cst, {value.name})};
        } else if constexpr (std::same_as<
                                 Value, syntax_cst::CstConstantParenthesized>) {
          return AstConstantParenthesized{
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.expression))};
        } else if constexpr (std::same_as<Value, syntax_cst::CstConstantCall>) {
          return AstConstantCall{
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.callee)),
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.argument))};
        } else if constexpr (std::same_as<Value, syntax_cst::CstConstantCast>) {
          return AstConstantCast{
              leafSyntax(cst, value.type),
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.operand))};
        } else if constexpr (std::same_as<Value,
                                          syntax_cst::CstConstantUnary>) {
          return AstConstantUnary{
              lowerConstantUnaryOperator(cst.token(value.operator_token).kind),
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.operand))};
        } else if constexpr (std::same_as<Value,
                                          syntax_cst::CstConstantBinary>) {
          return AstConstantBinary{
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.left)),
              lowerConstantBinaryOperator(cst.token(value.operator_token).kind),
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.right))};
        } else {
          return AstConstantConditional{
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.condition)),
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.true_expression)),
              std::make_unique<AstConstantExpression>(
                  lowerConstantExpression(cst, *value.false_expression))};
        }
      },
      expression.node);
  return AstConstantExpression{std::move(node),
                               cst.sourceRange(expression.token_range)};
}

syntax_ast::AstInitializer lowerInitializer(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstInitializer& initializer) {
  using namespace syntax_ast;
  const SourceRange range = cst.sourceRange(initializer.token_range);
  if (const auto* expression =
          std::get_if<syntax_cst::CstConstantExpression>(&initializer.value)) {
    return AstInitializer{lowerConstantExpression(cst, *expression), range};
  }

  const auto& list =
      std::get<syntax_cst::CstInitializerList>(initializer.value);
  std::vector<AstInitializer> elements;
  elements.reserve(list.elements.size());
  for (const auto& element : list.elements)
    elements.push_back(lowerInitializer(cst, element));
  return AstInitializer{AstInitializerList{std::move(elements), range}, range};
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

syntax_ast::AstCallParameter lowerCallParameter(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstCallParameter& parameter) {
  return std::visit(
      [&cst](const auto& value) -> syntax_ast::AstCallParameter {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_cst::CstIdentifier>)
          return lowerIdentifier(cst, value);
        else
          return lowerImmediate(cst, value);
      },
      parameter);
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
        } else if constexpr (std::same_as<Value, syntax_cst::CstVectorPack>) {
          std::vector<syntax_ast::AstVectorElement> elements;
          elements.reserve(value.elements.size());
          for (const auto& element : value.elements)
            elements.push_back(lowerVectorElement(cst, element));
          return syntax_ast::AstVectorPack{std::move(elements),
                                           cst.sourceRange(value.token_range)};
        } else if constexpr (std::same_as<Value,
                                          syntax_cst::CstCallParameterList>) {
          std::vector<syntax_ast::AstCallParameter> parameters;
          parameters.reserve(value.parameters.size());
          for (const auto& parameter : value.parameters)
            parameters.push_back(lowerCallParameter(cst, parameter));
          return syntax_ast::AstCallParameterList{
              .kind = value.kind == syntax_cst::CstCallParameterListKind::Return
                          ? syntax_ast::AstCallParameterListKind::Return
                          : syntax_ast::AstCallParameterListKind::Input,
              .parameters = std::move(parameters),
              .range = cst.sourceRange(value.token_range),
          };
        } else if constexpr (std::same_as<Value, syntax_cst::CstCallTarget>) {
          return syntax_ast::AstCallTarget{lowerIdentifier(cst, value.name),
                                           cst.sourceRange(value.token_range)};
        } else if constexpr (std::same_as<Value,
                                          syntax_cst::CstCallTargetSet>) {
          return syntax_ast::AstCallTargetSet{
              lowerIdentifier(cst, value.name),
              cst.sourceRange(value.token_range)};
        } else if constexpr (std::same_as<Value,
                                          syntax_cst::CstBranchTargetSet>) {
          return syntax_ast::AstBranchTargetSet{
              lowerIdentifier(cst, value.name),
              cst.sourceRange(value.token_range)};
        } else {
          return syntax_ast::AstBranchTarget{
              lowerIdentifier(cst, value.name),
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
    std::optional<syntax_ast::AstSyntax> parameterized_count;
    if (declarator.parameterized_count)
      parameterized_count = leafSyntax(cst, *declarator.parameterized_count);

    std::vector<syntax_ast::AstArrayDimension> dimensions;
    dimensions.reserve(declarator.array_dimensions.size());
    for (const auto& dimension : declarator.array_dimensions) {
      std::optional<syntax_ast::AstConstantExpression> size;
      if (dimension.size)
        size = lowerConstantExpression(cst, *dimension.size);
      dimensions.push_back(syntax_ast::AstArrayDimension{
          std::move(size), cst.sourceRange(dimension.token_range)});
    }

    std::optional<syntax_ast::AstInitializer> initializer;
    if (declarator.initializer)
      initializer = lowerInitializer(cst, *declarator.initializer);

    declarators.push_back(syntax_ast::AstVariableDeclarator{
        .name = lowerIdentifier(cst, {declarator.name}),
        .parameterized_count = std::move(parameterized_count),
        .array_dimensions = std::move(dimensions),
        .initializer = std::move(initializer),
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
  std::optional<syntax_ast::AstConstantExpression> array_size;
  if (parameter.array_size)
    array_size = lowerConstantExpression(cst, *parameter.array_size);

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

syntax_ast::AstCallPrototype lowerCallPrototype(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstCallPrototype& prototype) {
  const auto lower_parameters = [&cst](
                                    const std::optional<syntax_cst::CstFunctionParameterList>&
                                        parameters) {
    std::vector<syntax_ast::AstFunctionParameter> lowered;
    if (!parameters)
      return lowered;
    lowered.reserve(parameters->parameters.size());
    for (const auto& parameter : parameters->parameters)
      lowered.push_back(lowerFunctionParameter(cst, parameter));
    return lowered;
  };
  const auto lower_suffix = [&cst](
                                const std::optional<syntax_cst::CstCallPrototypeAbiSuffix>&
                                    suffix)
      -> std::optional<syntax_ast::AstCallPrototypeAbiSuffix> {
    if (!suffix)
      return std::nullopt;
    return syntax_ast::AstCallPrototypeAbiSuffix{
        .directive = leafSyntax(cst, suffix->directive),
        .count = leafSyntax(cst, suffix->count),
        .range = cst.sourceRange(suffix->token_range),
    };
  };
  std::optional<syntax_ast::AstSyntax> noreturn_directive;
  if (prototype.noreturn_directive)
    noreturn_directive = leafSyntax(cst, *prototype.noreturn_directive);
  return syntax_ast::AstCallPrototype{
      .label = lowerIdentifier(cst, {prototype.label}),
      .return_parameters = lower_parameters(prototype.return_parameters),
      .sink = lowerIdentifier(cst, {prototype.sink}),
      .parameters = lower_parameters(prototype.parameters),
      .noreturn_directive = std::move(noreturn_directive),
      .abi_preserve = lower_suffix(prototype.abi_preserve),
      .abi_preserve_control = lower_suffix(prototype.abi_preserve_control),
      .range = cst.sourceRange(prototype.token_range),
  };
}

syntax_ast::AstCallTargets lowerCallTargets(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstCallTargets& targets) {
  std::vector<syntax_ast::AstIdentifierRef> lowered_targets;
  lowered_targets.reserve(targets.targets.size());
  for (const auto target : targets.targets)
    lowered_targets.push_back(lowerIdentifier(cst, {target}));
  return syntax_ast::AstCallTargets{
      .label = lowerIdentifier(cst, {targets.label}),
      .targets = std::move(lowered_targets),
      .range = cst.sourceRange(targets.token_range),
  };
}

syntax_ast::AstBranchTargets lowerBranchTargets(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstBranchTargets& targets) {
  std::vector<syntax_ast::AstBranchTargetEntry> lowered_targets;
  lowered_targets.reserve(targets.targets.size());
  for (const auto& target : targets.targets) {
    std::optional<syntax_ast::AstSyntax> count;
    if (target.count)
      count = leafSyntax(cst, *target.count);
    lowered_targets.push_back(syntax_ast::AstBranchTargetEntry{
        .name = lowerIdentifier(cst, {target.name}),
        .count = std::move(count),
        .range = cst.sourceRange(target.token_range),
    });
  }
  return syntax_ast::AstBranchTargets{
      .label = lowerIdentifier(cst, {targets.label}),
      .targets = std::move(lowered_targets),
      .range = cst.sourceRange(targets.token_range),
  };
}

syntax_ast::AstLocDirective lowerLocDirective(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstLocDirective& location) {
  std::optional<syntax_ast::AstLocInlineContext> inline_context;
  if (location.inline_context) {
    const auto& context = *location.inline_context;
    inline_context = syntax_ast::AstLocInlineContext{
        .function_name_label = lowerIdentifier(cst, {context.function_name_label}),
        .function_name_offset =
            context.function_name_offset
                ? std::optional{leafSyntax(cst, *context.function_name_offset)}
                : std::nullopt,
        .file_index = leafSyntax(cst, context.file_index),
        .line_number = leafSyntax(cst, context.line_number),
        .column_position = leafSyntax(cst, context.column_position),
        .range = cst.sourceRange(context.token_range),
    };
  }
  return syntax_ast::AstLocDirective{
      .file_index = leafSyntax(cst, location.file_index),
      .line_number = leafSyntax(cst, location.line_number),
      .column_position = leafSyntax(cst, location.column_position),
      .inline_context = std::move(inline_context),
      .range = cst.sourceRange(location.token_range),
  };
}

syntax_ast::AstPragma lowerPragma(const syntax_cst::CstFile& cst,
                                  const syntax_cst::CstPragma& pragma) {
  std::vector<syntax_ast::AstSyntax> strings;
  strings.reserve(pragma.strings.size());
  for (const auto string : pragma.strings)
    strings.push_back(leafSyntax(cst, string));
  return syntax_ast::AstPragma{
      .strings = std::move(strings),
      .range = cst.sourceRange(pragma.token_range),
  };
}

syntax_ast::AstKernelResourceDirective lowerKernelResourceDirective(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstKernelResourceDirective& resource) {
  syntax_ast::AstKernelResourceKind kind;
  switch (cst.token(resource.directive).kind) {
    case TokenKind::DotMaxnreg:
      kind = syntax_ast::AstKernelResourceKind::MaxNreg;
      break;
    case TokenKind::DotMaxntid:
      kind = syntax_ast::AstKernelResourceKind::MaxNtid;
      break;
    case TokenKind::DotReqntid:
      kind = syntax_ast::AstKernelResourceKind::ReqNtid;
      break;
    case TokenKind::DotMinnctapersm:
      kind = syntax_ast::AstKernelResourceKind::MinNctaPerSm;
      break;
    default:
      throw std::logic_error("invalid kernel resource directive in CST");
  }
  std::vector<syntax_ast::AstSyntax> values;
  values.reserve(resource.values.size());
  for (const auto value : resource.values)
    values.push_back(leafSyntax(cst, value));
  return syntax_ast::AstKernelResourceDirective{
      .kind = kind,
      .values = std::move(values),
      .range = cst.sourceRange(resource.token_range),
  };
}

syntax_ast::AstBlock lowerBlock(const syntax_cst::CstFile& cst,
                                const syntax_cst::CstBlock& block);

syntax_ast::AstFunctionBodyItem lowerFunctionBodyItem(
    const syntax_cst::CstFile& cst,
    const syntax_cst::CstFunctionBodyItem& body_item) {
  if (const auto* declaration =
          std::get_if<syntax_cst::CstVariableDeclaration>(&body_item)) {
    return lowerVariableDeclaration(cst, *declaration);
  }
  if (const auto* label = std::get_if<syntax_cst::CstLabel>(&body_item)) {
    return syntax_ast::AstLabel{lowerIdentifier(cst, {label->name}),
                                cst.sourceRange(label->token_range)};
  }
  if (const auto* prototype =
          std::get_if<syntax_cst::CstCallPrototype>(&body_item)) {
    return lowerCallPrototype(cst, *prototype);
  }
  if (const auto* targets =
          std::get_if<syntax_cst::CstCallTargets>(&body_item)) {
    return lowerCallTargets(cst, *targets);
  }
  if (const auto* targets =
          std::get_if<syntax_cst::CstBranchTargets>(&body_item)) {
    return lowerBranchTargets(cst, *targets);
  }
  if (const auto* location =
          std::get_if<syntax_cst::CstLocDirective>(&body_item)) {
    return lowerLocDirective(cst, *location);
  }
  if (const auto* pragma = std::get_if<syntax_cst::CstPragma>(&body_item))
    return lowerPragma(cst, *pragma);
  if (const auto* block =
          std::get_if<std::unique_ptr<syntax_cst::CstBlock>>(&body_item)) {
    return std::make_unique<syntax_ast::AstBlock>(lowerBlock(cst, **block));
  }
  return lowerInstructionNode(
      cst, std::get<syntax_cst::CstInstruction>(body_item));
}

syntax_ast::AstBlock lowerBlock(const syntax_cst::CstFile& cst,
                                const syntax_cst::CstBlock& block) {
  std::vector<syntax_ast::AstFunctionBodyItem> body;
  body.reserve(block.body.size());
  for (const auto& item : block.body) {
    if (std::holds_alternative<syntax_cst::CstRecoveryNode>(item))
      continue;
    body.push_back(lowerFunctionBodyItem(cst, item));
  }
  return syntax_ast::AstBlock{
      .body = std::move(body),
      .range = cst.sourceRange(block.token_range),
  };
}

}  // namespace

AstInstructionLowerResult lowerSyntaxInstruction(
    const syntax_cst::CstFile& cst) {
  const auto* root = cst.instruction();
  if (root == nullptr) {
    return {.value = std::nullopt,
            .diagnostics = {AstLowerDiagnostic{
                .range = {},
                .message = "expected an instruction-fragment CST root",
            }}};
  }
  return {.value = lowerInstructionNode(cst, *root), .diagnostics = {}};
}

AstModuleLowerResult lowerSyntaxModule(const syntax_cst::CstFile& cst) {
  const auto* root = cst.module();
  if (root == nullptr) {
    return {.value = std::nullopt,
            .diagnostics = {AstLowerDiagnostic{
                .range = {},
                .message = "expected a module CST root",
            }}};
  }

  syntax_ast::AstModule ast{
      .items = {},
      .range = cst.sourceRange(root->token_range),
  };
  ast.items.reserve(root->items.size());

  for (const auto& item : root->items) {
    if (std::holds_alternative<syntax_cst::CstRecoveryNode>(item))
      continue;
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
        case TokenKind::DotFile:
          if (directive->arguments.size() != 2 &&
              directive->arguments.size() != 4) {
            return {.value = std::nullopt,
                    .diagnostics = {AstLowerDiagnostic{
                        .range = range,
                        .message = "invalid .file directive payload in CST",
                    }}};
          }
          ast.items.emplace_back(syntax_ast::AstFileDirective{
              .file_index = leafSyntax(cst, directive->arguments.at(0)),
              .filename = leafSyntax(cst, directive->arguments.at(1)),
              .timestamp = directive->arguments.size() == 4
                               ? std::optional{leafSyntax(
                                     cst, directive->arguments.at(2))}
                               : std::nullopt,
              .file_size = directive->arguments.size() == 4
                               ? std::optional{leafSyntax(
                                     cst, directive->arguments.at(3))}
                               : std::nullopt,
              .range = range,
          });
          break;
        default:
          return {.value = std::nullopt,
                  .diagnostics = {AstLowerDiagnostic{
                      .range = cst.token(directive->keyword).range,
                      .message = "unsupported module directive in CST",
                  }}};
      }
      continue;
    }

    if (const auto* section =
            std::get_if<syntax_cst::CstSectionDirective>(&item)) {
      std::vector<syntax_ast::AstSyntax> payload;
      payload.reserve(section->payload.size());
      for (const auto token : section->payload)
        payload.push_back(leafSyntax(cst, token));
      ast.items.emplace_back(syntax_ast::AstSectionDirective{
          .name = leafSyntax(cst, section->name),
          .payload = std::move(payload),
          .range = cst.sourceRange(section->token_range),
      });
      continue;
    }

    if (const auto* pragma = std::get_if<syntax_cst::CstPragma>(&item)) {
      ast.items.emplace_back(lowerPragma(cst, *pragma));
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
        .pragmas = {},
        .resources = {},
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
    lowered.pragmas.reserve(function.pragmas.size());
    for (const auto& pragma : function.pragmas)
      lowered.pragmas.push_back(lowerPragma(cst, pragma));
    lowered.resources.reserve(function.resources.size());
    for (const auto& resource : function.resources)
      lowered.resources.push_back(lowerKernelResourceDirective(cst, resource));
    lowered.body.reserve(function.body.size());
    for (const auto& body_item : function.body) {
      if (std::holds_alternative<syntax_cst::CstRecoveryNode>(body_item))
        continue;
      lowered.body.push_back(lowerFunctionBodyItem(cst, body_item));
    }
    ast.items.emplace_back(std::move(lowered));
  }
  return {.value = std::move(ast), .diagnostics = {}};
}

}  // namespace ptx_frontend

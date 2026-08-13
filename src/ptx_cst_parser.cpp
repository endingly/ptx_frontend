#include "ptx_ir/cst/ptx_cst_parser.hpp"

#include <stdexcept>
#include <utility>

namespace ptx_frontend::syntax_cst {

const PtxToken& CstFile::token(TokenId id) const {
  return tokens.at(id);
}

SourceRange CstFile::sourceRange(CstTokenRange range) const {
  if (range.first >= range.last || range.last > tokens.size())
    throw std::out_of_range("invalid CST token range");
  return SourceRange{token(range.first).range.start,
                     token(range.last - 1).range.end};
}

std::string CstFile::sourceText() const {
  std::string result;
  for (const PtxToken& token : tokens) {
    for (const Trivia& trivia : token.leading_trivia)
      result += trivia.text;
    result += token.text;
  }
  return result;
}

const CstInstruction* CstFile::instruction() const noexcept {
  return std::get_if<CstInstruction>(&root);
}

const CstModule* CstFile::module() const noexcept {
  return std::get_if<CstModule>(&root);
}

}  // namespace ptx_frontend::syntax_cst

namespace ptx_frontend {
namespace {

bool isImmediate(TokenKind kind) {
  switch (kind) {
    case TokenKind::Decimal:
    case TokenKind::Hex:
    case TokenKind::F32Hex:
    case TokenKind::F64Hex:
    case TokenKind::F64:
      return true;
    default:
      return false;
  }
}

bool isModifier(TokenKind kind) {
  return kind == TokenKind::DotIdent || kind == TokenKind::DotGlobal ||
         kind == TokenKind::DotConst || kind == TokenKind::DotShared ||
         kind == TokenKind::DotLocal || kind == TokenKind::DotParam;
}

bool isModuleDirective(TokenKind kind) {
  return kind == TokenKind::DotVersion || kind == TokenKind::DotTarget ||
         kind == TokenKind::DotAddressSize;
}

bool isFunctionQualifier(TokenKind kind) {
  return kind == TokenKind::DotExtern || kind == TokenKind::DotVisible ||
         kind == TokenKind::DotWeak;
}

bool isVariableStateSpace(TokenKind kind) {
  return kind == TokenKind::DotReg || kind == TokenKind::DotParam ||
         kind == TokenKind::DotLocal || kind == TokenKind::DotShared ||
         kind == TokenKind::DotGlobal || kind == TokenKind::DotConst;
}

}  // namespace

PtxCstParser::PtxCstParser(std::string_view source) : lexer_(source) {}

PtxCstParser::TokenId PtxCstParser::peek() {
  if (!peeked_) {
    tokens_.push_back(lexer_.consume());
    peeked_ = static_cast<TokenId>(tokens_.size() - 1);
  }
  return *peeked_;
}

PtxCstParser::TokenId PtxCstParser::consume() {
  const TokenId id = peek();
  peeked_.reset();
  return id;
}

const PtxToken& PtxCstParser::token(TokenId id) const {
  return tokens_.at(id);
}

bool PtxCstParser::atImmediateStart() {
  const TokenKind kind = token(peek()).kind;
  return isImmediate(kind) || kind == TokenKind::Plus ||
         kind == TokenKind::Minus;
}

std::expected<PtxCstParser::TokenId, CstParseDiagnostic> PtxCstParser::expect(
    TokenKind kind, std::string_view name) {
  const TokenId id = consume();
  if (token(id).kind == kind)
    return id;
  return std::unexpected(
      CstParseDiagnostic{token(id).range, "expected " + std::string(name)});
}

std::expected<syntax_cst::CstImmediate, CstParseDiagnostic>
PtxCstParser::parseImmediate(bool allow_sign) {
  std::optional<TokenId> sign;
  TokenId first = peek();
  if (allow_sign && (token(first).kind == TokenKind::Plus ||
                     token(first).kind == TokenKind::Minus)) {
    sign = consume();
    first = *sign;
  }

  const TokenId literal = consume();
  if (!isImmediate(token(literal).kind)) {
    return std::unexpected(
        CstParseDiagnostic{token(literal).range, "expected immediate literal"});
  }
  return syntax_cst::CstImmediate{sign, literal, {first, literal + 1}};
}

std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseBracketedAddress(TokenId open) {
  const TokenId first = open;
  syntax_cst::CstAddressBase base;
  TokenId last = first;

  if (atImmediateStart()) {
    auto immediate = parseImmediate();
    if (!immediate)
      return std::unexpected(immediate.error());
    last = immediate->token_range.last - 1;
    base = std::move(*immediate);
  } else {
    auto identifier = expect(TokenKind::Ident, "address base");
    if (!identifier)
      return std::unexpected(identifier.error());
    last = *identifier;
    base = syntax_cst::CstIdentifier{*identifier};
  }

  std::optional<syntax_cst::CstAddressOffset> offset;
  if (token(peek()).kind == TokenKind::Plus ||
      token(peek()).kind == TokenKind::Minus) {
    const TokenId op = consume();
    auto magnitude = parseImmediate(false);
    if (!magnitude)
      return std::unexpected(magnitude.error());
    last = magnitude->token_range.last - 1;
    offset =
        syntax_cst::CstAddressOffset{op, std::move(*magnitude), {op, last + 1}};
  }

  auto close = expect(TokenKind::RBracket, "']'");
  if (!close)
    return std::unexpected(close.error());
  last = *close;

  return syntax_cst::CstOperand{syntax_cst::CstAddress{
      open, std::move(base), std::move(offset), *close, {first, last + 1}}};
}

std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseVectorPack(TokenId open) {
  std::vector<syntax_cst::CstVectorElement> elements;
  std::vector<TokenId> commas;

  if (token(peek()).kind == TokenKind::RBrace) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "vector operand cannot be empty"});
  }

  for (;;) {
    if (token(peek()).kind == TokenKind::Ident) {
      elements.emplace_back(syntax_cst::CstIdentifier{consume()});
    } else if (atImmediateStart()) {
      auto immediate = parseImmediate();
      if (!immediate)
        return std::unexpected(immediate.error());
      elements.emplace_back(std::move(*immediate));
    } else {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          "expected identifier or immediate vector element"});
    }

    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
  }

  auto close = expect(TokenKind::RBrace, "'}'");
  if (!close)
    return std::unexpected(close.error());
  return syntax_cst::CstOperand{syntax_cst::CstVectorPack{open,
                                                          std::move(elements),
                                                          std::move(commas),
                                                          *close,
                                                          {open, *close + 1}}};
}

std::expected<syntax_cst::CstOperand, CstParseDiagnostic>
PtxCstParser::parseOperand() {
  if (token(peek()).kind == TokenKind::Exclamation) {
    const TokenId exclamation = consume();
    auto name = expect(TokenKind::Ident, "predicate operand");
    if (!name)
      return std::unexpected(name.error());
    return syntax_cst::CstOperand{syntax_cst::CstPredicateOperand{
        exclamation, *name, {exclamation, *name + 1}}};
  }
  if (token(peek()).kind == TokenKind::LBracket)
    return parseBracketedAddress(consume());
  if (token(peek()).kind == TokenKind::LBrace)
    return parseVectorPack(consume());
  if (atImmediateStart()) {
    auto immediate = parseImmediate();
    if (!immediate)
      return std::unexpected(immediate.error());
    return syntax_cst::CstOperand{std::move(*immediate)};
  }

  auto identifier = expect(TokenKind::Ident, "operand");
  if (!identifier)
    return std::unexpected(identifier.error());
  const syntax_cst::CstIdentifier base{*identifier};

  if (token(peek()).kind == TokenKind::DotIdent) {
    const TokenId selector = consume();
    return syntax_cst::CstOperand{syntax_cst::CstVectorMember{
        base, selector, {*identifier, selector + 1}}};
  }
  if (token(peek()).kind == TokenKind::Plus ||
      token(peek()).kind == TokenKind::Minus) {
    const TokenId op = consume();
    auto magnitude = parseImmediate(false);
    if (!magnitude)
      return std::unexpected(magnitude.error());
    const TokenId last = magnitude->token_range.last - 1;
    syntax_cst::CstAddressOffset offset{
        op, std::move(*magnitude), {op, last + 1}};
    return syntax_cst::CstOperand{
        syntax_cst::CstAddress{std::nullopt,
                               base,
                               std::move(offset),
                               std::nullopt,
                               {*identifier, last + 1}}};
  }
  return syntax_cst::CstOperand{base};
}

std::expected<syntax_cst::CstInstruction, CstParseDiagnostic>
PtxCstParser::parseInstructionNode(std::optional<TokenId> supplied_opcode) {
  std::optional<syntax_cst::CstPredicate> predicate;
  TokenId first = supplied_opcode.value_or(peek());
  if (!supplied_opcode && token(peek()).kind == TokenKind::At) {
    const TokenId at = consume();
    std::optional<TokenId> exclamation;
    if (token(peek()).kind == TokenKind::Exclamation)
      exclamation = consume();
    auto name = expect(TokenKind::Ident, "predicate identifier");
    if (!name)
      return std::unexpected(name.error());
    predicate =
        syntax_cst::CstPredicate{at, exclamation, *name, {at, *name + 1}};
  }

  std::expected<TokenId, CstParseDiagnostic> opcode =
      supplied_opcode
          ? std::expected<TokenId, CstParseDiagnostic>{*supplied_opcode}
          : expect(TokenKind::Ident, "instruction opcode");
  if (!opcode)
    return std::unexpected(opcode.error());
  if (!predicate)
    first = *opcode;

  std::vector<TokenId> modifiers;
  while (isModifier(token(peek()).kind))
    modifiers.push_back(consume());

  std::vector<syntax_cst::CstOperandElement> operands;
  if (token(peek()).kind != TokenKind::Semicolon) {
    for (;;) {
      auto operand = parseOperand();
      if (!operand)
        return std::unexpected(operand.error());
      syntax_cst::CstOperandElement element{std::move(*operand), std::nullopt};
      if (token(peek()).kind == TokenKind::Comma)
        element.trailing_comma = consume();
      const bool has_comma = element.trailing_comma.has_value();
      operands.push_back(std::move(element));
      if (!has_comma)
        break;
    }
  }

  auto semicolon = expect(TokenKind::Semicolon, "';'");
  if (!semicolon)
    return std::unexpected(semicolon.error());

  return syntax_cst::CstInstruction{
      std::move(predicate), *opcode,    std::move(modifiers),
      std::move(operands),  *semicolon, {first, *semicolon + 1}};
}

std::expected<syntax_cst::CstVariableDeclaration, CstParseDiagnostic>
PtxCstParser::parseVariableDeclaration(std::vector<TokenId> qualifiers,
                                       std::optional<TokenId> first_token) {
  const TokenId first = first_token.value_or(peek());
  const TokenId state_space = consume();
  if (!isVariableStateSpace(token(state_space).kind)) {
    return std::unexpected(CstParseDiagnostic{token(state_space).range,
                                              "expected variable state space"});
  }

  std::optional<TokenId> align_directive;
  std::optional<TokenId> alignment;
  if (token(peek()).kind == TokenKind::DotAlign) {
    align_directive = consume();
    auto value = expect(TokenKind::Decimal, "variable alignment");
    if (!value)
      return std::unexpected(value.error());
    alignment = *value;
  }

  std::optional<TokenId> vector_type;
  if (token(peek()).kind == TokenKind::DotIdent &&
      (token(peek()).text == ".v2" || token(peek()).text == ".v4")) {
    vector_type = consume();
  }

  auto type = expect(TokenKind::DotIdent, "variable type");
  if (!type)
    return std::unexpected(type.error());

  std::vector<syntax_cst::CstVariableDeclarator> declarators;
  std::vector<TokenId> commas;
  for (;;) {
    auto name = expect(TokenKind::Ident, "variable name");
    if (!name)
      return std::unexpected(name.error());

    std::optional<TokenId> left_angle;
    std::optional<TokenId> register_count;
    std::optional<TokenId> right_angle;
    std::vector<syntax_cst::CstArrayDimension> array_dimensions;
    TokenId last = *name;
    if (token(peek()).kind == TokenKind::Lt) {
      if (token(state_space).kind != TokenKind::DotReg) {
        return std::unexpected(CstParseDiagnostic{
            token(peek()).range,
            "register-bank declarator requires '.reg' state space"});
      }
      left_angle = consume();
      auto count_token = expect(TokenKind::Decimal, "register count");
      if (!count_token)
        return std::unexpected(count_token.error());
      register_count = *count_token;
      auto close = expect(TokenKind::Gt, "'>'");
      if (!close)
        return std::unexpected(close.error());
      right_angle = *close;
      last = *close;
    }

    while (token(peek()).kind == TokenKind::LBracket) {
      const TokenId left_bracket = consume();
      std::vector<TokenId> size_tokens;
      while (token(peek()).kind != TokenKind::RBracket) {
        if (token(peek()).kind == TokenKind::Eof ||
            token(peek()).kind == TokenKind::Semicolon) {
          return std::unexpected(CstParseDiagnostic{
              token(peek()).range, "expected ']' in array declarator"});
        }
        size_tokens.push_back(consume());
      }
      const TokenId right_bracket = consume();
      array_dimensions.push_back(
          syntax_cst::CstArrayDimension{left_bracket,
                                        std::move(size_tokens),
                                        right_bracket,
                                        {left_bracket, right_bracket + 1}});
      last = right_bracket;
    }
    declarators.push_back(
        syntax_cst::CstVariableDeclarator{*name,
                                          left_angle,
                                          register_count,
                                          right_angle,
                                          std::move(array_dimensions),
                                          {*name, last + 1}});

    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
  }

  auto semicolon = expect(TokenKind::Semicolon, "';'");
  if (!semicolon)
    return std::unexpected(semicolon.error());
  return syntax_cst::CstVariableDeclaration{
      .qualifiers = std::move(qualifiers),
      .state_space = state_space,
      .align_directive = align_directive,
      .alignment = alignment,
      .vector_type = vector_type,
      .type = *type,
      .declarators = std::move(declarators),
      .commas = std::move(commas),
      .semicolon = *semicolon,
      .token_range = {first, *semicolon + 1},
  };
}

std::expected<syntax_cst::CstFunctionParameter, CstParseDiagnostic>
PtxCstParser::parseFunctionParameter() {
  const TokenId state_space = consume();
  if (token(state_space).kind != TokenKind::DotReg &&
      token(state_space).kind != TokenKind::DotParam) {
    return std::unexpected(CstParseDiagnostic{
        token(state_space).range, "expected '.reg' or '.param' parameter"});
  }

  std::optional<TokenId> align_directive;
  std::optional<TokenId> alignment;
  if (token(peek()).kind == TokenKind::DotAlign) {
    align_directive = consume();
    auto value = expect(TokenKind::Decimal, "parameter alignment");
    if (!value)
      return std::unexpected(value.error());
    alignment = *value;
  }

  auto type = expect(TokenKind::DotIdent, "parameter type");
  if (!type)
    return std::unexpected(type.error());

  std::optional<TokenId> pointer_directive;
  std::optional<TokenId> pointer_space;
  std::optional<TokenId> pointer_align_directive;
  std::optional<TokenId> pointer_alignment;
  if (token(peek()).kind == TokenKind::DotPtr) {
    pointer_directive = consume();
    const TokenKind space_kind = token(peek()).kind;
    if (space_kind == TokenKind::DotConst ||
        space_kind == TokenKind::DotGlobal ||
        space_kind == TokenKind::DotLocal ||
        space_kind == TokenKind::DotShared) {
      pointer_space = consume();
    }
    if (token(peek()).kind == TokenKind::DotAlign) {
      pointer_align_directive = consume();
      auto value = expect(TokenKind::Decimal, "pointer alignment");
      if (!value)
        return std::unexpected(value.error());
      pointer_alignment = *value;
    }
  }

  auto name = expect(TokenKind::Ident, "parameter name");
  if (!name)
    return std::unexpected(name.error());

  std::optional<TokenId> left_bracket;
  std::optional<TokenId> array_size;
  std::optional<TokenId> right_bracket;
  TokenId last = *name;
  if (token(peek()).kind == TokenKind::LBracket) {
    left_bracket = consume();
    if (token(peek()).kind == TokenKind::Decimal)
      array_size = consume();
    auto close = expect(TokenKind::RBracket, "']'");
    if (!close)
      return std::unexpected(close.error());
    right_bracket = *close;
    last = *close;
  }

  return syntax_cst::CstFunctionParameter{
      state_space,
      align_directive,
      alignment,
      *type,
      pointer_directive,
      pointer_space,
      pointer_align_directive,
      pointer_alignment,
      *name,
      left_bracket,
      array_size,
      right_bracket,
      {state_space, last + 1},
  };
}

std::expected<syntax_cst::CstFunctionParameterList, CstParseDiagnostic>
PtxCstParser::parseFunctionParameterList() {
  auto left = expect(TokenKind::LParen, "'('");
  if (!left)
    return std::unexpected(left.error());

  std::vector<syntax_cst::CstFunctionParameter> parameters;
  std::vector<TokenId> commas;
  if (token(peek()).kind != TokenKind::RParen) {
    for (;;) {
      auto parameter = parseFunctionParameter();
      if (!parameter)
        return std::unexpected(parameter.error());
      parameters.push_back(std::move(*parameter));
      if (token(peek()).kind != TokenKind::Comma)
        break;
      commas.push_back(consume());
    }
  }

  auto right = expect(TokenKind::RParen, "')'");
  if (!right)
    return std::unexpected(right.error());
  return syntax_cst::CstFunctionParameterList{*left,
                                              std::move(parameters),
                                              std::move(commas),
                                              *right,
                                              {*left, *right + 1}};
}

std::expected<syntax_cst::CstFile, CstParseDiagnostic>
PtxCstParser::parseInstruction() {
  auto root = parseInstructionNode();
  if (!root)
    return std::unexpected(root.error());

  const TokenId eof = peek();
  if (token(eof).kind != TokenKind::Eof) {
    return std::unexpected(
        CstParseDiagnostic{token(eof).range, "expected end of input"});
  }
  return syntax_cst::CstFile{std::move(tokens_), std::move(*root)};
}

std::expected<syntax_cst::CstModuleDirective, CstParseDiagnostic>
PtxCstParser::parseModuleDirective() {
  const TokenId keyword = consume();
  std::vector<TokenId> arguments;
  std::vector<TokenId> separators;

  switch (token(keyword).kind) {
    case TokenKind::DotVersion: {
      auto version = expect(TokenKind::F64, "PTX version");
      if (!version)
        return std::unexpected(version.error());
      arguments.push_back(*version);
      break;
    }
    case TokenKind::DotTarget: {
      auto target = expect(TokenKind::Ident, "target architecture");
      if (!target)
        return std::unexpected(target.error());
      arguments.push_back(*target);
      while (token(peek()).kind == TokenKind::Comma) {
        separators.push_back(consume());
        auto option = expect(TokenKind::Ident, "target option");
        if (!option)
          return std::unexpected(option.error());
        arguments.push_back(*option);
      }
      break;
    }
    case TokenKind::DotAddressSize: {
      auto size = expect(TokenKind::Decimal, "address size");
      if (!size)
        return std::unexpected(size.error());
      arguments.push_back(*size);
      break;
    }
    default:
      return std::unexpected(CstParseDiagnostic{
          token(keyword).range, "expected supported module directive"});
  }

  std::optional<TokenId> terminator;
  if (token(peek()).kind == TokenKind::Semicolon)
    terminator = consume();
  const TokenId last = terminator.value_or(arguments.back());
  return syntax_cst::CstModuleDirective{keyword,
                                        std::move(arguments),
                                        std::move(separators),
                                        terminator,
                                        {keyword, last + 1}};
}

std::expected<syntax_cst::CstFunction, CstParseDiagnostic>
PtxCstParser::parseFunction(std::vector<TokenId> qualifiers,
                            TokenId first_token) {
  const TokenId first = first_token;
  std::vector<TokenId> header_tokens = qualifiers;

  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotEntry &&
      token(directive).kind != TokenKind::DotFunc) {
    return std::unexpected(CstParseDiagnostic{token(directive).range,
                                              "expected '.entry' or '.func'"});
  }
  header_tokens.push_back(directive);

  const auto append_range = [&header_tokens](syntax_cst::CstTokenRange range) {
    for (TokenId id = range.first; id < range.last; ++id)
      header_tokens.push_back(id);
  };

  std::optional<syntax_cst::CstFunctionParameterList> return_parameters;
  if (token(directive).kind == TokenKind::DotFunc &&
      token(peek()).kind == TokenKind::LParen) {
    auto parameters = parseFunctionParameterList();
    if (!parameters)
      return std::unexpected(parameters.error());
    append_range(parameters->token_range);
    return_parameters = std::move(*parameters);
  }

  auto name = expect(TokenKind::Ident, "function name");
  if (!name)
    return std::unexpected(name.error());
  header_tokens.push_back(*name);

  std::optional<syntax_cst::CstFunctionParameterList> parameters;
  if (token(peek()).kind == TokenKind::LParen) {
    auto parsed = parseFunctionParameterList();
    if (!parsed)
      return std::unexpected(parsed.error());
    append_range(parsed->token_range);
    parameters = std::move(*parsed);
  }

  std::optional<TokenId> noreturn_directive;
  if (token(peek()).kind == TokenKind::DotNoreturn) {
    noreturn_directive = consume();
    header_tokens.push_back(*noreturn_directive);
  }

  while (token(peek()).kind != TokenKind::LBrace &&
         token(peek()).kind != TokenKind::Semicolon) {
    const TokenId id = consume();
    if (token(id).kind == TokenKind::Eof) {
      return std::unexpected(CstParseDiagnostic{
          token(id).range, "expected function body or prototype terminator"});
    }
    header_tokens.push_back(id);
  }

  if (token(peek()).kind == TokenKind::Semicolon) {
    const TokenId terminator = consume();
    if (token(directive).kind != TokenKind::DotFunc) {
      return std::unexpected(CstParseDiagnostic{
          token(terminator).range, "an entry function must have a body"});
    }
    return syntax_cst::CstFunction{
        .qualifiers = std::move(qualifiers),
        .directive = directive,
        .return_parameters = std::move(return_parameters),
        .name = *name,
        .parameters = std::move(parameters),
        .noreturn_directive = noreturn_directive,
        .header_tokens = std::move(header_tokens),
        .left_brace = std::nullopt,
        .body = {},
        .right_brace = std::nullopt,
        .terminator = terminator,
        .token_range = {first, terminator + 1},
    };
  }

  const TokenId left_brace = consume();

  std::vector<syntax_cst::CstFunctionBodyItem> body;
  while (token(peek()).kind != TokenKind::RBrace) {
    if (token(peek()).kind == TokenKind::Eof) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range, "expected '}' at end of function body"});
    }
    if (isVariableStateSpace(token(peek()).kind)) {
      auto declaration = parseVariableDeclaration();
      if (!declaration)
        return std::unexpected(declaration.error());
      body.emplace_back(std::move(*declaration));
      continue;
    }

    if (token(peek()).kind == TokenKind::Ident) {
      const TokenId first_token = consume();
      if (token(peek()).kind == TokenKind::Colon) {
        const TokenId colon = consume();
        body.emplace_back(
            syntax_cst::CstLabel{first_token, colon, {first_token, colon + 1}});
        continue;
      }
      auto instruction = parseInstructionNode(first_token);
      if (!instruction)
        return std::unexpected(instruction.error());
      body.emplace_back(std::move(*instruction));
      continue;
    }

    auto instruction = parseInstructionNode();
    if (!instruction)
      return std::unexpected(instruction.error());
    body.emplace_back(std::move(*instruction));
  }
  const TokenId right_brace = consume();

  return syntax_cst::CstFunction{
      .qualifiers = std::move(qualifiers),
      .directive = directive,
      .return_parameters = std::move(return_parameters),
      .name = *name,
      .parameters = std::move(parameters),
      .noreturn_directive = noreturn_directive,
      .header_tokens = std::move(header_tokens),
      .left_brace = left_brace,
      .body = std::move(body),
      .right_brace = right_brace,
      .terminator = std::nullopt,
      .token_range = {first, right_brace + 1},
  };
}

std::expected<syntax_cst::CstFile, CstParseDiagnostic>
PtxCstParser::parseModule() {
  std::vector<syntax_cst::CstModuleItem> items;
  const TokenId first = peek();
  TokenId last = first;

  while (token(peek()).kind != TokenKind::Eof) {
    const TokenId item_first = peek();
    std::vector<TokenId> qualifiers;
    while (isFunctionQualifier(token(peek()).kind))
      qualifiers.push_back(consume());

    if (qualifiers.empty() && isModuleDirective(token(peek()).kind)) {
      auto directive = parseModuleDirective();
      if (!directive)
        return std::unexpected(directive.error());
      last = directive->token_range.last - 1;
      items.emplace_back(std::move(*directive));
      continue;
    }

    if (token(peek()).kind == TokenKind::DotEntry ||
        token(peek()).kind == TokenKind::DotFunc) {
      auto function = parseFunction(std::move(qualifiers), item_first);
      if (!function)
        return std::unexpected(function.error());
      last = function->token_range.last - 1;
      items.emplace_back(std::move(*function));
      continue;
    }

    if (isVariableStateSpace(token(peek()).kind)) {
      auto declaration =
          parseVariableDeclaration(std::move(qualifiers), item_first);
      if (!declaration)
        return std::unexpected(declaration.error());
      last = declaration->token_range.last - 1;
      items.emplace_back(std::move(*declaration));
      continue;
    }

    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "expected module directive, variable declaration, or function"});
  }

  if (items.empty()) {
    return std::unexpected(
        CstParseDiagnostic{token(first).range, "expected module item"});
  }
  syntax_cst::CstModule module{std::move(items), {first, last + 1}};
  return syntax_cst::CstFile{std::move(tokens_), std::move(module)};
}

}  // namespace ptx_frontend

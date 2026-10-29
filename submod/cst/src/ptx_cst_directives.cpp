#include "ptx_cst_parser_detail.hpp"

#include <ptx_frontend/base/ptx_target.hpp>

#include <algorithm>
#include <utility>

namespace ptx_frontend {
using namespace cst_detail;

/** Parse source-location metadata without interpreting file or scope identities. */
std::expected<syntax_cst::CstLocDirective, CstParseDiagnostic>
PtxCstParser::parseLocDirective() {
  const TokenId directive = consume();
  if (token(directive).kind != TokenKind::DotLoc) {
    return std::unexpected(
        CstParseDiagnostic{token(directive).range, "expected '.loc'"});
  }
  auto file_index = expectIntegerLiteral("source file index");
  if (!file_index)
    return std::unexpected(file_index.error());
  auto line_number = expect(TokenKind::Decimal, "source line number");
  if (!line_number)
    return std::unexpected(line_number.error());
  auto column_position = expect(TokenKind::Decimal, "source column position");
  if (!column_position)
    return std::unexpected(column_position.error());

  std::optional<syntax_cst::CstLocInlineContext> inline_context;
  TokenId last = *column_position;
  if (token(peek()).kind == TokenKind::Comma) {
    const TokenId function_name_comma = consume();
    const TokenId function_name_keyword = consume();
    if (token(function_name_keyword).kind != TokenKind::Ident ||
        token(function_name_keyword).text != "function_name") {
      return std::unexpected(CstParseDiagnostic{
          token(function_name_keyword).range, "expected 'function_name'"});
    }
    const TokenId function_name_label = consume();
    if (!isIdentifierToken(token(function_name_label).kind)) {
      return std::unexpected(CstParseDiagnostic{
          token(function_name_label).range, "expected function name label"});
    }
    std::optional<TokenId> plus;
    std::optional<TokenId> function_name_offset;
    if (token(peek()).kind == TokenKind::Plus) {
      plus = consume();
      const TokenId offset = consume();
      if (!isIntegerLiteral(token(offset).kind)) {
        return std::unexpected(CstParseDiagnostic{
            token(offset).range, "expected integer function name offset"});
      }
      function_name_offset = offset;
    }
    auto inlined_at_comma = expect(TokenKind::Comma, "comma before inlined_at");
    if (!inlined_at_comma)
      return std::unexpected(inlined_at_comma.error());
    const TokenId inlined_at_keyword = consume();
    if (token(inlined_at_keyword).kind != TokenKind::Ident ||
        token(inlined_at_keyword).text != "inlined_at") {
      return std::unexpected(CstParseDiagnostic{token(inlined_at_keyword).range,
                                                "expected 'inlined_at'"});
    }
    auto inline_file_index = expectIntegerLiteral("inlined source file index");
    if (!inline_file_index)
      return std::unexpected(inline_file_index.error());
    auto inline_line_number =
        expect(TokenKind::Decimal, "inlined source line number");
    if (!inline_line_number)
      return std::unexpected(inline_line_number.error());
    auto inline_column_position =
        expect(TokenKind::Decimal, "inlined source column position");
    if (!inline_column_position)
      return std::unexpected(inline_column_position.error());
    last = *inline_column_position;
    inline_context = syntax_cst::CstLocInlineContext{
        .function_name_comma = function_name_comma,
        .function_name_keyword = function_name_keyword,
        .function_name_label = function_name_label,
        .plus = plus,
        .function_name_offset = function_name_offset,
        .inlined_at_comma = *inlined_at_comma,
        .inlined_at_keyword = inlined_at_keyword,
        .file_index = *inline_file_index,
        .line_number = *inline_line_number,
        .column_position = *inline_column_position,
        .token_range = {function_name_comma, last + 1},
    };
  }
  std::optional<TokenId> terminator;
  if (token(peek()).kind == TokenKind::Semicolon) {
    terminator = consume();
    last = *terminator;
  }
  return syntax_cst::CstLocDirective{
      .directive = directive,
      .file_index = *file_index,
      .line_number = *line_number,
      .column_position = *column_position,
      .inline_context = std::move(inline_context),
      .terminator = terminator,
      .token_range = {directive, last + 1},
  };
}

/** Parse an unconsumed pragma directive and its source string. */
std::expected<syntax_cst::CstPragma, CstParseDiagnostic>
PtxCstParser::parsePragma() {
  const TokenId directive = consume();
  auto first = expect(TokenKind::String, "pragma string");
  if (!first)
    return std::unexpected(first.error());

  std::vector<TokenId> strings{*first};
  std::vector<TokenId> commas;
  while (token(peek()).kind == TokenKind::Comma) {
    commas.push_back(consume());
    auto string = expect(TokenKind::String, "pragma string after comma");
    if (!string)
      return std::unexpected(string.error());
    strings.push_back(*string);
  }
  auto terminator = expect(TokenKind::Semicolon, "pragma terminator");
  if (!terminator)
    return std::unexpected(terminator.error());
  return syntax_cst::CstPragma{
      .directive = directive,
      .strings = std::move(strings),
      .commas = std::move(commas),
      .terminator = *terminator,
      .token_range = {directive, *terminator + 1},
  };
}

/** Parse entry resource values without performing target legality checks. */
std::expected<syntax_cst::CstKernelResourceDirective, CstParseDiagnostic>
PtxCstParser::parseKernelResourceDirective() {
  const TokenId directive = consume();
  const TokenKind kind = token(directive).kind;
  if (!isKernelResourceDirective(kind)) {
    return std::unexpected(CstParseDiagnostic{
        token(directive).range, "expected kernel resource directive"});
  }

  if (kind == TokenKind::DotExplicitcluster) {
    if (token(peek()).kind == TokenKind::Decimal ||
        token(peek()).kind == TokenKind::Comma) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range, ".explicitcluster does not accept a value"});
    }
    return syntax_cst::CstKernelResourceDirective{
        .directive = directive,
        .token_range = {directive, directive + 1},
    };
  }

  auto first = expect(TokenKind::Decimal, "kernel resource value");
  if (!first)
    return std::unexpected(first.error());
  std::vector<TokenId> values{*first};
  std::vector<TokenId> commas;

  if (kind == TokenKind::DotMaxnreg || kind == TokenKind::DotMinnctapersm ||
      kind == TokenKind::DotMaxclusterrank) {
    if (token(peek()).kind == TokenKind::Comma) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          "this kernel resource directive accepts one value"});
    }
  } else {
    while (token(peek()).kind == TokenKind::Comma) {
      commas.push_back(consume());
      if (values.size() == 3) {
        return std::unexpected(
            CstParseDiagnostic{token(peek()).range,
                               "thread-count kernel resource directives accept "
                               "at most three values"});
      }
      auto value =
          expect(TokenKind::Decimal, "kernel resource value after comma");
      if (!value)
        return std::unexpected(value.error());
      values.push_back(*value);
    }
  }
  const TokenId last = values.back();
  return syntax_cst::CstKernelResourceDirective{
      .directive = directive,
      .values = std::move(values),
      .commas = std::move(commas),
      .token_range = {directive, last + 1},
  };
}

/** Parse a recognized language directive and its source literal. */
std::expected<syntax_cst::CstLanguageDirective, CstParseDiagnostic>
PtxCstParser::parseLanguageDirective() {
  const TokenId directive = consume();
  std::vector<TokenId> values;
  std::vector<TokenId> commas;
  for (;;) {
    const TokenKind kind = token(peek()).kind;
    if (kind != TokenKind::String && kind != TokenKind::Decimal &&
        kind != TokenKind::Hex) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range, "expected string or integer in .language"});
    }
    values.push_back(consume());
    if (token(peek()).kind != TokenKind::Comma)
      break;
    commas.push_back(consume());
  }
  return syntax_cst::CstLanguageDirective{
      .directive = directive,
      .values = std::move(values),
      .commas = std::move(commas),
      .token_range = {directive, peek()},
  };
}

/** Parse the dotted alias source form through its terminating semicolon. */
std::expected<syntax_cst::CstAliasDirective, CstParseDiagnostic>
PtxCstParser::parseAliasDirective() {
  const TokenId directive = consume();
  auto alias = expect(TokenKind::Ident, "alias function name");
  if (!alias)
    return std::unexpected(alias.error());
  auto comma = expect(TokenKind::Comma, "',' in .alias");
  if (!comma)
    return std::unexpected(comma.error());
  auto aliasee = expect(TokenKind::Ident, "aliased function name");
  if (!aliasee)
    return std::unexpected(aliasee.error());
  auto semicolon = expect(TokenKind::Semicolon, "';' after .alias");
  if (!semicolon)
    return std::unexpected(semicolon.error());
  return syntax_cst::CstAliasDirective{
      .directive = directive,
      .alias = *alias,
      .comma = *comma,
      .aliasee = *aliasee,
      .semicolon = *semicolon,
      .token_range = {directive, *semicolon + 1},
  };
}

/** Select one body source form; return recognized malformed syntax to the driver. */
std::expected<syntax_cst::CstFunctionBodyItem, CstParseDiagnostic>
PtxCstParser::parseFunctionBodyItem(CstParseDiagnostics& diagnostics,
                                    BlockDepth block_depth) {
  if (token(peek()).kind == TokenKind::LBrace) {
    if (block_depth.value >= kMaxNestedBlockDepth) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range, "nested block depth exceeds parser limit"});
    }
    auto block = parseBlock(diagnostics, BlockDepth{block_depth.value + 1});
    if (!block)
      return std::unexpected(block.error());
    return std::make_unique<syntax_cst::CstBlock>(std::move(*block));
  }
  if (isVariableStateSpace(token(peek()).kind)) {
    auto declaration = parseVariableDeclaration();
    if (!declaration)
      return std::unexpected(declaration.error());
    return std::move(*declaration);
  }

  if (token(peek()).kind == TokenKind::DotLoc) {
    auto location = parseLocDirective();
    if (!location)
      return std::unexpected(location.error());
    return std::move(*location);
  }

  if (token(peek()).kind == TokenKind::DotPragma) {
    auto pragma = parsePragma();
    if (!pragma)
      return std::unexpected(pragma.error());
    return std::move(*pragma);
  }

  if (isKernelResourceDirective(token(peek()).kind)) {
    return std::unexpected(
        CstParseDiagnostic{token(peek()).range,
                           "kernel resource directives are only valid in an "
                           "entry function header"});
  }

  if (token(peek()).kind == TokenKind::Ident) {
    const TokenId first_token = consume();
    if (token(peek()).kind == TokenKind::Colon) {
      const TokenId colon = consume();
      if (token(peek()).kind == TokenKind::DotCallPrototype) {
        auto prototype = parseCallPrototype(first_token, colon);
        if (!prototype)
          return std::unexpected(prototype.error());
        return std::move(*prototype);
      }
      if (token(peek()).kind == TokenKind::DotCallTargets) {
        auto targets = parseCallTargets(first_token, colon);
        if (!targets)
          return std::unexpected(targets.error());
        return std::move(*targets);
      }
      if (token(peek()).kind == TokenKind::DotBranchTargets) {
        auto targets = parseBranchTargets(first_token, colon);
        if (!targets)
          return std::unexpected(targets.error());
        return std::move(*targets);
      }
      return syntax_cst::CstLabel{first_token, colon, {first_token, colon + 1}};
    }
    auto instruction = parseInstructionAfterHead(first_token);
    if (!instruction)
      return std::unexpected(instruction.error());
    return std::move(*instruction);
  }

  if (token(peek()).kind == TokenKind::DotCallPrototype) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "'.callprototype' requires a preceding function-local label"});
  }
  if (token(peek()).kind == TokenKind::DotCallTargets) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "'.calltargets' requires a preceding function-local label"});
  }
  if (token(peek()).kind == TokenKind::DotBranchTargets) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range,
        "'.branchtargets' requires a preceding function-local label"});
  }

  auto instruction = parseInstructionNode();
  if (!instruction)
    return std::unexpected(instruction.error());
  return std::move(*instruction);
}

/** Parse a nested block from its unconsumed opener through its body outcome. */
std::expected<syntax_cst::CstBlock, CstParseDiagnostic>
PtxCstParser::parseBlock(CstParseDiagnostics& diagnostics,
                         BlockDepth block_depth) {
  const TokenId left_brace = consume();
  syntax_cst::CstBlock block{.left_brace = left_brace};
  const auto outcome =
      parseBody(block.body, diagnostics, block_depth, BodyContext::NestedBlock);
  if (const auto* closed = std::get_if<ClosedBody>(&outcome)) {
    block.right_brace = closed->right_brace;
    block.token_range = {left_brace, closed->right_brace + 1};
  } else {
    block.token_range = {left_brace,
                         std::get<MissingClosingBrace>(outcome).anchor};
  }
  return block;
}

/** Parse module directive syntax; retain strict shared target-item classification. */
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
      if (base::classify_target_directive_item(token(*target).text) !=
          base::TargetDirectiveItemKind::Architecture)
        return std::unexpected(CstParseDiagnostic{
            token(*target).range, "expected target architecture spelling"});
      arguments.push_back(*target);
      while (token(peek()).kind == TokenKind::Comma) {
        separators.push_back(consume());
        auto option = expect(TokenKind::Ident, "target option");
        if (!option)
          return std::unexpected(option.error());
        const auto kind =
            base::classify_target_directive_item(token(*option).text);
        if (kind == base::TargetDirectiveItemKind::Invalid ||
            kind == base::TargetDirectiveItemKind::Architecture)
          return std::unexpected(CstParseDiagnostic{
              token(*option).range, "expected modeled target option"});
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
    case TokenKind::DotFile: {
      auto index = expectIntegerLiteral("file index");
      if (!index)
        return std::unexpected(index.error());
      auto filename = expect(TokenKind::String, "file name");
      if (!filename)
        return std::unexpected(filename.error());
      arguments.push_back(*index);
      arguments.push_back(*filename);
      if (token(peek()).kind != TokenKind::Comma)
        break;
      separators.push_back(consume());
      auto timestamp = expect(TokenKind::Decimal, "file timestamp");
      if (!timestamp)
        return std::unexpected(timestamp.error());
      auto comma = expect(TokenKind::Comma, "comma before file size");
      if (!comma)
        return std::unexpected(comma.error());
      auto size = expect(TokenKind::Decimal, "file size");
      if (!size)
        return std::unexpected(size.error());
      arguments.push_back(*timestamp);
      separators.push_back(*comma);
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

/** Parse a balanced opaque section payload with its established boundary policy. */
std::expected<syntax_cst::CstSectionDirective, CstParseDiagnostic>
PtxCstParser::parseSectionDirective() {
  const TokenId directive = consume();
  const TokenId name = consume();
  if (!isIdentifierToken(token(name).kind)) {
    return std::unexpected(
        CstParseDiagnostic{token(name).range, "expected section name"});
  }
  auto left_brace = expect(TokenKind::LBrace, "section opening brace");
  if (!left_brace)
    return std::unexpected(left_brace.error());

  std::vector<TokenId> payload;
  size_t brace_depth = 1;
  TokenId right_brace{};
  while (brace_depth != 0) {
    const TokenId next = peek();
    if (token(next).kind == TokenKind::Eof ||
        (brace_depth == 1 && isSectionRecoveryBoundary(token(next)))) {
      return std::unexpected(CstParseDiagnostic{
          token(next).range, "expected section closing brace",
          TokenKind::RBrace});
    }
    consume();
    if (token(next).kind == TokenKind::LBrace) {
      ++brace_depth;
      payload.push_back(next);
      continue;
    }
    if (token(next).kind == TokenKind::RBrace) {
      --brace_depth;
      if (brace_depth == 0) {
        right_brace = next;
        break;
      }
    }
    payload.push_back(next);
  }

  std::optional<TokenId> terminator;
  if (token(peek()).kind == TokenKind::Semicolon)
    terminator = consume();
  const TokenId last = terminator.value_or(right_brace);
  return syntax_cst::CstSectionDirective{
      .directive = directive,
      .name = name,
      .left_brace = *left_brace,
      .payload = std::move(payload),
      .right_brace = right_brace,
      .terminator = terminator,
      .token_range = {directive, last + 1},
  };
}

/** Parse a single header and complete it with a prototype or shared body driver. */
std::expected<syntax_cst::CstFunction, CstParseDiagnostic>
PtxCstParser::parseFunction(std::vector<TokenId> qualifiers,
                            CstParseDiagnostics& diagnostics) {
  const TokenId first = qualifiers.empty() ? peek() : qualifiers.front();
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

  std::optional<syntax_cst::CstAttributeList> attributes;
  if (token(directive).kind == TokenKind::DotFunc &&
      token(peek()).kind == TokenKind::DotIdent &&
      token(peek()).text == ".attribute") {
    auto parsed = parseAttributeList();
    if (!parsed)
      return std::unexpected(parsed.error());
    append_range(parsed->token_range);
    attributes = std::move(*parsed);
  }

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
  if (token(directive).kind == TokenKind::DotFunc &&
      token(peek()).kind == TokenKind::DotNoreturn) {
    noreturn_directive = consume();
    header_tokens.push_back(*noreturn_directive);
  }

  const auto parse_abi_suffix =
      [this]() -> std::expected<syntax_cst::CstCallPrototypeAbiSuffix,
                                CstParseDiagnostic> {
    const TokenId suffix_directive = consume();
    auto count = expect(TokenKind::Decimal, "ABI preserved register count");
    if (!count)
      return std::unexpected(count.error());
    return syntax_cst::CstCallPrototypeAbiSuffix{
        .directive = suffix_directive,
        .count = *count,
        .token_range = {suffix_directive, *count + 1},
    };
  };
  std::optional<syntax_cst::CstCallPrototypeAbiSuffix> abi_preserve;
  std::optional<syntax_cst::CstCallPrototypeAbiSuffix> abi_preserve_control;
  if (token(directive).kind == TokenKind::DotFunc &&
      token(peek()).kind == TokenKind::DotIdent &&
      token(peek()).text == ".abi_preserve") {
    auto suffix = parse_abi_suffix();
    if (!suffix)
      return std::unexpected(suffix.error());
    append_range(suffix->token_range);
    abi_preserve = std::move(*suffix);
  }
  if (token(directive).kind == TokenKind::DotFunc &&
      token(peek()).kind == TokenKind::DotIdent &&
      token(peek()).text == ".abi_preserve_control") {
    auto suffix = parse_abi_suffix();
    if (!suffix)
      return std::unexpected(suffix.error());
    append_range(suffix->token_range);
    abi_preserve_control = std::move(*suffix);
  }

  std::vector<syntax_cst::CstPragma> pragmas;
  std::vector<syntax_cst::CstKernelResourceDirective> resources;
  while (token(peek()).kind == TokenKind::DotPragma ||
         isKernelResourceDirective(token(peek()).kind)) {
    if (token(directive).kind != TokenKind::DotEntry) {
      return std::unexpected(CstParseDiagnostic{
          token(peek()).range,
          token(peek()).kind == TokenKind::DotPragma
              ? "'.pragma' is only valid in an entry function header"
              : "kernel resource directives are only valid in an entry "
                "function header"});
    }
    if (token(peek()).kind == TokenKind::DotPragma) {
      auto pragma = parsePragma();
      if (!pragma)
        return std::unexpected(pragma.error());
      append_range(pragma->token_range);
      pragmas.push_back(std::move(*pragma));
    } else {
      auto resource = parseKernelResourceDirective();
      if (!resource)
        return std::unexpected(resource.error());
      append_range(resource->token_range);
      resources.push_back(std::move(*resource));
    }
  }

  std::optional<TokenId> blocks_are_clusters;
  if (token(directive).kind == TokenKind::DotEntry &&
      token(peek()).kind == TokenKind::DotIdent &&
      token(peek()).text == ".blocksareclusters") {
    blocks_are_clusters = consume();
    header_tokens.push_back(*blocks_are_clusters);
  }
  std::optional<syntax_cst::CstLanguageDirective> language;
  if (token(peek()).kind == TokenKind::DotIdent &&
      token(peek()).text == ".language") {
    auto parsed = parseLanguageDirective();
    if (!parsed)
      return std::unexpected(parsed.error());
    append_range(parsed->token_range);
    language = std::move(*parsed);
  }

  if (token(peek()).kind == TokenKind::Eof) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "expected function body or prototype terminator"});
  }
  if (token(peek()).kind != TokenKind::LBrace &&
      token(peek()).kind != TokenKind::Semicolon) {
    return std::unexpected(CstParseDiagnostic{
        token(peek()).range, "unsupported function header token '" +
                                 std::string{token(peek()).text} + "'"});
  }

  syntax_cst::CstFunction function{
      .qualifiers = std::move(qualifiers),
      .directive = directive,
      .attributes = std::move(attributes),
      .return_parameters = std::move(return_parameters),
      .name = *name,
      .parameters = std::move(parameters),
      .noreturn_directive = noreturn_directive,
      .abi_preserve = std::move(abi_preserve),
      .abi_preserve_control = std::move(abi_preserve_control),
      .blocks_are_clusters = blocks_are_clusters,
      .language = std::move(language),
      .pragmas = std::move(pragmas),
      .resources = std::move(resources),
      .header_tokens = std::move(header_tokens),
  };
  if (token(peek()).kind == TokenKind::Semicolon) {
    const TokenId terminator = consume();
    if (token(directive).kind != TokenKind::DotFunc) {
      return std::unexpected(CstParseDiagnostic{
          token(terminator).range, "an entry function must have a body"});
    }
    function.terminator = terminator;
    function.token_range = {first, terminator + 1};
    return function;
  }

  function.left_brace = consume();
  const auto outcome = parseBody(function.body, diagnostics, BlockDepth{},
                                 BodyContext::Function);
  if (const auto* closed = std::get_if<ClosedBody>(&outcome)) {
    function.right_brace = closed->right_brace;
    function.token_range = {first, closed->right_brace + 1};
  } else {
    function.token_range = {first,
                            std::get<MissingClosingBrace>(outcome).anchor};
  }
  return function;
}

/** Parse module items and retain recoverable failures beside valid source neighbors. */
CstParseResult PtxCstParser::parseModule() {
  std::vector<syntax_cst::CstModuleItem> items;
  CstParseDiagnostics diagnostics;
  const TokenId first = peek();
  TokenId last = first;

  const auto recover_item = [this, &diagnostics, &items, &last](
                                TokenId item_first,
                                CstParseDiagnostic diagnostic) {
    diagnostics.push_back(diagnostic);
    auto recovery = recover(item_first, diagnostic, RecoveryContext::Module);
    for (auto& node : recovery.nodes)
      items.emplace_back(std::move(node));
    if (recovery.last)
      last = *recovery.last;
  };

  while (token(peek()).kind != TokenKind::Eof) {
    const TokenId item_first = peek();
    std::vector<TokenId> qualifiers;
    while (isFunctionQualifier(token(peek()).kind))
      qualifiers.push_back(consume());
    const ModuleSourceForm source_form = classifyModuleSource(token(peek()));

    if (qualifiers.empty() && source_form == ModuleSourceForm::Pragma) {
      auto pragma = parsePragma();
      if (!pragma) {
        recover_item(item_first, std::move(pragma.error()));
        continue;
      }
      last = pragma->token_range.last - 1;
      items.emplace_back(std::move(*pragma));
      continue;
    }

    if (qualifiers.empty() && source_form == ModuleSourceForm::Section) {
      auto section = parseSectionDirective();
      if (!section) {
        recover_item(item_first, std::move(section.error()));
        continue;
      }
      last = section->token_range.last - 1;
      items.emplace_back(std::move(*section));
      continue;
    }

    if (qualifiers.empty() && source_form == ModuleSourceForm::Alias) {
      auto alias = parseAliasDirective();
      if (!alias) {
        recover_item(item_first, std::move(alias.error()));
        continue;
      }
      last = alias->token_range.last - 1;
      items.emplace_back(std::move(*alias));
      continue;
    }

    if (qualifiers.empty() && source_form == ModuleSourceForm::Directive) {
      auto directive = parseModuleDirective();
      if (!directive) {
        recover_item(item_first, std::move(directive.error()));
        continue;
      }
      last = directive->token_range.last - 1;
      items.emplace_back(std::move(*directive));
      continue;
    }

    if (source_form == ModuleSourceForm::Function) {
      auto function = parseFunction(std::move(qualifiers), diagnostics);
      if (!function) {
        recover_item(item_first, std::move(function.error()));
        continue;
      }
      last = function->token_range.last - 1;
      items.emplace_back(std::move(*function));
      continue;
    }

    if (source_form == ModuleSourceForm::Variable) {
      auto declaration = parseVariableDeclaration(std::move(qualifiers));
      if (!declaration) {
        recover_item(item_first, std::move(declaration.error()));
        continue;
      }
      last = declaration->token_range.last - 1;
      items.emplace_back(std::move(*declaration));
      continue;
    }

    if (token(peek()).kind == TokenKind::DotCallPrototype) {
      recover_item(
          item_first,
          CstParseDiagnostic{
              token(peek()).range,
              "'.callprototype' is only valid inside a function body"});
      continue;
    }
    if (token(peek()).kind == TokenKind::DotCallTargets) {
      recover_item(item_first,
                   CstParseDiagnostic{
                       token(peek()).range,
                       "'.calltargets' is only valid inside a function body"});
      continue;
    }
    if (token(peek()).kind == TokenKind::DotBranchTargets) {
      recover_item(
          item_first,
          CstParseDiagnostic{
              token(peek()).range,
              "'.branchtargets' is only valid inside a function body"});
      continue;
    }
    if (isKernelResourceDirective(token(peek()).kind)) {
      recover_item(item_first,
                   CstParseDiagnostic{token(peek()).range,
                                      "kernel resource directives are only "
                                      "valid in an entry function header"});
      continue;
    }

    if (token(peek()).kind == TokenKind::Ident) {
      consume();
      if (token(peek()).kind == TokenKind::Colon) {
        consume();
        std::string_view message;
        switch (token(peek()).kind) {
          case TokenKind::DotCallPrototype:
            message = "'.callprototype' is only valid inside a function body";
            break;
          case TokenKind::DotCallTargets:
            message = "'.calltargets' is only valid inside a function body";
            break;
          case TokenKind::DotBranchTargets:
            message = "'.branchtargets' is only valid inside a function body";
            break;
          default:
            break;
        }
        if (!message.empty()) {
          recover_item(item_first, CstParseDiagnostic{token(peek()).range,
                                                      std::string(message)});
          continue;
        }
      }
    }

    recover_item(
        item_first,
        CstParseDiagnostic{
            token(peek()).range,
            "expected module directive, variable declaration, or function"});
  }

  if (items.empty()) {
    return parseFailure(
        CstParseDiagnostic{token(first).range, "expected module item"});
  }
  syntax_cst::CstModule module{std::move(items), {first, last + 1}};
  return {.value = syntax_cst::CstFile{std::move(tokens_), std::move(module)},
          .diagnostics = std::move(diagnostics)};
}

}  // namespace ptx_frontend

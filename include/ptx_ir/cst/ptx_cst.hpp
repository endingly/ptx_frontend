#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ptx_ir/lex/ptx_token.hpp"

namespace ptx_frontend::syntax_cst {

using TokenId = uint32_t;

/** Half-open range in a CstFile token buffer. */
struct CstTokenRange {
  TokenId first{};
  TokenId last{};
};

struct CstIdentifier {
  TokenId token{};
};

struct CstImmediate {
  std::optional<TokenId> sign;
  TokenId literal{};
  CstTokenRange token_range;
};

struct CstPredicate {
  TokenId at_token{};
  std::optional<TokenId> exclamation_token;
  TokenId name{};
  CstTokenRange token_range;
};

struct CstPredicateOperand {
  std::optional<TokenId> exclamation_token;
  TokenId name{};
  CstTokenRange token_range;
};

struct CstAddressOffset {
  TokenId operator_token{};
  CstImmediate magnitude;
  CstTokenRange token_range;
};

using CstAddressBase = std::variant<CstIdentifier, CstImmediate>;

struct CstAddress {
  std::optional<TokenId> left_bracket;
  CstAddressBase base;
  std::optional<CstAddressOffset> offset;
  std::optional<TokenId> right_bracket;
  CstTokenRange token_range;
};

struct CstVectorMember {
  CstIdentifier base;
  TokenId selector{};
  CstTokenRange token_range;
};

using CstVectorElement = std::variant<CstIdentifier, CstImmediate>;

struct CstVectorPack {
  TokenId left_brace{};
  std::vector<CstVectorElement> elements;
  std::vector<TokenId> commas;
  TokenId right_brace{};
  CstTokenRange token_range;
};

using CstOperand =
    std::variant<CstIdentifier, CstPredicateOperand, CstImmediate, CstAddress,
                 CstVectorMember, CstVectorPack>;

struct CstOperandElement {
  CstOperand operand;
  std::optional<TokenId> trailing_comma;
};

struct CstInstruction {
  std::optional<CstPredicate> predicate;
  TokenId opcode{};
  std::vector<TokenId> modifiers;
  std::vector<CstOperandElement> operands;
  TokenId semicolon{};
  CstTokenRange token_range;
};

struct CstArrayDimension {
  TokenId left_bracket{};
  std::vector<TokenId> size_tokens;
  TokenId right_bracket{};
  CstTokenRange token_range;
};

struct CstVariableDeclarator {
  TokenId name{};
  std::optional<TokenId> left_angle;
  std::optional<TokenId> register_count;
  std::optional<TokenId> right_angle;
  std::vector<CstArrayDimension> array_dimensions;
  CstTokenRange token_range;
};

struct CstVariableDeclaration {
  std::vector<TokenId> qualifiers;
  TokenId state_space{};
  std::optional<TokenId> align_directive;
  std::optional<TokenId> alignment;
  std::optional<TokenId> vector_type;
  TokenId type{};
  std::vector<CstVariableDeclarator> declarators;
  std::vector<TokenId> commas;
  TokenId semicolon{};
  CstTokenRange token_range;
};

struct CstLabel {
  TokenId name{};
  TokenId colon{};
  CstTokenRange token_range;
};

using CstFunctionBodyItem =
    std::variant<CstVariableDeclaration, CstLabel, CstInstruction>;

/** A module-level directive and its concrete token payload. */
struct CstModuleDirective {
  TokenId keyword{};
  std::vector<TokenId> arguments;
  std::vector<TokenId> separators;
  std::optional<TokenId> terminator;
  CstTokenRange token_range;
};

/** Initial function container; declaration/body grammar will refine it. */
struct CstFunctionParameter {
  TokenId state_space{};
  std::optional<TokenId> align_directive;
  std::optional<TokenId> alignment;
  TokenId type{};
  std::optional<TokenId> pointer_directive;
  std::optional<TokenId> pointer_space;
  std::optional<TokenId> pointer_align_directive;
  std::optional<TokenId> pointer_alignment;
  TokenId name{};
  std::optional<TokenId> left_bracket;
  std::optional<TokenId> array_size;
  std::optional<TokenId> right_bracket;
  CstTokenRange token_range;
};

struct CstFunctionParameterList {
  TokenId left_paren{};
  std::vector<CstFunctionParameter> parameters;
  std::vector<TokenId> commas;
  TokenId right_paren{};
  CstTokenRange token_range;
};

struct CstFunction {
  std::vector<TokenId> qualifiers;
  TokenId directive{};
  std::optional<CstFunctionParameterList> return_parameters;
  TokenId name{};
  std::optional<CstFunctionParameterList> parameters;
  std::optional<TokenId> noreturn_directive;
  std::vector<TokenId> header_tokens;
  std::optional<TokenId> left_brace;
  std::vector<CstFunctionBodyItem> body;
  std::optional<TokenId> right_brace;
  std::optional<TokenId> terminator;
  CstTokenRange token_range;
};

using CstModuleItem =
    std::variant<CstModuleDirective, CstVariableDeclaration, CstFunction>;

struct CstModule {
  std::vector<CstModuleItem> items;
  CstTokenRange token_range;
};

using CstRoot = std::variant<CstInstruction, CstModule>;

/** Lossless token owner for either a fragment or a complete PTX module. */
struct CstFile {
  std::vector<PtxToken> tokens;
  CstRoot root;

  [[nodiscard]] const PtxToken& token(TokenId id) const;
  [[nodiscard]] SourceRange sourceRange(CstTokenRange range) const;
  [[nodiscard]] std::string sourceText() const;
  [[nodiscard]] const CstInstruction* instruction() const noexcept;
  [[nodiscard]] const CstModule* module() const noexcept;
};

}  // namespace ptx_frontend::syntax_cst

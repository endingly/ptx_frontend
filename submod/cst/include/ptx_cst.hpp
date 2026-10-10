#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <ptx_frontend/lexer/ptx_token.hpp>

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

struct CstConstantExpression;

/** An owned constant tree used in an instruction numeric leaf position. */
struct CstConstantOperand {
  /** Exclusively owned syntax tree, limited by the parser depth budget. */
  std::unique_ptr<CstConstantExpression> expression;
  /** Full written expression token range. */
  CstTokenRange token_range;
  /** Own a parsed operation tree. */
  CstConstantOperand(std::unique_ptr<CstConstantExpression> value,
                     CstTokenRange range);
  /** Copy syntax with independent tree ownership. */
  CstConstantOperand(const CstConstantOperand&);
  /** Replace syntax with an independently cloned tree. */
  CstConstantOperand& operator=(const CstConstantOperand&);
  /** Transfer tree ownership. */
  CstConstantOperand(CstConstantOperand&&) noexcept;
  /** Transfer tree ownership. */
  CstConstantOperand& operator=(CstConstantOperand&&) noexcept;
  /** Release the bounded owned tree. */
  ~CstConstantOperand();
};

/** Literal or expression syntax before operand-specific numeric conversion. */
using CstNumericOperand = std::variant<CstImmediate, CstConstantOperand>;

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

/** An integer instruction operand complemented with a leading ``!``. */
struct CstNegatedImmediate {
  TokenId exclamation_token{};
  CstNumericOperand immediate;
  CstTokenRange token_range;
};

struct CstAddressOffset {
  TokenId operator_token{};
  CstNumericOperand magnitude;
  CstTokenRange token_range;
};

using CstAddressBase =
    std::variant<CstIdentifier, CstImmediate, CstConstantOperand>;

/** One named-array subscript, distinct from a byte-address offset. */
struct CstNamedArrayIndex {
  /** Written index brackets and their owned numeric/register child. */
  TokenId left_bracket{};
  CstAddressBase index;
  /** Optional register displacement in element units. */
  std::optional<CstAddressOffset> displacement;
  TokenId right_bracket{};
  CstTokenRange token_range;
};

struct CstAddress {
  std::optional<TokenId> left_bracket;
  CstAddressBase base;
  std::optional<CstAddressOffset> offset;
  std::optional<TokenId> right_bracket;
  /** Optional `.unified` suffix token following a bracketed memory address. */
  std::optional<TokenId> unified_token;
  CstTokenRange token_range;
  /** Present only for the single-bracket named-array source form. */
  std::optional<CstNamedArrayIndex> named_index;
};

struct CstVectorMember {
  CstIdentifier base;
  TokenId selector{};
  CstTokenRange token_range;
};

/** Arithmetic minus applied to one identifier or selected register spelling. */
struct CstNegatedRegisterOperand {
  /** Written minus token; never a predicate complement. */
  TokenId minus{};
  /** Nonrecursive child retaining its own token range. */
  std::variant<CstIdentifier, CstVectorMember> operand;
  CstTokenRange token_range;
};

using CstVectorElement =
    std::variant<CstIdentifier, CstImmediate, CstConstantOperand>;

struct CstVectorPack {
  TokenId left_brace{};
  std::vector<CstVectorElement> elements;
  std::vector<TokenId> commas;
  TokenId right_brace{};
  CstTokenRange token_range;
};

/** Braced texture result followed by an optional residency predicate sink. */
struct CstVectorPredicatePair {
  /** Source-order data register pack. */
  CstVectorPack data;
  /** Separator between data and predicate destinations. */
  TokenId pipe{};
  /** Scalar predicate destination spelling. */
  CstIdentifier predicate;
  CstTokenRange token_range;
};

/** One bracketed tensor-map address and its nested coordinate tuple. */
struct CstTensorOperand {
  TokenId left_bracket{};
  CstAddress tensor_map;
  TokenId comma{};
  CstVectorPack coordinates;
  TokenId right_bracket{};
  CstTokenRange token_range;
};

/** Bracketed scalar elements of a CUDA Fabric Transport handle. */
struct CstFabricHandle {
  /** Endpoint, data offset, and optional counter offset in source order. */
  std::vector<CstAddressBase> elements;
  /** Comma tokens between elements, retained for precise diagnostics. */
  std::vector<TokenId> commas;
  TokenId left_bracket{};
  TokenId right_bracket{};
  CstTokenRange token_range;
};

/** Bracketed scalar resource heads followed by one coordinate brace pack. */
struct CstCompoundBracket {
  /** One or two identifier/immediate heads; semantic kind is resolved later. */
  std::vector<CstAddressBase> heads;
  /** All separators, including the one before the coordinate pack. */
  std::vector<TokenId> commas;
  CstVectorPack coordinates;
  TokenId left_bracket{};
  TokenId right_bracket{};
  CstTokenRange token_range;
};

enum class CstCallParameterListKind : uint8_t {
  Return,
  Input,
};

using CstCallParameter =
    std::variant<CstIdentifier, CstImmediate, CstConstantOperand>;

struct CstCallParameterList {
  CstCallParameterListKind kind{};
  TokenId left_paren{};
  std::vector<CstCallParameter> parameters;
  std::vector<TokenId> commas;
  TokenId right_paren{};
  CstTokenRange token_range;
};

struct CstCallTarget {
  CstIdentifier name;
  CstTokenRange token_range;
};

struct CstCallTargetSet {
  CstIdentifier name;
  CstTokenRange token_range;
};

struct CstBranchTarget {
  CstIdentifier name;
  CstTokenRange token_range;
};

struct CstBranchTargetSet {
  CstIdentifier name;
  CstTokenRange token_range;
};

struct CstRegisterPredicatePair {
  CstIdentifier dst;
  TokenId pipe_token;
  CstIdentifier predicate;
  CstTokenRange token_range;
};

using CstOperand = std::variant<
    CstIdentifier, CstPredicateOperand, CstNegatedImmediate, CstImmediate,
    CstConstantOperand, CstAddress, CstVectorMember, CstNegatedRegisterOperand,
    CstVectorPack, CstVectorPredicatePair, CstTensorOperand, CstFabricHandle,
    CstCompoundBracket, CstCallParameterList, CstCallTarget, CstCallTargetSet,
    CstBranchTarget, CstBranchTargetSet, CstRegisterPredicatePair>;

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

struct CstConstantExpression;
using CstConstantExpressionPtr = std::unique_ptr<CstConstantExpression>;

struct CstConstantLiteral {
  TokenId literal{};
};

struct CstConstantSymbol {
  TokenId name{};
};

struct CstConstantParenthesized {
  TokenId left_paren{};
  CstConstantExpressionPtr expression;
  TokenId right_paren{};
};

struct CstConstantCall {
  CstConstantExpressionPtr callee;
  TokenId left_paren{};
  CstConstantExpressionPtr argument;
  TokenId right_paren{};
};

struct CstConstantCast {
  TokenId left_paren{};
  TokenId type{};
  TokenId right_paren{};
  CstConstantExpressionPtr operand;
};

struct CstConstantUnary {
  TokenId operator_token{};
  CstConstantExpressionPtr operand;
};

struct CstConstantBinary {
  CstConstantExpressionPtr left;
  TokenId operator_token{};
  CstConstantExpressionPtr right;
};

struct CstConstantConditional {
  CstConstantExpressionPtr condition;
  TokenId question{};
  CstConstantExpressionPtr true_expression;
  TokenId colon{};
  CstConstantExpressionPtr false_expression;
};

using CstConstantExpressionNode =
    std::variant<CstConstantLiteral, CstConstantSymbol,
                 CstConstantParenthesized, CstConstantCall, CstConstantCast,
                 CstConstantUnary, CstConstantBinary, CstConstantConditional>;

struct CstConstantExpression {
  CstConstantExpressionNode node;
  CstTokenRange token_range;
};

struct CstInitializer;

struct CstInitializerList {
  TokenId left_brace{};
  std::vector<CstInitializer> elements;
  std::vector<TokenId> commas;
  TokenId right_brace{};
  CstTokenRange token_range;
};

/** One named opaque-resource member assignment in a static initializer. */
struct CstNamedInitializer {
  /** Member identifier and assignment token in source order. */
  TokenId member{};
  TokenId equals{};
  /** Unevaluated constant expression used as the member value. */
  CstConstantExpression value;
  CstTokenRange token_range;
};

struct CstInitializer {
  std::variant<CstConstantExpression, CstInitializerList, CstNamedInitializer>
      value;
  CstTokenRange token_range;
};

struct CstArrayDimension {
  TokenId left_bracket{};
  std::optional<CstConstantExpression> size;
  TokenId right_bracket{};
  CstTokenRange token_range;
};

struct CstVariableDeclarator {
  TokenId name{};
  std::optional<TokenId> left_angle;
  std::optional<TokenId> parameterized_count;
  std::optional<TokenId> right_angle;
  std::vector<CstArrayDimension> array_dimensions;
  std::optional<TokenId> equals;
  std::optional<CstInitializer> initializer;
  CstTokenRange token_range;
};

/** A typed `.attribute(...)` member retained at its declaration site. */
struct CstAttribute {
  TokenId name{};
  std::vector<TokenId> values;
  std::vector<TokenId> commas;
  CstTokenRange token_range;
};

struct CstAttributeList {
  TokenId directive{};
  TokenId left_paren{};
  std::vector<CstAttribute> attributes;
  std::vector<TokenId> commas;
  TokenId right_paren{};
  CstTokenRange token_range;
};

struct CstVariableDeclaration {
  std::vector<TokenId> qualifiers;
  TokenId state_space{};
  std::optional<CstAttributeList> attributes;
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

struct CstLocInlineContext {
  TokenId function_name_comma{};
  TokenId function_name_keyword{};
  TokenId function_name_label{};
  std::optional<TokenId> plus;
  std::optional<TokenId> function_name_offset;
  TokenId inlined_at_comma{};
  TokenId inlined_at_keyword{};
  TokenId file_index{};
  TokenId line_number{};
  TokenId column_position{};
  CstTokenRange token_range;
};

/** A function-body source location directive. */
struct CstLocDirective {
  TokenId directive{};
  TokenId file_index{};
  TokenId line_number{};
  TokenId column_position{};
  std::optional<CstLocInlineContext> inline_context;
  std::optional<TokenId> terminator;
  CstTokenRange token_range;
};

/** An opaque backend pragma at module, entry, or statement scope. */
struct CstPragma {
  TokenId directive{};
  std::vector<TokenId> strings;
  std::vector<TokenId> commas;
  TokenId terminator{};
  CstTokenRange token_range;
};

/** A per-entry kernel resource constraint in a function header. */
struct CstKernelResourceDirective {
  TokenId directive{};
  std::vector<TokenId> values;
  std::vector<TokenId> commas;
  CstTokenRange token_range;
};

struct CstLanguageDirective {
  TokenId directive{};
  std::vector<TokenId> values;
  std::vector<TokenId> commas;
  CstTokenRange token_range;
};

struct CstAliasDirective {
  TokenId directive{};
  TokenId alias{};
  TokenId comma{};
  TokenId aliasee{};
  TokenId semicolon{};
  CstTokenRange token_range;
};

/** A module-level directive and its concrete token payload. */
struct CstModuleDirective {
  TokenId keyword{};
  std::vector<TokenId> arguments;
  std::vector<TokenId> separators;
  std::optional<TokenId> terminator;
  CstTokenRange token_range;
};

/** An outermost DWARF section with a lossless, uninterpreted payload. */
struct CstSectionDirective {
  TokenId directive{};
  TokenId name{};
  TokenId left_brace{};
  std::vector<TokenId> payload;
  TokenId right_brace{};
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
  std::optional<CstConstantExpression> array_size;
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

struct CstCallPrototypeAbiSuffix {
  TokenId directive{};
  TokenId count{};
  CstTokenRange token_range;
};

/** A function-local label-associated indirect-call prototype. */
struct CstCallPrototype {
  TokenId label{};
  TokenId colon{};
  TokenId directive{};
  std::optional<CstFunctionParameterList> return_parameters;
  TokenId sink{};
  std::optional<CstFunctionParameterList> parameters;
  std::optional<TokenId> noreturn_directive;
  std::optional<CstCallPrototypeAbiSuffix> abi_preserve;
  std::optional<CstCallPrototypeAbiSuffix> abi_preserve_control;
  TokenId semicolon{};
  CstTokenRange token_range;
};

/** A function-local label-associated indirect-call target list. */
struct CstCallTargets {
  TokenId label{};
  TokenId colon{};
  TokenId directive{};
  std::vector<TokenId> targets;
  std::vector<TokenId> commas;
  TokenId semicolon{};
  CstTokenRange token_range;
};

struct CstBranchTargetEntry {
  TokenId name{};
  std::optional<TokenId> left_angle;
  std::optional<TokenId> count;
  std::optional<TokenId> right_angle;
  CstTokenRange token_range;
};

/** A function-local label-associated indexed branch target list. */
struct CstBranchTargets {
  TokenId label{};
  TokenId colon{};
  TokenId directive{};
  std::vector<CstBranchTargetEntry> targets;
  std::vector<TokenId> commas;
  TokenId semicolon{};
  CstTokenRange token_range;
};

enum class CstRecoveryKind : uint8_t {
  Inserted,
  Skipped,
  Error,
};

/**
 * CST-only recovery marker. Inserted nodes have an expected token kind and a
 * zero-width range, without a token-buffer span. Skipped nodes have a
 * nonempty span of real tokens. Error nodes have either such a span or an EOF
 * zero-width range. parseModule() produces these nodes; AST lowering filters
 * them rather than representing recovery in the AST.
 */
struct CstRecoveryNode {
  CstRecoveryKind kind{};
  std::optional<TokenKind> expected_kind;
  std::optional<CstTokenRange> token_range;
  SourceRange range;
};

struct CstBlock;

using CstFunctionBodyItem =
    std::variant<CstVariableDeclaration, CstLabel, CstCallPrototype,
                 CstCallTargets, CstBranchTargets, CstLocDirective, CstPragma,
                 std::unique_ptr<CstBlock>, CstInstruction, CstRecoveryNode>;

/** A lexically nested function-body block. */
struct CstBlock {
  TokenId left_brace{};
  std::vector<CstFunctionBodyItem> body;
  std::optional<TokenId> right_brace;
  CstTokenRange token_range;
};

struct CstFunction {
  std::vector<TokenId> qualifiers;
  TokenId directive{};
  std::optional<CstAttributeList> attributes;
  std::optional<CstFunctionParameterList> return_parameters;
  TokenId name{};
  std::optional<CstFunctionParameterList> parameters;
  std::optional<TokenId> noreturn_directive;
  std::optional<CstCallPrototypeAbiSuffix> abi_preserve;
  std::optional<CstCallPrototypeAbiSuffix> abi_preserve_control;
  std::optional<TokenId> blocks_are_clusters;
  std::optional<CstLanguageDirective> language;
  std::vector<CstPragma> pragmas;
  std::vector<CstKernelResourceDirective> resources;
  std::vector<TokenId> header_tokens;
  std::optional<TokenId> left_brace;
  std::vector<CstFunctionBodyItem> body;
  std::optional<TokenId> right_brace;
  std::optional<TokenId> terminator;
  CstTokenRange token_range;
};

using CstModuleItem =
    std::variant<CstModuleDirective, CstSectionDirective, CstPragma,
                 CstVariableDeclaration, CstAliasDirective, CstFunction,
                 CstRecoveryNode>;

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

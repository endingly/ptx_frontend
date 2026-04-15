/*
 * ptx_parser.cpp  —  Recursive-descent parser for NVIDIA PTX ISA
 *
 * Input  : PTX source text (string_view)
 * Output : ptx_frontend::Module  (defined in ptx_ir/instr.hpp)
 *
 * All string_view values in the returned AST borrow from the
 * original source — the caller keeps it alive.
 */
#include "ptx_parser.hpp"
#include "ptx_lexer.hpp"

#include <cassert>
#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

// Propagate errors like Rust's `?` operator.
// Usage inside a function returning tl::expected<T, ParseError>:
//   auto val = TRY(some_expected_returning_call());
#define TRY(expr)                          \
  ({                                       \
    auto _res = (expr);                    \
    if (!_res)                             \
      return tl::unexpected(_res.error()); \
    std::move(*_res);                      \
  })

namespace ptx_frontend {

// ============================================================
// §0  Internal parser state
// ============================================================

struct Parser {
  PtxLexer lex;
  explicit Parser(std::string_view src) : lex(src) {}

  // ── error ──────────────────────────────────────────────────
  tl::unexpected<ParseError> err(std::string msg) {
    return tl::unexpected(ParseError{lex.peek().line, std::move(msg)});
  }
  tl::unexpected<ParseError> err_tok(const char* what) {
    auto t = lex.peek();
    return err(std::string("expected ") + what + ", got '" +
               std::string(t.text) + "' at line " + std::to_string(t.line));
  }

  // ── token predicates ──────────────────────────────────────
  bool check(TokenKind k) { return lex.peek().kind == k; }

  bool check_any(std::initializer_list<TokenKind> ks) {
    for (auto k : ks)
      if (check(k))
        return true;
    return false;
  }

  bool match(TokenKind k) {
    if (check(k)) {
      lex.consume();
      return true;
    }
    return false;
  }

  // consume or return error
  using TokResult = tl::expected<PtxLexer::Token, ParseError>;
  TokResult expect(TokenKind k, const char* what) {
    if (!check(k))
      return err_tok(what);
    return lex.consume();
  }

  // ── numeric helpers ───────────────────────────────────────
  // Parse integer from text that might be hex (0x...) or decimal.
  // Optional trailing 'U' is accepted.
  static uint64_t parse_uint(std::string_view sv) {
    // strip trailing U
    if (!sv.empty() && (sv.back() == 'U' || sv.back() == 'u'))
      sv.remove_suffix(1);
    if (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
      uint64_t v = 0;
      std::from_chars(sv.data() + 2, sv.data() + sv.size(), v, 16);
      return v;
    }
    uint64_t v = 0;
    std::from_chars(sv.data(), sv.data() + sv.size(), v, 10);
    return v;
  }

  static int64_t parse_sint(std::string_view sv) {
    return static_cast<int64_t>(parse_uint(sv));
  }

  // ── type token → ScalarType ───────────────────────────────
  static std::optional<ScalarType> token_to_scalar_type(TokenKind k) {
    switch (k) {
      case TokenKind::DotPred:
        return ScalarType::Pred;
      case TokenKind::DotU8:
        return ScalarType::U8;
      case TokenKind::DotU16:
        return ScalarType::U16;
      case TokenKind::DotU16x2:
        return ScalarType::U16x2;
      case TokenKind::DotU32:
        return ScalarType::U32;
      case TokenKind::DotU64:
        return ScalarType::U64;
      case TokenKind::DotS8:
        return ScalarType::S8;
      case TokenKind::DotS16:
        return ScalarType::S16;
      case TokenKind::DotS16x2:
        return ScalarType::S16x2;
      case TokenKind::DotS32:
        return ScalarType::S32;
      case TokenKind::DotS64:
        return ScalarType::S64;
      case TokenKind::DotB8:
        return ScalarType::B8;
      case TokenKind::DotB16:
        return ScalarType::B16;
      case TokenKind::DotB32:
        return ScalarType::B32;
      case TokenKind::DotB64:
        return ScalarType::B64;
      case TokenKind::DotB128:
        return ScalarType::B128;
      case TokenKind::DotF16:
        return ScalarType::F16;
      case TokenKind::DotF16x2:
        return ScalarType::F16x2;
      case TokenKind::DotF32:
        return ScalarType::F32;
      case TokenKind::DotF64:
        return ScalarType::F64;
      case TokenKind::DotBF16:
        return ScalarType::BF16;
      case TokenKind::DotBF16x2:
        return ScalarType::BF16x2;
      case TokenKind::DotE4m3x2:
        return ScalarType::E4m3x2;
      case TokenKind::DotE5m2x2:
        return ScalarType::E5m2x2;
      case TokenKind::DotU8x4:
        return ScalarType::U8x4;
      case TokenKind::DotS8x4:
        return ScalarType::S8x4;
      case TokenKind::DotF32x2:
        return ScalarType::F32x2;
      default:
        return std::nullopt;
    }
  }

  // require a scalar-type token and consume it
  tl::expected<ScalarType, ParseError> parse_scalar_type() {
    auto k = lex.peek().kind;
    if (auto t = token_to_scalar_type(k)) {
      lex.consume();
      return *t;
    }
    return err_tok("scalar type (.b32 / .f32 / ...)");
  }

  // ── state-space token → StateSpace ────────────────────────
  static std::optional<StateSpace> token_to_state_space(TokenKind k) {
    switch (k) {
      case TokenKind::DotReg:
        return StateSpace::Reg;
      case TokenKind::DotConst:
        return StateSpace::Const;
      case TokenKind::DotGlobal:
        return StateSpace::Global;
      case TokenKind::DotLocal:
        return StateSpace::Local;
      case TokenKind::DotParam:
        return StateSpace::Param;
      case TokenKind::DotShared:
        return StateSpace::Shared;
      case TokenKind::DotGeneric:
        return StateSpace::Generic;
      default:
        return std::nullopt;
    }
  }

  // ============================================================
  // §1  Module header
  //     .version X.Y
  //     .target smXXX [, feature]*
  //     .address_size 32|64
  // ============================================================

  tl::expected<std::pair<uint8_t, uint8_t>, ParseError> parse_version() {
    lex.consume();  // DotVersion

    // The lexer's longest-match rule causes "8.0" to be tokenized as a
    // single F64 token rather than Decimal + Dot + Decimal.
    auto tok = lex.peek();
    if (tok.kind == TokenKind::F64) {
      lex.consume();
      auto sv = tok.text;
      auto dot = sv.find('.');
      if (dot == std::string_view::npos)
        return err("malformed .version number");
      uint8_t major = (uint8_t)parse_uint(sv.substr(0, dot));
      uint8_t minor = (uint8_t)parse_uint(sv.substr(dot + 1));
      return std::make_pair(major, minor);
    }

    // Fallback: Decimal '.' Decimal
    auto major_tok = TRY(expect(TokenKind::Decimal, "major version"));
    uint8_t major = (uint8_t)parse_uint(major_tok.text);
    TRY(expect(TokenKind::Dot, "."));
    auto minor_tok = TRY(expect(TokenKind::Decimal, "minor version"));
    uint8_t minor = (uint8_t)parse_uint(minor_tok.text);
    return std::make_pair(major, minor);
  }

  // .target sm_XX  [, texmode_independent | debug | ...]
  tl::expected<uint32_t, ParseError> parse_target() {
    lex.consume();  // DotTarget
    // expect an identifier like sm_80
    auto id = TRY(expect(TokenKind::Ident, "sm_XX"));
    // parse the numeric part after "sm_"
    std::string_view sv = id.text;
    uint32_t sm = 0;
    if (sv.size() > 3 && sv.substr(0, 3) == "sm_") {
      std::from_chars(sv.data() + 3, sv.data() + sv.size(), sm);
    } else {
      return err("expected sm_XX identifier, got '" + std::string(sv) + "'");
    }
    // skip optional comma-separated feature tokens (texmode_*, debug, etc.)
    while (match(TokenKind::Comma)) {
      TRY(expect(TokenKind::Ident, "target feature"));
    }
    return sm;
  }

  // .address_size 32|64
  tl::expected<void, ParseError> parse_address_size() {
    lex.consume();  // DotAddressSize
    TRY(expect(TokenKind::Decimal, "32 or 64"));
    // we don't store this for now; PTX frontend always works in 64-bit
    return {};
  }

  // ============================================================
  // §2  Linking directive  (.extern / .visible / .weak)
  // ============================================================

  LinkingDirective parse_linking() {
    LinkingDirective d = LinkingDirective::None;
    while (true) {
      if (match(TokenKind::DotExtern))
        d = d | LinkingDirective::Extern;
      else if (match(TokenKind::DotVisible))
        d = d | LinkingDirective::Visible;
      else if (match(TokenKind::DotWeak))
        d = d | LinkingDirective::Weak;
      else
        break;
    }
    return d;
  }

  // ============================================================
  // §3  Type  (for variable declarations: scalar / vector / array)
  //
  //   .v2 .f32          →  VectorTypeT{2, F32}
  //   .b32              →  ScalarTypeT{B32}
  //   .b32 [4]          →  ArrayTypeT{nullopt, B32, {4}}
  //   .v2 .f32 [4]      →  ArrayTypeT{2, F32, {4}}
  // ============================================================

  tl::expected<Type, ParseError> parse_type() {
    // optional vector prefix
    std::optional<uint8_t> vec;
    if (match(TokenKind::DotV2))
      vec = 2;
    else if (match(TokenKind::DotV4))
      vec = 4;
    else if (match(TokenKind::DotV8))
      vec = 8;

    auto stype = TRY(parse_scalar_type());

    // optional array dimensions  [N][M]...
    std::vector<uint32_t> dims;
    while (check(TokenKind::LBracket)) {
      lex.consume();
      if (check(TokenKind::RBracket)) {
        // unsized dimension []
        dims.push_back(0);
        lex.consume();
      } else {
        auto ntok = TRY(expect(TokenKind::Decimal, "array dimension"));
        dims.push_back((uint32_t)parse_uint(ntok.text));
        TRY(expect(TokenKind::RBracket, "]"));
      }
    }

    if (!dims.empty()) {
      return ArrayTypeT{vec, stype, std::move(dims)};
    }
    if (vec) {
      return VectorTypeT{*vec, stype};
    }
    return ScalarTypeT{stype};
  }

  // Parse instruction operand type, supporting .vN vector prefix but not [N] array dimensions. Used for all instruction parsing functions (as opposed to parse_type() used in variable declarations).
  tl::expected<Type, ParseError> parse_instr_type() {
    std::optional<uint8_t> vec;
    if (match(TokenKind::DotV2))
      vec = 2;
    else if (match(TokenKind::DotV4))
      vec = 4;
    else if (match(TokenKind::DotV8))
      vec = 8;

    auto stype = TRY(parse_scalar_type());

    if (vec)
      return VectorTypeT{*vec, stype};
    return ScalarTypeT{stype};
  }

  // ============================================================
  // §4  Variable declaration
  //
  //   .reg   .b32  %r<4>;
  //   .reg   .b32  %r0, %r1, %r2;
  //   .global .align 16  .b32  foo[4];
  //   .param .b32  param0;
  // ============================================================

  tl::expected<MultiVariable, ParseError> parse_variable() {
    // optional .align N
    std::optional<uint32_t> align;
    if (match(TokenKind::DotAlign)) {
      auto n = TRY(expect(TokenKind::Decimal, "alignment value"));
      align = (uint32_t)parse_uint(n.text);
    }

    // optional .ptr  (ignored for now — just consume)
    match(TokenKind::DotPtr);

    // type
    auto typ = TRY(parse_type());

    // optional array init for global: = { ... }
    // (we parse names first, then look for initializer)

    // name(s)
    // Could be:
    //   %r<4>          → MultiVariableParameterized
    //   %r0, %r1, %r2  → MultiVariableNames
    //   foo            → single variable (MultiVariableNames with 1 element)

    auto first_name = TRY(expect(TokenKind::Ident, "variable name"));
    Ident name_sv = first_name.text;

    VariableInfo info;
    info.align = align;
    info.v_type = typ;
    // state_space will be filled in by the caller (parse_variable_decl)
    info.state_space = StateSpace::Reg;  // placeholder

    // check for parameterized range: %r<4>
    if (match(TokenKind::Lt)) {
      auto count_tok = TRY(expect(TokenKind::Decimal, "register count"));
      TRY(expect(TokenKind::Gt, ">"));
      uint32_t count = (uint32_t)parse_uint(count_tok.text);
      // optional initializer list is not common for .reg, skip
      return MultiVariableParameterized{info, name_sv, count};
    }

    // explicit list: name0, name1, ...
    std::vector<Ident> names;
    names.push_back(name_sv);
    while (match(TokenKind::Comma)) {
      // could be another name or an initializer value
      if (check(TokenKind::Ident)) {
        names.push_back(lex.consume().text);
      } else {
        // could be an immediate initializer — stop
        break;
      }
    }

    // optional = { initializer-list } for global variables
    if (match(TokenKind::Eq)) {
      if (match(TokenKind::LBrace)) {
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
          // parse each element as reg-or-imm
          if (check(TokenKind::Ident)) {
            auto id = lex.consume();
            info.array_init.push_back(RegOrImmediate<Ident>::Reg(id.text));
          } else {
            auto imm = TRY(parse_immediate());
            info.array_init.push_back(RegOrImmediate<Ident>::Imm(imm));
          }
          match(TokenKind::Comma);
        }
        TRY(expect(TokenKind::RBrace, "}"));
      } else {
        // scalar initializer
        auto imm = TRY(parse_immediate());
        info.array_init.push_back(RegOrImmediate<Ident>::Imm(imm));
      }
    }

    if (names.size() == 1) {
      return MultiVariableNames{info, names};
    }
    return MultiVariableNames{info, std::move(names)};
  }

  // Full variable-declaration statement (includes state-space):
  //   .reg .b32 %r<4>;
  //   .global .align 16 .b32 foo[4] = {0,0,0,0};
  tl::expected<MultiVariable, ParseError> parse_variable_decl() {
    auto k = lex.peek().kind;
    auto ss_opt = token_to_state_space(k);
    if (!ss_opt)
      return err_tok("state-space (.reg / .global / ...)");
    lex.consume();
    StateSpace ss = *ss_opt;

    auto mv = TRY(parse_variable());
    // patch in the state space
    std::visit([&](auto& v) { v.info.state_space = ss; }, mv);
    return mv;
  }

  // ============================================================
  // §5  Immediate value
  // ============================================================

  tl::expected<ImmediateValue, ParseError> parse_immediate() {
    auto tok = lex.peek();
    switch (tok.kind) {
      case TokenKind::Hex:
      case TokenKind::Decimal:
        lex.consume();
        return ImmediateValue::from_value(parse_uint(tok.text));

      case TokenKind::Minus: {
        lex.consume();
        auto ntok = TRY(expect(TokenKind::Decimal, "integer"));
        return ImmediateValue::from_value(
            -static_cast<int64_t>(parse_uint(ntok.text)));
      }

      case TokenKind::F32Hex: {
        // 0fXXXXXXXX  — 8 hex digits = float bits
        lex.consume();
        uint32_t bits = 0;
        auto sv = tok.text;
        std::from_chars(sv.data() + 2, sv.data() + sv.size(), bits, 16);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return ImmediateValue::from_value(f);
      }

      case TokenKind::F64Hex: {
        // 0dXXXXXXXXXXXXXXXX  — 16 hex digits = double bits
        lex.consume();
        uint64_t bits = 0;
        auto sv = tok.text;
        std::from_chars(sv.data() + 2, sv.data() + sv.size(), bits, 16);
        double d;
        std::memcpy(&d, &bits, sizeof(d));
        return ImmediateValue::from_value(d);
      }

      case TokenKind::F64: {
        lex.consume();
        double d = 0.0;
        auto sv = tok.text;
        std::from_chars(sv.data(), sv.data() + sv.size(), d);
        return ImmediateValue::from_value(d);
      }

      case TokenKind::WarpSz:
        lex.consume();
        return ImmediateValue::from_value(uint64_t{32});

      default:
        return err_tok("immediate value");
    }
  }

  // ============================================================
  // §6  Operand
  //
  //   %r0          → RegOrImmediate::Reg
  //   %r0+16       → RegOffset
  //   %r0.x        → VecMemberIdx
  //   {%r0, %r1}   → VecPack
  //   0x1234       → ImmediateValue
  // ============================================================

  tl::expected<ParsedOp, ParseError> parse_operand() {
    // VecPack   { a, b, ... }
    if (check(TokenKind::LBrace)) {
      lex.consume();
      ParsedOperand<Ident>::VecPack pack;
      // first element
      if (!check(TokenKind::RBrace)) {
        pack.push_back(TRY(parse_reg_or_imm()));
        while (match(TokenKind::Comma)) {
          if (check(TokenKind::RBrace))
            break;
          pack.push_back(TRY(parse_reg_or_imm()));
        }
      }
      TRY(expect(TokenKind::RBrace, "}"));
      return ParsedOp::from_value(std::move(pack));
    }

    // Identifier-based operands
    if (check(TokenKind::Ident)) {
      auto id = lex.consume();
      Ident base = id.text;

      // %r0.x  → VecMemberIdx
      if (check(TokenKind::Dot)) {
        lex.consume();
        auto member_tok =
            TRY(expect(TokenKind::Ident, "vector member (x/y/z/w)"));
        uint8_t m = 0;
        if (member_tok.text == "x")
          m = 0;
        else if (member_tok.text == "y")
          m = 1;
        else if (member_tok.text == "z")
          m = 2;
        else if (member_tok.text == "w")
          m = 3;
        else
          return err("unknown vector member '" + std::string(member_tok.text) +
                     "'");
        return ParsedOp::from_value(
            ParsedOperand<Ident>::VecMemberIdx{base, m});
      }

      // %r0+offset  or  %r0-offset → RegOffset
      if (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        int sign = check(TokenKind::Plus) ? 1 : -1;
        lex.consume();
        auto offset_tok = TRY(expect(TokenKind::Decimal, "offset"));
        int32_t off = sign * (int32_t)parse_uint(offset_tok.text);
        return ParsedOp::from_value(ParsedOperand<Ident>::RegOffset{base, off});
      }

      // plain register
      return ParsedOp::from_value(RegOrImmediate<Ident>::Reg(base));
    }

    // Address operand  [reg]  /  [reg+offset]  /  [reg-offset]  /  [imm]
    if (check(TokenKind::LBracket)) {
      lex.consume();  // eat '['
      ParsedOp inner;
      if (check(TokenKind::Ident)) {
        auto id = lex.consume();
        Ident base = id.text;
        if (check(TokenKind::Plus) || check(TokenKind::Minus)) {
          int sign = check(TokenKind::Plus) ? 1 : -1;
          lex.consume();
          // offset might be hex or decimal
          auto offset_tok = lex.peek();
          if (offset_tok.kind != TokenKind::Decimal &&
              offset_tok.kind != TokenKind::Hex)
            return err_tok("address offset");
          lex.consume();
          int32_t off = sign * (int32_t)parse_uint(offset_tok.text);
          inner =
              ParsedOp::from_value(ParsedOperand<Ident>::RegOffset{base, off});
        } else {
          inner = ParsedOp::from_value(RegOrImmediate<Ident>::Reg(base));
        }
      } else {
        auto imm = TRY(parse_immediate());
        inner = ParsedOp::from_value(imm);
      }
      TRY(expect(TokenKind::RBracket, "]"));
      return inner;
    }

    // Immediate
    auto imm = TRY(parse_immediate());
    return ParsedOp::from_value(imm);
  }

  tl::expected<RegOrImmediate<Ident>, ParseError> parse_reg_or_imm() {
    if (check(TokenKind::Ident)) {
      auto id = lex.consume();
      return RegOrImmediate<Ident>::Reg(id.text);
    }
    auto imm = TRY(parse_immediate());
    return RegOrImmediate<Ident>::Imm(imm);
  }

  // ============================================================
  // §7  Predicate guard   @p / @!p
  // ============================================================

  std::optional<PredAt> try_parse_pred() {
    if (!check(TokenKind::At))
      return std::nullopt;
    lex.consume();
    bool neg = match(TokenKind::Exclamation);
    if (!check(TokenKind::Ident))
      return std::nullopt;  // shouldn't happen
    Ident label = lex.consume().text;
    return PredAt{neg, label};
  }

  // ============================================================
  // §8  Instructions
  //     Each parse_instr_XXX() consumes from the token AFTER
  //     the opcode keyword.
  // ============================================================

  // ── helper: rounding mode ───────────────────��─────────────
  std::optional<RoundingMode> try_parse_rounding() {
    switch (lex.peek().kind) {
      case TokenKind::DotRn:
      case TokenKind::DotRni:
        lex.consume();
        return RoundingMode::NearestEven;
      case TokenKind::DotRz:
      case TokenKind::DotRzi:
        lex.consume();
        return RoundingMode::Zero;
      case TokenKind::DotRm:
      case TokenKind::DotRmi:
        lex.consume();
        return RoundingMode::NegativeInf;
      case TokenKind::DotRp:
      case TokenKind::DotRpi:
        lex.consume();
        return RoundingMode::PositiveInf;
      default:
        return std::nullopt;
    }
  }

  // ── helper: ftz ──────────────────────────────────────────
  bool try_parse_ftz() { return match(TokenKind::DotFtz); }
  bool try_parse_sat() { return match(TokenKind::DotSat); }

  // ── 8.1  mov ──────────────────────────────────────────────
  // mov.type  dst, src;    (scalar)
  // mov.type  dst, {a,b};  (pack)
  // mov.type  {a,b}, src;  (unpack)
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_mov() {
    auto stype = TRY(parse_scalar_type());
    Type typ = ScalarTypeT{stype};

    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrMov<ParsedOp>{MovDetails{typ}, dst, src};
  }

  // ── 8.2  ld / st ─────────────────────────────────────────
  tl::expected<LdStQualifierData, ParseError> parse_ld_qualifier() {
    LdStQualifierData q;
    switch (lex.peek().kind) {
      case TokenKind::DotVolatile:
        lex.consume();
        q.kind = LdStQualifier::Volatile;
        break;
      case TokenKind::DotRelaxed:
        lex.consume();
        q.kind = LdStQualifier::Relaxed;
        q.scope = TRY(parse_mem_scope());
        break;
      case TokenKind::DotAcquire:
        lex.consume();
        q.kind = LdStQualifier::Acquire;
        q.scope = TRY(parse_mem_scope());
        break;
      case TokenKind::DotRelease:
        lex.consume();
        q.kind = LdStQualifier::Release;
        q.scope = TRY(parse_mem_scope());
        break;
      default:
        q.kind = LdStQualifier::Weak;
        break;
    }
    return q;
  }

  tl::expected<MemScope, ParseError> parse_mem_scope() {
    switch (lex.peek().kind) {
      case TokenKind::DotCta:
        lex.consume();
        return MemScope::Cta;
      case TokenKind::DotCluster:
        lex.consume();
        return MemScope::Cluster;
      case TokenKind::DotGpu:
        lex.consume();
        return MemScope::Gpu;
      case TokenKind::DotSys:
        lex.consume();
        return MemScope::Sys;
      case TokenKind::DotGl:
        lex.consume();
        return MemScope::Gpu;
      default:
        return err_tok("memory scope (.cta/.gpu/.sys/.cluster/.gl)");
    }
  }

  tl::expected<StateSpace, ParseError> parse_state_space() {
    auto k = lex.peek().kind;
    if (auto ss = token_to_state_space(k)) {
      lex.consume();
      return *ss;
    }
    return err_tok("state space (.global/.shared/.local/...)");
  }

  LdCacheOperator try_parse_ld_cache() {
    switch (lex.peek().kind) {
      case TokenKind::DotCa:
        lex.consume();
        return LdCacheOperator::Cached;
      case TokenKind::DotCg:
        lex.consume();
        return LdCacheOperator::L2Only;
      case TokenKind::DotCs:
        lex.consume();
        return LdCacheOperator::Streaming;
      case TokenKind::DotLu:
        lex.consume();
        return LdCacheOperator::LastUse;
      case TokenKind::DotCv:
        lex.consume();
        return LdCacheOperator::Uncached;
      default:
        return LdCacheOperator::Cached;
    }
  }
  StCacheOperator try_parse_st_cache() {
    switch (lex.peek().kind) {
      case TokenKind::DotWb:
        lex.consume();
        return StCacheOperator::Writeback;
      case TokenKind::DotCg:
        lex.consume();
        return StCacheOperator::L2Only;
      case TokenKind::DotCs:
        lex.consume();
        return StCacheOperator::Streaming;
      case TokenKind::DotWt:
        lex.consume();
        return StCacheOperator::Writethrough;
      default:
        return StCacheOperator::Writeback;
    }
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_ld() {
    auto qual = TRY(parse_ld_qualifier());
    auto ss = TRY(parse_state_space());
    auto cache = try_parse_ld_cache();
    bool nc = match(TokenKind::DotNc);
    auto typ = TRY(parse_instr_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    LdDetails d{qual, ss, cache, typ, nc};
    return InstrLd<ParsedOp>{d, dst, src};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_st() {
    auto qual = TRY(parse_ld_qualifier());
    auto ss = TRY(parse_state_space());
    auto cache = try_parse_st_cache();
    auto typ = TRY(parse_instr_type());
    auto src1 = TRY(parse_operand());  // [addr]
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());  // value
    StData d{qual, ss, cache, typ};
    return InstrSt<ParsedOp>{d, src1, src2};
  }

  // ── 8.3  Arithmetic (integer) ─────────────────────────────
  //  add / sub
  tl::expected<ArithDetails, ParseError> parse_arith_details() {
    // PTX serials: [.rnd] [.ftz] [.sat] .type
    // all before .type, and in any order with optional presence
    auto rm = try_parse_rounding();  // .rn / .rz / .rm / .rp  (optional)
    bool ftz = try_parse_ftz();      // .ftz                    (optional)
    bool sat = try_parse_sat();      // .sat                    (optional)

    auto stype = TRY(parse_scalar_type());  // .f32 / .s32 / ...   (required)

    if (scalar_kind(stype) == ScalarKind::Float) {
      ArithFloat af;
      af.type_ = stype;
      af.rounding = rm.value_or(RoundingMode::NearestEven);
      af.flush_to_zero = ftz ? std::optional<bool>{true} : std::nullopt;
      af.saturate = sat;
      return af;
    } else {
      if (rm.has_value())
        return err("rounding mode not valid for integer add/sub");
      ArithInteger ai;
      ai.type_ = stype;
      ai.saturate = sat;
      return ai;
    }
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_add() {
    auto data = TRY(parse_arith_details());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());
    return InstrAdd<ParsedOp>{data, dst, src1, src2};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_sub() {
    auto data = TRY(parse_arith_details());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());
    return InstrSub<ParsedOp>{data, dst, src1, src2};
  }

  // mul
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_mul() {
    MulDetails data;
    auto stype = [&]() -> tl::expected<ScalarType, ParseError> {
      // .hi / .lo / .wide  for integer;  rounding for float
      return parse_scalar_type();
    };
    // determine int/float
    // peek: first modifier after "mul"
    if (check(TokenKind::DotHi) || check(TokenKind::DotLo) ||
        check(TokenKind::DotWide)) {
      MulInt mi;
      if (match(TokenKind::DotHi))
        mi.control = MulIntControl::High;
      else if (match(TokenKind::DotLo))
        mi.control = MulIntControl::Low;
      else {
        lex.consume();
        mi.control = MulIntControl::Wide;
      }
      mi.type_ = TRY(parse_scalar_type());
      data = mi;
    } else {
      // float path
      ArithFloat af;
      auto rm = try_parse_rounding();
      bool ftz = try_parse_ftz();
      bool sat = try_parse_sat();
      af.type_ = TRY(parse_scalar_type());
      af.rounding = rm.value_or(RoundingMode::NearestEven);
      af.flush_to_zero = ftz ? std::optional<bool>{true} : std::nullopt;
      af.saturate = sat;
      data = af;
    }
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());
    return InstrMul<ParsedOp>{data, dst, src1, src2};
  }

  // mad
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_mad() {
    MadDetails data;
    if (check(TokenKind::DotHi) || check(TokenKind::DotLo) ||
        check(TokenKind::DotWide)) {
      MadInt mi;
      if (match(TokenKind::DotHi))
        mi.control = MulIntControl::High;
      else if (match(TokenKind::DotLo))
        mi.control = MulIntControl::Low;
      else {
        lex.consume();
        mi.control = MulIntControl::Wide;
      }
      mi.saturate = try_parse_sat();
      mi.type_ = TRY(parse_scalar_type());
      data = mi;
    } else {
      ArithFloat af;
      auto rm = try_parse_rounding();
      bool ftz = try_parse_ftz();
      bool sat = try_parse_sat();
      af.type_ = TRY(parse_scalar_type());
      af.rounding = rm.value_or(RoundingMode::NearestEven);
      af.flush_to_zero = ftz ? std::optional<bool>{true} : std::nullopt;
      af.saturate = sat;
      af.is_fusable = false;
      data = af;
    }
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src3 = TRY(parse_operand());
    return InstrMad<ParsedOp>{data, dst, src1, src2, src3};
  }

  // fma (always float)
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_fma() {
    ArithFloat af;
    auto rm = try_parse_rounding();
    bool ftz = try_parse_ftz();
    bool sat = try_parse_sat();
    af.type_ = TRY(parse_scalar_type());
    af.rounding = rm.value_or(RoundingMode::NearestEven);
    af.flush_to_zero = ftz ? std::optional<bool>{true} : std::nullopt;
    af.saturate = sat;
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src3 = TRY(parse_operand());
    return InstrFma<ParsedOp>{af, dst, src1, src2, src3};
  }

  // div
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_div() {
    DivDetails data;
    // float?
    if (check(TokenKind::DotApprox) || check(TokenKind::DotFull) ||
        check(TokenKind::DotRn) || check(TokenKind::DotRz) ||
        check(TokenKind::DotRm) || check(TokenKind::DotRp)) {
      DivFloat df;
      if (match(TokenKind::DotApprox))
        df.kind = DivFloat::Kind::Approx;
      else if (match(TokenKind::DotFull))
        df.kind = DivFloat::Kind::ApproxFull;
      else {
        df.kind = DivFloat::Kind::Rounding;
        df.rounding = try_parse_rounding().value_or(RoundingMode::NearestEven);
      }
      bool ftz = try_parse_ftz();
      df.type_ = TRY(parse_scalar_type());
      df.flush_to_zero = ftz ? std::optional<bool>{true} : std::nullopt;
      data = df;
    } else {
      DivInt di;
      // .u32 / .s32 etc.
      auto stype = TRY(parse_scalar_type());
      di.sign = (scalar_kind(stype) == ScalarKind::Signed) ? DivSign::Signed
                                                           : DivSign::Unsigned;
      di.type_ = stype;
      data = di;
    }
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());
    return InstrDiv<ParsedOp>{data, dst, src1, src2};
  }

  // ── 8.4  Unary arithmetic ─────────────────────────────────
  tl::expected<TypeFtz, ParseError> parse_type_ftz() {
    bool ftz = try_parse_ftz();
    auto stype = TRY(parse_scalar_type());
    TypeFtz tf;
    tf.flush_to_zero = ftz ? std::optional<bool>{true} : std::nullopt;
    tf.type_ = stype;
    return tf;
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_abs() {
    auto data = TRY(parse_type_ftz());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrAbs<ParsedOp>{data, dst, src};
  }
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_neg() {
    auto data = TRY(parse_type_ftz());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrNeg<ParsedOp>{data, dst, src};
  }

  // min / max
  tl::expected<MinMaxDetails, ParseError> parse_min_max_details() {
    bool ftz = try_parse_ftz();
    bool nan = match(TokenKind::DotNaN);
    auto stype = TRY(parse_scalar_type());
    if (scalar_kind(stype) == ScalarKind::Float) {
      MinMaxFloat mf;
      mf.flush_to_zero = ftz ? std::optional<bool>{true} : std::nullopt;
      mf.nan = nan;
      mf.type_ = stype;
      return mf;
    } else {
      MinMaxInt mi;
      mi.sign = (scalar_kind(stype) == ScalarKind::Signed)
                    ? MinMaxSign::Signed
                    : MinMaxSign::Unsigned;
      mi.type_ = stype;
      return mi;
    }
  }
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_min() {
    auto data = TRY(parse_min_max_details());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s2 = TRY(parse_operand());
    return InstrMin<ParsedOp>{data, dst, s1, s2};
  }
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_max() {
    auto data = TRY(parse_min_max_details());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s2 = TRY(parse_operand());
    return InstrMax<ParsedOp>{data, dst, s1, s2};
  }

  // ── 8.5  Logic & bitwise ─────────────────────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_logic(
      TokenKind opcode) {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s2 = TRY(parse_operand());
    switch (opcode) {
      case TokenKind::And:
        return InstrAnd<ParsedOp>{stype, dst, s1, s2};
      case TokenKind::Or:
        return InstrOr<ParsedOp>{stype, dst, s1, s2};
      case TokenKind::Xor:
        return InstrXor<ParsedOp>{stype, dst, s1, s2};
      default:
        return err("unexpected logic opcode");
    }
  }
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_not() {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrNot<ParsedOp>{stype, dst, src};
  }
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_shl() {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s2 = TRY(parse_operand());
    return InstrShl<ParsedOp>{stype, dst, s1, s2};
  }
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_shr() {
    bool arith = check(TokenKind::DotS8) || check(TokenKind::DotS16) ||
                 check(TokenKind::DotS32) || check(TokenKind::DotS64);
    auto stype = TRY(parse_scalar_type());
    ShrData d;
    d.type_ = stype;
    d.kind = arith ? RightShiftKind::Arithmetic : RightShiftKind::Logical;
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s2 = TRY(parse_operand());
    return InstrShr<ParsedOp>{d, dst, s1, s2};
  }

  // ── 8.6  Comparison / setp ────────────────────────────────
  tl::expected<SetpCompareOp, ParseError> parse_cmp_op(bool is_float) {
    auto k = lex.peek().kind;
    if (is_float) {
      switch (k) {
        case TokenKind::DotEq:
          lex.consume();
          return SetpCompareFloat::Eq;
        case TokenKind::DotNe:
          lex.consume();
          return SetpCompareFloat::NotEq;
        case TokenKind::DotLt2:
          lex.consume();
          return SetpCompareFloat::Less;
        case TokenKind::DotLe:
          lex.consume();
          return SetpCompareFloat::LessOrEq;
        case TokenKind::DotGt2:
          lex.consume();
          return SetpCompareFloat::Greater;
        case TokenKind::DotGe:
          lex.consume();
          return SetpCompareFloat::GreaterOrEq;
        case TokenKind::DotEqu:
          lex.consume();
          return SetpCompareFloat::NanEq;
        case TokenKind::DotNeu:
          lex.consume();
          return SetpCompareFloat::NanNotEq;
        case TokenKind::DotLtu:
          lex.consume();
          return SetpCompareFloat::NanLess;
        case TokenKind::DotLeu:
          lex.consume();
          return SetpCompareFloat::NanLessOrEq;
        case TokenKind::DotGtu:
          lex.consume();
          return SetpCompareFloat::NanGreater;
        case TokenKind::DotGeu:
          lex.consume();
          return SetpCompareFloat::NanGreaterOrEq;
        case TokenKind::DotNum:
          lex.consume();
          return SetpCompareFloat::IsNotNan;
        case TokenKind::DotNan:
          lex.consume();
          return SetpCompareFloat::IsAnyNan;
        default:
          return err_tok("float compare op (.eq/.lt/...)");
      }
    } else {
      switch (k) {
        case TokenKind::DotEq:
          lex.consume();
          return SetpCompareInt::Eq;
        case TokenKind::DotNe:
          lex.consume();
          return SetpCompareInt::NotEq;
        case TokenKind::DotLt2:
        case TokenKind::DotLo:
          lex.consume();
          return SetpCompareInt::SignedLess;
        case TokenKind::DotLe:
        case TokenKind::DotLs:
          lex.consume();
          return SetpCompareInt::SignedLessOrEq;
        case TokenKind::DotGt2:
        case TokenKind::DotHs:
          lex.consume();
          return SetpCompareInt::SignedGreater;
        case TokenKind::DotGe:
          lex.consume();
          return SetpCompareInt::SignedGreaterOrEq;
        case TokenKind::DotLtu:
          lex.consume();
          return SetpCompareInt::UnsignedLess;
        case TokenKind::DotLeu:
          lex.consume();
          return SetpCompareInt::UnsignedLessOrEq;
        case TokenKind::DotGtu:
          lex.consume();
          return SetpCompareInt::UnsignedGreater;
        case TokenKind::DotGeu:
          lex.consume();
          return SetpCompareInt::UnsignedGreaterOrEq;
        default:
          return err_tok("int compare op (.eq/.lt/...)");
      }
    }
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_setp() {
    // PTX 语法: setp.CmpOp[.BoolOp][.ftz].type
    // 先读 cmp_op，但此时还不知道 is_float，需要 peek 类型来决定
    // 实际上 cmp_op token 本身已经隐含了 int/float，可直接尝试两者

    // 先尝试读 cmp_op（整数或浮点都试）
    // 策略：先 peek，若是已知的 cmp token 则消耗
    auto parse_cmp_any = [&]() -> tl::expected<SetpCompareOp, ParseError> {
      auto k = lex.peek().kind;
      // 整数专用
      switch (k) {
        case TokenKind::DotLt2:
          lex.consume();
          return SetpCompareInt::SignedLess;
        case TokenKind::DotLe:
          lex.consume();
          return SetpCompareInt::SignedLessOrEq;
        case TokenKind::DotGt2:
          lex.consume();
          return SetpCompareInt::SignedGreater;
        case TokenKind::DotGe:
          lex.consume();
          return SetpCompareInt::SignedGreaterOrEq;
        case TokenKind::DotEq:
          lex.consume();
          return SetpCompareInt::Eq;
        case TokenKind::DotNe:
          lex.consume();
          return SetpCompareInt::NotEq;
        case TokenKind::DotLtu:
          lex.consume();
          return SetpCompareInt::UnsignedLess;
        case TokenKind::DotLeu:
          lex.consume();
          return SetpCompareInt::UnsignedLessOrEq;
        case TokenKind::DotGtu:
          lex.consume();
          return SetpCompareInt::UnsignedGreater;
        case TokenKind::DotGeu:
          lex.consume();
          return SetpCompareInt::UnsignedGreaterOrEq;
        case TokenKind::DotLo:
          lex.consume();
          return SetpCompareInt::SignedLess;
        case TokenKind::DotLs:
          lex.consume();
          return SetpCompareInt::SignedLessOrEq;
        case TokenKind::DotHs:
          lex.consume();
          return SetpCompareInt::SignedGreater;
        // 浮点 NaN 变体（Equ/Neu/... 与整数 Ltu/... 不重叠）
        case TokenKind::DotEqu:
          lex.consume();
          return SetpCompareFloat::NanEq;
        case TokenKind::DotNeu:
          lex.consume();
          return SetpCompareFloat::NanNotEq;
        case TokenKind::DotNum:
          lex.consume();
          return SetpCompareFloat::IsNotNan;
        case TokenKind::DotNan:
          lex.consume();
          return SetpCompareFloat::IsAnyNan;
        default:
          return err_tok("compare op (.eq/.lt/...)");
      }
    };

    auto cmp = TRY(parse_cmp_any());

    // 可选 bool post-op modifier（在 ftz/type 之前）
    std::optional<SetpBoolPostOp> bop;
    if (match(TokenKind::DotAnd))
      bop = SetpBoolPostOp::And;
    else if (match(TokenKind::DotOr))
      bop = SetpBoolPostOp::Or;
    else if (match(TokenKind::DotXor))
      bop = SetpBoolPostOp::Xor;

    bool ftz = try_parse_ftz();
    auto stype = TRY(parse_scalar_type());
    bool is_fp = scalar_kind(stype) == ScalarKind::Float;

    // 如果 cmp 是整数变体但类型是浮点（或反之），在此可做语义修正
    if (is_fp) {
      if (auto* ci = std::get_if<SetpCompareInt>(&cmp)) {
        switch (*ci) {
          case SetpCompareInt::Eq:
            cmp = SetpCompareFloat::Eq;
            break;
          case SetpCompareInt::NotEq:
            cmp = SetpCompareFloat::NotEq;
            break;
          case SetpCompareInt::SignedLess:
            cmp = SetpCompareFloat::Less;
            break;
          case SetpCompareInt::SignedLessOrEq:
            cmp = SetpCompareFloat::LessOrEq;
            break;
          case SetpCompareInt::SignedGreater:
            cmp = SetpCompareFloat::Greater;
            break;
          case SetpCompareInt::SignedGreaterOrEq:
            cmp = SetpCompareFloat::GreaterOrEq;
            break;
          default:
            break;
        }
      }
    }

    SetpData data{stype, ftz ? std::optional<bool>{true} : std::nullopt, cmp};

    auto dst1 = TRY(parse_operand());
    std::optional<ParsedOp> dst2;
    if (match(TokenKind::Pipe))
      dst2 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());

    if (bop.has_value()) {
      bool neg_src3 = match(TokenKind::Exclamation);
      TRY(expect(TokenKind::Comma, ","));
      auto src3 = TRY(parse_operand());
      SetpBoolData bd{data, *bop, neg_src3};
      return InstrSetpBool<ParsedOp>{bd, dst1, dst2, src1, src2, src3};
    }

    return InstrSetp<ParsedOp>{data, dst1, dst2, src1, src2};
  }

  // ── 8.7  selp ────────────────────────────────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_selp() {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s2 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s3 = TRY(parse_operand());
    return InstrSelp<ParsedOp>{stype, dst, s1, s2, s3};
  }

  // ── 8.8  cvt ─────────────────────────────────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_cvt() {
    // parse all modifiers before the two types
    auto rm = try_parse_rounding();
    bool irnd = match(TokenKind::DotRni) || match(TokenKind::DotRzi) ||
                match(TokenKind::DotRmi) || match(TokenKind::DotRpi);
    bool ftz = try_parse_ftz();
    bool sat = try_parse_sat();
    bool relu = match(TokenKind::DotRelu);

    auto to_type = TRY(parse_scalar_type());
    auto from_type = TRY(parse_scalar_type());

    CvtMode mode;
    ScalarKind to_k = scalar_kind(to_type);
    ScalarKind from_k = scalar_kind(from_type);
    uint8_t to_sz = scalar_size_of(to_type);
    uint8_t from_sz = scalar_size_of(from_type);

    if (to_k == ScalarKind::Bit && from_k == ScalarKind::Bit &&
        to_sz == from_sz)
      mode = CvtModeBitcast{};
    else if (to_k == ScalarKind::Unsigned && from_k == ScalarKind::Unsigned)
      mode = (to_sz > from_sz) ? CvtMode{CvtModeZeroExtend{}}
                               : CvtMode{CvtModeTruncate{}};
    else if (to_k == ScalarKind::Signed && from_k == ScalarKind::Signed)
      mode = (to_sz > from_sz) ? CvtMode{CvtModeSignExtend{}}
                               : CvtMode{CvtModeTruncate{}};
    else if (to_k == ScalarKind::Float && from_k == ScalarKind::Float) {
      if (to_sz > from_sz)
        mode = CvtModeFPExtend{ftz ? std::optional<bool>{true} : std::nullopt,
                               sat};
      else {
        CvtModeFPTruncate t;
        t.rounding = rm.value_or(RoundingMode::NearestEven);
        t.is_integer_rounding = irnd;
        t.flush_to_zero = ftz ? std::optional<bool>{true} : std::nullopt;
        t.saturate = sat;
        t.relu = relu;
        mode = t;
      }
    } else if (to_k == ScalarKind::Float && (from_k == ScalarKind::Signed ||
                                             from_k == ScalarKind::Unsigned)) {
      mode = (from_k == ScalarKind::Signed)
                 ? CvtMode{CvtModeFPFromSigned{
                       rm.value_or(RoundingMode::NearestEven), sat}}
                 : CvtMode{CvtModeFPFromUnsigned{
                       rm.value_or(RoundingMode::NearestEven), sat}};
    } else if ((to_k == ScalarKind::Signed || to_k == ScalarKind::Unsigned) &&
               from_k == ScalarKind::Float) {
      mode = (to_k == ScalarKind::Signed)
                 ? CvtMode{CvtModeSignedFromFP{
                       rm.value_or(RoundingMode::Zero),
                       ftz ? std::optional<bool>{true} : std::nullopt}}
                 : CvtMode{CvtModeUnsignedFromFP{
                       rm.value_or(RoundingMode::Zero),
                       ftz ? std::optional<bool>{true} : std::nullopt}};
    } else if (sat && to_k == ScalarKind::Signed)
      mode = CvtModeIntSatSigned{};
    else if (sat && to_k == ScalarKind::Unsigned)
      mode = CvtModeIntSatUnsigned{};
    else
      mode = CvtModeBitcast{};

    CvtDetails data{from_type, to_type, mode};
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    // optional src2 for cvt with two sources (rare)
    std::optional<ParsedOp> src2;
    if (match(TokenKind::Comma)) {
      src2 = TRY(parse_operand());
    }
    return InstrCvt<ParsedOp>{data, dst, src, src2};
  }

  // ── 8.9  bra / call / ret / trap ────────���────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_bra() {
    match(TokenKind::DotUni);  // optional .uni
    auto label = TRY(expect(TokenKind::Ident, "branch label"));
    return InstrBra<Ident>{label.text};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_ret() {
    bool uni = match(TokenKind::DotUni);
    RetData d{uni};
    return InstrRet{d};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_trap() {
    return InstrTrap{};
  }

  // call  (uni)?  (retlist,)?  fname, (arglist)?;
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_call() {
    bool uni = match(TokenKind::DotUni);
    CallDetails details;
    details.uniform = uni;

    CallArgs<Ident> args;

    // optional return argument list  ( ret , )
    if (check(TokenKind::LParen)) {
      lex.consume();
      while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
        auto ret = TRY(expect(TokenKind::Ident, "return variable"));
        args.return_arguments.push_back(ret.text);
        match(TokenKind::Comma);
      }
      TRY(expect(TokenKind::RParen, ")"));
      TRY(expect(TokenKind::Comma, ","));
    }

    // function name
    auto fname = TRY(expect(TokenKind::Ident, "function name"));
    args.func = fname.text;

    // optional input argument list  , ( args )
    if (match(TokenKind::Comma)) {
      TRY(expect(TokenKind::LParen, "("));
      while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
        auto op = TRY(parse_operand());
        args.input_arguments.push_back(op);
        match(TokenKind::Comma);
      }
      TRY(expect(TokenKind::RParen, ")"));
    }

    return InstrCall<ParsedOp>{details, args};
  }

  // ── 8.10  membar ─────────────────────────────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_membar() {
    MemScope sc = TRY(parse_mem_scope());
    return InstrMembar{sc};
  }

  // ── 8.11  bar ────────────────────────────────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_bar() {
    bool aligned = match(TokenKind::DotAligned);

    // bar.sync / bar.red / bar.warp
    if (match(TokenKind::DotRed)) {
      // bar.red.aligned  pred_op  barrier, nthreads, p;
      Reduction red;
      if (match(TokenKind::DotAnd))
        red = Reduction::And;
      else if (match(TokenKind::DotOr))
        red = Reduction::Or;
      else if (match(TokenKind::DotPopc))
        red = Reduction::Popc;
      else
        return err_tok("reduction op (.and/.or/.popc)");

      BarRedData d{aligned, red};
      auto bar = TRY(parse_operand());
      TRY(expect(TokenKind::Comma, ","));
      std::optional<ParsedOp> nth;
      // optional nthreads
      auto s2 = TRY(parse_operand());
      TRY(expect(TokenKind::Comma, ","));
      auto s3 = TRY(parse_operand());
      auto s4 = TRY(parse_operand());
      return InstrBarRed<ParsedOp>{d, bar, s2, s3, s4, nth};
    }

    if (match(TokenKind::DotWarp)) {
      auto src = TRY(parse_operand());
      return InstrBarWarp<ParsedOp>{src};
    }

    // default: bar.sync / bar.aligned
    match(TokenKind::DotSync);
    BarData d{aligned};
    auto src1 = TRY(parse_operand());
    std::optional<ParsedOp> src2;
    if (match(TokenKind::Comma)) {
      src2 = TRY(parse_operand());
    }
    return InstrBar<ParsedOp>{d, src1, src2};
  }

  // ── 8.12  atom ───────────────────────────────────────────
  tl::expected<AtomicOp, ParseError> parse_atomic_op() {
    switch (lex.peek().kind) {
      case TokenKind::DotAnd:
        lex.consume();
        return AtomicOp::And;
      case TokenKind::DotOr:
        lex.consume();
        return AtomicOp::Or;
      case TokenKind::DotXor:
        lex.consume();
        return AtomicOp::Xor;
      case TokenKind::DotExch:
        lex.consume();
        return AtomicOp::Exchange;
      case TokenKind::DotAdd:
        lex.consume();
        return AtomicOp::Add;
      case TokenKind::DotInc:
        lex.consume();
        return AtomicOp::IncrementWrap;
      case TokenKind::DotDec:
        lex.consume();
        return AtomicOp::DecrementWrap;
      case TokenKind::DotMin:
        lex.consume();
        return AtomicOp::SignedMin;
      case TokenKind::DotMax:
        lex.consume();
        return AtomicOp::SignedMax;
      default:
        return err_tok("atomic op (.and/.or/.add/...)");
    }
  }

  tl::expected<AtomSemantics, ParseError> parse_atom_semantics() {
    switch (lex.peek().kind) {
      case TokenKind::DotRelaxed:
        lex.consume();
        return AtomSemantics::Relaxed;
      case TokenKind::DotAcquire:
        lex.consume();
        return AtomSemantics::Acquire;
      case TokenKind::DotRelease:
        lex.consume();
        return AtomSemantics::Release;
      case TokenKind::DotAcqRel:
        lex.consume();
        return AtomSemantics::AcqRel;
      default:
        return AtomSemantics::Relaxed;
    }
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_atom() {
    auto sem = TRY(parse_atom_semantics());
    auto sc = check(TokenKind::DotCta) || check(TokenKind::DotGpu) ||
                      check(TokenKind::DotSys) || check(TokenKind::DotCluster)
                  ? TRY(parse_mem_scope())
                  : MemScope::Gpu;
    auto ss = TRY(parse_state_space());

    if (match(TokenKind::DotCas)) {
      auto stype = TRY(parse_scalar_type());
      AtomCasDetails d{stype, sem, sc, ss};
      auto dst = TRY(parse_operand());
      TRY(expect(TokenKind::Comma, ","));
      auto src1 = TRY(parse_operand());
      TRY(expect(TokenKind::Comma, ","));
      auto src2 = TRY(parse_operand());
      TRY(expect(TokenKind::Comma, ","));
      auto src3 = TRY(parse_operand());
      return InstrAtomCas<ParsedOp>{d, dst, src1, src2, src3};
    }

    auto op = TRY(parse_atomic_op());
    auto typ = TRY(parse_instr_type());
    AtomDetails d{typ, sem, sc, ss, op};
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());
    return InstrAtom<ParsedOp>{d, dst, src1, src2};
  }

  // ── 8.13  vote ───────────────────────────────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_vote() {
    match(TokenKind::DotSync);
    VoteMode mode;
    if (match(TokenKind::DotAll))
      mode = VoteMode::All;
    else if (match(TokenKind::DotAny))
      mode = VoteMode::Any;
    else if (match(TokenKind::DotBallot))
      mode = VoteMode::Ballot;
    else
      return err_tok("vote mode (.all/.any/.ballot)");
    bool neg = match(TokenKind::Exclamation);
    // .pred
    TRY(expect(TokenKind::DotPred, ".pred"));
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src2 = TRY(parse_operand());
    return InstrVote<ParsedOp>{VoteDetails{mode, neg}, dst, src1, src2};
  }

  // ── 8.14  shfl.sync ──────────────────────────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_shfl() {
    TRY(expect(TokenKind::DotSync, ".sync"));
    ShuffleMode mode;
    if (match(TokenKind::DotUp))
      mode = ShuffleMode::Up;
    else if (match(TokenKind::DotDown))
      mode = ShuffleMode::Down;
    else if (match(TokenKind::DotBfly))
      mode = ShuffleMode::Bfly;
    else if (match(TokenKind::DotIdx))
      mode = ShuffleMode::Idx;
    else
      return err_tok("shfl mode (.up/.down/.bfly/.idx)");
    // .b32
    TRY(expect(TokenKind::DotB32, ".b32"));
    auto dst = TRY(parse_operand());
    std::optional<ParsedOp> dst_pred;
    if (match(TokenKind::Pipe))
      dst_pred = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src_lane = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src_opts = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src_mask = TRY(parse_operand());
    return InstrShflSync<ParsedOp>{ShflSyncDetails{mode},
                                   dst,
                                   dst_pred,
                                   src,
                                   src_lane,
                                   src_opts,
                                   src_mask};
  }

  // ── 8.15  Miscellaneous small instructions ────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_activemask() {
    TRY(expect(TokenKind::DotB32, ".b32"));
    auto dst = TRY(parse_operand());
    return InstrActivemask<ParsedOp>{dst};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_popc() {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrPopc<ParsedOp>{stype, dst, src};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_clz() {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrClz<ParsedOp>{stype, dst, src};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_brev() {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrBrev<ParsedOp>{stype, dst, src};
  }

  // bfind[.shiftamt].type dst, a;
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_bfind() {
    bool shiftamt = match(TokenKind::DotShiftamt);
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrBfind<ParsedOp>{BfindDetails{stype, shiftamt}, dst, src};
  }

  // ldu.{ss}.type dst, [src];
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_ldu() {
    // optional state space (default: global)
    StateSpace ss = StateSpace::Global;
    if (check(TokenKind::DotGlobal)) { lex.consume(); ss = StateSpace::Global; }
    else if (check(TokenKind::DotConst)) { lex.consume(); ss = StateSpace::Const; }
    auto typ = TRY(parse_instr_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrLdu<ParsedOp>{LduDetails{ss, typ}, dst, src};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_rem() {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s2 = TRY(parse_operand());
    return InstrRem<ParsedOp>{stype, dst, s1, s2};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_sad() {
    auto stype = TRY(parse_scalar_type());
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s1 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s2 = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto s3 = TRY(parse_operand());
    return InstrSad<ParsedOp>{stype, dst, s1, s2, s3};
  }

  // ── 8.16  cvta ───────────────────────────────────────────
  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_cvta() {
    // cvta.to.ss  or  cvta.ss
    CvtaDirection dir = CvtaDirection::ExplicitToGeneric;
    StateSpace ss;
    if (match(TokenKind::DotTo)) {  // cvta.to.global
      dir = CvtaDirection::GenericToExplicit;
      ss = TRY(parse_state_space());
    } else {
      ss = TRY(parse_state_space());
    }
    // accept .u32 or .u64 (address size)
    if (!match(TokenKind::DotU32) && !match(TokenKind::DotU64))
      return err_tok(".u32 or .u64");
    // (we ignore the size here; it's implicit from the register width)
    auto dst = TRY(parse_operand());
    TRY(expect(TokenKind::Comma, ","));
    auto src = TRY(parse_operand());
    return InstrCvta<ParsedOp>{CvtaDetails{ss, dir}, dst, src};
  }

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instr_fence() {
    // ── fence.proxy.* ────────────────────────────────────────────────────
    if (match(TokenKind::DotProxy)) {
      // fence.proxy.async::generic.sem.sync_restrict.scope — uni-dir
      if (match(TokenKind::DotAsyncGeneric)) {
        FenceSemantics sem;
        if (match(TokenKind::DotAcquire))
          sem = FenceSemantics::Acquire;
        else if (match(TokenKind::DotRelease))
          sem = FenceSemantics::Release;
        else
          return err_tok(
              "fence proxy async::generic sem (.acquire / .release)");
        FenceSyncRestrictSpace rspace;
        if (match(TokenKind::DotSyncRestrictSharedCta))
          rspace = FenceSyncRestrictSpace::SharedCta;
        else if (match(TokenKind::DotSyncRestrictSharedCluster))
          rspace = FenceSyncRestrictSpace::SharedCluster;
        else
          return err_tok("fence proxy async::generic sync_restrict");
        auto scope = TRY(parse_mem_scope());
        return InstrFence<ParsedOp>{
            FenceDetails{FenceProxyAsyncGeneric{sem, rspace, scope}}};
      }

      // fence.proxy.tensormap::generic.sem.scope — uni-dir
      // ".tensormap::generic" as DotIdent by lexer
      if (check(TokenKind::DotIdent)) {
        lex.consume();  // eat .tensormap::generic
        FenceSemantics sem;
        if (match(TokenKind::DotAcquire))
          sem = FenceSemantics::Acquire;
        else if (match(TokenKind::DotRelease))
          sem = FenceSemantics::Release;
        else
          return err_tok("fence proxy tensormap sem (.acquire / .release)");
        auto scope = TRY(parse_mem_scope());
        return InstrFence<ParsedOp>{
            FenceDetails{FenceProxyTensormapUnidir{sem, scope}}};
      }

      // fence.proxy.proxykind — bi-dir (non sem / scope)
      FenceProxyKind kind;
      if (match(TokenKind::DotAlias))
        kind = FenceProxyKind::Alias;
      else if (match(TokenKind::DotAsyncGlobal))
        kind = FenceProxyKind::AsyncGlobal;
      else if (match(TokenKind::DotAsyncSharedCta))
        kind = FenceProxyKind::AsyncSharedCta;
      else if (match(TokenKind::DotAsyncSharedCluster))
        kind = FenceProxyKind::AsyncSharedCluster;
      else if (match(TokenKind::DotAsync))
        kind = FenceProxyKind::Async;
      else
        return err_tok(
            "fence proxy kind (.alias/.async/.async.global/"
            ".async.shared::cta/.async.shared::cluster)");
      return InstrFence<ParsedOp>{FenceDetails{FenceProxyBidir{kind}}};
    }

    // ── fence.op_restrict.release.<scope> ───────────────────────────────
    if (match(TokenKind::DotOpRestrict)) {
      if (!match(TokenKind::DotRelease))
        return err_tok("fence op_restrict sem (.release)");
      auto scope = TRY(parse_mem_scope());
      return InstrFence<ParsedOp>{FenceDetails{FenceOpRestrict{scope}}};
    }

    // ── fence{.sem}.scope  /  fence.sem.sync_restrict.scope ─────────────
    std::optional<FenceSemantics> sem;
    if (match(TokenKind::DotSc))
      sem = FenceSemantics::Sc;
    else if (match(TokenKind::DotAcqRel))
      sem = FenceSemantics::AcqRel;
    else if (match(TokenKind::DotAcquire))
      sem = FenceSemantics::Acquire;
    else if (match(TokenKind::DotRelease))
      sem = FenceSemantics::Release;
    // sem == nullopt → fence.scope form (omitting sem), valid

    // sync_restrict only appears under .acquire / .release
    if (sem == FenceSemantics::Acquire || sem == FenceSemantics::Release) {
      FenceSyncRestrictSpace rspace;
      bool has_sr = false;
      if (match(TokenKind::DotSyncRestrictSharedCta)) {
        rspace = FenceSyncRestrictSpace::SharedCta;
        has_sr = true;
      } else if (match(TokenKind::DotSyncRestrictSharedCluster)) {
        rspace = FenceSyncRestrictSpace::SharedCluster;
        has_sr = true;
      }
      if (has_sr) {
        auto scope = TRY(parse_mem_scope());
        return InstrFence<ParsedOp>{
            FenceDetails{FenceSyncRestrict{*sem, rspace, scope}}};
      }
    }

    // simple fence{.sem}.scope
    auto scope = TRY(parse_mem_scope());
    return InstrFence<ParsedOp>{FenceDetails{FenceThread{sem, scope}}};
  }

  // ============================================================
  // §9  Instruction dispatch (after consuming the opcode token)
  // ============================================================

  tl::expected<Instruction<ParsedOp>, ParseError> parse_instruction(
      TokenKind opcode) {
    switch (opcode) {
        // data movement
        // clang-format off
      case TokenKind::Mov:  return parse_instr_mov();
      case TokenKind::Ld:   return parse_instr_ld();
      case TokenKind::St:   return parse_instr_st();
      case TokenKind::Cvt:  return parse_instr_cvt();
      case TokenKind::Cvta: return parse_instr_cvta();
      // arithmetic
      case TokenKind::Add:  return parse_instr_add();
      case TokenKind::Sub:  return parse_instr_sub();
      case TokenKind::Mul:  return parse_instr_mul();
      case TokenKind::Mad:  return parse_instr_mad();
      case TokenKind::Fma:  return parse_instr_fma();
      case TokenKind::Div:  return parse_instr_div();
      case TokenKind::Rem:  return parse_instr_rem();
      case TokenKind::Abs:  return parse_instr_abs();
      case TokenKind::Neg:  return parse_instr_neg();
      case TokenKind::Min:  return parse_instr_min();
      case TokenKind::Max:  return parse_instr_max();
      case TokenKind::Sad:  return parse_instr_sad();
      case TokenKind::Popc: return parse_instr_popc();
      case TokenKind::Clz:  return parse_instr_clz();
      case TokenKind::Brev: return parse_instr_brev();
      case TokenKind::Bfind: return parse_instr_bfind();
      case TokenKind::Ldu:  return parse_instr_ldu();
      // logic
      case TokenKind::And:  return parse_instr_logic(TokenKind::And);
      case TokenKind::Or:   return parse_instr_logic(TokenKind::Or);
      case TokenKind::Xor:  return parse_instr_logic(TokenKind::Xor);
      case TokenKind::Not:  return parse_instr_not();
      case TokenKind::Shl:  return parse_instr_shl();
      case TokenKind::Shr:  return parse_instr_shr();
      // compare / select
      case TokenKind::Setp: return parse_instr_setp();
      case TokenKind::Selp: return parse_instr_selp();
      // control
      case TokenKind::Bra:  return parse_instr_bra();
      case TokenKind::Call: return parse_instr_call();
      case TokenKind::Ret:  return parse_instr_ret();
      case TokenKind::Trap: return parse_instr_trap();
      // sync / mem
      case TokenKind::Bar:
      case TokenKind::Barrier: return parse_instr_bar();
      case TokenKind::Membar:  return parse_instr_membar();
      case TokenKind::Atom:    return parse_instr_atom();
      // warp / vote
      case TokenKind::Vote:       return parse_instr_vote();
      case TokenKind::Shfl:       return parse_instr_shfl();
      case TokenKind::Activemask: return parse_instr_activemask();
      // ptx 9.2 new instructions
      case TokenKind::Fence:       return parse_instr_fence();
      default:
        return err("unimplemented opcode (token " +
                   std::to_string((int)opcode) + ")");
    }
    // clang-format on
  }

  // ============================================================
  // §10  Statement
  //
  //   label:
  //   variable-declaration;
  //   [@pred]  instruction  operands  ;
  //   { block }
  // ============================================================

  bool is_state_space_token(TokenKind k) {
    return token_to_state_space(k).has_value();
  }

  bool is_opcode_token(TokenKind k) {
    // a quick membership test — true if k is one of the instruction opcodes
    switch (k) {
      case TokenKind::Mov:
      case TokenKind::Ld:
      case TokenKind::St:
      case TokenKind::Cvt:
      case TokenKind::Cvta:
      case TokenKind::Add:
      case TokenKind::Sub:
      case TokenKind::Mul:
      case TokenKind::Mad:
      case TokenKind::Fma:
      case TokenKind::Div:
      case TokenKind::Rem:
      case TokenKind::Abs:
      case TokenKind::Neg:
      case TokenKind::Min:
      case TokenKind::Max:
      case TokenKind::Sad:
      case TokenKind::Popc:
      case TokenKind::Clz:
      case TokenKind::Brev:
      case TokenKind::Bfind:
      case TokenKind::Ldu:
      case TokenKind::And:
      case TokenKind::Or:
      case TokenKind::Xor:
      case TokenKind::Not:
      case TokenKind::Shl:
      case TokenKind::Shr:
      case TokenKind::Setp:
      case TokenKind::Selp:
      case TokenKind::Bra:
      case TokenKind::Call:
      case TokenKind::Ret:
      case TokenKind::Trap:
      case TokenKind::Bar:
      case TokenKind::Barrier:
      case TokenKind::Membar:
      case TokenKind::Atom:
      case TokenKind::Vote:
      case TokenKind::Shfl:
      case TokenKind::Activemask:
      case TokenKind::Fence:
      case TokenKind::Red:
      case TokenKind::Mbarrier:
      case TokenKind::Stmatrix:
      case TokenKind::Wgmma:
      case TokenKind::Tcgen05:
      case TokenKind::Setmaxnreg:
      case TokenKind::Getctarank:
      case TokenKind::Elect:
      case TokenKind::Discard:
      case TokenKind::Brkpt:
      case TokenKind::Movmatrix:
      case TokenKind::Tensormap:
      case TokenKind::Prefetchu:
      case TokenKind::Clusterlaunchcontrol:
      case TokenKind::Cp:  // cp.async.bulk.* (Cp already handled in lexer)
        return true;
      default:
        return false;
    }
  }

  tl::expected<Statement<ParsedOp>, ParseError> parse_statement() {
    // block
    if (check(TokenKind::LBrace)) {
      return parse_block();
    }

    // label:  (ident followed by colon)
    if (check(TokenKind::Ident)) {
      // we need 2-token lookahead: peek at second token
      // we use peek() / consume() / "put back" via a saved token approach
      auto id_tok = lex.consume();
      if (check(TokenKind::Colon)) {
        lex.consume();  // eat ':'
        return Statement<ParsedOp>::Label(id_tok.text);
      }
      // not a label — push back by... we can't easily push back.
      // Solution: we assume all bare idents at statement start that
      // are NOT opcodes are labels, and opcodes start instructions.
      // But we already consumed the ident. We need to handle this.
      // Since we consumed it and it wasn't followed by ':', it must
      // be an opcode-style ident. Treat it as an opcode.
      if (is_opcode_token(id_tok.kind)) {
        return err("unexpected ident '" + std::string(id_tok.text) + "'");
      }
      // Fallthrough: treat as parse error
      return err("expected ':' after label '" + std::string(id_tok.text) + "'");
    }

    // predicate guard  @p  or @!p
    auto pred = try_parse_pred();

    // opcode
    auto op_tok = lex.peek();
    if (!is_opcode_token(op_tok.kind))
      return err_tok("instruction opcode");
    lex.consume();

    auto instr = TRY(parse_instruction(op_tok.kind));
    TRY(expect(TokenKind::Semicolon, ";"));

    return Statement<ParsedOp>::Instr(pred, std::move(instr));
  }

  tl::expected<Statement<ParsedOp>, ParseError> parse_block() {
    TRY(expect(TokenKind::LBrace, "{"));
    std::vector<Statement<ParsedOp>> stmts;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
      // variable declarations inside a block
      if (is_state_space_token(lex.peek().kind)) {
        auto mv = TRY(parse_variable_decl());
        TRY(expect(TokenKind::Semicolon, ";"));
        stmts.push_back(Statement<ParsedOp>::Var(std::move(mv)));
        continue;
      }
      stmts.push_back(TRY(parse_statement()));
    }
    TRY(expect(TokenKind::RBrace, "}"));
    return Statement<ParsedOp>::Block(std::move(stmts));
  }

  // ============================================================
  // §11  Function  (.entry / .func)
  // ============================================================

  // Parse a parameter list:  ( .param .b32 p0, .param .b32 p1, ... )
  tl::expected<std::vector<Variable>, ParseError> parse_param_list() {
    TRY(expect(TokenKind::LParen, "("));
    std::vector<Variable> params;
    while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
      // optional linking directives on each param are ignored
      // each param: .param .type name[array]?
      TRY(expect(TokenKind::DotParam, ".param"));
      auto mv = TRY(parse_variable());
      // MultiVariable → flatten to single Variable
      std::visit(
          [&](auto& v) {
            v.info.state_space = StateSpace::Param;
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>,
                                         MultiVariableNames>) {
              for (auto& nm : v.names)
                params.push_back(Variable{v.info, nm});
            } else {
              // parameterized (unusual for params but handle gracefully)
              params.push_back(Variable{v.info, v.name});
            }
          },
          mv);
      match(TokenKind::Comma);
    }
    TRY(expect(TokenKind::RParen, ")"));
    return params;
  }

  tl::expected<Function<ParsedOp>, ParseError> parse_function(MethodKind kind) {
    MethodDeclaration decl;
    decl.kind = kind;

    // optional return params  ( .param .b32 ret0 )
    if (check(TokenKind::LParen)) {
      decl.return_arguments = TRY(parse_param_list());
    }

    // function name
    auto name_tok = TRY(expect(TokenKind::Ident, "function name"));
    decl.name = name_tok.text;

    // input params
    if (check(TokenKind::LParen)) {
      decl.input_arguments = TRY(parse_param_list());
    }

    Function<ParsedOp> func;
    func.func_directive = decl;

    // optional tuning directives before body
    while (true) {
      if (check(TokenKind::DotMaxnreg)) {
        lex.consume();
        auto n = TRY(expect(TokenKind::Decimal, "maxnreg value"));
        func.tuning.push_back(TuningMaxNReg{(uint32_t)parse_uint(n.text)});
      } else if (check(TokenKind::DotMaxntid)) {
        lex.consume();
        auto x = TRY(expect(TokenKind::Decimal, "ntid.x"));
        uint32_t xi = (uint32_t)parse_uint(x.text);
        uint32_t yi = 1, zi = 1;
        if (match(TokenKind::Comma)) {
          auto y = TRY(expect(TokenKind::Decimal, "ntid.y"));
          yi = (uint32_t)parse_uint(y.text);
          if (match(TokenKind::Comma)) {
            auto z = TRY(expect(TokenKind::Decimal, "ntid.z"));
            zi = (uint32_t)parse_uint(z.text);
          }
        }
        func.tuning.push_back(TuningMaxNtid{xi, yi, zi});
      } else if (check(TokenKind::DotReqntid)) {
        lex.consume();
        auto x = TRY(expect(TokenKind::Decimal, "reqntid.x"));
        uint32_t xi = (uint32_t)parse_uint(x.text);
        uint32_t yi = 1, zi = 1;
        if (match(TokenKind::Comma)) {
          auto y = TRY(expect(TokenKind::Decimal, "reqntid.y"));
          yi = (uint32_t)parse_uint(y.text);
          if (match(TokenKind::Comma)) {
            auto z = TRY(expect(TokenKind::Decimal, "reqntid.z"));
            zi = (uint32_t)parse_uint(z.text);
          }
        }
        func.tuning.push_back(TuningReqNtid{xi, yi, zi});
      } else if (check(TokenKind::DotMinnctapersm)) {
        lex.consume();
        auto n = TRY(expect(TokenKind::Decimal, "minnctapersm value"));
        func.tuning.push_back(TuningMinNCtaPerSm{(uint32_t)parse_uint(n.text)});
      } else if (check(TokenKind::DotNoreturn)) {
        lex.consume();
        func.tuning.push_back(TuningNoReturn{});
      } else {
        break;
      }
    }

    // optional body { ... }  or just declaration ending with ;
    if (check(TokenKind::LBrace)) {
      auto block = TRY(parse_block());
      // block is a Statement::BlockS — extract its statements
      auto& inner = std::get<typename Statement<ParsedOp>::BlockS>(block.inner);
      func.body = std::move(inner.stmts);
    } else {
      func.body = std::nullopt;  // declaration only
    }

    return func;
  }

  // ============================================================
  // §12  Top-level directive
  // ============================================================

  tl::expected<Directive<ParsedOp>, ParseError> parse_directive() {
    LinkingDirective linking = parse_linking();

    // .entry / .func
    if (check(TokenKind::DotEntry) || check(TokenKind::DotFunc)) {
      MethodKind kind =
          check(TokenKind::DotEntry) ? MethodKind::Kernel : MethodKind::Func;
      lex.consume();
      auto func = TRY(parse_function(kind));
      Directive<ParsedOp> d;
      d.inner = typename Directive<ParsedOp>::MethodD{linking, std::move(func)};
      return d;
    }

    // global variable declaration
    if (is_state_space_token(lex.peek().kind) &&
        lex.peek().kind != TokenKind::DotReg) {
      auto mv = TRY(parse_variable_decl());
      TRY(expect(TokenKind::Semicolon, ";"));
      // flatten MultiVariable to Variable (first name only, or parameterized)
      Variable var;
      std::visit(
          [&](auto& v) {
            var.info = v.info;
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>,
                                         MultiVariableNames>) {
              var.name = v.names.empty() ? Ident{} : v.names[0];
            } else {
              var.name = v.name;
            }
          },
          mv);
      Directive<ParsedOp> d;
      d.inner = typename Directive<ParsedOp>::VariableD{linking, var};
      return d;
    }

    return err_tok(".entry / .func / .global / .const / .shared");
  }

  // ============================================================
  // §13  Module (entry point)
  // ============================================================

  tl::expected<Module, ParseError> parse() {
    Module mod;

    // ── module header ──────────────────────────────────────
    if (!check(TokenKind::DotVersion))
      return err_tok(".version");
    mod.ptx_version = TRY(parse_version());

    if (!check(TokenKind::DotTarget))
      return err_tok(".target");
    mod.sm_version = TRY(parse_target());

    // optional .address_size
    if (check(TokenKind::DotAddressSize)) {
      auto r = parse_address_size();
      if (!r)
        return tl::unexpected(r.error());
    }

    // ── directives ─────────────────────────────────────────
    mod.invalid_directives = 0;

    while (!check(TokenKind::Eof)) {
      // skip stray .loc / .file / .section / .pragma
      if (check(TokenKind::DotLoc) || check(TokenKind::DotFile) ||
          check(TokenKind::DotSection) || check(TokenKind::DotPragma)) {
        // consume until end-of-line / next directive
        while (!check(TokenKind::Eof) && !check(TokenKind::DotVersion) &&
               !check(TokenKind::DotTarget) && !check(TokenKind::DotEntry) &&
               !check(TokenKind::DotFunc) && !check(TokenKind::DotExtern) &&
               !check(TokenKind::DotVisible) && !check(TokenKind::DotWeak) &&
               !is_state_space_token(lex.peek().kind)) {
          lex.consume();
        }
        continue;
      }

      auto dir = parse_directive();
      if (!dir) {
        ++mod.invalid_directives;
        // error recovery: skip to next top-level token
        while (!check(TokenKind::Eof) && !check(TokenKind::DotEntry) &&
               !check(TokenKind::DotFunc) && !check(TokenKind::DotExtern) &&
               !check(TokenKind::DotVisible)) {
          lex.consume();
        }
        continue;
      }
      mod.directives.push_back(std::move(*dir));
    }

    return mod;
  }
};

// ── helpers for TRY macro ────────────────────────────────────────────────
// We need a way to early-return on error inside member functions.
// Define TRY as a local macro (undef'd at end of file).

}  // namespace ptx_frontend

// ── macro (file-scope) ────────────────────────────────────────────────────
// Must be defined before use inside the struct above.
// We reopen the namespace and define after the struct definition.
// (Actually the macro must be defined before the struct — move it to top
//  of the translation unit, before the struct definition.)

namespace ptx_frontend {

tl::expected<Module, ParseError> parse_module(std::string_view src) {
  Parser p(src);
  return p.parse();
}

};  // namespace ptx_frontend
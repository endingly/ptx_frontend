#pragma once
#include <cstdint>
#include <cstring>
#include <expected.hpp>
#include <limits>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>
#include "utils.hpp"

namespace ptx_frontend {

using tl::expected;

enum class ScalarType : uint8_t {
  U8,
  U16,
  U16x2,
  U32,
  U64,
  S8,
  S16,
  S16x2,
  S32,
  S64,
  B8,
  B16,
  B32,
  B64,
  B128,
  F16,
  F16x2,
  F32,
  F64,
  BF16,
  BF16x2,
  E4m3x2,
  E5m2x2,
  Pred,
};

enum class ScalarKind { Bit, Unsigned, Signed, Float, Pred };

inline ScalarKind scalar_kind(ScalarType t) {
  switch (t) {
    case ScalarType::U8:
    case ScalarType::U16:
    case ScalarType::U16x2:
    case ScalarType::U32:
    case ScalarType::U64:
      return ScalarKind::Unsigned;
    case ScalarType::S8:
    case ScalarType::S16:
    case ScalarType::S16x2:
    case ScalarType::S32:
    case ScalarType::S64:
      return ScalarKind::Signed;
    case ScalarType::B8:
    case ScalarType::B16:
    case ScalarType::B32:
    case ScalarType::B64:
    case ScalarType::B128:
      return ScalarKind::Bit;
    case ScalarType::F16:
    case ScalarType::F16x2:
    case ScalarType::F32:
    case ScalarType::F64:
    case ScalarType::BF16:
    case ScalarType::BF16x2:
    case ScalarType::E4m3x2:
    case ScalarType::E5m2x2:
      return ScalarKind::Float;
    case ScalarType::Pred:
      return ScalarKind::Pred;
  }
  return ScalarKind::Bit;  // unreachable
}

inline uint8_t scalar_size_of(ScalarType t) {
  switch (t) {
    case ScalarType::U8:
    case ScalarType::S8:
    case ScalarType::B8:
    case ScalarType::Pred:
      return 1;
    case ScalarType::U16:
    case ScalarType::S16:
    case ScalarType::B16:
    case ScalarType::F16:
    case ScalarType::BF16:
    case ScalarType::E4m3x2:
    case ScalarType::E5m2x2:
      return 2;
    case ScalarType::U32:
    case ScalarType::S32:
    case ScalarType::B32:
    case ScalarType::F32:
    case ScalarType::U16x2:
    case ScalarType::S16x2:
    case ScalarType::F16x2:
    case ScalarType::BF16x2:
      return 4;
    case ScalarType::U64:
    case ScalarType::S64:
    case ScalarType::B64:
    case ScalarType::F64:
      return 8;
    case ScalarType::B128:
      return 16;
  }
  return 0;
}

struct ScalarTypeT {
  ScalarType type;
};

struct VectorTypeT {
  uint8_t count;
  ScalarType type;
};

struct ArrayTypeT {
  std::optional<uint8_t> vector_size;  // non-zero if present
  ScalarType type;
  std::vector<uint32_t> dims;
};

using Type = std::variant<ScalarTypeT, VectorTypeT, ArrayTypeT>;

inline Type make_scalar(ScalarType t) {
  return ScalarTypeT{t};
}

inline Type make_vector(uint8_t n, ScalarType t) {
  return VectorTypeT{n, t};
}

enum class StateSpace : uint8_t {
  Reg,
  Const,
  Global,
  Local,
  Param,
  Shared,
  SharedCluster,
  Tex,
  Generic,
};

enum class MemScope : uint8_t { Cta, Cluster, Gpu, Sys };

struct ImmediateValue {
  std::variant<uint64_t, int64_t, float, double> value;

  template <typename T>
    requires std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t> ||
             std::is_same_v<T, float> || std::is_same_v<T, double>
  static ImmediateValue from_value(T v) {
    ImmediateValue i;
    i.value = v;
    return i;
  }

  uint64_t get_bits() const {
    return std::visit(
        [](auto v) -> uint64_t {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, uint64_t>) {
            return v;
          } else if constexpr (std::is_same_v<T, int64_t>) {
            return static_cast<uint64_t>(v);
          } else if constexpr (std::is_same_v<T, float>) {
            uint32_t b;
            std::memcpy(&b, &v, sizeof(b));
            return b;
          } else {  // double
            uint64_t b;
            std::memcpy(&b, &v, sizeof(b));
            return b;
          }
        },
        value);
  }

  bool operator==(const ImmediateValue& other) const = default;
};

using Ident = std::string_view;

template <typename Id>
concept IdentLike = std::copyable<Id> and std::equality_comparable<Id>;

template <IdentLike Id>
struct RegOrImmediate {
  std::variant<ImmediateValue, Id> value;

  static RegOrImmediate Reg(Id id) {
    RegOrImmediate r;
    r.value = id;
    return r;
  }

  static RegOrImmediate Imm(ImmediateValue v) {
    RegOrImmediate r;
    r.value = v;
    return r;
  }

  bool operator==(const RegOrImmediate& other) const = default;
};

template <IdentLike Id>
struct ParsedOperand {

  //     Reg,        // %r0
  //     RegOffset,  // %r0+4
  //     Imm,        // 0x1234
  //     VecMember,  // %r0.x  (.x=0 .y=1 .z=2 .w=3)
  //     VecPack,    // {%r0, %r1, %r2, %r3}

  struct RegOffset {
    Id base;
    int32_t offset;

    bool operator==(const RegOffset& other) const = default;
  };

  struct VecMemberIdx {
    Id base;
    uint8_t member;  // .x=0 .y=1 .z=2 .w=3

    bool operator==(const VecMemberIdx& other) const = default;
  };

  using VecPack = std::vector<RegOrImmediate<Id>>;

  // start for operand type traits
  static constexpr bool is_operand = true;
  using id_type = Id;
  // end for operand type traits

  std::variant<RegOffset, VecMemberIdx, RegOrImmediate<Id>, ImmediateValue,
               VecPack>
      value;

  template <typename T>
    requires utils::is_one_of<T, RegOffset, VecMemberIdx, RegOrImmediate<Id>,
                              ImmediateValue, VecPack>

  static ParsedOperand from_value(T v) {
    ParsedOperand p;
    if constexpr (std::is_same_v<T, VecPack>) {
      p.value = std::move(v);
    } else {
      p.value = v;
    }
    return p;
  }

  template <IdentLike NewId, typename Err, typename Fn>
    requires std::invocable<Fn, Id>
  expected<ParsedOperand<NewId>, Err> map_id(Fn&& fn) const {
    /*  doc:
        variant member              map_id operantor
        ─────────────────────────────────────────────────────
        RegOffset{base, offset}     fn(base) → new base，offset 不变
        VecMemberIdx{base,member}   fn(base) → new base，member 不变
        RegOrImmediate<Id>          is_reg ? fn(reg) : copy imm directly
        ImmediateValue              not have Id, copy directly
        VecPack                     process each element, is_reg ? fn : copy directly
     */
    return std::visit(
        [&](const auto& v) -> expected<ParsedOperand<NewId>, Err> {
          using T = std::decay_t<decltype(v)>;

          // %r0+4
          if constexpr (std::is_same_v<T, RegOffset>) {
            auto r = fn(v.base);
            if (!r)
              return tl::unexpected(r.error());
            return ParsedOperand<NewId>::from_value(
                typename ParsedOperand<NewId>::RegOffset{*r, v.offset});
          }

          // %r0.x
          else if constexpr (std::is_same_v<T, VecMemberIdx>) {
            auto r = fn(v.base);
            if (!r)
              return tl::unexpected(r.error());
            return ParsedOperand<NewId>::from_value(
                typename ParsedOperand<NewId>::VecMemberIdx{*r, v.member});
          }

          // %r0(Reg) or imm like 0xffff
          else if constexpr (std::is_same_v<T, RegOrImmediate<Id>>) {
            if (std::holds_alternative<Id>(v.value)) {
              auto r = fn(std::get<Id>(v.value));
              if (!r)
                return unexpected(r.error());
              return ParsedOperand<NewId>::from_value(
                  RegOrImmediate<NewId>::Reg(*r));
            } else {
              return ParsedOperand<NewId>::from_value(
                  RegOrImmediate<NewId>::Imm(
                      std::get<ImmediateValue>(v.value)));
            }
          }

          // 0x1234, not have id
          else if constexpr (std::is_same_v<T, ImmediateValue>) {
            return ParsedOperand<NewId>::from_value(v);
          }

          // {%r0, %r1, ...}
          else if constexpr (std::is_same_v<T, VecPack>) {
            typename ParsedOperand<NewId>::VecPack new_vec;
            new_vec.reserve(v.size());
            for (const auto& item : v) {
              if (std::holds_alternative<Id>(item.value)) {
                auto r = fn(std::get<Id>(item.value));
                if (!r)
                  return tl::unexpected(r.error());
                new_vec.push_back(RegOrImmediate<NewId>::Reg(*r));
              } else {
                new_vec.push_back(RegOrImmediate<NewId>::Imm(
                    std::get<ImmediateValue>(item.value)));
              }
            }
            return ParsedOperand<NewId>::from_value(std::move(new_vec));
          }
        },
        value);
  }

  bool operator==(const ParsedOperand&) const = default;
};

using ParsedOp = ParsedOperand<Ident>;

// ============================================================
// § 7  Predicate guard  ←  ast.rs::PredAt
// ============================================================

struct PredAt {
  bool negate;  // @!p vs @p
  Ident label;
};

// ============================================================
// § 8  Data structs for instruction semantics
//       (one-to-one with ast.rs structs / enums)
// ============================================================

// --- RoundingMode ---
enum class RoundingMode { NearestEven, Zero, NegativeInf, PositiveInf };

// --- LdStQualifier ---
enum class LdStQualifier { Weak, Volatile, Relaxed, Acquire, Release };
struct LdStQualifierData {
  LdStQualifier kind = LdStQualifier::Weak;
  MemScope scope{};  // used for Relaxed / Acquire / Release
};

// --- Cache operators ---
enum class LdCacheOperator { Cached, L2Only, Streaming, LastUse, Uncached };
enum class StCacheOperator { Writeback, L2Only, Streaming, Writethrough };
enum class CpAsyncCacheOperator { Cached, L2Only };

// --- ArithInteger / ArithFloat / ArithDetails ---
struct ArithInteger {
  ScalarType type_;
  bool saturate = false;
};
struct ArithFloat {
  ScalarType type_;
  RoundingMode rounding = RoundingMode::NearestEven;
  std::optional<bool> flush_to_zero;  // nullopt = not applicable
  bool saturate = false;
  bool is_fusable = false;  // see ZLUDA comment about fused mul/add
};
using ArithDetails = std::variant<ArithInteger, ArithFloat>;

// --- TypeFtz (Abs, Neg, Rsqrt, Ex2) ---
struct TypeFtz {
  std::optional<bool> flush_to_zero;
  ScalarType type_;
};

// --- FlushToZero (Sin, Cos, Lg2, Ex2 approx path) ---
struct FlushToZero {
  bool flush_to_zero;
};

// --- MulIntControl ---
enum class MulIntControl { Low, High, Wide };

// --- MulDetails ---
struct MulInt {
  ScalarType type_;
  MulIntControl control;
};
using MulDetails = std::variant<MulInt, ArithFloat>;

// --- Mul24Details ---
enum class Mul24Control { Lo, Hi };
struct Mul24Details {
  ScalarType type_;
  Mul24Control control;
};

// --- MadDetails ---
struct MadInt {
  MulIntControl control;
  bool saturate;
  ScalarType type_;
};
using MadDetails = std::variant<MadInt, ArithFloat>;

// --- CarryKind / CarryDetails / MadCarryDetails ---
enum class CarryKind { CarryIn, CarryOut, CarryInCarryOut };
struct CarryDetails {
  CarryKind kind;
  ScalarType type_;
};
struct MadCarryDetails {
  CarryKind kind;
  MulIntControl control;
  ScalarType type_;
};

// --- MinMaxFloat / MinMaxDetails ---
struct MinMaxFloat {
  std::optional<bool> flush_to_zero;
  bool nan;
  ScalarType type_;
};
enum class MinMaxSign { Signed, Unsigned };
struct MinMaxInt {
  MinMaxSign sign;
  ScalarType type_;
};
using MinMaxDetails = std::variant<MinMaxInt, MinMaxFloat>;

// --- DivDetails ---
struct DivFloat {
  ScalarType type_;
  std::optional<bool> flush_to_zero;
  enum class Kind { Approx, ApproxFull, Rounding } kind;
  RoundingMode rounding = RoundingMode::NearestEven;
};
enum class DivSign { Unsigned, Signed };
struct DivInt {
  DivSign sign;
  ScalarType type_;
};
using DivDetails = std::variant<DivInt, DivFloat>;

// --- RcpKind / RcpData ---
struct RcpCompliant {
  RoundingMode rounding;
};
using RcpKind = std::variant<std::monostate /*Approx*/, RcpCompliant>;
struct RcpData {
  RcpKind kind;
  std::optional<bool> flush_to_zero;
  ScalarType type_;
};

// --- LdDetails / StData ---
struct LdDetails {
  LdStQualifierData qualifier;
  StateSpace state_space;
  LdCacheOperator caching = LdCacheOperator::Cached;
  Type typ;
  bool non_coherent = false;
};
struct StData {
  LdStQualifierData qualifier;
  StateSpace state_space;
  StCacheOperator caching = StCacheOperator::Writeback;
  Type typ;
};

// --- MovDetails ---
struct MovDetails {
  Type typ;
};

// --- RetData ---
struct RetData {
  bool uniform = false;
};

// --- BarData / BarRedData ---
enum class Reduction { And, Or, Popc };
struct BarData {
  bool aligned;
};
struct BarRedData {
  bool aligned;
  Reduction pred_reduction;
};

// --- ShflSync ---
enum class ShuffleMode { Up, Down, Bfly, Idx };
struct ShflSyncDetails {
  ShuffleMode mode;
};

// --- ShfDetails (funnel shift) ---
enum class ShiftDirection { Left, Right };
enum class FunnelShiftMode { Clamp, Wrap };
struct ShfDetails {
  ShiftDirection direction;
  FunnelShiftMode mode;
};

// --- ShrData ---
enum class RightShiftKind { Arithmetic, Logical };
struct ShrData {
  ScalarType type_;
  RightShiftKind kind;
};

// --- CvtMode / CvtDetails ---
struct CvtModeZeroExtend {};
struct CvtModeSignExtend {};
struct CvtModeTruncate {};
struct CvtModeBitcast {};
struct CvtModeIntSatSigned {};
struct CvtModeIntSatUnsigned {};
struct CvtModeFPExtend {
  std::optional<bool> flush_to_zero;
  bool saturate;
};
struct CvtModeFPTruncate {
  RoundingMode rounding;
  bool is_integer_rounding;
  std::optional<bool> flush_to_zero;
  bool saturate;
  bool relu;
};
struct CvtModeFPRound {
  std::optional<RoundingMode> integer_rounding;
  std::optional<bool> flush_to_zero;
  bool saturate;
};
struct CvtModeSignedFromFP {
  RoundingMode rounding;
  std::optional<bool> flush_to_zero;
};
struct CvtModeUnsignedFromFP {
  RoundingMode rounding;
  std::optional<bool> flush_to_zero;
};
struct CvtModeFPFromSigned {
  RoundingMode rounding;
  bool saturate;
};
struct CvtModeFPFromUnsigned {
  RoundingMode rounding;
  bool saturate;
};

using CvtMode =
    std::variant<CvtModeZeroExtend, CvtModeSignExtend, CvtModeTruncate,
                 CvtModeBitcast, CvtModeIntSatSigned, CvtModeIntSatUnsigned,
                 CvtModeFPExtend, CvtModeFPTruncate, CvtModeFPRound,
                 CvtModeSignedFromFP, CvtModeUnsignedFromFP,
                 CvtModeFPFromSigned, CvtModeFPFromUnsigned>;
struct CvtDetails {
  ScalarType from;
  ScalarType to;
  CvtMode mode;
};

// --- CvtaDetails ---
enum class CvtaDirection { GenericToExplicit, ExplicitToGeneric };
struct CvtaDetails {
  StateSpace state_space;
  CvtaDirection direction;
};

// --- SetpCompareOp ---
enum class SetpCompareInt {
  Eq,
  NotEq,
  UnsignedLess,
  UnsignedLessOrEq,
  UnsignedGreater,
  UnsignedGreaterOrEq,
  SignedLess,
  SignedLessOrEq,
  SignedGreater,
  SignedGreaterOrEq,
};
enum class SetpCompareFloat {
  Eq,
  NotEq,
  Less,
  LessOrEq,
  Greater,
  GreaterOrEq,
  NanEq,
  NanNotEq,
  NanLess,
  NanLessOrEq,
  NanGreater,
  NanGreaterOrEq,
  IsNotNan,
  IsAnyNan,
};
using SetpCompareOp = std::variant<SetpCompareInt, SetpCompareFloat>;

enum class SetpBoolPostOp { And, Or, Xor };

struct SetpData {
  ScalarType type_;
  std::optional<bool> flush_to_zero;
  SetpCompareOp cmp_op;
};
struct SetpBoolData {
  SetpData base;
  SetpBoolPostOp bool_op;
  bool negate_src3;
};
struct SetData {
  ScalarType dtype;
  SetpData base;
};
struct SetBoolData {
  ScalarType dtype;
  SetpBoolData base;
};

// --- AtomDetails ---
enum class AtomSemantics { Relaxed, Acquire, Release, AcqRel };
enum class AtomicOp {
  And,
  Or,
  Xor,
  Exchange,
  Add,
  IncrementWrap,
  DecrementWrap,
  SignedMin,
  UnsignedMin,
  SignedMax,
  UnsignedMax,
  FloatAdd,
  FloatMin,
  FloatMax,
};
struct AtomDetails {
  Type typ;
  AtomSemantics semantics;
  MemScope scope;
  StateSpace space;
  AtomicOp op;
};
struct AtomCasDetails {
  ScalarType type_;
  AtomSemantics semantics;
  MemScope scope;
  StateSpace space;
};

// --- VoteDetails ---
enum class VoteMode { All, Any, Ballot };
struct VoteDetails {
  VoteMode mode;
  bool negate;
};

// --- ReduxSyncData ---
struct ReduxSyncData {
  ScalarType type_;
  Reduction reduction;
};

// --- LdMatrixDetails ---
enum class MatrixShape { M8N8 };  // extend as needed
enum class MatrixNumber { X1, X2, X4 };
struct LdMatrixDetails {
  MatrixShape shape;
  MatrixNumber number;
  bool transpose;
  StateSpace state_space;
  ScalarType type_;

  Type get_loaded_type() const {
    uint8_t count = number == MatrixNumber::X1   ? 1
                    : number == MatrixNumber::X2 ? 2
                                                 : 4;
    return make_vector(count, ScalarType::B32);
  }
};

// --- MmaDetails ---
enum class MatrixLayout { RowMajor, ColMajor };
struct MmaDetails {
  MatrixLayout alayout, blayout;
  ScalarType cd_type_scalar, ab_type_scalar;

  Type dtype() const {
    return scalar_kind(cd_type_scalar) == ScalarKind::Float
               ? make_vector(4, ScalarType::F32)
               : make_vector(4, ScalarType::U32);
  }
  Type atype() const { return make_vector(4, ScalarType::U32); }
  Type btype() const { return make_vector(2, ScalarType::U32); }
  Type ctype() const { return dtype(); }
};

// --- Dp4aDetails / Dp2aDetails ---
struct Dp4aDetails {
  ScalarType atype, btype;
  ScalarType ctype() const {
    return (atype == ScalarType::U32 && btype == ScalarType::U32)
               ? ScalarType::U32
               : ScalarType::S32;
  }
};
enum class Dp2aControl { Low, High };
struct Dp2aData {
  ScalarType atype, btype;
  Dp2aControl control;
  ScalarType ctype() const {
    return (atype == ScalarType::U32 && btype == ScalarType::U32)
               ? ScalarType::U32
               : ScalarType::S32;
  }
};

// --- CpAsyncDetails ---
enum class CpAsyncCpSize { Bytes4, Bytes8, Bytes16 };
struct CpAsyncDetails {
  CpAsyncCacheOperator caching;
  StateSpace space;
  CpAsyncCpSize cp_size;
  std::optional<uint64_t> src_size;
};

// --- CreatePolicyFractionalDetails ---
enum class EvictionPriority { Normal, PreferL2, LastUse, NoAllocate };
struct CreatePolicyFractionalDetails {
  EvictionPriority primary_priority;
  float fraction;
};

// --- PrefetchData ---
enum class CacheLevel { L1, L2 };
struct PrefetchData {
  StateSpace space;
  CacheLevel level;
};

// --- BmskMode ---
enum class BmskMode { Clamp, Wrap };

// --- GridDepControlAction ---
enum class GridDepControlAction { LaunchDependents, WaitOnDependent };

// --- CallDetails / CallArgs ---
struct CallDetails {
  bool uniform = false;
  std::vector<std::pair<Type, StateSpace>> return_arguments;
  std::vector<std::pair<Type, StateSpace>> input_arguments;
};

template <IdentLike Id>
struct CallArgs {
  std::vector<Id> return_arguments;
  Id func;
  std::vector<ParsedOperand<Id>> input_arguments;
  bool is_external = false;  // see ZLUDA hack comment
};

// ============================================================
// § 9  Instruction<Op>  ←  ast.rs  generate_instruction_type!(...)
//      Using std::variant for the closed sum type.
// ============================================================

/**
 * @brief OperandLike concept to constrain instruction operand types. An operand type must be copyable and equality comparable, tagged with a static boolean is_operand, and have an associated id_type for the IdentLike constraint on operands.
 * 
 * @tparam Op The operand type to be constrained.
 * @note Generally, only two types satisfy this constraint: `ParsedOperand`/`ResolveOperand`
 */
template <typename Op>
concept OperandLike = requires {
  requires Op::is_operand == true;  // taged with is_operand
  typename Op::
      id_type;  // must have an associated id_type for the IdentLike constraint on operands
} && std::copyable<Op> && std::equality_comparable<Op>;

template <OperandLike Op>
using Opt = std::optional<Op>;  // models Option<T> arguments

template <typename Op>
struct InstrAbs {
  TypeFtz data;
  Op dst, src;
};
template <typename Op>
struct InstrActivemask {
  Op dst;
};
template <typename Op>
struct InstrAdd {
  ArithDetails data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrAddExtended {
  CarryDetails data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrSubExtended {
  CarryDetails data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrMadExtended {
  MadCarryDetails data;
  Op dst, src1, src2, src3;
};
template <typename Op>
struct InstrAnd {
  ScalarType data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrAtom {
  AtomDetails data;
  Op dst, src1 /*addr*/, src2;
};
template <typename Op>
struct InstrAtomCas {
  AtomCasDetails data;
  Op dst, src1, src2, src3;
};
template <typename Op>
struct InstrBarWarp {
  Op src;
};
template <typename Op>
struct InstrBar {
  BarData data;
  Op src1;
  Opt<Op> src2;
};
template <typename Op>
struct InstrBarRed {
  BarRedData data;
  Op dst1, src_barrier, src_predicate, src_negate_predicate;
  Opt<Op> src_threadcount;
};
template <typename Op>
struct InstrBfe {
  ScalarType data;
  Op dst, src1, src2 /*u32*/, src3 /*u32*/;
};
template <typename Op>
struct InstrBfi {
  ScalarType data;
  Op dst, src1, src2, src3 /*u32*/, src4 /*u32*/;
};
template <typename Op>
struct InstrBmsk {
  BmskMode data;
  Op dst, src_a, src_b;
};

// Bra takes an Ident, not a full operand
template <typename Id>
struct InstrBra {
  Id src;
};

template <typename Op>
struct InstrBrev {
  ScalarType data;
  Op dst, src;
};

// Call has its own args struct
template <typename Op>
struct InstrCall {
  CallDetails data;
  CallArgs<typename Op::id_type /* if Op = ParsedOperand<Id> */> arguments;
};
// NOTE: In C++ without Rust's associated types, you may need to pass Id as
// a second template parameter for Call. See usage note below.

template <typename Op>
struct InstrClz {
  ScalarType data;
  Op dst /*u32*/, src;
};
template <typename Op>
struct InstrCos {
  FlushToZero data;
  Op dst, src;
};
template <typename Op>
struct InstrCpAsync {
  CpAsyncDetails data;
  Op src_to /*shared*/, src_from /*global*/;
};
struct InstrCpAsyncCommitGroup {};
template <typename Op>
struct InstrCpAsyncWaitGroup {
  Op src_group;
};
struct InstrCpAsyncWaitAll {};
template <typename Op>
struct InstrCreatePolicyFractional {
  CreatePolicyFractionalDetails data;
  Op dst_policy;
};
template <typename Op>
struct InstrCvt {
  CvtDetails data;
  Op dst, src;
  Opt<Op> src2;
};
template <typename Op>
struct InstrCvtPack {
  ScalarType data;
  Op dst /*u32*/, src1 /*s32*/, src2 /*s32*/, src3 /*b32*/;
};
template <typename Op>
struct InstrCvta {
  CvtaDetails data;
  Op dst, src;
};
template <typename Op>
struct InstrDiv {
  DivDetails data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrDp4a {
  Dp4aDetails data;
  Op dst /*b32*/, src1, src2, src3;
};
template <typename Op>
struct InstrEx2 {
  TypeFtz data;
  Op dst, src;
};
template <typename Op>
struct InstrFma {
  ArithFloat data;
  Op dst, src1, src2, src3;
};
template <typename Op>
struct InstrLd {
  LdDetails data;
  Op dst, src;
};
template <typename Op>
struct InstrLg2 {
  FlushToZero data;
  Op dst, src;
};
template <typename Op>
struct InstrMad {
  MadDetails data;
  Op dst, src1, src2, src3;
};
template <typename Op>
struct InstrMax {
  MinMaxDetails data;
  Op dst, src1, src2;
};
struct InstrMembar {
  MemScope data;
};
template <typename Op>
struct InstrMin {
  MinMaxDetails data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrMov {
  MovDetails data;
  Op dst, src;
};
template <typename Op>
struct InstrMul {
  MulDetails data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrMul24 {
  Mul24Details data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrNanosleep {
  Op src;
};
template <typename Op>
struct InstrNeg {
  TypeFtz data;
  Op dst, src;
};
template <typename Op>
struct InstrNot {
  ScalarType data;
  Op dst, src;
};
template <typename Op>
struct InstrOr {
  ScalarType data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrPopc {
  ScalarType data;
  Op dst /*u32*/, src;
};
template <typename Op>
struct InstrPrmt {
  Op dst, src1, src2, src3;
};
template <typename Op>
struct InstrRcp {
  RcpData data;
  Op dst, src;
};
template <typename Op>
struct InstrRem {
  ScalarType data;
  Op dst, src1, src2;
};
struct InstrRet {
  RetData data;
};
template <typename Op>
struct InstrRsqrt {
  TypeFtz data;
  Op dst, src;
};
template <typename Op>
struct InstrSelp {
  ScalarType data;
  Op dst, src1, src2, src3 /*pred*/;
};
template <typename Op>
struct InstrSet {
  SetData data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrSetBool {
  SetBoolData data;
  Op dst, src1, src2, src3 /*pred*/;
};
template <typename Op>
struct InstrSetp {
  SetpData data;
  Op dst1 /*pred*/;
  Opt<Op> dst2 /*pred*/;
  Op src1, src2;
};
template <typename Op>
struct InstrSetpBool {
  SetpBoolData data;
  Op dst1;
  Opt<Op> dst2;
  Op src1, src2, src3;
};
template <typename Op>
struct InstrShflSync {
  ShflSyncDetails data;
  Op dst;
  Opt<Op> dst_pred;
  Op src, src_lane, src_opts, src_membermask;
};
template <typename Op>
struct InstrShf {
  ShfDetails data;
  Op dst, src_a, src_b, src_c;
};
template <typename Op>
struct InstrShl {
  ScalarType data;
  Op dst, src1, src2 /*u32*/;
};
template <typename Op>
struct InstrShr {
  ShrData data;
  Op dst, src1, src2 /*u32*/;
};
template <typename Op>
struct InstrSin {
  FlushToZero data;
  Op dst, src;
};
template <typename Op>
struct InstrSqrt {
  RcpData data;
  Op dst, src;
};
template <typename Op>
struct InstrSt {
  StData data;
  Op src1, src2;
};
template <typename Op>
struct InstrSub {
  ArithDetails data;
  Op dst, src1, src2;
};
struct InstrTrap {};
template <typename Op>
struct InstrXor {
  ScalarType data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrTanh {
  ScalarType data;
  Op dst, src;
};
template <typename Op>
struct InstrVote {
  VoteDetails data;
  Op dst, src1 /*pred*/, src2 /*u32*/;
};
template <typename Op>
struct InstrReduxSync {
  ReduxSyncData data;
  Op dst, src, src_membermask /*u32*/;
};
template <typename Op>
struct InstrLdMatrix {
  LdMatrixDetails data;
  Op dst, src;
};
struct InstrGridDepControl {
  GridDepControlAction data;
};
template <typename Op>
struct InstrMma {
  MmaDetails data;
  Op dst, src1, src2, src3;
};
template <typename Op>
struct InstrCopysign {
  ScalarType data;
  Op dst, src1, src2;
};
template <typename Op>
struct InstrPrefetch {
  PrefetchData data;
  Op src;
};
template <typename Op>
struct InstrSad {
  ScalarType data;
  Op dst, src1, src2, src3;
};
template <typename Op>
struct InstrDp2a {
  Dp2aData data;
  Op dst, src1, src2, src3;
};

// Instruction variant — Op = ParsedOp for the parsed stage
template <OperandLike Op>
using Instruction = std::variant<
    InstrAbs<Op>, InstrActivemask<Op>, InstrAdd<Op>, InstrAddExtended<Op>,
    InstrSubExtended<Op>, InstrMadExtended<Op>, InstrAnd<Op>, InstrAtom<Op>,
    InstrAtomCas<Op>, InstrBarWarp<Op>, InstrBar<Op>, InstrBarRed<Op>,
    InstrBfe<Op>, InstrBfi<Op>, InstrBmsk<Op>,
    InstrBra<typename Op::id_type>,  // <-- Ident only
    InstrBrev<Op>,
    InstrCall<Op>,  // NOTE: CallArgs needs Id; adapt as needed
    InstrClz<Op>, InstrCos<Op>, InstrCpAsync<Op>, InstrCpAsyncCommitGroup,
    InstrCpAsyncWaitGroup<Op>, InstrCpAsyncWaitAll,
    InstrCreatePolicyFractional<Op>, InstrCvt<Op>, InstrCvtPack<Op>,
    InstrCvta<Op>, InstrDiv<Op>, InstrDp4a<Op>, InstrEx2<Op>, InstrFma<Op>,
    InstrLd<Op>, InstrLg2<Op>, InstrMad<Op>, InstrMax<Op>, InstrMembar,
    InstrMin<Op>, InstrMov<Op>, InstrMul<Op>, InstrMul24<Op>,
    InstrNanosleep<Op>, InstrNeg<Op>, InstrNot<Op>, InstrOr<Op>, InstrPopc<Op>,
    InstrPrmt<Op>, InstrRcp<Op>, InstrRem<Op>, InstrRet, InstrRsqrt<Op>,
    InstrSelp<Op>, InstrSet<Op>, InstrSetBool<Op>, InstrSetp<Op>,
    InstrSetpBool<Op>, InstrShflSync<Op>, InstrShf<Op>, InstrShl<Op>,
    InstrShr<Op>, InstrSin<Op>, InstrSqrt<Op>, InstrSt<Op>, InstrSub<Op>,
    InstrTrap, InstrXor<Op>, InstrTanh<Op>, InstrVote<Op>, InstrReduxSync<Op>,
    InstrLdMatrix<Op>, InstrGridDepControl, InstrMma<Op>, InstrCopysign<Op>,
    InstrPrefetch<Op>, InstrSad<Op>, InstrDp2a<Op>>;

// ============================================================
// § 10  Variable / MultiVariable  ←  ast.rs
// ============================================================

struct VariableInfo {
  std::optional<uint32_t> align;
  Type v_type;
  StateSpace state_space;
  std::vector<RegOrImmediate<Ident>> array_init;
};

struct Variable {
  VariableInfo info;
  Ident name;
};

// MultiVariable: either a parameterized range (%r<N>) or an explicit list
struct MultiVariableParameterized {
  VariableInfo info;
  Ident name;
  uint32_t count;
};

struct MultiVariableNames {
  VariableInfo info;
  std::vector<Ident> names;
};

using MultiVariable =
    std::variant<MultiVariableParameterized, MultiVariableNames>;

// ============================================================
// § 11  Statement<Op>  ←  ast.rs::Statement
// ============================================================

template <typename Op>
struct Statement {
  using IdType = typename Op::id_type;  // for IdentLike constraint on Label

  struct LabelS {
    IdType label;
  };

  struct VariableS {
    MultiVariable var;
  };

  struct InstructionS {
    std::optional<PredAt> pred;
    Instruction<Op> instr;
  };

  struct BlockS {
    std::vector<Statement<Op>> stmts;
  };

  using Inner = std::variant<LabelS, VariableS, InstructionS, BlockS>;
  Inner inner;

  static Statement Label(IdType id) { return {LabelS{id}}; }
  static Statement Var(MultiVariable v) { return {VariableS{v}}; }
  static Statement Instr(std::optional<PredAt> p, Instruction<Op> i) {
    return {InstructionS{p, std::move(i)}};
  }
  static Statement Block(std::vector<Statement<Op>> s) {
    return {BlockS{std::move(s)}};
  }
};

// ============================================================
// § 12  TuningDirective / MethodDeclaration / Function / Directive / Module
//        ←  ast.rs
// ============================================================

// .pragma maxnreg etc.
struct TuningMaxNReg {
  uint32_t n;
};
struct TuningMaxNtid {
  uint32_t x, y, z;
};
struct TuningReqNtid {
  uint32_t x, y, z;
};
struct TuningMinNCtaPerSm {
  uint32_t n;
};
struct TuningNoReturn {};
using TuningDirective =
    std::variant<TuningMaxNReg, TuningMaxNtid, TuningReqNtid,
                 TuningMinNCtaPerSm, TuningNoReturn>;

// .entry vs .func
enum class MethodKind { Kernel, Func };

struct MethodDeclaration {
  std::vector<Variable> return_arguments;
  MethodKind kind;        // Kernel=.entry, Func=.func
  std::string_view name;  // raw source slice
  std::vector<Variable> input_arguments;
  std::optional<Ident> shared_mem;
};

template <typename Op>
struct Function {
  MethodDeclaration func_directive;
  std::vector<TuningDirective> tuning;
  std::optional<std::vector<Statement<Op>>> body;  // nullopt = declaration only
};

// Linking visibility
enum class LinkingDirective : uint8_t {
  None = 0,
  Extern = 1 << 0,
  Visible = 1 << 1,
  Weak = 1 << 2,
};

inline LinkingDirective operator|(LinkingDirective a, LinkingDirective b) {
  return static_cast<LinkingDirective>(static_cast<uint8_t>(a) |
                                       static_cast<uint8_t>(b));
}

inline bool has_flag(LinkingDirective d, LinkingDirective f) {
  return (static_cast<uint8_t>(d) & static_cast<uint8_t>(f)) != 0;
}

template <typename Op, typename Id = Ident>
struct Directive {
  struct VariableD {
    LinkingDirective linking;
    Variable var;
  };
  struct MethodD {
    LinkingDirective linking;
    Function<Op> func;
  };
  using Inner = std::variant<VariableD, MethodD>;
  Inner inner;
};

// ============================================================
// § 13  Module  ←  ast.rs::Module
// ============================================================

struct Module {
  std::pair<uint8_t, uint8_t> ptx_version;  // e.g. {8, 5}
  uint32_t sm_version;                      // e.g. 900 for sm_90
  std::vector<Directive<ParsedOp>> directives;
  size_t invalid_directives = 0;

  static Module empty() {
    return {{1, 0}, 0, {}, std::numeric_limits<size_t>::max()};
  }
};

};  // namespace ptx_frontend
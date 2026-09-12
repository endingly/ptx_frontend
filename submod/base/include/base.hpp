#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace ptx_frontend::base {

// Each row keeps the modeled scalar's public enum order, PTX spelling, and
// declaration contract together.  Expand it only inside this header so clients
// do not depend on a preprocessor macro.
#define PTX_FRONTEND_FOR_EACH_SCALAR_TYPE(X)       \
  X(U8, ".u8", Unsigned, 1, Fundamental)           \
  X(U8x4, ".u8x4", Unsigned, 4, InstructionOnly)   \
  X(U16, ".u16", Unsigned, 2, Fundamental)         \
  X(U16x2, ".u16x2", Unsigned, 4, InstructionOnly) \
  X(U32, ".u32", Unsigned, 4, Fundamental)         \
  X(U64, ".u64", Unsigned, 8, Fundamental)         \
  X(S8, ".s8", Signed, 1, Fundamental)             \
  X(S8x4, ".s8x4", Signed, 4, InstructionOnly)     \
  X(S16, ".s16", Signed, 2, Fundamental)           \
  X(S16x2, ".s16x2", Signed, 4, InstructionOnly)   \
  X(S32, ".s32", Signed, 4, Fundamental)           \
  X(S64, ".s64", Signed, 8, Fundamental)           \
  X(B8, ".b8", Bit, 1, Fundamental)                \
  X(B16, ".b16", Bit, 2, Fundamental)              \
  X(B32, ".b32", Bit, 4, Fundamental)              \
  X(B64, ".b64", Bit, 8, Fundamental)              \
  X(B128, ".b128", Bit, 16, Fundamental)           \
  X(F16, ".f16", Float, 2, Fundamental)            \
  X(F16x2, ".f16x2", Float, 4, Fundamental)        \
  X(F32, ".f32", Float, 4, Fundamental)            \
  X(F32x2, ".f32x2", Float, 8, InstructionOnly)    \
  X(F64, ".f64", Float, 8, Fundamental)            \
  X(BF16, ".bf16", Float, 2, InstructionOnly)      \
  X(BF16x2, ".bf16x2", Float, 4, InstructionOnly)  \
  X(E4m3x2, ".e4m3x2", Float, 2, InstructionOnly)  \
  X(E5m2x2, ".e5m2x2", Float, 2, InstructionOnly)  \
  X(Pred, ".pred", Pred, 1, Fundamental)           \
  X(TF32, ".tf32", Float, 4, InstructionOnly)      \
  X(E4m3, ".e4m3", Float, 1, InstructionOnly)      \
  X(E5m2, ".e5m2", Float, 1, InstructionOnly)

/** Modeled PTX scalar identities in their stable public numeric order. */
enum class ScalarType : uint8_t {
  Invalid = 0,
#define PTX_FRONTEND_SCALAR_TYPE_ENUM(name, spelling, kind, size, usage) name,
  PTX_FRONTEND_FOR_EACH_SCALAR_TYPE(PTX_FRONTEND_SCALAR_TYPE_ENUM)
#undef PTX_FRONTEND_SCALAR_TYPE_ENUM
};

enum class ScalarKind { Invalid, Bit, Unsigned, Signed, Float, Pred };

/** Whether a modeled scalar can appear as a `.reg` declaration type. */
enum class ScalarDeclarationUsage : uint8_t {
  Fundamental,
  InstructionOnly,
};

/** Canonical properties for one modeled PTX scalar type. */
struct ScalarTypeMetadata {
  /** Public scalar identity; Invalid deliberately has no metadata row. */
  ScalarType type;
  /** Exact dotted PTX source spelling used by parser-facing consumers. */
  std::string_view source_spelling;
  /** Fundamental kind used by register compatibility checks. */
  ScalarKind kind;
  /** Storage width in bytes; preserves scalar_size_of's established values. */
  uint8_t size_bytes;
  /** Whether the spelling is fundamental or instruction-only in `.reg`. */
  ScalarDeclarationUsage register_declaration_usage;
};

namespace detail {

inline constexpr std::array kScalarTypeMetadata = {
#define PTX_FRONTEND_SCALAR_TYPE_METADATA(name, spelling, kind, size, usage) \
  ScalarTypeMetadata{ScalarType::name, spelling, ScalarKind::kind, size,     \
                     ScalarDeclarationUsage::usage},
    PTX_FRONTEND_FOR_EACH_SCALAR_TYPE(PTX_FRONTEND_SCALAR_TYPE_METADATA)
#undef PTX_FRONTEND_SCALAR_TYPE_METADATA
};

}  // namespace detail

#undef PTX_FRONTEND_FOR_EACH_SCALAR_TYPE

/** Width relation accepted when checking a register against an instruction. */
enum class ScalarTypeSizePolicy : uint8_t {
  Exact,
  SameWidth,
  EqualOrWider,
};

/** Semantic value of a PTX floating-point rounding modifier. */
enum class RoundingMode : uint8_t {
  Invalid = 0,
  Rn,
  Rz,
  Rm,
  Rp,
  Rzi,
};

/** Semantic value of a PTX comparison operator modifier. */
enum class ComparisonOperator : uint8_t {
  Invalid = 0,
  Eq,
  Lt,
  Ge,
  Ne,
  Le,
  Gt,
  Lo,
  Ls,
  Hi,
  Hs,
  Equ,
  Neu,
  Ltu,
  Leu,
  Gtu,
  Geu,
  Num,
  Nan,
};

/** Semantic value of a PTX predicate-combine operator modifier. */
enum class BooleanOperator : uint8_t {
  Invalid = 0,
  And,
  Or,
  Xor,
};

/** Semantic value of a PTX ld/st cache operator modifier. */
enum class CacheOperator : uint8_t {
  Unspecified = 0,
  Ca,
  Cg,
  Cs,
  Lu,
  Cv,
  Wb,
  Wt,
};

/** Semantic value of the accepted PTX eviction-priority modifiers. */
enum class EvictionPriority : uint8_t {
  Invalid = 0,
  EvictNormal,
  EvictFirst,
  EvictLast,
  NoAllocate,
  EvictUnchanged,
};

/** Byte count requested by a PTX L2 prefetch qualifier. */
enum class PrefetchSize : uint8_t {
  None = 0,
  Bytes64,
  Bytes128,
  Bytes256,
};

/** Source-level memory-consistency qualifier for ld/st.  Omitted is kept
 * distinct from explicit .weak so target availability and source provenance
 * remain observable in Resolved IR. */
enum class MemoryConsistency : uint8_t {
  Omitted = 0,
  Weak,
  Volatile,
  Relaxed,
  Acquire,
  Release,
  AcqRel,
};

/** Scope carried by memory-consistency operations; None represents omission. */
enum class MemoryScope : uint8_t {
  None = 0,
  Cta,
  Cluster,
  Gpu,
  Sys,
};

/** Semantic value of a PTX mbarrier .phase_type qualifier. */
enum class MbarrierPhaseType : uint8_t {
  Primary,
  Conditional,
};

/** Semantic value of a PTX mbarrier .layout qualifier. */
enum class MbarrierLayout : uint8_t {
  V0,
  V1,
};

/** Proxy selected by the bi-directional fence.proxy.async form. */
enum class AsyncProxyKind : uint8_t {
  Async,
  AsyncGlobal,
  AsyncSharedCta,
  AsyncSharedCluster,
};

/** Ordered to::from identity selected by a uni-directional proxy fence. */
enum class ProxyKindPair : uint8_t {
  TensormapToGeneric,
  AsyncToGeneric,
};

template <typename Enum>
  requires std::is_enum_v<Enum>
std::string to_string(Enum e) {
  return std::string{magic_enum::enum_name(e)};
}

/** Return metadata for every modeled scalar in public ScalarType order. */
std::span<const ScalarTypeMetadata> scalar_type_metadata() noexcept;

/** Find immutable program-lifetime metadata by identity, excluding Invalid. */
const ScalarTypeMetadata* find_scalar_type_metadata(ScalarType type) noexcept;

/** Find immutable program-lifetime metadata by an exact dotted PTX spelling. */
const ScalarTypeMetadata* find_scalar_type_metadata(
    std::string_view source_spelling) noexcept;

/** Return the fundamental kind of a scalar, or Invalid for unmodeled values. */
ScalarKind scalar_kind(ScalarType t);

/** Return a scalar's storage width in bytes, or zero for unmodeled values. */
uint8_t scalar_size_of(ScalarType t);

/** PTX fundamental-type compatibility under an explicit register-size policy. */
bool scalar_types_compatible(
    ScalarType actual, ScalarType instruction,
    ScalarTypeSizePolicy size_policy = ScalarTypeSizePolicy::SameWidth);

};  // namespace ptx_frontend::base

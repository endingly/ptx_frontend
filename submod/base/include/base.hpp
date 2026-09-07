#pragma once
#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <string>
#include <type_traits>

namespace ptx_frontend::base {

enum class ScalarType : uint8_t {
  Invalid = 0,
  U8,
  U8x4,
  U16,
  U16x2,
  U32,
  U64,
  S8,
  S8x4,
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
  F32x2,
  F64,
  BF16,
  BF16x2,
  E4m3x2,
  E5m2x2,
  Pred,
  TF32,  // .tf32  — 19-bit mantissa, sm_80+ tensor core
  E4m3,  // .e4m3  — FP8 single element (non packed)
  E5m2,  // .e5m2  — FP8 single element (non packed)
};

enum class ScalarKind { Invalid, Bit, Unsigned, Signed, Float, Pred };

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

ScalarKind scalar_kind(ScalarType t);
uint8_t scalar_size_of(ScalarType t);

/** PTX fundamental-type compatibility under an explicit register-size policy. */
bool scalar_types_compatible(
    ScalarType actual, ScalarType instruction,
    ScalarTypeSizePolicy size_policy = ScalarTypeSizePolicy::SameWidth);

};  // namespace ptx_frontend

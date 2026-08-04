#pragma once
#include <expected>
#include <ptx_ir/base.hpp>
#include "ptx_ir/ptx_syntax_ast.hpp"

namespace ptx_frontend::resolved_ir {

struct ResolveDiagnostic {
  SourceRange range;
  std::string message;
};

struct ResolvedRegisterId {
  uint32_t value;
  bool operator==(const ResolvedRegisterId&) const = default;
};

struct ResolvedImmediate {
  uint64_t bits;
  ScalarType type;
  bool operator==(const ResolvedImmediate&) const = default;
};

using RegOrImm = std::variant<ResolvedRegisterId, ResolvedImmediate>;

struct Add {
  enum class VariantType {
    IntegerNoSat,
    SatS32,
    SimdNoSatSm90,
    PackedOptionalSatSm120,
    SatSm120,
  };

  struct IntegerNoSat {
    enum class Type {
      U16,
      U32,
      U64,
      S16,
      S32,
      S64,
    };
    // no saturation for this variant
    WithLocs<Type> type;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_sat_s32
  struct SatS32 {
    // saturation is always true for this variant
    // type is always S32 for this variant
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_simd_no_sat_sm90
  struct SimdNoSatSm90 {
    enum class Type {
      U16x2,
      S16x2,
    };
    // no saturation for this variant
    WithLocs<Type> type;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_packed_optional_sat_sm120
  struct PackedOptionalSatSm120 {
    enum class Type {
      U8x4,
      S8x4,
    };

    WithLocs<Type> type;
    WithLocs<bool> saturate;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  // YAML: add_sat_sm120
  struct SatSm120 {
    enum class Type {
      U16x2,
      S16x2,
      U32,
    };
    // saturation is always true for this variant
    WithLocs<Type> type;
    WithLocs<ResolvedRegisterId> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  using Variant = std::variant<IntegerNoSat, SatS32, SimdNoSatSm90,
                               PackedOptionalSatSm120, SatSm120>;
  Variant variant;

  static std::expected<Add, ResolveDiagnostic> resolve(
      const syntax_ast::AstInstruction& ast);
  static std::expected<VariantType, ResolveDiagnostic> selectVariant(
      const syntax_ast::AstInstruction& ast);
};

}  // namespace ptx_frontend::resolved_ir
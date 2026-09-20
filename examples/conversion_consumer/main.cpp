#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** PTX source exercising public conversion, ownership, and validation APIs. */
constexpr std::string_view kFixture = R"ptx(
.version 9.3
.target sm_121a
.address_size 64
.shared .align 8 .u64 cvta_shared_value;
.visible .entry conversion_consumer() {
  .reg .pred %p<2>;
  .reg .u32 %r<2>;
  .reg .u32 %u0;
  .reg .u64 %rd0;
  .reg .s32 %s<3>;
  .reg .b32 %b<5>;
  .reg .f32 %f<4>;
  .reg .b16 %h<2>;

  isspacep.shared::cluster %p0, %r0;
  cvta.shared::cluster.u64 %rd0, cvta_shared_value+8;
  prmt.b32.rc16 %b0, %b1, %b2, %b3;
  prmt.b32.rc16 %b4, %b1, %b2, 0x1;
  cvt.pack.sat.u2.s32.b32 %u0, %s0, %s1, 0x12345678;
  cvt.rmi.s32.f32 %s2, %f0;
  cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.f32 %h0, %f2, %f3, %h1;
  ret;
}
)ptx";

/**
 * Resolve a module while parser state is alive, then validate the owned model
 * after the source, parser, and syntax AST have left scope.
 */
int runOwnedValidation() {
  /// Owns resolved values after parser-owned state has been destroyed.
  std::optional<ptx_frontend::resolved_ir::ResolvedModule> owned;
  {
    std::string source{kFixture};
    ptx_frontend::PtxSyntaxParser parser{source};
    auto parsed = parser.parseModule();
    if (!parsed) {
      std::cerr << "parseModule failed with " << parsed.diagnostics.size()
                << " diagnostic(s)\n";
      return 1;
    }

    auto resolved = ptx_frontend::resolved_ir::resolveModuleOnly(*parsed);
    if (!resolved) {
      std::cerr << "resolveModuleOnly failed with " << resolved.error().size()
                << " diagnostic(s)\n";
      for (const auto& diagnostic : resolved.error())
        std::cerr << diagnostic.message << '\n';
      return 2;
    }

    owned.emplace(std::move(*resolved));
  }

  assert(owned.has_value());
  const auto checked = ptx_frontend::resolved_ir::validateModule(
      *owned, ptx_frontend::resolved_ir::ModuleValidationPolicy::
                  RequireCompleteContext);
  if (!checked) {
    std::cerr << "owned validateModule failed with " << checked.error().size()
              << " diagnostic(s)\n";
    for (const auto& diagnostic : checked.error())
      std::cerr << diagnostic.message << '\n';
    return 3;
  }
  std::cout << "conversion consumer passed\n";
  return 0;
}

}  // namespace

/** Check the installed public conversion and resolved-IR contract. */
int main() {
  using ptx_frontend::base::RoundingMode;
  using ptx_frontend::base::ScalarType;

  static_assert(ScalarType::F16x2 != ScalarType::Invalid);
  static_assert(ScalarType::BF16x2 != ScalarType::Invalid);
  static_assert(ScalarType::U2 != ScalarType::Invalid);
  static_assert(ScalarType::S2 != ScalarType::Invalid);
  static_assert(ScalarType::U4 != ScalarType::Invalid);
  static_assert(ScalarType::S4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m1x2 != ScalarType::Invalid);
  static_assert(ScalarType::E2m3x2 != ScalarType::Invalid);
  static_assert(ScalarType::E3m2x2 != ScalarType::Invalid);
  static_assert(ScalarType::E4m3x4 != ScalarType::Invalid);
  static_assert(ScalarType::E5m2x4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m1x4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m3x4 != ScalarType::Invalid);
  static_assert(ScalarType::E3m2x4 != ScalarType::Invalid);
  static_assert(ScalarType::UE8M0x2 != ScalarType::Invalid);
  static_assert(ScalarType::S2f6x2 != ScalarType::Invalid);
  static_assert(RoundingMode::Rni != RoundingMode::Invalid);
  static_assert(RoundingMode::Rmi != RoundingMode::Invalid);
  static_assert(RoundingMode::Rpi != RoundingMode::Invalid);
  static_assert(RoundingMode::Rna != RoundingMode::Invalid);
  static_assert(RoundingMode::Rs != RoundingMode::Invalid);

  return runOwnedValidation();
}

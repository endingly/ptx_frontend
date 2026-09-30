#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Complete module whose scale selector includes both register and immediate IDs. */
constexpr std::string_view kMatrixSource = R"ptx(
.version 9.3
.target sm_120a
.address_size 64
.visible .entry matrix_consumer() {
  .reg .f32 %d<4>, %c<4>;
  .reg .b32 %a<4>, %b<2>, %sa, %sb;
  .reg .u16 %byte_id, %thread_id;
  mma.sync.aligned.m16n8k32.row.col.kind::mxf8f6f4.block_scale.scale_vec::1X.f32.e4m3.e4m3.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {%byte_id,%thread_id}, %sb, {2,3};
  ret;
}
)ptx";

/** Report a failed public matrix contract in Debug and Release builds. */
bool require(bool condition, std::string_view contract) {
  if (!condition)
    std::cerr << "matrix consumer: " << contract << '\n';
  return condition;
}

}  // namespace

/** Resolve an installed matrix form and inspect owned topology after AST release. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{kMatrixSource};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "source parses"))
      return 1;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "module resolves"))
      return 1;
    owned.emplace(std::move(*resolved));
  }
  if (!require(owned->functions.size() == 1 &&
                   owned->functions.front().body.size() >= 1,
               "owned function body is available"))
    return 1;
  auto* mma = std::get_if<ir::Mma>(&owned->functions.front().body.front());
  if (!require(mma != nullptr, "matrix instruction is typed Mma"))
    return 1;
  using Scaled = ir::Mma::SyncAlignedM16n8k32RowColKindMxf8f6f4BlockScaleScaleVec1F32E4m3E4m3F32Ue8m0;
  auto* selected = std::get_if<Scaled>(&mma->variant);
  if (!require(selected != nullptr, "scaled alternative is selected"))
    return 1;
  const ir::MatrixInstructionDescriptor& descriptor = selected->matrix.value;
  if (!require(descriptor.family == ir::MatrixFamily::MMA &&
                   descriptor.kind == ir::MatrixKind::MXF8F6F4 &&
                   descriptor.shape == ir::MatrixShape{16, 8, 32} &&
                   descriptor.scale_type == ir::MatrixScaleType::UE8M0 &&
                   descriptor.scale_vector_size == 1 &&
                   descriptor.scale_selector_count == 2 &&
                   descriptor.scale_selectors[0].byte_mask == 0b1111 &&
                   descriptor.scale_selectors[0].thread_max == 1,
               "owned public matrix descriptor is complete"))
    return 1;
  if (!require(std::holds_alternative<ir::ResolvedRegisterRef>(
                   selected->scale_a_selector.value.byte_id) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       selected->scale_a_selector.value.thread_id) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       selected->scale_b_selector.value.byte_id),
               "owned public selector tuple retains both value kinds"))
    return 1;
  const auto profile = ptx_frontend::base::find_target_profile("sm_120a");
  if (!require(profile.has_value(), "target profile exists"))
    return 1;
  const ir::checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
  };
  return require(ir::checker::check(*mma, context).has_value(),
                 "owned instruction validates")
             ? 0
             : 1;
}

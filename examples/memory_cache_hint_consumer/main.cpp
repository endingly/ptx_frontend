#include <optional>
#include <string>

#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ir = ptx_frontend::resolved_ir;

/** Return only owned IR, destroying source text, parser, and AST before use. */
std::optional<ir::ResolvedModule> resolve_owned() {
  const std::string source = R"ptx(
.version 7.4
.target sm_80
.address_size 64
.global .align 16 .u32 data[4];
.entry kernel() {
  .reg .u32 %r<4>;
  .reg .u64 %policy;
  ld.global.L2::cache_hint.u32 %r0, [data];
  ld.global.L2::cache_hint.u32 %r0, [data], %policy;
  ld.global.L2::cache_hint.v2.u32 {%r0,%r1}, [data];
  ld.global.L2::cache_hint.v2.u32 {%r0,%r1}, [data], %policy;
  ld.global.nc.L2::cache_hint.u32 %r0, [data];
  ld.global.nc.L2::cache_hint.u32 %r0, [data], %policy;
  ld.global.nc.L2::cache_hint.v2.u32 {%r0,%r1}, [data];
  ld.global.nc.L2::cache_hint.v2.u32 {%r0,%r1}, [data], %policy;
  st.global.L2::cache_hint.u32 [data], %r0;
  st.global.L2::cache_hint.u32 [data], %r0, %policy;
  st.global.L2::cache_hint.v2.u32 [data], {%r0,%r1};
  st.global.L2::cache_hint.v2.u32 [data], {%r0,%r1}, %policy;
}
)ptx";
  ptx_frontend::PtxSyntaxParser parser{source};
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = ir::resolveAndValidateModule(*ast);
  if (!module)
    return std::nullopt;
  return std::move(*module);
}

/** Exercise installed stable form identities and optional policy storage. */
int main() {
  auto module = resolve_owned();
  if (!module || module->functions.size() != 1 ||
      module->functions.front().body.size() != 12 ||
      !ir::validateModule(*module,
                          ir::ModuleValidationPolicy::RequireCompleteContext))
    return 1;
  auto& body = module->functions.front().body;
  for (std::size_t index = 0; index < body.size(); index += 2)
    if (body[index]->instruction_kind() != body[index + 1]->instruction_kind())
      return 2;
  const auto& ld = dynamic_cast<const ir::LdGlobalU32L2CacheHint&>(*body[0]);
  auto& ld_policy = dynamic_cast<ir::LdGlobalU32L2CacheHint&>(*body[1]);
  const auto& ld_vector =
      dynamic_cast<const ir::LdGlobalL2CacheHintVector&>(*body[2]);
  const auto& ld_vector_policy =
      dynamic_cast<const ir::LdGlobalL2CacheHintVector&>(*body[3]);
  const auto& nc = dynamic_cast<const ir::LdGlobalNcCacheHintScalar&>(*body[4]);
  const auto& nc_policy =
      dynamic_cast<const ir::LdGlobalNcCacheHintScalar&>(*body[5]);
  const auto& nc_vector =
      dynamic_cast<const ir::LdGlobalNcCacheHintVector&>(*body[6]);
  const auto& nc_vector_policy =
      dynamic_cast<const ir::LdGlobalNcCacheHintVector&>(*body[7]);
  const auto& st = dynamic_cast<const ir::StGlobalU32L2CacheHint&>(*body[8]);
  const auto& st_policy =
      dynamic_cast<const ir::StGlobalU32L2CacheHint&>(*body[9]);
  const auto& st_vector =
      dynamic_cast<const ir::StGlobalL2CacheHintVector&>(*body[10]);
  const auto& st_vector_policy =
      dynamic_cast<const ir::StGlobalL2CacheHintVector&>(*body[11]);
  if (!ld.cache_hint || ld.cache_policy || ld_vector.cache_policy ||
      nc.cache_policy || nc_vector.cache_policy || st.cache_policy ||
      st_vector.cache_policy || !ld_policy.cache_policy ||
      !ld_vector_policy.cache_policy || !nc_policy.cache_policy ||
      !nc_vector_policy.cache_policy || !st_policy.cache_policy ||
      !st_vector_policy.cache_policy || ld.operand_layout.value != 1 ||
      ld_policy.operand_layout.value != 0 ||
      ld_policy.cache_policy->value.declared_type != ir::ScalarType::U64 ||
      ld_policy.cache_policy->locs.empty())
    return 3;
  ld_policy.cache_policy.reset();
  if (ir::validateModule(*module,
                         ir::ModuleValidationPolicy::RequireCompleteContext))
    return 4;
  return 0;
}

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ldu.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Supply legal declarations shared by complete data-movement chains. */
std::string integration_module(std::string_view operations,
                               std::string_view target = "sm_90",
                               std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) +
         "\n.address_size 64\n.global .align 32 .v4 .f32 A[64];\n"
         ".global .align 32 .f64 B[64];\n"
         ".entry kernel() {\n.reg .v4 .f32 F, G;\n"
         ".reg .v2 .u32 V, W, %T<2>; .reg .v4 .u32 Q;\n"
         ".reg .v2 .f64 D, E; .reg .u32 idx, %r<8>;\n"
         ".reg .u64 %rd, %wide<4>; .reg .b128 %bits;\n" +
         std::string(operations) + "\nret; }";
}

/** Return only owned IR; local source text and the entire AST die on return. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_chain(
    std::string_view operations, std::string_view target = "sm_90",
    std::string_view version = "9.3") {
  const auto source = integration_module(operations, target, version);
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{
        ResolveDiagnostic{{}, "Integration fixture did not parse."}});
  return resolveAndValidateModule(*ast);
}

/** Build a real standalone checker context for one owned instruction. */
checker::Context chain_context(const ResolvedModule& module, size_t index) {
  return {
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = module.functions.front().instruction_ranges[index]};
}

/** Check both strict owned validation and exact per-instruction ownership. */
void expect_owned_chain(const ResolvedModule& module) {
  EXPECT_TRUE(validateModule(module));
  for (size_t index = 0; index < module.functions.front().body.size(); ++index)
    EXPECT_TRUE(module.functions.front().body[index]->check(
        chain_context(module, index)));
}

/** Confirm named lanes rejoin the actual declaration without invented selectors. */
void expect_named_vector(const ResolvedModule& module,
                         const WithLocs<ResolvedRegisterVector>& value,
                         ScalarType type, uint8_t width) {
  ASSERT_EQ(value.value.source.kind, ResolvedVectorSourceKind::NamedVector);
  ASSERT_TRUE(value.value.source.whole_base);
  const auto& base = value.value.source.whole_base->value.register_ref;
  ASSERT_TRUE(base.symbol_id);
  const auto& declaration = module.symbols.symbol(*base.symbol_id);
  EXPECT_EQ(base.spelling,
            declaration.name + (base.parameterized_index
                                    ? std::to_string(*base.parameterized_index)
                                    : std::string{}));
  EXPECT_EQ(declaration.vector_width, width);
  EXPECT_EQ(base.declared_type, type);
  EXPECT_EQ(base.vector_width, width);
  EXPECT_FALSE(base.component);
  ASSERT_EQ(value.value.elements.size(), width);
  for (size_t lane = 0; lane < width; ++lane) {
    ASSERT_TRUE(value.value.elements[lane]);
    const auto& reg = *value.value.elements[lane];
    ASSERT_TRUE(reg.component);
    EXPECT_EQ(reg.symbol_id, base.symbol_id);
    EXPECT_EQ(reg.parameterized_index, base.parameterized_index);
    EXPECT_EQ(reg.declared_type, type);
    EXPECT_FALSE(reg.vector_width);
    EXPECT_EQ(reg.component->lane, lane);
    EXPECT_EQ(reg.component->declaration_width, width);
    EXPECT_EQ(reg.component->origin, RegisterComponentOrigin::NamedProjection);
    EXPECT_FALSE(reg.component->selector);
    EXPECT_EQ(reg.component->base_range, value.value.source.range);
    EXPECT_EQ(reg.component->range, value.value.source.range);
    EXPECT_EQ(value.locs[lane], value.value.source.range);
  }
}

/** Require an owned rejection to be located inside the instruction being mutated. */
void expect_located_rejection(const ResolvedModule& module, size_t index) {
  const auto result = validateModule(module);
  ASSERT_FALSE(result);
  ASSERT_FALSE(result.error().empty());
  const auto owner = module.functions.front().instruction_ranges[index];
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
    return diagnostic.range != SourceRange{} &&
           diagnostic.range.start.line >= owner.start.line &&
           diagnostic.range.end.line <= owner.end.line;
  })) << result.error().front().message;
}

/** Load, scalar selection, named/brace MOV and store keep one real lane identity. */
TEST(VectorIntegration, OwnsF32LoadComponentMovStoreChain) {
  auto owned = owned_chain(
      "ld.global.v4.f32 F, [A];\n"
      "add.f32 F.w, F.a, F.x;\n"
      "mov.v4.f32 G, F;\n"
      "mov.v4.f32 {F.x,F.y,F.z,F.w}, {G.r,G.g,G.b,G.a};\n"
      "st.global.v4.f32 [A+16], F;");
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto copied = *owned;
  auto moved = std::move(copied);
  expect_owned_chain(moved);
  const auto& body = moved.functions.front().body;
  const auto& load = dynamic_cast<const LdExplicitVector&>(*body[0]);
  const auto& add = dynamic_cast<const AddFloatF32&>(*body[1]);
  const auto& named_mov = dynamic_cast<const MovV4U32&>(*body[2]);
  const auto& brace_mov = dynamic_cast<const MovV4U32&>(*body[3]);
  const auto& store = dynamic_cast<const StExplicitVector&>(*body[4]);
  expect_named_vector(moved, load.dst, ScalarType::F32, 4);
  expect_named_vector(moved, named_mov.dst, ScalarType::F32, 4);
  expect_named_vector(moved, store.src, ScalarType::F32, 4);
  EXPECT_TRUE(
      same_register_storage(*load.dst.value.elements[3], add.dst.value));
  EXPECT_TRUE(same_register_storage(
      add.dst.value, std::get<ResolvedRegisterRef>(add.src1.value)));
  EXPECT_EQ(add.dst.value.component->origin,
            RegisterComponentOrigin::ExplicitSelector);
  const auto& named_src = std::get<ResolvedMovValueVector>(named_mov.src.value);
  EXPECT_EQ(named_src.source.kind, ResolvedVectorSourceKind::NamedVector);
  EXPECT_EQ(brace_mov.dst.value.source.kind,
            ResolvedVectorSourceKind::BraceList);
  const auto& brace_src = std::get<ResolvedMovValueVector>(brace_mov.src.value);
  EXPECT_EQ(brace_src.source.kind, ResolvedVectorSourceKind::BraceList);
  for (size_t lane = 0; lane < 4; ++lane) {
    EXPECT_TRUE(same_register_storage(
        *load.dst.value.elements[lane],
        std::get<ResolvedRegisterRef>(named_src.elements[lane])));
    EXPECT_TRUE(same_register_storage(
        *named_mov.dst.value.elements[lane],
        std::get<ResolvedRegisterRef>(brace_src.elements[lane])));
    EXPECT_TRUE(same_register_storage(*brace_mov.dst.value.elements[lane],
                                      *store.src.value.elements[lane]));
  }
  EXPECT_TRUE(validateModule(*owned));
}

/** Folded values, aliases, repeated sources and overlap coexist in one owned chain. */
TEST(VectorIntegration, OwnsExpressionsAliasesAndLegalOverlap) {
  auto owned = owned_chain(
      "mov.v2.u32 %T1, {(1+2)*3,WARP_SZ};\n"
      "mov.v2.u32 {V.x,V.g}, {%T1.r,%T1.y};\n"
      "mov.v2.u32 {V.r,V.y}, {V.x,V.x};\n"
      "st.global.v2.u32 [A], V;");
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto copied = *owned;
  auto moved = std::move(copied);
  expect_owned_chain(moved);
  const auto& body = moved.functions.front().body;
  const auto& expression = dynamic_cast<const MovV4U32&>(*body[0]);
  const auto& values = std::get<ResolvedMovValueVector>(expression.src.value);
  EXPECT_EQ(std::get<ResolvedImmediate>(values.elements[0]).bits, 9u);
  EXPECT_EQ(std::get<ResolvedImmediate>(values.elements[1]).bits, 32u);
  expect_named_vector(moved, expression.dst, ScalarType::U32, 2);
  EXPECT_EQ(expression.dst.value.source.whole_base->value.register_ref
                .parameterized_index,
            1u);
  const auto& alias = dynamic_cast<const MovV4U32&>(*body[1]);
  const auto& alias_src = std::get<ResolvedMovValueVector>(alias.src.value);
  EXPECT_TRUE(same_register_storage(
      *expression.dst.value.elements[0],
      std::get<ResolvedRegisterRef>(alias_src.elements[0])));
  const auto& overlap = dynamic_cast<const MovV4U32&>(*body[2]);
  const auto& repeat = std::get<ResolvedMovValueVector>(overlap.src.value);
  EXPECT_TRUE(
      same_register_storage(std::get<ResolvedRegisterRef>(repeat.elements[0]),
                            std::get<ResolvedRegisterRef>(repeat.elements[1])));
  EXPECT_TRUE(
      same_register_storage(*overlap.dst.value.elements[0],
                            std::get<ResolvedRegisterRef>(repeat.elements[0])));
  auto bad = owned_chain("mov.v2.u32 {V.x,V.r}, W;");
  ASSERT_FALSE(bad);
  EXPECT_NE(bad.error().front().range, SourceRange{});
}

/** Actual named f64 NC and both LDU forms pass through MOV and store after release. */
TEST(VectorIntegration, OwnsNamedF64NcAndLduChains) {
  for (const auto load_source :
       {"ld.global.nc.v2.f64 D, [B];", "ldu.v2.f64 D, [B];",
        "ldu.global.v2.f64 D, [B];"}) {
    SCOPED_TRACE(load_source);
    auto owned = owned_chain(std::string(load_source) +
                             "\nmov.v2.f64 E, D;\nst.global.v2.f64 [B+16], E;");
    ASSERT_TRUE(owned) << owned.error().front().message;
    auto copied = *owned;
    auto moved = std::move(copied);
    expect_owned_chain(moved);
    const auto& body = moved.functions.front().body;
    const WithLocs<ResolvedRegisterVector>* loaded = nullptr;
    if (const auto* nc = dynamic_cast<const LdGlobalNcVector*>(body[0].get())) {
      EXPECT_EQ(nc->type.value, ScalarType::F64);
      loaded = &nc->dst;
    } else if (const auto* generic =
                   dynamic_cast<const LduGenericV2*>(body[0].get())) {
      EXPECT_EQ(generic->type.value, ScalarType::F64);
      loaded = &generic->dst;
    } else if (const auto* explicit_ldu =
                   dynamic_cast<const LduExplicitV2*>(body[0].get())) {
      EXPECT_EQ(explicit_ldu->type.value, ScalarType::F64);
      loaded = &explicit_ldu->dst;
    }
    ASSERT_NE(loaded, nullptr);
    expect_named_vector(moved, *loaded, ScalarType::F64, 2);
    const auto& mov = dynamic_cast<const MovV4U32&>(*body[1]);
    const auto& source = std::get<ResolvedMovValueVector>(mov.src.value);
    const auto& store = dynamic_cast<const StExplicitVector&>(*body[2]);
    expect_named_vector(moved, mov.dst, ScalarType::F64, 2);
    expect_named_vector(moved, store.src, ScalarType::F64, 2);
    for (size_t lane = 0; lane < 2; ++lane) {
      EXPECT_TRUE(same_register_storage(
          *loaded->value.elements[lane],
          std::get<ResolvedRegisterRef>(source.elements[lane])));
      EXPECT_TRUE(same_register_storage(*mov.dst.value.elements[lane],
                                        *store.src.value.elements[lane]));
    }
  }
}

/** Generic LDU, explicit LDU, NC and MOV keep their distinct historical floors. */
TEST(VectorIntegration, PreservesF64AvailabilityPerSelectedForm) {
  constexpr std::string_view operations[] = {
      "ld.global.nc.v2.f64 D, [B];", "ldu.v2.f64 D, [B];",
      "ldu.global.v2.f64 D, [B];", "mov.v2.f64 E, D;"};
  constexpr uint32_t floors[] = {32, 20, 13, 13};
  for (size_t index = 0; index < std::size(operations); ++index) {
    const auto operation = operations[index];
    SCOPED_TRACE(operation);
    auto positive = owned_chain(operation);
    ASSERT_TRUE(positive) << positive.error().front().message;
    EXPECT_TRUE(validateModule(*positive));
    auto context = chain_context(*positive, 0);
    context.target.sm_version = floors[index];
    ASSERT_TRUE(positive->functions.front().body[0]->check(context));
    --context.target.sm_version;
    const auto negative = positive->functions.front().body[0]->check(context);
    ASSERT_FALSE(negative);
    EXPECT_NE(negative.error().front().range, SourceRange{});
  }
  for (size_t index = 0; index < 3; ++index) {
    const auto baseline = owned_chain(operations[index]);
    ASSERT_TRUE(baseline) << baseline.error().front().message;
    auto context = chain_context(*baseline, 0);
    if (index == 0)
      context.target.ptx_version = {3, 1};
    else
      context.target.ptx_version = {2, 0};
    ASSERT_TRUE(baseline->functions.front().body[0]->check(context));
    if (index == 0)
      context.target.ptx_version = {3, 0};
    else
      context.target.ptx_version = {1, 9};
    EXPECT_FALSE(baseline->functions.front().body[0]->check(context));
  }
}

/** Scalar array stride and total vector access alignment remain different units. */
TEST(VectorIntegration, OwnsArrayAddressVectorChainAndRejectsDynamicAlignment) {
  auto owned = owned_chain(
      "ld.global.v4.f32 F, A[2+2];\n"
      "mov.v4.f32 G, F;\nst.global.v4.f32 A[8], G;");
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto copied = *owned;
  auto moved = std::move(copied);
  expect_owned_chain(moved);
  const auto& load =
      dynamic_cast<const LdExplicitVector&>(*moved.functions.front().body[0]);
  ASSERT_TRUE(load.address.value.named_index);
  const auto& index = *load.address.value.named_index;
  EXPECT_EQ(index.scalar_stride, 4u);
  EXPECT_EQ(index.byte_displacement, 16);
  EXPECT_EQ(
      std::get<declaration_semantics::IntegerConstantValue>(index.index).bits,
      4u);
  EXPECT_EQ(resolved_address_alignment(load.address.value), 16u);
  const auto ast = test_helpers::parseModule(
      integration_module("ld.global.v4.f32 F, A[idx];\nmov.v4.f32 G, "
                         "F;\nst.global.v4.f32 A[8], G;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto dynamic = resolveModuleOnly(*ast);
  ASSERT_TRUE(dynamic) << dynamic.error().front().message;
  const auto& dynamic_load = dynamic_cast<const LdExplicitVector&>(
      *dynamic->functions.front().body[0]);
  EXPECT_EQ(resolved_address_alignment(dynamic_load.address.value), 4u);
  const auto rejected = validateModule(*dynamic);
  ASSERT_FALSE(rejected);
  EXPECT_TRUE(
      std::ranges::any_of(rejected.error(), [&](const auto& diagnostic) {
        return diagnostic.kind ==
                   checker::CheckDiagnosticKind::AddressAlignmentMismatch &&
               diagnostic.range == dynamic_load.address.locs.front();
      }));
}

/** Complete passing chains diagnose component, shape, source and owner cache forgery. */
TEST(VectorIntegration, LocatesOwnedTamperingInCompleteChains) {
  const auto baseline = owned_chain(
      "ld.global.v4.f32 F, [A];\n"
      "mov.v4.f32 G, F;\nst.global.v4.f32 [A+16], G;");
  ASSERT_TRUE(baseline) << baseline.error().front().message;
  expect_owned_chain(*baseline);
  for (int mutation = 0; mutation < 6; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = *baseline;
    auto& mov = dynamic_cast<MovV4U32&>(*changed.functions.front().body[1]);
    auto& source = std::get<ResolvedMovValueVector>(mov.src.value);
    auto& lane = std::get<ResolvedRegisterRef>(source.elements[0]);
    switch (mutation) {
      case 0:
        lane.component->lane = 3;
        break;
      case 1:
        lane.component->declaration_width = 2;
        break;
      case 2:
        source.source.kind = ResolvedVectorSourceKind::BraceList;
        break;
      case 3:
        lane.declared_type = ScalarType::U32;
        break;
      case 4:
        mov.vector.value = VectorArity::V2;
        break;
      case 5:
        mov.src.locs[0] = changed.functions.front().instruction_ranges[2];
        break;
    }
    EXPECT_FALSE(mov.check(chain_context(changed, 1)));
    expect_located_rejection(changed, 1);
  }
  auto foreign = chain_context(*baseline, 1);
  foreign.instruction_range = baseline->functions.front().instruction_ranges[2];
  const auto rejected = baseline->functions.front().body[1]->check(foreign);
  ASSERT_FALSE(rejected);
  EXPECT_NE(rejected.error().front().range, SourceRange{});
}

/** Adjacent families keep brace, hardware-component, packing and payload fences. */
TEST(VectorIntegration, PreservesFamilyAndPackingFences) {
  for (const auto& pair :
       {std::array<std::string_view, 2>{"mov.u32 %r0, Q.w;",
                                        "mov.u32 %r0, %tid.w;"},
        std::array<std::string_view, 2>{
            "atom.global.add.v2.f32 {F.x,F.y}, [A], {G.x,G.y};",
            "atom.global.add.v2.f32 F, [A], G;"},
        std::array<std::string_view, 2>{"red.global.add.v2.f32 [A], {F.x,F.y};",
                                        "red.global.add.v2.f32 [A], F;"},
        std::array<std::string_view, 2>{
            "mov.b64 %rd, {V.x,V.y}; mov.b64 {W.x,_}, %rd;", "mov.b64 %rd, V;"},
        std::array<std::string_view, 2>{
            "mov.b128 %bits, {%wide0,%wide1}; mov.b128 {%wide2,_}, %bits;",
            "mov.b128 %bits, %bits;"},
        std::array<std::string_view, 2>{"mov.v2.f32 {F.x,F.y}, {G.x,G.y};",
                                        "mov.f32 F.x, {V.x,V.y};"},
        std::array<std::string_view, 2>{
            "ld.v4.u64 {%wide0,%wide1,%wide2,%wide3}, [%rd];",
            "ld.v4.u64 D, [%rd];"},
        std::array<std::string_view, 2>{
            "ld.v8.u32 {%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7}, [%rd];",
            "mov.v8.u32 Q, Q;"}}) {
    SCOPED_TRACE(pair[0]);
    const auto positive = owned_chain(pair[0], "sm_100");
    ASSERT_TRUE(positive) << positive.error().front().message;
    EXPECT_TRUE(validateModule(*positive));
    const auto negative = owned_chain(pair[1], "sm_100");
    ASSERT_FALSE(negative);
    EXPECT_NE(negative.error().front().range, SourceRange{});
  }
  const auto packed = owned_chain("mov.b128 %bits, {%wide0,%wide1};");
  ASSERT_TRUE(packed) << packed.error().front().message;
  auto context = chain_context(*packed, 0);
  context.target.sm_version = 70;
  context.target.ptx_version = {8, 3};
  ASSERT_TRUE(packed->functions.front().body[0]->check(context));
  context.target.sm_version = 69;
  EXPECT_FALSE(packed->functions.front().body[0]->check(context));
  context.target.sm_version = 70;
  context.target.ptx_version = {8, 2};
  EXPECT_FALSE(packed->functions.front().body[0]->check(context));
  const auto modern =
      owned_chain("ld.v4.u64 {%wide0,%wide1,%wide2,%wide3}, [%rd];", "sm_100");
  ASSERT_TRUE(modern) << modern.error().front().message;
  context = chain_context(*modern, 0);
  context.target.sm_version = 100;
  context.target.ptx_version = {8, 8};
  ASSERT_TRUE(modern->functions.front().body[0]->check(context));
  context.target.sm_version = 99;
  EXPECT_FALSE(modern->functions.front().body[0]->check(context));
  context.target.sm_version = 100;
  context.target.ptx_version = {8, 7};
  EXPECT_FALSE(modern->functions.front().body[0]->check(context));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

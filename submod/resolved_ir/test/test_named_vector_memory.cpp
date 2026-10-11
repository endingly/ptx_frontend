#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Valid standalone target; omit an owner range only for source-fragment checks. */
const checker::Context kVectorContext{
    .target = {.ptx_version = {9, 3}, .sm_version = 100}};

/** Provide aligned storage and ordinary vector declarations for source tests. */
std::string vector_memory_module(std::string_view operations,
                                 std::string_view target = "sm_90") {
  return ".version 9.3\n.target " + std::string(target) +
         "\n.address_size 64\n.global .align 32 .u32 A[64];\n"
         ".shared .align 32 .u32 S[64];\n.shared .align 8 .b64 bar;\n"
         ".global .texref tex0;\n.entry kernel() {\n"
         ".reg .u64 %rd, %wide<4>; .reg .u32 %r<8>; .reg .pred %p;\n"
         ".reg .v2 .u32 V; .reg .v4 .u32 Q; .reg .v2 .u32 %T<2>;\n"
         ".reg .v4 .f32 F; .reg .v2 .f32 G; .reg .v2 .f64 D; .reg .v2 .u64 "
         "W;\n" +
         std::string(operations) + "\nret; }";
}

/** Borrow the first generic vector load for owned mutation tests. */
LdGenericVector& vector_load(ResolvedModule& module) {
  return dynamic_cast<LdGenericVector&>(*module.functions.front().body.front());
}

/** All opted families retain brace compatibility and memory register widening. */
TEST(NamedVectorMemory, ResolvesNamedAndBraceFamilies) {
  for (const auto operation : {"ld.v2.u32 V, [%rd];",
                               "st.v2.u32 [%rd], V;",
                               "ld.global.nc.v2.u32 V, [A];",
                               "ldu.v2.u32 V, [A];",
                               "ldu.global.v4.u32 Q, [A];",
                               "ld.v4.f32 F, [%rd];",
                               "st.v4.f32 [%rd], F;",
                               "ld.v2.f64 D, [%rd];",
                               "st.v2.f64 [%rd], D;",
                               "ld.v2.u16 V, [%rd];",
                               "st.v2.u16 [%rd], V;",
                               "ld.ca.v2.u32 %T1, [%rd];",
                               "ld.relaxed.cta.global.v2.u32 V, [A];",
                               "st.release.cta.global.v4.u32 [A], Q;",
                               "ld.shared.v2.u32 V, [S];",
                               "st.shared.v2.u32 [S], V;",
                               "ld.v2.u32 {V.x,V.y}, [%rd];",
                               "st.v2.u32 [%rd], {V.r,V.g};",
                               "ld.global.nc.v2.u32 {%r0,%r1}, [A];",
                               "ldu.v2.u32 {%r0,%r1}, [A];"}) {
    SCOPED_TRACE(operation);
    const auto ast = test_helpers::parseModule(vector_memory_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_TRUE(validateModule(*resolved));
  }
}

/** Implicit lanes retain real identity, declared shape and the written range. */
TEST(NamedVectorMemory, OwnsExactProjectionAfterAstDestructionAndCopies) {
  auto owned = [] {
    const auto ast = test_helpers::parseModule(
        vector_memory_module("ld.v2.u32 %T1, [%rd];"));
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto copied = *owned;
  auto moved = std::move(copied);
  auto& value = vector_load(moved).dst;
  ASSERT_EQ(value.value.source.kind, ResolvedVectorSourceKind::NamedVector);
  ASSERT_TRUE(value.value.source.whole_base);
  const auto& base = value.value.source.whole_base->value.register_ref;
  EXPECT_EQ(base.spelling, "%T1");
  EXPECT_EQ(base.parameterized_index, 1u);
  EXPECT_EQ(base.vector_width, 2);
  EXPECT_FALSE(base.component);
  ASSERT_EQ(value.value.elements.size(), 2u);
  for (size_t lane = 0; lane < 2; ++lane) {
    const auto& ref = *value.value.elements[lane];
    ASSERT_TRUE(ref.component);
    EXPECT_EQ(ref.symbol_id, base.symbol_id);
    EXPECT_EQ(ref.parameterized_index, base.parameterized_index);
    EXPECT_EQ(ref.declared_type, base.declared_type);
    EXPECT_FALSE(ref.vector_width);
    EXPECT_EQ(ref.component->lane, lane);
    EXPECT_EQ(ref.component->declaration_width, 2);
    EXPECT_EQ(ref.component->origin, RegisterComponentOrigin::NamedProjection);
    EXPECT_FALSE(ref.component->selector);
    EXPECT_EQ(ref.spelling, base.spelling);
    EXPECT_EQ(ref.component->base_range, value.value.source.range);
    EXPECT_EQ(ref.component->range, value.value.source.range);
    EXPECT_EQ(value.locs[lane], value.value.source.range);
    EXPECT_FALSE(valid_register_component(ref));
  }
  EXPECT_FALSE(same_register_storage(*value.value.elements[0],
                                     *value.value.elements[1]));
  EXPECT_TRUE(valid_register_vector_source(value.value, value.locs));
  EXPECT_TRUE(validateModule(moved));
}

/** Source form does not relax arity, type, binding, or modern-vector limits. */
TEST(NamedVectorMemory, RejectsBadNamedShapesAndExcludedFamilies) {
  for (const auto operation : {"ld.v2.u32 Q, [%rd];",
                               "st.v4.u32 [%rd], V;",
                               "ld.v2.u32 F, [%rd];",
                               "ld.v2.u32 G, [%rd];",
                               "ld.v4.u64 Q, [%rd];",
                               "ld.v2.u32 %p, [%rd];",
                               "ld.v2.u32 %r0, [%rd];",
                               "ld.v2.u32 _, [%rd];",
                               "ld.v2.u32 V.x, [%rd];",
                               "ld.v2.u32 %tid, [%rd];",
                               "ld.v2.u32 missing, [%rd];",
                               "ld.v2.u32 %T2, [%rd];",
                               "ld.v8.u32 Q, [%rd];",
                               "ld.v4.u64 W, [%rd];",
                               "st.v4.u64 [%rd], Q;",
                               "mov.b64 %rd, V;",
                               "atom.global.add.v2.u32 V, [A], V;",
                               "red.global.add.v2.u32 [A], V;",
                               "st.async.shared::cluster.mbarrier::complete_tx:"
                               ":bytes.v2.u32 [S], V, [S];",
                               "ld.global.nc.v2.u32 V, A[1];",
                               "ldu.v2.u32 V, A[1];"}) {
    SCOPED_TRACE(operation);
    const auto ast =
        test_helpers::parseModule(vector_memory_module(operation, "sm_100"));
    if (ast && ast.diagnostics.empty())
      EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  const auto modern = test_helpers::parseModule(vector_memory_module(
      "ld.v8.u32 {%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7}, [%rd]; "
      "ld.v4.u64 {%wide0,%wide1,%wide2,%wide3}, [%rd];",
      "sm_100"));
  ASSERT_MODULE_PARSE_SUCCEEDS(modern);
  EXPECT_TRUE(resolveAndValidateModule(*modern));
  PtxSyntaxParser parser{"ld.v2.u32 V, [%rd0];"};
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast);
  EXPECT_FALSE(resolveLd(*ast));
  const auto oversized = test_helpers::parseModule(vector_memory_module(
      ".reg .v4 .u64 oversized; ld.v4.u64 oversized, [%rd];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(oversized);
  const auto invalid_declaration = resolveAndValidateModule(*oversized);
  ASSERT_FALSE(invalid_declaration);
  EXPECT_NE(invalid_declaration.error().front().message.find("128 bits"),
            std::string::npos);
}

/** Container validation is required before projected lanes receive admission. */
TEST(NamedVectorMemory, RejectsOwnedAndStandaloneProjectionForgery) {
  const auto ast =
      test_helpers::parseModule(vector_memory_module("ld.v2.u32 V, [%rd];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto original = resolveAndValidateModule(*ast);
  ASSERT_TRUE(original) << original.error().front().message;
  ASSERT_TRUE(original->functions.front().body.front()->check(kVectorContext));
  for (int mutation = 0; mutation < 13; ++mutation) {
    auto changed = *original;
    auto& value = vector_load(changed).dst;
    auto& lane = *value.value.elements[0];
    auto& source = value.value.source;
    switch (mutation) {
      case 0:
        source.whole_base.reset();
        break;
      case 1:
        source.range = {};
        break;
      case 2:
        source.whole_base->locs.clear();
        break;
      case 3:
        source.kind = ResolvedVectorSourceKind::BraceList;
        break;
      case 4:
        std::swap(value.value.elements[0], value.value.elements[1]);
        break;
      case 5:
        lane.component->origin = RegisterComponentOrigin::ExplicitSelector;
        break;
      case 6:
        lane.component->selector = ResolvedRegisterSelector{".x", source.range};
        break;
      case 7:
        lane.component->declaration_width = 4;
        break;
      case 8:
        lane.parameterized_index = 1;
        break;
      case 9:
        lane.declared_type = ScalarType::F32;
        break;
      case 10:
        value.locs[0] = {};
        break;
      case 11:
        source.whole_base->value.register_ref.vector_width = 4;
        break;
      case 12:
        source.whole_base->value.register_ref.symbol_id.reset();
        break;
    }
    SCOPED_TRACE(mutation);
    EXPECT_FALSE(vector_load(changed).check(kVectorContext));
    EXPECT_FALSE(validateModule(changed));
  }
  auto changed = *original;
  auto& base =
      vector_load(changed).dst.value.source.whole_base->value.register_ref;
  base.symbol_id = binding::SymbolId{999999};
  for (auto& lane : vector_load(changed).dst.value.elements)
    lane->symbol_id = base.symbol_id;
  EXPECT_FALSE(validateModule(changed));
}

/** Legacy standalone brace payloads do not gain owned source provenance. */
TEST(NamedVectorMemory, PreservesLegacyBraceStandaloneButRequiresOwnedRange) {
  const auto ast = test_helpers::parseModule(
      vector_memory_module("ld.v2.u32 {%r0,%r1}, [%rd];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto original = resolveAndValidateModule(*ast);
  ASSERT_TRUE(original);
  auto changed = *original;
  auto& load = vector_load(changed);
  ASSERT_TRUE(load.check(kVectorContext));
  ASSERT_NE(load.dst.value.source.range, SourceRange{});
  load.dst.value.source.range = {};
  EXPECT_TRUE(load.check(kVectorContext));
  EXPECT_FALSE(validateModule(changed));
  load.dst.value.source.range = {{2, 4}, {2, 1}};
  EXPECT_FALSE(load.check(kVectorContext));
  load.dst.value.source = dynamic_cast<const LdGenericVector&>(
                              *original->functions.front().body.front())
                              .dst.value.source;
  load.dst.value.source.whole_base = WithLocs<ResolvedVectorRegisterRef>{};
  EXPECT_FALSE(load.check(kVectorContext));
}

/** Container admission never changes the default-off descriptor policy. */
TEST(NamedVectorMemory, RejectsNamedPayloadOnBraceOnlyDescriptor) {
  const auto ast =
      test_helpers::parseModule(vector_memory_module("ld.v2.u32 V, [%rd];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto resolved = resolveAndValidateModule(*ast);
  ASSERT_TRUE(resolved);
  const auto& value = vector_load(*resolved).dst;
  checker::OperandView view{.field_id = "data",
                            .actual_shape = checker::OperandShape::Vector,
                            .register_vector = &value.value,
                            .vector_arity = 2,
                            .locations = value.locs};
  for (size_t lane = 0; lane < 2; ++lane) {
    view.vector_element_shapes[lane] = checker::OperandShape::Register;
    view.vector_element_types[lane] = ScalarType::U32;
    view.vector_element_registers[lane] = &*value.value.elements[lane];
  }
  constexpr uint8_t arities[]{2};
  checker::OperandDescriptor descriptor{
      .target_field_id = "data",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::U32},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes = checker::OperandShape::Vector,
      .allowed_vector_arities = arities,
      .vector_type_policy = checker::VectorTypePolicy::Element};
  const auto check = [&] {
    return checker::check_operands(std::span{&descriptor, 1}, {},
                                   std::span{&view, 1}, {}, {});
  };
  EXPECT_FALSE(check());
  descriptor.allow_named_vector = true;
  EXPECT_TRUE(check());
  auto oversized = value.value;
  oversized.source.whole_base->value.register_ref.declared_type =
      ScalarType::B128;
  for (auto& lane : oversized.elements)
    lane->declared_type = ScalarType::B128;
  EXPECT_FALSE(valid_register_vector_source(oversized, value.locs));
  descriptor.vector_type_policy = checker::VectorTypePolicy::Aggregate;
  EXPECT_FALSE(check());
  descriptor.vector_type_policy = checker::VectorTypePolicy::Element;
  view.vector_element_registers[0] = view.vector_element_registers[1];
  EXPECT_FALSE(check());
}

/** Legal brace controls ensure family negatives fail on the named source form. */
TEST(NamedVectorMemory, KeepsUnrelatedFamiliesBraceOnly) {
  for (const auto& [braced, named] :
       {std::pair<std::string_view, std::string_view>{
            "atom.global.add.v2.f32 {G.x,G.y}, [A], {G.x,G.y};",
            "atom.global.add.v2.f32 {G.x,G.y}, [A], G;"},
        {"red.global.add.v2.f32 [A], {G.x,G.y};",
         "red.global.add.v2.f32 [A], G;"},
        {"ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0,%r1}, [S];",
         "ldmatrix.sync.aligned.m8n8.x2.shared.b16 V, [S];"},
        {"st.async.shared::cluster.mbarrier::complete_tx::bytes.v2.u32 "
         "[%rd], {V.x,V.y}, [bar];",
         "st.async.shared::cluster.mbarrier::complete_tx::bytes.v2.u32 "
         "[%rd], V, [bar];"},
        {"st.bulk [S], 64, 0;", "st.bulk [S], 64, V;"},
        {"tex.2d.v4.f32.f32 {F.x,F.y,F.z,F.w}, [tex0,{G.x,G.y}];",
         "tex.2d.v4.f32.f32 F, [tex0,{G.x,G.y}];"}}) {
    SCOPED_TRACE(braced);
    const auto positive =
        test_helpers::parseModule(vector_memory_module(braced, "sm_100"));
    ASSERT_MODULE_PARSE_SUCCEEDS(positive);
    const auto good = resolveAndValidateModule(*positive);
    ASSERT_TRUE(good) << good.error().front().message;
    const auto negative =
        test_helpers::parseModule(vector_memory_module(named, "sm_100"));
    ASSERT_MODULE_PARSE_SUCCEEDS(negative);
    EXPECT_FALSE(resolveAndValidateModule(*negative));
  }
}

/** Whole-name binding remains lexical and module provenance remains strict. */
TEST(NamedVectorMemory, PreservesShadowingAndRejectsForeignSourceOwner) {
  const auto ast = test_helpers::parseModule(vector_memory_module(
      "{ .reg .v2 .u16 V; ld.v2.u16 V, [%rd]; } ld.v2.u32 V, [%rd];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto resolved = resolveAndValidateModule(*ast);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  auto& first = vector_load(*resolved).dst;
  ASSERT_TRUE(vector_load(*resolved).check(kVectorContext));
  const auto& second =
      dynamic_cast<const LdGenericVector&>(*resolved->functions.front().body[1])
          .dst;
  EXPECT_NE(first.value.source.whole_base->value.register_ref.symbol_id,
            second.value.source.whole_base->value.register_ref.symbol_id);
  const SourceRange foreign{{1, 1}, {1, 2}};
  first.value.source.range = foreign;
  first.value.source.whole_base->locs = {foreign};
  for (size_t lane = 0; lane < 2; ++lane) {
    first.locs[lane] = foreign;
    first.value.elements[lane]->component->base_range = foreign;
    first.value.elements[lane]->component->range = foreign;
  }
  EXPECT_TRUE(vector_load(*resolved).check(kVectorContext));
  EXPECT_FALSE(validateModule(*resolved));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Supply real declarations and a recognized source context for vector MOV. */
std::string vector_mov_module(std::string_view operation,
                              std::string_view target = "sm_90") {
  return ".version 9.3\n.target " + std::string(target) +
         "\n.address_size 64\n.global .align 32 .u32 A[64];\n"
         ".entry kernel() {\n"
         ".reg .v2 .u32 V, W, %T<2>; .reg .v4 .u32 Q, R;\n"
         ".reg .v2 .u16 H, I; .reg .v4 .u16 J;\n"
         ".reg .v2 .u64 L, M;\n"
         ".reg .v2 .f64 D, E; .reg .v2 .f32 F, G; .reg .v4 .f32 X, Y;\n"
         ".reg .u32 %r<8>; .reg .u16 %h<4>; .reg .u64 %rd<4>;\n"
         ".reg .f32 %f<4>; .reg .pred %p; .reg .b128 %b;\n" +
         std::string(operation) + "\nret; }";
}

/** Borrow the one preserved explicit-vector MOV semantic identity. */
MovV4U32& vector_mov(ResolvedModule& module) {
  return dynamic_cast<MovV4U32&>(*module.functions.front().body.front());
}

/** Standalone mutations use the actual owner range and recognized target. */
checker::Context vector_mov_context(const ResolvedModule& module) {
  static constexpr std::array<std::string_view, 1> capabilities{"cluster"};
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 90,
                 .capabilities = capabilities},
      .instruction_range = module.functions.front().instruction_ranges.front()};
}

/** Named/brace forms, scalar special lanes, expressions, sinks and overlap resolve. */
TEST(VectorMov, ResolvesSupportedLayoutsAndSources) {
  for (const auto operation : {"mov.v2.u32 V, W;",
                               "mov.v2.u32 V, {%r0,%r1};",
                               "mov.v2.u32 {%r0,%r1}, V;",
                               "mov.v2.u32 {%r0,%r1}, {%r1,%r0};",
                               "mov.v2.u32 V, V;",
                               "mov.v2.u32 %T1, %T0;",
                               "mov.v2.u32 {V.x,V.g}, {W.r,W.y};",
                               "mov.v2.u32 {_,V.y}, {(1+2)*3,WARP_SZ};",
                               "mov.v2.u32 V, {%laneid,%tid.x};",
                               "mov.v2.u16 H, {%tid.y,%gridid};",
                               "mov.v2.s16 H, I;",
                               "mov.v4.b16 J, {%h0,%h1,%h2,%h3};",
                               "mov.v2.b32 V, F;",
                               "mov.v2.s32 V, W;",
                               "mov.v4.u32 Q, R;",
                               "mov.v4.s32 Q, %clusterid;",
                               "mov.v4.b32 {%r0,_,%r2,%r3}, %cluster_ctaid;",
                               "mov.v4.f32 X, Y;",
                               "mov.v2.f32 F, {1.0+2.0,0f3f800000};",
                               "mov.v2.u64 L, M;",
                               "mov.v2.s64 L, M;",
                               "mov.v2.b64 L, D;",
                               "mov.v2.f64 D, E;",
                               "mov.v2.f64 D, {1.0,2.0*3.0};",
                               "mov.v2.u64 L, {%clock64,%globaltimer};",
                               "mov.v2.u32 V, {%r0,%r0};"}) {
    SCOPED_TRACE(operation);
    const auto ast = test_helpers::parseModule(vector_mov_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    EXPECT_EQ(vector_mov(*module).instruction_kind(), MovV4U32::kind);
    EXPECT_TRUE(validateModule(*module));
  }
}

/** Every admitted element type exercises exact-width declaration binding. */
TEST(VectorMov, ResolvesEverySupportedElementCohort) {
  for (const auto type : {"b16", "u16", "s16", "b32", "u32", "s32", "f32",
                          "b64", "u64", "s64", "f64"}) {
    for (const auto arity : {2, 4}) {
      if (arity == 4 && std::string_view(type).ends_with("64"))
        continue;
      const auto vector_type = ".v" + std::to_string(arity) + " ." + type;
      const auto operation =
          "mov.v" + std::to_string(arity) + "." + type + " dst, src;";
      const auto ast = test_helpers::parseModule(
          ".version 9.3\n.target sm_90\n.entry kernel() { .reg " + vector_type +
          " dst, src; " + operation + " }");
      SCOPED_TRACE(operation);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      auto module = resolveAndValidateModule(*ast);
      ASSERT_TRUE(module) << module.error().front().message;
      EXPECT_TRUE(validateModule(*module));
    }
  }
}

/** Unsupported scalar broadcasts, widths and operand families remain fenced. */
TEST(VectorMov, RejectsUnsupportedDomainsAndDuplicates) {
  for (const auto operation :
       {"mov.v2.u32 V, 1;",
        "mov.v2.u32 V, %r0;",
        "mov.v2.u32 V, {_,%r0};",
        "mov.v2.u32 {_,_}, V;",
        "mov.v2.u32 {V.x,V.r}, W;",
        "mov.v2.u32 {%r0,%r0}, V;",
        "mov.v2.u32 V, Q;",
        "mov.v4.u32 Q, V;",
        "mov.v2.u32 V, {%r0};",
        "mov.v2.u32 V, {%p,%r0};",
        "mov.v2.u32 V, {W,%r0};",
        "mov.v2.u32 V, {A,%r0};",
        "mov.v2.u32 V, {kernel,%r0};",
        "mov.v2.u32 V, {[%rd0],%r0};",
        "mov.v2.u32 V, {%clusterid,%r0};",
        "mov.v2.u32 V, {%tid.w,%r0};",
        "mov.v2.u32 V, {%is_explicit_cluster,%r0};",
        "mov.v2.u32 V, {%clock64,%r0};",
        "mov.v2.u32 V, {%f0,%r0};",
        "mov.v2.u16 V, H;",
        "mov.v2.f32 F, V;",
        "mov.v4.u64 {%rd0,%rd1,%rd2,%rd3}, {%rd0,%rd1,%rd2,%rd3};",
        "mov.v4.f64 {D.x,D.y,E.x,E.y}, {D.x,D.y,E.x,E.y};",
        "mov.v2.u8 H, I;",
        "mov.v2.f16 H, I;",
        "mov.v2.b128 L, M;",
        "mov.v8.u32 Q, R;",
        "mov.v2.u32 V, %clusterid;",
        "mov.v4.f32 X, %clusterid;",
        "mov.v4.u32 %clusterid, Q;",
        "mov.b64 L, {%r0,%r1};",
        "mov.f32 %f0, {%h0,%h1};",
        "mov.b128 %b, %b;",
        "mov.v2.u32 V, {1/0,7};"}) {
    SCOPED_TRACE(operation);
    const auto ast = test_helpers::parseModule(vector_mov_module(operation));
    if (!ast || !ast.diagnostics.empty())
      continue;
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Owned source form and projected identity survive AST release, clone and move. */
TEST(VectorMov, OwnsNamedAndBracePayloadsAfterSourceRelease) {
  auto owned = [] {
    const auto ast =
        test_helpers::parseModule(vector_mov_module("mov.v2.u32 %T1, V;"));
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto copy = *owned;
  auto moved = std::move(copy);
  auto& mov = vector_mov(moved);
  const auto& source = std::get<ResolvedMovValueVector>(mov.src.value);
  ASSERT_TRUE(source.source.whole_base);
  EXPECT_TRUE(valid_mov_vector_source(
      source, mov.src.locs, true,
      moved.functions.front().instruction_ranges.front()));
  for (size_t index = 0; index < 2; ++index) {
    const auto& reg = std::get<ResolvedRegisterRef>(source.elements[index]);
    EXPECT_EQ(reg.symbol_id,
              source.source.whole_base->value.register_ref.symbol_id);
    ASSERT_TRUE(reg.component);
    EXPECT_EQ(reg.component->lane, index);
    EXPECT_EQ(reg.component->origin, RegisterComponentOrigin::NamedProjection);
    EXPECT_FALSE(reg.component->selector);
    EXPECT_FALSE(reg.vector_width);
    EXPECT_EQ(mov.src.locs[index], source.source.range);
    EXPECT_FALSE(valid_register_component(reg));
  }
  EXPECT_TRUE(validateModule(moved));
  EXPECT_TRUE(mov.clone()->check(vector_mov_context(moved)));
}

/** Every named-container mutation starts from a passing real-context baseline. */
TEST(VectorMov, RejectsNamedSourceAndDestinationTampering) {
  const auto ast =
      test_helpers::parseModule(vector_mov_module("mov.v2.u32 %T1, V;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto baseline = resolveAndValidateModule(*ast);
  ASSERT_TRUE(baseline) << baseline.error().front().message;
  ASSERT_TRUE(vector_mov(*baseline).check(vector_mov_context(*baseline)));
  for (int mutation = 0; mutation < 13; ++mutation) {
    SCOPED_TRACE(mutation);
    auto copy = *baseline;
    auto& mov = vector_mov(copy);
    auto& source = std::get<ResolvedMovValueVector>(mov.src.value);
    auto& reg = std::get<ResolvedRegisterRef>(source.elements.front());
    switch (mutation) {
      case 0:
        source.source.kind = ResolvedVectorSourceKind::BraceList;
        break;
      case 1:
        source.source.whole_base.reset();
        break;
      case 2:
        reg.component->lane = 1;
        break;
      case 3:
        reg.component->origin = RegisterComponentOrigin::ExplicitSelector;
        break;
      case 4:
        reg.component->declaration_width = 4;
        break;
      case 5:
        reg.declared_type = ScalarType::U64;
        break;
      case 6:
        reg.parameterized_index = 1;
        break;
      case 7:
        reg.symbol_id = mov.dst.value.elements.front()->symbol_id;
        break;
      case 8:
        source.elements.front() = ResolvedImmediate{
            .bits = 1, .type = ScalarType::U32, .integer_source_bits = 1};
        break;
      case 9:
        source.source.range = {};
        break;
      case 10:
        mov.src.locs.front().start.column++;
        break;
      case 11:
        mov.dst.value.elements.front()->component->lane = 1;
        break;
      case 12:
        mov.vector.value = VectorArity::V4;
        break;
    }
    EXPECT_FALSE(mov.check(vector_mov_context(copy)));
    EXPECT_FALSE(validateModule(copy));
  }
  auto owner = vector_mov_context(*baseline);
  owner.instruction_range.start.line += 100;
  owner.instruction_range.end.line += 100;
  EXPECT_FALSE(vector_mov(*baseline).check(owner));
  const auto other_ast =
      test_helpers::parseModule(vector_mov_module("mov.v2.u32 V, %T1;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(other_ast);
  EXPECT_FALSE(validateModule(*other_ast, *baseline));
}

/** Self-consistent projection caches still require an actual declaration rejoin. */
TEST(VectorMov, RejoinsCoherentCachesToActualDeclarations) {
  const auto ast =
      test_helpers::parseModule(vector_mov_module("mov.v2.u32 %T1, V;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto baseline = resolveAndValidateModule(*ast);
  ASSERT_TRUE(baseline) << baseline.error().front().message;
  ASSERT_TRUE(vector_mov(*baseline).check(vector_mov_context(*baseline)));
  for (int mutation = 0; mutation < 2; ++mutation) {
    auto copy = *baseline;
    auto& mov = vector_mov(copy);
    auto& source = std::get<ResolvedMovValueVector>(mov.src.value);
    auto& base = source.source.whole_base->value.register_ref;
    if (mutation == 0)
      base.parameterized_index = 1;
    else
      base.symbol_id =
          mov.dst.value.source.whole_base->value.register_ref.symbol_id;
    for (auto& value : source.elements) {
      auto& lane = std::get<ResolvedRegisterRef>(value);
      lane.parameterized_index = base.parameterized_index;
      lane.symbol_id = base.symbol_id;
    }
    // Structural checking cannot replace the owned declaration rejoin.
    EXPECT_TRUE(mov.check(vector_mov_context(copy)));
    EXPECT_FALSE(validateModule(copy));
  }
}

/** Numeric source bits survive narrowing, source release and independent rechecking. */
TEST(VectorMov, OwnsTypedImmediateConversionAndRejectsTampering) {
  auto baseline = [] {
    const auto ast = test_helpers::parseModule(
        vector_mov_module("mov.v2.u16 H, {65536+1,-1};"));
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(baseline) << baseline.error().front().message;
  auto& mov = vector_mov(*baseline);
  auto& values = std::get<ResolvedMovValueVector>(mov.src.value);
  const auto& first = std::get<ResolvedImmediate>(values.elements[0]);
  const auto& second = std::get<ResolvedImmediate>(values.elements[1]);
  EXPECT_EQ(first.bits, 1);
  EXPECT_EQ(first.integer_source_bits, 65537);
  EXPECT_EQ(second.bits, 65535);
  EXPECT_TRUE(second.is_negative);
  ASSERT_TRUE(mov.check(vector_mov_context(*baseline)));
  ASSERT_TRUE(validateModule(*baseline));
  for (int mutation = 0; mutation < 3; ++mutation) {
    auto copy = *baseline;
    auto& changed_mov = vector_mov(copy);
    auto& source = std::get<ResolvedMovValueVector>(changed_mov.src.value);
    auto& immediate = std::get<ResolvedImmediate>(source.elements[0]);
    if (mutation == 0)
      immediate.bits = 2;
    else if (mutation == 1)
      immediate.integer_source_bits.reset();
    else
      immediate.type = ScalarType::U32;
    EXPECT_FALSE(changed_mov.check(vector_mov_context(copy)));
    EXPECT_FALSE(validateModule(copy));
  }
}

/** Complete brace payloads reject forged projection, scalar shape and special metadata. */
TEST(VectorMov, RejectsBraceAndHardwareTampering) {
  const auto ast = test_helpers::parseModule(
      vector_mov_module("mov.v2.u32 V, {W.x,%tid.y};"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto baseline = resolveAndValidateModule(*ast);
  ASSERT_TRUE(baseline) << baseline.error().front().message;
  ASSERT_TRUE(vector_mov(*baseline).check(vector_mov_context(*baseline)));
  for (int mutation = 0; mutation < 8; ++mutation) {
    auto copy = *baseline;
    auto& mov = vector_mov(copy);
    auto& source = std::get<ResolvedMovValueVector>(mov.src.value);
    auto& reg = std::get<ResolvedRegisterRef>(source.elements[0]);
    auto& special = std::get<ResolvedSpecialRegisterRef>(source.elements[1]);
    switch (mutation) {
      case 0:
        reg.component->origin = RegisterComponentOrigin::NamedProjection;
        break;
      case 1:
        reg.vector_width = 2;
        break;
      case 2:
        special.component = base::VectorComponent::X;
        break;
      case 3:
        special.component.reset();
        break;
      case 4:
        special.id = base::lookup("%clock64")->id;
        break;
      case 5:
        source.source.range = {};
        break;
      case 6:
        source.source.whole_base = mov.dst.value.source.whole_base;
        break;
      case 7:
        source.source.range = {};
        mov.src.locs.clear();
        break;
    }
    if (mutation != 5)
      EXPECT_FALSE(mov.check(vector_mov_context(copy)));
    EXPECT_FALSE(validateModule(copy));
  }
  const auto hardware_ast =
      test_helpers::parseModule(vector_mov_module("mov.v4.u32 Q, %clusterid;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(hardware_ast);
  auto hardware = resolveAndValidateModule(*hardware_ast);
  ASSERT_TRUE(hardware) << hardware.error().front().message;
  ASSERT_TRUE(vector_mov(*hardware).check(vector_mov_context(*hardware)));
  std::get<ResolvedVectorSpecialRegisterRef>(vector_mov(*hardware).src.value)
      .id = base::lookup("%tid")->id;
  EXPECT_FALSE(vector_mov(*hardware).check(vector_mov_context(*hardware)));
  EXPECT_FALSE(validateModule(*hardware));
}

/** Availability is checked per real special lane and not hidden by vector form. */
TEST(VectorMov, PreservesAvailabilityAndModernMemoryFences) {
  const auto ast = test_helpers::parseModule(
      vector_mov_module("mov.v2.u32 V, {%laneid,%clusterid.x};"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  auto context = vector_mov_context(*module);
  ASSERT_TRUE(vector_mov(*module).check(context));
  context.target.sm_version = 80;
  EXPECT_FALSE(vector_mov(*module).check(context));
  context = vector_mov_context(*module);
  context.target.ptx_version = {7, 7};
  EXPECT_FALSE(vector_mov(*module).check(context));
  const auto memory = test_helpers::parseModule(vector_mov_module(
      "ld.global.v8.u32 {%r0,%r1,%r2,%r3,%r4,%r5,%r6,%r7}, [A];", "sm_100"));
  ASSERT_MODULE_PARSE_SUCCEEDS(memory);
  EXPECT_TRUE(resolveAndValidateModule(*memory));
  const auto floating =
      test_helpers::parseModule(vector_mov_module("mov.v2.f64 D, E;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(floating);
  auto double_module = resolveAndValidateModule(*floating);
  ASSERT_TRUE(double_module) << double_module.error().front().message;
  auto double_context = vector_mov_context(*double_module);
  ASSERT_TRUE(vector_mov(*double_module).check(double_context));
  double_context.target.sm_version = 12;
  EXPECT_FALSE(vector_mov(*double_module).check(double_context));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

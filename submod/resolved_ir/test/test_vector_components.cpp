#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/tex.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fabric.gen.hpp>
#include <ptx_frontend/resolved_ir/model/video/vmad.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Supply scalar/vector declarations without requiring numbered vector names. */
std::string component_module(std::string_view operations) {
  return ".version 9.3\n.target sm_90\n.address_size 64\n"
         ".global .align 16 .u32 A[16];\n"
         ".extern .func take(.reg .u32 arg);\n"
         ".entry kernel() {\n"
         ".reg .u32 %r<3>; .reg .u64 %rd; .reg .pred %p;\n"
         ".reg .v2 .u32 V; .reg .v4 .u32 Q; .reg .v2 .u32 %T<2>;\n"
         ".reg .v4 .f32 F; .reg .v2 .f64 D; .reg .v2 .u16 H;\n" +
         std::string(operations) + "\nret; }";
}

/** Borrow the first scalar MOV's selected destination for mutation tests. */
ResolvedRegisterRef& component_destination(ResolvedModule& module) {
  return dynamic_cast<MovScalar&>(*module.functions.back().body.front())
      .dst_register->value;
}

/** Locate a unique source slice independently of the parser's stored ranges. */
SourceRange component_range(std::string_view source, std::string_view slice) {
  const auto offset = source.rfind(slice);
  EXPECT_NE(offset, std::string_view::npos);
  if (offset == std::string_view::npos)
    return {};
  SourcePos position{1, 1};
  for (const char character : source.substr(0, offset)) {
    if (character == '\n') {
      ++position.line;
      position.column = 1;
    } else
      ++position.column;
  }
  return {
      position,
      {position.line, position.column + static_cast<int32_t>(slice.size())}};
}

/** Scalar roles reuse existing type, width and instruction admission rules. */
TEST(VectorComponents, ResolvesScalarRolesAndAliases) {
  for (const auto operation :
       {"add.u32 V.x, V.g, %r1;", "add.u32 %r0, Q.a, Q.z;", "mov.u32 Q.w, V.r;",
        "mov.u32 %r0, %T1.g;", "add.f32 F.x, F.g, F.z;",
        "add.f64 D.x, D.y, 1.0;", "add.u16 H.x, H.g, 1;", "mov.b16 H.r, H.y;",
        "ld.u32 V.x, [%rd];", "st.u32 [%rd], Q.a;"}) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(component_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_TRUE(validateModule(*resolved));
  }
  for (const auto selector : {".x", ".r", ".y", ".g", ".z", ".b", ".w", ".a"}) {
    const auto parsed = test_helpers::parseModule(
        component_module("mov.u32 %r0, Q" + std::string(selector) + ";"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved);
    const auto& mov = dynamic_cast<const MovScalar&>(
        *resolved->functions.back().body.front());
    const auto& ref = std::get<ResolvedRegisterRef>(mov.src_mov_source->value);
    ASSERT_TRUE(ref.component);
    EXPECT_EQ(ref.component->lane, ordinary_register_lane(selector));
    EXPECT_EQ(ref.component->declaration_width, 4);
    EXPECT_FALSE(ref.vector_width);
    EXPECT_TRUE(ref.symbol_id);
    EXPECT_TRUE(valid_register_component(ref));
  }
}

/** Brace lane identities distinguish siblings but reject selector aliases. */
TEST(VectorComponents, BraceLanesAndCanonicalDuplicates) {
  for (const auto operation :
       {"ld.v2.u32 {V.x,V.y}, [%rd];", "st.v2.u32 [%rd], {V.r,V.g};",
        "mov.b64 {Q.x,Q.y}, %rd;", "mov.b64 %rd, {V.r,V.g};",
        "ld.v4.u32 {Q.x,Q.y,Q.z,Q.w}, [%rd];",
        "ld.v2.u32 {%T0.x,%T1.r}, [%rd];"}) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(component_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_TRUE(validateModule(*resolved));
  }
  const std::string source = component_module("ld.v2.u32 {V.x,V.r}, [%rd];");
  const auto parsed = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_FALSE(resolved);
  EXPECT_EQ(resolved.error().front().range, component_range(source, "V.r"));
}

/** Component failures identify the offending base or selector, not the opcode. */
TEST(VectorComponents, RejectsBadSelectorsTypesAndRoles) {
  for (const auto& [operation, failing] :
       {std::pair<std::string_view, std::string_view>{"mov.u32 %r0, V.z;",
                                                      ".z"},
        {"mov.u32 %r0, V.a;", ".a"},
        {"mov.u32 %r0, V.xy;", ".xy"},
        {"mov.u32 %r0, %r1.x;", "%r1.x"}}) {
    const auto source = component_module(operation);
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModule(*parsed);
    ASSERT_FALSE(resolved);
    const auto expected = failing == "%r1.x" ? component_range(source, "%r1")
                                             : component_range(source, failing);
    // The declaration itself also contains %r1 only as a parameterized member.
    EXPECT_EQ(resolved.error().front().range, expected);
  }
  for (const auto operation :
       {"add.u32 %r0, F.x, 1;", "mov.pred %p, V.x;", "add.u32 %r0, V, 1;",
        "mov.u32 %r0, %T2.x;", "mov.u32 %r0, %tid.w;", "mov.u32 %r0, %tid.r;",
        "ld.v2.u32 {%tid.x,%r0}, [%rd];", "vadd.u32.u32.u32 %r0, -V.x, %r1;"}) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(component_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModule(*parsed));
  }
  const auto hardware =
      test_helpers::parseModule(component_module("mov.u32 %r0, %tid.x;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(hardware);
  EXPECT_TRUE(resolveModule(*hardware));
}

/** Ordinary selections remain whole carriers in existing VIDEO scalar roles. */
TEST(VectorComponents, VideoCarriersPreserveSelectorAndMinusBoundaries) {
  for (const auto operation : {"vadd.u32.u32.u32 V.x, V.r, %r1;",
                               "vadd.u32.u32.u32.add V.x, V.y, %r1, %r2;",
                               "vmad.u32.u32.u32 %r0, -V.x, %r1, %r2;",
                               "vmad.u32.u32.u32 %r0, %r1, %r2, -V.y;"}) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(component_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_TRUE(validateModule(*resolved));
  }
  for (const auto operation :
       {"add.u32 %r0, -V.x, %r1;", "vmad.u32.u32.u32.po %r0, -V.x, %r1, %r2;",
        "vadd.u32.u32.u32 %r0, %tid.x, %r1;",
        "vadd.u32.u32.u32 %r0, V.x.b0, %r1;",
        "vadd.u32.u32.u32 V.x, V.y, %r1, %r2;"}) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(component_module(operation));
    EXPECT_TRUE(!parsed || !parsed.diagnostics.empty() ||
                !resolveModule(*parsed));
  }
}

/** Real base/member identity survives source destruction, copies and aliases. */
TEST(VectorComponents, OwnsIdentityAndSourceFaithfulness) {
  constexpr std::string_view brace_source = "mov.b64 %rd0, {%T1.x,%T1.r};";
  const auto brace_ast = [&] {
    PtxSyntaxParser parser{brace_source};
    auto parsed = parser.parseInstruction();
    EXPECT_TRUE(parsed);
    EXPECT_TRUE(parsed.diagnostics.empty());
    return *parsed;
  }();
  auto brace_copy = brace_ast;
  auto& selected = std::get<syntax_ast::AstVectorMember>(
      std::get<syntax_ast::AstVectorPack>(brace_copy.operands[1]).elements[1]);
  EXPECT_EQ(selected.base.syntax.text, "%T1");
  EXPECT_EQ(selected.base.syntax.range, component_range(brace_source, "%T1"));
  EXPECT_EQ(selected.selector.range, component_range(brace_source, ".r"));
  EXPECT_EQ(selected.range, component_range(brace_source, "%T1.r"));
  selected.selector.text = ".g";
  const auto& original = std::get<syntax_ast::AstVectorMember>(
      std::get<syntax_ast::AstVectorPack>(brace_ast.operands[1]).elements[1]);
  EXPECT_EQ(original.selector.text, ".r");
  auto owned = [] {
    const auto parsed =
        test_helpers::parseModule(component_module("mov.u32 %T1.x, %T1.r;"));
    return resolveModule(*parsed);
  }();
  ASSERT_TRUE(owned) << owned.error().front().message;
  const auto& dst = component_destination(*owned);
  const auto& mov =
      dynamic_cast<const MovScalar&>(*owned->functions.back().body.front());
  const auto& src = std::get<ResolvedRegisterRef>(mov.src_mov_source->value);
  EXPECT_EQ(dst.symbol_id, src.symbol_id);
  EXPECT_EQ(dst.parameterized_index, 1);
  EXPECT_TRUE(same_register_storage(dst, src));
  EXPECT_FALSE(dst.vector_width);
  EXPECT_EQ(dst.declared_type, ScalarType::U32);
  ResolvedModule copied = *owned;
  ResolvedModule moved = std::move(copied);
  EXPECT_TRUE(validateModule(moved));
  const auto changed =
      test_helpers::parseModule(component_module("mov.u32 %T1.r, %T1.r;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(changed);
  EXPECT_FALSE(validateModule(*changed, moved));
}

/** Explicit scalar provenance cannot be replaced by a future named projection. */
TEST(VectorComponents, RejectsOwnedAndStandaloneMetadataTampering) {
  const auto parsed =
      test_helpers::parseModule(component_module("mov.u32 V.x, V.y;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto original = resolveModule(*parsed);
  ASSERT_TRUE(original);
  for (int mutation = 0; mutation < 10; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = *original;
    auto& ref = component_destination(changed);
    auto& part = *ref.component;
    switch (mutation) {
      case 0:
        part.lane = 1;
        break;
      case 1:
        part.declaration_width = 4;
        break;
      case 2:
        ref.declared_type = ScalarType::F32;
        break;
      case 3:
        ref.symbol_id->value = UINT32_MAX;
        break;
      case 4:
        part.base_spelling = "Q";
        break;
      case 5:
        part.selector.reset();
        break;
      case 6:
        part.origin = RegisterComponentOrigin::NamedProjection;
        part.selector.reset();
        ref.spelling = part.base_spelling;
        part.range = part.base_range;
        break;
      case 7:
        part.base_range.start.line += 10;
        part.base_range.end.line += 10;
        part.selector->range.start.line += 10;
        part.selector->range.end.line += 10;
        part.range.start.line += 10;
        part.range.end.line += 10;
        break;
      case 8:
        ref.parameterized_index = 1;
        break;
      case 9:
        ref.vector_width = 2;
        break;
    }
    EXPECT_FALSE(validateModule(changed));
    if (mutation == 0 || mutation == 5 || mutation == 6 || mutation == 7 ||
        mutation == 9)
      EXPECT_FALSE(changed.functions.back().body.front()->check({}));
  }
}

/** Brace and delegated VIDEO/texture payloads close standalone provenance. */
TEST(VectorComponents, NestedPayloadsRejectProjectedOrShiftedProvenance) {
  for (const bool shifted : {false, true}) {
    const auto parsed =
        test_helpers::parseModule(component_module("mov.b64 %rd, {V.x,V.y};"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto module = resolveModule(*parsed);
    ASSERT_TRUE(module) << module.error().front().message;
    auto& mov =
        dynamic_cast<MovScalar&>(*module->functions.back().body.front());
    auto& ref = *mov.src_register_vector->value.elements[1];
    if (shifted) {
      ++ref.component->range.start.line;
      ++ref.component->range.end.line;
      ++ref.component->base_range.start.line;
      ++ref.component->base_range.end.line;
      ++ref.component->selector->range.start.line;
      ++ref.component->selector->range.end.line;
    } else {
      ref.component->origin = RegisterComponentOrigin::NamedProjection;
      ref.component->selector.reset();
    }
    EXPECT_FALSE(mov.check({}));
    EXPECT_FALSE(validateModule(*module));
  }
  const auto parsed = test_helpers::parseModule(
      component_module("vmad.u32.u32.u32 %r0, -V.x, %r1, %r2;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto module = resolveModule(*parsed);
  ASSERT_TRUE(module) << module.error().front().message;
  auto& vmad =
      dynamic_cast<VmadScalar&>(*module->functions.back().body.front());
  const checker::Context video_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = module->functions.back().instruction_ranges[0]};
  ASSERT_TRUE(vmad.check(video_context));
  auto invalid_owner = video_context;
  invalid_owner.instruction_range.start.line = 0;
  EXPECT_FALSE(vmad.check(invalid_owner));
  const auto original_video = vmad.a.value;
  auto& video_ref = std::get<ResolvedRegisterRef>(vmad.a.value.value.value);
  EXPECT_EQ(
      video_ref.component->range,
      component_range(component_module("vmad.u32.u32.u32 %r0, -V.x, %r1, %r2;"),
                      "V.x"));
  vmad.a.value.selector = WithLocs<VideoSelector>{
      VideoByteSelector{0}, video_ref.component->selector->range};
  const auto invalid_video = vmad.check(video_context);
  ASSERT_FALSE(invalid_video);
  EXPECT_NE(invalid_video.error().front().message.find("component provenance"),
            std::string::npos);
  EXPECT_FALSE(validateModule(*module));
  vmad.a.value = original_video;
  auto& projected_video =
      std::get<ResolvedRegisterRef>(vmad.a.value.value.value);
  projected_video.component->origin = RegisterComponentOrigin::NamedProjection;
  projected_video.component->selector.reset();
  EXPECT_FALSE(vmad.check(video_context));
  EXPECT_FALSE(validateModule(*module));

  const auto texture_ast = test_helpers::parseModule(
      ".version 9.3\n.target sm_80\n.address_size 64\n.global .texref tex0;\n"
      ".entry kernel() { .reg .v4 .f32 F; .reg .v2 .f32 C;\n"
      "tex.2d.v4.f32.f32 {F.x,F.y,F.z,F.w}, [tex0,{C.x,C.y}];\nret; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(texture_ast);
  auto texture_module = resolveModule(*texture_ast);
  ASSERT_TRUE(texture_module) << texture_module.error().front().message;
  auto& tex = dynamic_cast<TexOmitted2dF32U32S32F32&>(
      *texture_module->functions.front().body.front());
  const checker::Context texture_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range =
          texture_module->functions.front().instruction_ranges[0]};
  ASSERT_TRUE(tex.check(texture_context));
  auto& coordinate =
      std::get<ResolvedRegisterRef>(tex.access.value.coordinates[1].value);
  coordinate.component->origin = RegisterComponentOrigin::NamedProjection;
  coordinate.component->selector.reset();
  EXPECT_FALSE(tex.check(texture_context));
  EXPECT_FALSE(validateModule(*texture_module));
}

/** Tensor coordinates are scalar lanes, but their address bases stay excluded. */
TEST(VectorComponents, TensorCoordinatesAndNestedAddressFence) {
  const auto ast = test_helpers::parseModule(
      ".version 9.3\n.target sm_90\n.address_size 64\n"
      ".global .align 64 .b8 tensor_map[128];\n"
      ".shared .align 16 .b8 src[1024];\n"
      ".entry kernel() { .reg .v2 .s32 V;\n"
      "cp.reduce.async.bulk.tensor.2d.global.shared::cta.add.bulk_group "
      "[tensor_map, {V.x,V.y}], [src];\n}");
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto original = resolveModule(*ast);
  ASSERT_TRUE(original) << original.error().front().message;
  EXPECT_TRUE(validateModule(*original));
  for (const bool address_base : {false, true}) {
    auto module = *original;
    auto& copy = dynamic_cast<CpReduceAsyncBulkTensor2dAdd&>(
        *module.functions.front().body.front());
    const checker::Context context{
        .target = {.ptx_version = {9, 3}, .sm_version = 90},
        .instruction_range = module.functions.front().instruction_ranges[0]};
    ASSERT_TRUE(copy.check(context));
    auto& coordinate = std::get<ResolvedRegisterRef>(
        copy.tensor.value.coordinates.elements[1]);
    if (address_base)
      copy.tensor.value.tensor_map.address.base = coordinate;
    else {
      coordinate.component->origin = RegisterComponentOrigin::NamedProjection;
      coordinate.component->selector.reset();
    }
    EXPECT_FALSE(copy.check(context));
    EXPECT_FALSE(validateModule(module));
  }
}

/** Fabric's identifier-only handle grammar survives selected-ref transplants. */
TEST(VectorComponents, FabricHandleRejectsBothComponentOrigins) {
  const auto ast = test_helpers::parseModule(
      ".version 9.3\n.target sm_100\n.address_size 64\n"
      ".shared .align 16 .b8 data[512];\n.shared .align 16 .b8 bar[16];\n"
      ".entry kernel() { .reg .b32 %endpoint; .reg .b64 %dataoff;\n"
      ".reg .b64 %counteroff; .reg .v2 .b32 E; .reg .v2 .b64 W;\n"
      "mov.b32 E.x, %endpoint; mov.b64 W.x, %dataoff;\n"
      "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
      "mbarrier::report::fabric.counted::bytes.relaxed.sys.b128 "
      "[%endpoint,%dataoff,%counteroff], [data], 16, [bar];\nret; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto original = resolveModule(*ast);
  ASSERT_TRUE(original) << original.error().front().message;
  const auto profile = base::find_target_profile("sm_100");
  ASSERT_TRUE(profile);
  for (size_t index = 0; index < 3; ++index)
    for (const auto origin : {RegisterComponentOrigin::ExplicitSelector,
                              RegisterComponentOrigin::NamedProjection}) {
      auto module = *original;
      auto& body = module.functions.front().body;
      auto& form = dynamic_cast<FabricTryPutUnicastCounted&>(*body[2]);
      const checker::Context context{
          .target = {.ptx_version = {9, 3},
                     .sm_version = 100,
                     .enabled_family_features =
                         profile->enabled_family_features,
                     .identity = profile->identity,
                     .capabilities = profile->capabilities},
          .instruction_range = module.functions.front().instruction_ranges[2]};
      ASSERT_TRUE(form.check(context));
      auto& field = index == 0   ? form.dst.value.endpoint
                    : index == 1 ? form.dst.value.data_offset
                                 : *form.dst.value.counter_offset;
      const auto& mov =
          dynamic_cast<const MovScalar&>(*body[index == 0 ? 0 : 1]);
      field.value = mov.dst_register->value;
      auto& component = *field.value.component;
      const SourcePos start = field.locs.front().start;
      component.base_range = {start, {start.line, start.column + 1}};
      component.range = {start, {start.line, start.column + 3}};
      component.selector->range = {{start.line, start.column + 1},
                                   component.range.end};
      component.origin = origin;
      if (origin == RegisterComponentOrigin::NamedProjection) {
        component.selector.reset();
        component.range = component.base_range;
        field.value.spelling = component.base_spelling;
      } else
        ASSERT_TRUE(valid_register_component(field.value));
      field.locs = {component.range};
      EXPECT_FALSE(form.check(context));
      EXPECT_FALSE(validateModule(module));
    }
}

/** Excluded nested grammars and forged named-index payloads remain closed. */
TEST(VectorComponents, PreservesAddressCallAndArrayIndexFences) {
  for (const auto operation :
       {"mov.u64 %rd, A[V.x];", "ld.u32 %r0, [V.x];", "call take, (V.x);"}) {
    const auto parsed = test_helpers::parseModule(component_module(operation));
    EXPECT_TRUE(!parsed || !parsed.diagnostics.empty());
  }
  const auto parsed = test_helpers::parseModule(
      component_module("mov.u32 V.x, V.y; mov.u64 %rd, A[%r0];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto original = resolveModule(*parsed);
  ASSERT_TRUE(original);
  auto changed = *original;
  auto& mov = dynamic_cast<MovScalar&>(*changed.functions.back().body[1]);
  auto& address = std::get<ResolvedAddress>(mov.src_mov_source->value);
  auto& index =
      std::get<WithLocs<ResolvedRegisterRef>>(address.named_index->index);
  index.value = component_destination(changed);
  EXPECT_FALSE(index.value.vector_width);
  EXPECT_FALSE(valid_named_array_address(address, NamedArrayAddressPolicy::Mov,
                                         mov.src_mov_source->locs.front()));
  EXPECT_FALSE(validateModule(changed));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

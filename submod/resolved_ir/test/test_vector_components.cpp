#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
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

/** Fabric's whole-register handle role survives selected-ref transplants. */
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

/** Delivered call selections and stale named-index ranges remain independently checked. */
TEST(VectorComponents, PreservesCallAndStaleIndexRangeFences) {
  for (const auto operation : {"call take, (V.x);"}) {
    const auto parsed = test_helpers::parseModule(component_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_TRUE(resolveAndValidateModule(*parsed));
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

/** Selected scalar address roles bind real vector owners and preserve aliases. */
TEST(VectorComponents, ResolvesAddressBasesAndNamedArrayIndices) {
  for (const auto operation :
       {"ld.u32 %r0, [V.x];", "ld.u32 %r0, [Q.a+4];", "st.u32 [V.r-4], %r0;",
        "mov.u64 %rd, A[%T1.g];", "ld.global.u32 %r0, A[V.y];",
        "st.global.u32 A[Q.z], %r0;", "cvta.global.u64 %rd, A[V.r];",
        ".reg .v2 .u64 W; isspacep.global %p, W.x;"}) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(component_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    EXPECT_TRUE(validateModule(*resolved));
    EXPECT_TRUE(validateModule(*parsed, *resolved));
  }
}

/** Existing member nodes own physical ranges inside address and subscript wrappers. */
TEST(VectorComponents,
     AddressSyntaxPreservesSelectedChildrenAndWrittenOffsets) {
  const std::string source = "mov.u64 %rd, A[%T1.g-1];";
  PtxCstParser concrete_parser(source);
  const auto concrete = concrete_parser.parseInstruction();
  ASSERT_TRUE(concrete);
  ASSERT_TRUE(concrete.diagnostics.empty());
  EXPECT_EQ(concrete->sourceText(), source);
  const auto& cst_address = std::get<syntax_cst::CstAddress>(
      concrete->instruction()->operands[1].operand);
  ASSERT_TRUE(cst_address.named_index);
  const auto& cst_member =
      std::get<syntax_cst::CstVectorMember>(cst_address.named_index->index);
  EXPECT_EQ(concrete->token(cst_member.selector).text, ".g");
  EXPECT_EQ(concrete->sourceRange(cst_member.token_range),
            component_range(source, "%T1.g"));
  PtxSyntaxParser parser(source);
  const auto parsed = parser.parseInstruction();
  ASSERT_TRUE(parsed);
  ASSERT_TRUE(parsed.diagnostics.empty());
  const auto& address = std::get<syntax_ast::AstAddress>(parsed->operands[1]);
  ASSERT_TRUE(address.named_index);
  const auto& index = *address.named_index;
  const auto& member = std::get<syntax_ast::AstVectorMember>(index.index);
  EXPECT_EQ(member.base.syntax.text, "%T1");
  EXPECT_EQ(member.base.syntax.range, component_range(source, "%T1"));
  EXPECT_EQ(member.selector.range, component_range(source, ".g"));
  EXPECT_EQ(member.range, component_range(source, "%T1.g"));
  ASSERT_TRUE(index.displacement);
  EXPECT_EQ(index.displacement->operation,
            syntax_ast::AstAddressOffset::Operator::Subtract);
  EXPECT_EQ(index.operator_range, component_range(source, "-"));
  auto copied = *parsed;
  std::get<syntax_ast::AstVectorMember>(
      std::get<syntax_ast::AstAddress>(copied.operands[1]).named_index->index)
      .selector.text = ".y";
  EXPECT_EQ(member.selector.text, ".g");
}

/** Current selected carriers survive real source death, copies, assignments and clones. */
TEST(VectorComponents, AddressComponentsRetainCurrentOwnedAuthority) {
  constexpr std::string_view operation =
      "ld.u32 %r0, [%T1.g+4]; mov.u64 %rd, A[%T1.r-1];";
  auto owned = [operation] {
    const auto parsed = test_helpers::parseModule(component_module(operation));
    EXPECT_TRUE(parsed);
    EXPECT_TRUE(parsed.diagnostics.empty());
    return resolveModule(*parsed);
  }();
  ASSERT_TRUE(owned) << owned.error().front().message;
  ASSERT_TRUE(validateModule(*owned));
  auto copied = *owned;
  auto moved = std::move(copied);
  ResolvedModule assigned;
  assigned = *owned;
  ResolvedModule move_assigned;
  move_assigned = std::move(assigned);
  for (auto* current : {&moved, &move_assigned}) {
    for (auto& instruction : current->functions.back().body)
      instruction = instruction->clone();
    EXPECT_TRUE(validateModule(*current));
    auto& load =
        dynamic_cast<LdGenericScalar&>(*current->functions.back().body[0]);
    auto& base = std::get<ResolvedRegisterRef>(load.address.value.base);
    ASSERT_TRUE(base.component);
    EXPECT_EQ(base.parameterized_index, 1u);
    EXPECT_EQ(base.component->lane, 1u);
    EXPECT_EQ(base.component->declaration_width, 2u);
    EXPECT_FALSE(base.vector_width);
    auto& mov = dynamic_cast<MovScalar&>(*current->functions.back().body[1]);
    const auto& address = std::get<ResolvedAddress>(mov.src_mov_source->value);
    ASSERT_TRUE(address.named_index);
    EXPECT_EQ(address.named_index->scalar_stride, 4u);
    EXPECT_EQ(address.named_index->byte_displacement, -4);
    const auto provided =
        test_helpers::parseModule(component_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(provided);
    base.component->lane = 0;
    base.component->selector->spelling = ".r";
    base.spelling = "%T1.r";
    EXPECT_TRUE(validateModule(*current));
    EXPECT_TRUE(validateModule(*provided, *current));
    base.component->lane = 3;
    EXPECT_FALSE(validateModule(*current));
    EXPECT_FALSE(validateModule(*provided, *current));
  }
}

/** Build matched scalar call signatures without changing declaration ownership. */
std::string selected_call_source(std::string_view type, unsigned width,
                                 std::string_view mode, bool returns,
                                 std::string_view selected) {
  const auto result = returns ? ".reg ." + std::string(type) + " result" : "";
  const auto input = returns ? "" : ".reg ." + std::string(type) + " arg";
  std::string source = ".version 9.3\n.target sm_90\n.address_size 64\n.func ";
  if (returns)
    source += "(" + result + ") ";
  source += "callee(" + input + ");\n.entry kernel(){.reg .v" +
            std::to_string(width) + " ." + std::string(type) + " V; .reg ." +
            std::string(type) + " scalar; .reg .u64 fp;\n";
  if (mode == "callprototype") {
    source += "meta: .callprototype ";
    if (returns)
      source += "(" + result + ") ";
    source += "_ (" + input + ");\n";
  } else if (mode == "calltargets")
    source += "meta: .calltargets callee;\n";
  source += "call ";
  if (returns)
    source += "(" + std::string(selected) + "), ";
  source += mode == "direct" ? "callee" : "fp";
  source += returns ? ", ()" : ", (" + std::string(selected) + ")";
  if (mode != "direct")
    source += ", meta";
  return source + ";ret;}";
}

/** Every scalar ABI combination keeps its actual v2/v4 owner and written alias. */
TEST(CallComponents, ScalarInputReturnGrid) {
  size_t cases = 0;
  for (const auto type :
       {"b32", "s32", "u32", "f32", "b64", "s64", "u64", "f64"})
    for (const unsigned width : {2u, 4u}) {
      if (std::string_view(type).ends_with("64") && width == 4)
        continue;
      for (const auto mode : {"direct", "callprototype", "calltargets"})
        for (const bool returns : {false, true}) {
          ++cases;
          for (const auto selector : {"x", "r", "y", "g", "z", "b", "w", "a"}) {
            if (width == 2 && std::string_view("zbwa").find(selector) !=
                                  std::string_view::npos)
              continue;
            const std::string selected = "V." + std::string(selector);
            const auto source =
                selected_call_source(type, width, mode, returns, selected);
            SCOPED_TRACE(source);
            const auto parsed = test_helpers::parseModule(source);
            ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
            const auto module = resolveAndValidateModule(*parsed);
            ASSERT_TRUE(module) << module.error().front().message;
            EXPECT_TRUE(validateModule(*module));
            EXPECT_TRUE(validateModule(*parsed, *module));
            const auto& call = dynamic_cast<const CallDirect&>(
                *module->functions.back().body.front());
            const auto& value =
                returns ? call.return_value->value
                        : std::get<ResolvedCallParameterRef>(
                              call.arguments->value.values.front().value);
            ASSERT_TRUE(value.component);
            EXPECT_EQ(value.component->declaration_width, width);
            EXPECT_EQ(value.spelling, selected);
            EXPECT_EQ(module->symbols.symbol(*value.symbol_id).vector_width,
                      width);
            if (returns)
              EXPECT_NE(call.return_value->locs.front(),
                        value.component->range);
          }
        }
    }
  EXPECT_EQ(cases, 72u);
}

/** Indirect selected targets reuse the universal integer/bit carrier guard. */
TEST(CallComponents, SelectedFunctionAddressTransport) {
  for (const auto type : {"b32", "s32", "u32", "b64", "s64", "u64"})
    for (const unsigned width : {2u, 4u}) {
      if (std::string_view(type).ends_with("64") && width == 4)
        continue;
      for (const auto mode : {"callprototype", "calltargets"})
        for (const auto selector : {"x", "r", "y", "g", "z", "b", "w", "a"}) {
          if (width == 2 &&
              std::string_view("zbwa").find(selector) != std::string_view::npos)
            continue;
          const std::string source =
              ".version 9.3\n.target sm_90\n.address_size 64\n.func callee(); "
              ".entry kernel(){.reg .v" +
              std::to_string(width) + " ." + type + " FP; meta: ." + mode +
              (std::string_view(mode) == "callprototype" ? " _ ();"
                                                         : " callee;") +
              "call FP." + selector + ", meta;ret;}";
          SCOPED_TRACE(source);
          const auto parsed = test_helpers::parseModule(source);
          ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
          auto module = resolveAndValidateModule(*parsed);
          ASSERT_TRUE(module) << module.error().front().message;
          EXPECT_TRUE(validateModule(*module));
          EXPECT_TRUE(validateModule(*parsed, *module));
        }
    }
  for (const auto type : {"f32", "u16", "b16"})
    for (const bool selected : {false, true}) {
      const std::string source =
          ".version 9.3\n.target sm_90\n.address_size 64\n.entry "
          "kernel(){.reg " +
          std::string(selected ? ".v2 " : "") + "." + type +
          " FP; meta: .callprototype _ ();call FP" + (selected ? ".x" : "") +
          ", meta;ret;}";
      const auto parsed = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      EXPECT_EQ(bool(resolveAndValidateModule(*parsed)),
                std::string_view(type) != "f32");
    }
}

/** Borrow a real selected input/return only while its owning call remains alive. */
ResolvedCallParameterRef& selected_call_actual(ResolvedModule& module,
                                               bool returns) {
  auto& call = dynamic_cast<CallDirect&>(*module.functions.back().body.front());
  return returns ? call.return_value->value
                 : std::get<ResolvedCallParameterRef>(
                       call.arguments->value.values.front().value);
}

/** Mutated caches never replace actual base facts; coherent aliases remain legal. */
TEST(CallComponents, CurrentOwnersAndCachesPrecedeScalarABI) {
  for (const bool returns : {false, true}) {
    const auto source =
        selected_call_source("u32", 2, "direct", returns, "V.x");
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto original = resolveAndValidateModule(*parsed);
    ASSERT_TRUE(original) << original.error().front().message;
    for (unsigned mutation = 0; mutation < 17; ++mutation) {
      SCOPED_TRACE(mutation);
      auto current = *original;
      auto& value = selected_call_actual(current, returns);
      ASSERT_TRUE(value.component);
      auto& part = *value.component;
      auto& symbol = const_cast<binding::Symbol&>(
          current.symbols.symbol(*value.symbol_id));
      switch (mutation) {
        case 0:
          value.symbol_id = binding::SymbolId{0};
          break;
        case 1:
          value.parameterized_index = 8;
          break;
        case 2:
          part.declaration_width = 4;
          break;
        case 3:
          part.lane = 2;
          break;
        case 4:
          part.selector->spelling = ".y";
          break;
        case 5:
          part.origin = RegisterComponentOrigin::NamedProjection;
          break;
        case 6:
          part.base_spelling = "scalar";
          break;
        case 7:
          part.range.end.column += 1000;
          break;
        case 8:
          value.declared_type = ScalarType::F32;
          break;
        case 9:
          value.state_space = base::DeclarationStateSpace::Parameter;
          break;
        case 10:
          symbol.state_space = base::DeclarationStateSpace::Global;
          break;
        case 11:
          symbol.type = ".bf16x2";
          value.declared_type = ScalarType::BF16x2;
          break;
        case 12:
          value.component.reset();
          break;
        case 13:
          value.declared_type.reset();
          break;
        case 14:
          value.state_space.reset();
          break;
        case 15:
          part.selector->spelling = ".r";
          value.spelling = "V.r";
          break;
        case 16:
          part.selector->spelling = ".y";
          part.lane = 1;
          value.spelling = "V.y";
          break;
      }
      const bool coherent = mutation >= 15;
      EXPECT_EQ(bool(validateModule(current)), coherent);
      EXPECT_EQ(bool(validateModule(*parsed, current)), coherent);
      if (!coherent) {
        current = *original;
        EXPECT_TRUE(validateModule(current));
      }
    }
  }
}

/** Source destruction and every owning copy retain components and group provenance. */
TEST(CallComponents, OwnsReturnInputsAndSelectedTarget) {
  constexpr std::string_view source =
      ".version 9.3\n.target sm_90\n.address_size 64\n.entry kernel(){"
      ".reg .v2 .u32 %T<2>; .reg .v2 .u64 FP;"
      "meta: .callprototype (.reg .u32 result) _ (.reg .u32 arg);"
      "call (%T0.r), FP.x, (%T1.g), meta;ret;}";
  auto original = [source] {
    const auto ast = test_helpers::parseModule(source);
    EXPECT_TRUE(ast);
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(original) << original.error().front().message;
  auto copied = *original;
  auto moved = std::move(copied);
  ResolvedModule assigned;
  assigned = *original;
  ResolvedModule move_assigned;
  move_assigned = std::move(assigned);
  for (auto* module : {&moved, &move_assigned}) {
    module->functions.back().body.front() =
        module->functions.back().body.front()->clone();
    EXPECT_TRUE(validateModule(*module));
    auto& call =
        dynamic_cast<CallDirect&>(*module->functions.back().body.front());
    EXPECT_EQ(call.return_value->value.parameterized_index, 0u);
    EXPECT_EQ(call.return_value->value.component->lane, 0u);
    const auto& input = std::get<ResolvedCallParameterRef>(
        call.arguments->value.values.front().value);
    EXPECT_EQ(input.parameterized_index, 1u);
    EXPECT_EQ(input.component->lane, 1u);
    EXPECT_TRUE(
        std::get<ResolvedRegisterRef>(call.target_indirect_callee->value)
            .component);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *module));
    const auto different =
        test_helpers::parseModule(std::string(source).replace(
            std::string(source).find("%T1.g"), 5, "%T1.y"));
    ASSERT_MODULE_PARSE_SUCCEEDS(different);
    EXPECT_FALSE(validateModule(*different, *module));
  }
  ResolvedCallParameterRef old{.spelling = "value"};
  EXPECT_FALSE(old.component);
  EXPECT_EQ(old, ResolvedCallParameterRef{.spelling = "value"});
}

/** Invalid syntax, declaration domains and ABI shapes fail independently of selection. */
TEST(CallComponents, SourceBoundariesAndWholeVectorControls) {
  const std::string header = ".version 9.3\n.target sm_90\n.address_size 64\n";
  for (const auto body :
       {".func callee(.reg .v2 .u32 arg); .entry kernel(){.reg .v2 .u32 V;call "
        "callee,(V.x);}",
        ".func callee(.reg .u32 arg); .entry kernel(){.reg .v2 .u64 V;call "
        "callee,(V.x);}",
        ".func callee(.reg .u32 arg); .entry kernel(){.reg .v2 .f32 V;call "
        "callee,(V.x);}",
        ".func callee(.reg .u32 arg); .entry kernel(){.param .v2 .u32 V;call "
        "callee,(V.x);}",
        ".func callee(.reg .u32 arg); .entry kernel(.param .u32 V){call "
        "callee,(V);}",
        ".func callee(.reg .u32 arg); .entry kernel(){.reg .u32 a,b;call "
        "callee,({a,b});}",
        ".func (.reg .u32 r) callee(); .entry kernel(){.reg .u32 a,b;call "
        "({a,b}),callee,();}",
        ".func (.reg .u32 r) callee(); .entry kernel(){.reg .v2 .u32 V;call "
        "(V.x,V.y),callee,();}",
        ".func (.reg .u32 r) callee(); .entry kernel(){call (1),callee,();}",
        ".func callee(.reg .u32 arg); .entry kernel(){.reg .v2 .u32 V;call "
        "callee,(V.z);}",
        ".func callee(.reg .u32 arg); .entry kernel(){.reg .v2 .u32 V;call "
        "callee,(V.q);}",
        ".func callee(); .entry kernel(){call callee.x;}",
        ".entry kernel(){.reg .v2 .u32 V;meta:.callprototype _ ();call "
        "V.x,meta.x;}",
        ".entry kernel(){.reg .v2 .u32 V;call V.x;}",
        ".entry kernel(){.reg .v2 .u32 V;.reg .u32 meta;call V.x,meta;}",
        ".func callee(.reg .u64 arg); .entry kernel(){.reg .v4 .u64 V;call "
        "callee,(V.x);}",
        ".func callee(.reg .u16 arg); .entry kernel(){.reg .v2 .u16 V;call "
        "callee,(V.x);}",
        ".func callee(.reg .u32 arg); .entry kernel(){.reg .v2 .u32 V;call "
        "callee,(V);}"}) {
    SCOPED_TRACE(body);
    const auto ast = test_helpers::parseModule(header + body);
    EXPECT_TRUE(!ast || !ast.diagnostics.empty() ||
                !resolveAndValidateModule(*ast));
  }
  for (const auto body :
       {".func callee(.reg .v2 .u32 arg); .entry kernel(){.reg .v2 .u32 V;call "
        "callee,(V);}",
        ".func (.param .u16 r) callee(.param .u16 arg); .entry kernel(){.reg "
        ".v2 .u16 V;call (V.y),callee,(V.x);}",
        ".func callee(.reg .u32 a,.reg .u32 b); .entry kernel(){.reg .v2 .u32 "
        "V;call callee,(V.x,V.r);}",
        ".func callee(.reg .u32 arg); .entry kernel(){.reg .v2 .u32 V;{.reg "
        ".v2 .u32 V;call callee,(V.y);}}"}) {
    SCOPED_TRACE(body);
    const auto ast = test_helpers::parseModule(header + body);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto owned = resolveAndValidateModule(*ast);
    ASSERT_TRUE(owned) << owned.error().front().message;
    EXPECT_TRUE(validateModule(*owned));
    EXPECT_TRUE(validateModule(*ast, *owned));
  }
  PtxSyntaxParser parser("call callee,(V.x);");
  const auto fragment = parser.parseInstruction();
  ASSERT_TRUE(fragment);
  EXPECT_FALSE(resolveCall(*fragment));
}

/** Known cache and ABI failures remain intrinsic while availability context defers. */
TEST(CallComponents, PartialContextKeepsKnownOwnerFacts) {
  for (const bool returns : {false, true}) {
    auto source = selected_call_source("u32", 2, "direct", returns, "V.x");
    source.erase(0, source.find(".func"));
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto owned = resolveModuleOnly(*ast);
    ASSERT_TRUE(owned) << owned.error().front().message;
    EXPECT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*ast, *owned, ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
    auto& value = selected_call_actual(*owned, returns);
    value.declared_type.reset();
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(
        validateModule(*ast, *owned, ModuleValidationPolicy::AvailableContext));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

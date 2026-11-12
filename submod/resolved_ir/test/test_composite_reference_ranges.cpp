#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/model/matrix/mma.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Locate the final token occurrence independently of retained operand metadata. */
SourceRange token_range(std::string_view source, std::string_view token) {
  const auto offset = source.rfind(token);
  EXPECT_NE(offset, std::string_view::npos);
  if (offset == std::string_view::npos)
    return {};
  SourcePos start{1, 1};
  for (size_t index = 0; index < offset; ++index) {
    if (source[index] == '\n') {
      ++start.line;
      start.column = 1;
    } else {
      ++start.column;
    }
  }
  return {start,
          {start.line, start.column + static_cast<int32_t>(token.size())}};
}

/** Return validated owned IR after source, parser, and syntax tree destruction. */
std::optional<ResolvedModule> owned_module(std::string source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = resolveAndValidateModule(*ast);
  EXPECT_TRUE(module) << (module ? "" : module.error().front().message);
  if (!module)
    return std::nullopt;
  return std::move(*module);
}

/** Change only cached type, require the declaration diagnostic, then restore it. */
void expect_mutated_reference_range(ResolvedModule& module,
                                    ResolvedRegisterRef& reg,
                                    ScalarType replacement,
                                    SourceRange expected) {
  ASSERT_TRUE(reg.symbol_id);
  const auto original = reg.declared_type;
  ASSERT_NE(original, replacement);
  reg.declared_type = replacement;
  const auto checked = validateModule(module);
  reg.declared_type = original;
  ASSERT_FALSE(checked);
  const auto diagnostic =
      std::ranges::find_if(checked.error(), [](const auto& item) {
        return item.kind ==
                   checker::CheckDiagnosticKind::ModuleSourceMismatch &&
               item.message ==
                   "Resolved register metadata disagrees with its owned "
                   "declaration.";
      });
  ASSERT_NE(diagnostic, checked.error().end());
  EXPECT_EQ(diagnostic->range, expected)
      << reg.spelling << ": expected " << expected.start.line << ':'
      << expected.start.column << '-' << expected.end.column << ", got "
      << diagnostic->range.start.line << ':' << diagnostic->range.start.column
      << '-' << diagnostic->range.end.column;
}

/** Require clean owned-module controls around a single cached-type mutation. */
void expect_reference_range(ResolvedModule& module, ResolvedRegisterRef& reg,
                            ScalarType replacement, SourceRange expected) {
  ASSERT_TRUE(validateModule(module));
  expect_mutated_reference_range(module, reg, replacement, expected);
  EXPECT_TRUE(validateModule(module));
}

/** A nonfirst vector lane retains its own token even after source release. */
TEST(CompositeReferenceRanges, RegisterVectorMemberAndMissingLocation) {
  std::string source = R"ptx(.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 tensor_map[128];
.entry kernel() {
  .reg .b64 %v<2>;
  st.global.v2.b64 [tensor_map], {%v0, %v1};
}
)ptx";
  const auto member_range = token_range(source, "%v1");
  auto module = owned_module(std::move(source));
  ASSERT_TRUE(module);
  auto* instruction = dynamic_cast<StExplicitVector*>(
      module->functions.front().body.front().get());
  ASSERT_NE(instruction, nullptr);
  ASSERT_EQ(instruction->src.value.elements.size(), 2u);
  ASSERT_TRUE(instruction->src.value.elements[1]);
  auto& member = *instruction->src.value.elements[1];
  expect_reference_range(*module, member, ScalarType::U64, member_range);
  const auto locations = instruction->src.locs;
  ASSERT_EQ(locations.size(), 2u);
  instruction->src.locs.resize(1);
  ASSERT_FALSE(validateModule(*module));
  expect_mutated_reference_range(
      *module, member, ScalarType::U64,
      module->functions.front().instruction_ranges.front());
  instruction->src.locs = locations;
  EXPECT_TRUE(validateModule(*module));
}

/** Nested coordinates and a standalone im2col pack retain separate member ranges. */
TEST(CompositeReferenceRanges, TensorCoordinateAndIm2colMembers) {
  std::string source = R"ptx(.version 9.3
.target sm_100a
.address_size 64
.global .align 64 .b8 tensor_map[128];
.entry kernel() {
  .reg .s32 %r<4>;
  .reg .u16 %u<2>;
  cp.async.bulk.prefetch.tensor.4d.L2.global.im2col [tensor_map, {%r0, %r1, %r2, %r3}], {%u0, %u1};
}
)ptx";
  const auto coordinate_range = token_range(source, "%r1");
  const auto info_range = token_range(source, "%u1");
  auto module = owned_module(std::move(source));
  ASSERT_TRUE(module);
  auto* instruction = dynamic_cast<CpAsyncBulkPrefetchTensor4dIm2col*>(
      module->functions.front().body.front().get());
  ASSERT_NE(instruction, nullptr);
  auto& tensor = instruction->tensor.value;
  ASSERT_EQ(tensor.coordinates.elements.size(), 4u);
  auto* coordinate =
      std::get_if<ResolvedRegisterRef>(&tensor.coordinates.elements[1]);
  ASSERT_NE(coordinate, nullptr);
  expect_reference_range(*module, *coordinate, ScalarType::B32,
                         coordinate_range);
  ASSERT_TRUE(instruction->im2col_info);
  auto& info = *instruction->im2col_info;
  ASSERT_EQ(info.value.elements.size(), 2u);
  auto* member = std::get_if<ResolvedRegisterRef>(&info.value.elements[1]);
  ASSERT_NE(member, nullptr);
  expect_reference_range(*module, *member, ScalarType::B16, info_range);
  const auto locations = info.locs;
  ASSERT_EQ(locations.size(), 2u);
  info.locs.resize(1);
  ASSERT_FALSE(validateModule(*module));
  expect_mutated_reference_range(
      *module, *member, ScalarType::B16,
      module->functions.front().instruction_ranges.front());
  info.locs = locations;
  EXPECT_TRUE(validateModule(*module));
}

/** Byte and thread selectors have independent locations within the second pack. */
TEST(CompositeReferenceRanges, ScaleSelectorByteAndThreadMembers) {
  std::string source = R"ptx(.version 9.3
.target sm_120a
.address_size 64
.entry kernel() {
  .reg .f32 %d<4>, %c<4>;
  .reg .b32 %a<4>, %b<2>, %sa, %sb;
  .reg .u16 %s<4>;
  mma.sync.aligned.m16n8k32.row.col.kind::mxf8f6f4.block_scale.f32.e4m3.e4m3.f32.ue8m0 {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1}, {%c0,%c1,%c2,%c3}, %sa, {%s0,%s1}, %sb, {%s2,%s3};
}
)ptx";
  const auto byte_range = token_range(source, "%s2");
  const auto thread_range = token_range(source, "%s3");
  auto module = owned_module(std::move(source));
  ASSERT_TRUE(module);
  auto* instruction = dynamic_cast<
      MmaSyncAlignedM16n8k32RowColKindMxf8f6f4BlockScaleScaleVecDefaultF32E4m3E4m3F32Ue8m0*>(
      module->functions.front().body.front().get());
  ASSERT_NE(instruction, nullptr);
  auto& selector = instruction->scale_b_selector;
  auto* byte = std::get_if<ResolvedRegisterRef>(&selector.value.byte_id);
  auto* thread = std::get_if<ResolvedRegisterRef>(&selector.value.thread_id);
  ASSERT_NE(byte, nullptr);
  ASSERT_NE(thread, nullptr);
  expect_reference_range(*module, *byte, ScalarType::B16, byte_range);
  expect_reference_range(*module, *thread, ScalarType::B16, thread_range);
  const auto locations = selector.locs;
  ASSERT_EQ(locations.size(), 2u);
  selector.locs.resize(1);
  expect_reference_range(*module, *thread, ScalarType::B16,
                         module->functions.front().instruction_ranges.front());
  selector.locs.clear();
  expect_reference_range(*module, *byte, ScalarType::B16,
                         module->functions.front().instruction_ranges.front());
  selector.locs = locations;
  EXPECT_TRUE(validateModule(*module));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

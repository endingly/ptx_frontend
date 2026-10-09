#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap one MX8 statement in a declared, target-qualified module. */
std::string mx8_source(std::string_view body,
                       std::string_view target = "sm_100a",
                       std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d;
  .reg .b32 %a;
  .reg .b32 %i;
  .reg .b32 %sa;
  .reg .b32 %sb;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse a complete module without retaining a live parser in the result. */
std::optional<syntax_ast::AstModule> parse_mx8(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Check source-to-owned-module resolution for one candidate. */
bool accepts_mx8(std::string_view source) {
  auto ast = parse_mx8(source);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Test one enum in a report without interpreting missing facts as success. */
template <typename T>
  requires std::is_enum_v<T>
bool contains(const std::vector<T>& values, T item) {
  return std::find(values.begin(), values.end(), item) != values.end();
}

/** Check both A placements, source omission, and the narrow selector gates. */
TEST(TcgenMmaMx8, SourceTargetsAndExclusions) {
  for (unsigned group : {1u, 2u}) {
    for (bool shared_a : {false, true}) {
      const std::string args = " [%d], " +
                               std::string(shared_a ? "%ad" : "[%a]") +
                               ", %bd, %i, [%sa], [%sb], !%p;";
      const std::string prefix =
          "tcgen05.mma.cta_group::" + std::to_string(group) +
          ".kind::mxf8f6f4.block_scale";
      for (auto suffix : {"", ".scale_vec::1X"}) {
        const auto instruction = prefix + suffix + args;
        SCOPED_TRACE(instruction);
        EXPECT_TRUE(accepts_mx8(mx8_source(instruction)));
      }
      EXPECT_TRUE(accepts_mx8(
          mx8_source(prefix + ".block32" + args, "sm_100f", "8.8")));
      EXPECT_TRUE(accepts_mx8(mx8_source(prefix + args, "sm_110a", "9.0")));
      EXPECT_FALSE(accepts_mx8(
          mx8_source(prefix + ".scale_vec::1X" + args, "sm_110a", "9.0")));
      EXPECT_FALSE(accepts_mx8(
          mx8_source(prefix + ".block32" + args, "sm_100a", "8.6")));
    }
  }
  for (std::string_view bad : {
           "tcgen05.mma.cta_group::1.kind::mxf8f6f4 [%d], %ad, %bd, %i, [%sa], "
           "[%sb], %p;",
           "tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale.scale_vec::2X "
           "[%d], %ad, %bd, %i, [%sa], [%sb], %p;",
           "tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale [%d], %ad, "
           "%bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale [%d], %ad, "
           "%bd, %i, [%sa], [%sb], {%a}, %p;",
           "tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale [%d], %ad, "
           "%bd, %i, [%sa], [%sb], %p, 1;",
       }) {
    SCOPED_TRACE(bad);
    EXPECT_FALSE(accepts_mx8(mx8_source(bad)));
  }
}

/** Keep width-compatible Tensor Memory addresses independent of signedness. */
TEST(TcgenMmaMx8, TensorMemoryAddressWidthCompatibility) {
  constexpr std::string_view statement =
      "tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale "
      "[%d], [%a], %bd, %i, [%sa], [%sb], %p;";
  for (std::string_view role : {"d", "a", "sa", "sb"}) {
    for (std::string_view type : {"u32", "s32", "f32", "pred", "b64"}) {
      auto source = mx8_source(statement);
      const std::string declared = ".reg .b32 %" + std::string(role) + ";";
      const std::string replacement =
          ".reg ." + std::string(type) + " %" + std::string(role) + ";";
      const auto offset = source.find(declared);
      ASSERT_NE(offset, std::string::npos);
      source.replace(offset, declared.size(), replacement);
      SCOPED_TRACE(replacement);
      EXPECT_EQ(accepts_mx8(source), type == "u32" || type == "s32");
    }
  }
}

/** Retain scale roles and source locations after the syntax tree dies. */
TEST(TcgenMmaMx8, OwnedSourceAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_mx8(
        mx8_source("tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale "
                   "[%d], %ad, %bd, %i, [%sa], [%sb], %p;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaMxf8f6f4*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  const auto view = tcgen_mma_mx8_view(*form);
  ASSERT_TRUE(view);
  EXPECT_EQ(view->group, TcgenCtaGroup::One);
  EXPECT_TRUE(view->a_shared.has_value());
  EXPECT_EQ(view->scale_selector->value, TcgenScaleVectorSize::Absent);
  EXPECT_TRUE(view->scale_selector->locs.empty());
  EXPECT_EQ(view->scale_a, &form->scale_a.value);
  EXPECT_EQ(view->scale_b, &form->scale_b.value);
  ASSERT_EQ(form->scale_a.locs.size(), 1U);
  ASSERT_EQ(form->scale_b.locs.size(), 1U);
  form->scale_vector_size.value = TcgenScaleVectorSize::Vec1X;
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->scale_vector_size.value = TcgenScaleVectorSize::Absent;
  EXPECT_TRUE(validateModule(*owned).has_value());
  auto& scale_register =
      std::get<ResolvedRegisterRef>(form->scale_a.value.value);
  const auto original_type = scale_register.declared_type;
  scale_register.declared_type = ScalarType::F32;
  EXPECT_FALSE(validateModule(*owned).has_value());
  scale_register.declared_type = original_type;
  EXPECT_TRUE(validateModule(*owned).has_value());
  form->scale_a.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
}

/** Check independent Table 46 and Table 59/60 words without live registers. */
TEST(TcgenMmaMx8, KnownOperationAndScaleRows) {
  constexpr uint32_t word = (1U << 27) | (2U << 17) | (1U << 23) | (1U << 10) |
                            (1U << 29) | (3U << 4);
  const TcgenMx8KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .scale_selector = TcgenScaleVectorSize::Absent,
      .instruction = {word, TcgenMmaKind::MxF8F6F4},
      .scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx1, 1},
      .scale_b_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::OneXN, 1},
  };
  const auto report = check_tcgen_mx8_known_operation(facts);
  EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_TRUE(report.scale_a_layout.has_value());
  EXPECT_TRUE(report.scale_b_layout.has_value());
  EXPECT_EQ(report.scale_a_layout->selector, TcgenScaleVectorSize::Vec1X);
  EXPECT_EQ(report.scale_a_layout->factor_count, 1);
  EXPECT_EQ(report.scale_b_layout->subcolumn_alignment_bytes, 1);
  EXPECT_TRUE(contains(report.missing, TcgenMx8Obligation::Target));
  auto bad = facts;
  bad.scale_b_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx1, 2};
  const auto mismatch = check_tcgen_mx8_known_operation(bad);
  EXPECT_TRUE(contains(mismatch.violations, TcgenMx8Violation::ScaleBLayout));
  EXPECT_TRUE(
      contains(mismatch.violations, TcgenMx8Violation::ScaleBAlignment));
  bad = facts;
  bad.instruction.bits &= ~(3U << 29);
  bad.scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx1, 4};
  EXPECT_FALSE(contains(check_tcgen_mx8_known_operation(bad).violations,
                        TcgenMx8Violation::ScaleAAlignment));
  bad.scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx1, 0};
  EXPECT_TRUE(contains(check_tcgen_mx8_known_operation(bad).violations,
                       TcgenMx8Violation::ScaleAAlignment));
  bad = facts;
  bad.instruction.bits =
      (word & ~(7U << 7) & ~(7U << 10)) | (5U << 7) | (3U << 10);
  bad.a_packing = TcgenMxInputPacking::TmemEightBitContainer;
  bad.b_packing = TcgenMxInputPacking::SharedPaddedSixBit;
  const auto packed = check_tcgen_mx8_known_operation(bad);
  EXPECT_EQ(packed.required_a_packing,
            TcgenMxInputPacking::TmemEightBitContainer);
  EXPECT_EQ(packed.required_b_packing, TcgenMxInputPacking::SharedPaddedSixBit);
  EXPECT_TRUE(
      contains(packed.missing, TcgenMx8Obligation::ALivePackingContents));
  bad.a_packing = TcgenMxInputPacking::SharedPaddedFourBit;
  EXPECT_TRUE(contains(check_tcgen_mx8_known_operation(bad).violations,
                       TcgenMx8Violation::APackingFact));
  bad.instruction.bits = (bad.instruction.bits & ~(7U << 7)) | (2U << 7);
  bad.a_packing = static_cast<TcgenMxInputPacking>(255);
  EXPECT_TRUE(contains(check_tcgen_mx8_known_operation(bad).violations,
                       TcgenMx8Violation::APackingFact));
  bad = facts;
  bad.scale_selector = TcgenScaleVectorSize::Vec2X;
  EXPECT_TRUE(contains(check_tcgen_mx8_known_operation(bad).violations,
                       TcgenMx8Violation::ScaleSelector));
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm110a = base::find_target_profile("sm_110a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm110a);
  bad = facts;
  bad.target = sm100a->identity;
  bad.ptx_version = checker::PtxVersion{8, 6};
  bad.scale_selector = TcgenScaleVectorSize::Vec1X;
  EXPECT_FALSE(contains(check_tcgen_mx8_known_operation(bad).violations,
                        TcgenMx8Violation::Target));
  bad.target = sm110a->identity;
  bad.ptx_version = checker::PtxVersion{9, 0};
  EXPECT_TRUE(contains(check_tcgen_mx8_known_operation(bad).violations,
                       TcgenMx8Violation::Target));
  bad.scale_selector = TcgenScaleVectorSize::Absent;
  EXPECT_FALSE(contains(check_tcgen_mx8_known_operation(bad).violations,
                        TcgenMx8Violation::Target));
  bad.target = sm100a->identity;
  bad.ptx_version = checker::PtxVersion{8, 6};
  bad.scale_selector = TcgenScaleVectorSize::Block32;
  EXPECT_TRUE(contains(check_tcgen_mx8_known_operation(bad).violations,
                       TcgenMx8Violation::Target));
  bad.ptx_version = checker::PtxVersion{8, 8};
  EXPECT_FALSE(contains(check_tcgen_mx8_known_operation(bad).violations,
                        TcgenMx8Violation::Target));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

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

/** Wrap a block-scaled MX NV source statement in a target-qualified module. */
std::string mxnv_source(std::string_view body,
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

/** Parse a complete module and transfer its independent AST ownership. */
std::optional<syntax_ast::AstModule> parse_mxnv(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Check one source candidate through full owned module validation. */
bool accepts_mxnv(std::string_view source) {
  auto ast = parse_mxnv(source);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Find one typed item without equating missing obligations with success. */
template <typename T>
  requires std::is_enum_v<T>
bool contains(const std::vector<T>& values, T item) {
  return std::find(values.begin(), values.end(), item) != values.end();
}

/** Enforce mandatory selectors and both A placements at source targets. */
TEST(TcgenMmaMxNv, SourceTargetsAndExclusions) {
  for (unsigned group : {1u, 2u}) {
    for (bool shared_a : {false, true}) {
      const std::string args = " [%d], " +
                               std::string(shared_a ? "%ad" : "[%a]") +
                               ", %bd, %i, [%sa], [%sb], !%p;";
      const std::string prefix =
          "tcgen05.mma.cta_group::" + std::to_string(group) +
          ".kind::mxf4nvf4.block_scale";
      for (std::string_view selector : {".scale_vec::2X", ".scale_vec::4X"}) {
        SCOPED_TRACE(prefix + std::string(selector) + args);
        EXPECT_TRUE(accepts_mxnv(mxnv_source(
            prefix + std::string(selector) + args, "sm_100a", "8.7")));
        EXPECT_FALSE(accepts_mxnv(mxnv_source(
            prefix + std::string(selector) + args, "sm_100a", "8.6")));
      }
      for (std::string_view selector : {".block32", ".block16"}) {
        EXPECT_TRUE(accepts_mxnv(mxnv_source(
            prefix + std::string(selector) + args, "sm_100f", "8.8")));
        EXPECT_TRUE(accepts_mxnv(mxnv_source(
            prefix + std::string(selector) + args, "sm_110f", "9.0")));
        EXPECT_TRUE(accepts_mxnv(mxnv_source(
            prefix + std::string(selector) + args, "sm_103a", "8.8")));
        EXPECT_FALSE(accepts_mxnv(mxnv_source(
            prefix + std::string(selector) + args, "sm_100a", "8.7")));
      }
      EXPECT_FALSE(accepts_mxnv(mxnv_source(prefix + args)));
      EXPECT_FALSE(accepts_mxnv(mxnv_source(prefix + ".scale_vec::1X" + args)));
      EXPECT_FALSE(accepts_mxnv(
          mxnv_source(prefix + ".scale_vec::4X" + args, "sm_100f", "8.8")));
      EXPECT_FALSE(accepts_mxnv(
          mxnv_source(prefix + ".scale_vec::4X" + args, "sm_110a", "9.0")));
      EXPECT_FALSE(accepts_mxnv(
          mxnv_source(prefix + ".block16" + args, "sm_100f", "8.7")));
      EXPECT_FALSE(accepts_mxnv(mxnv_source(
          prefix + ".block16 [%d], %ad, %bd, %i, [%sa], [%sb], {%i}, %p;")));
    }
  }
}

/** Retain written selector and addresses after the parser tree is destroyed. */
TEST(TcgenMmaMxNv, OwnedSourceAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_mxnv(mxnv_source(
        "tcgen05.mma.cta_group::1.kind::mxf4nvf4.block_scale.scale_vec::4X "
        "[%d], %ad, %bd, %i, [%sa], [%sb], %p;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaMxf4nvf4*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  const auto view = tcgen_mma_mxnv_view(*form);
  ASSERT_TRUE(view);
  EXPECT_TRUE(view->a_shared.has_value());
  EXPECT_EQ(view->scale_selector->value, TcgenScaleVectorSize::Vec4X);
  EXPECT_EQ(view->scale_selector->locs.size(), 1U);
  EXPECT_EQ(view->scale_a, &form->scale_a.value);
  EXPECT_EQ(view->scale_b, &form->scale_b.value);
  form->scale_vector_size.value = TcgenScaleVectorSize::Absent;
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->scale_vector_size.value = TcgenScaleVectorSize::Vec4X;
  EXPECT_TRUE(validateModule(*owned).has_value());
  auto& scale_register =
      std::get<ResolvedRegisterRef>(form->scale_a.value.value);
  const auto original_type = scale_register.declared_type;
  scale_register.declared_type = ScalarType::F32;
  EXPECT_FALSE(validateModule(*owned).has_value());
  scale_register.declared_type = original_type;
  form->scale_b.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
}

/** Test the independent 32-bit Tensor Memory address carrier contract. */
TEST(TcgenMmaMxNv, AddressWidths) {
  constexpr std::string_view statement =
      "tcgen05.mma.cta_group::1.kind::mxf4nvf4.block_scale.scale_vec::2X "
      "[%d], [%a], %bd, %i, [%sa], [%sb], %p;";
  for (std::string_view role : {"d", "a", "sa", "sb"}) {
    for (std::string_view type : {"u32", "s32", "f32", "pred", "b64"}) {
      auto source = mxnv_source(statement);
      const std::string declared = ".reg .b32 %" + std::string(role);
      const std::string replacement =
          ".reg ." + std::string(type) + " %" + std::string(role);
      const auto offset = source.find(declared);
      ASSERT_NE(offset, std::string::npos);
      source.replace(offset, declared.size(), replacement);
      SCOPED_TRACE(replacement);
      EXPECT_EQ(accepts_mxnv(source), type == "u32" || type == "s32");
    }
  }
}

/** Check Table 47 defined IDs, selector-specific scales and K96 obligations. */
TEST(TcgenMmaMxNv, KnownScaleRowsAndTargetIntersections) {
  constexpr uint32_t k64 = (1U << 27) | (2U << 17) | (1U << 7) | (1U << 10);
  TcgenMxNvKnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .scale_selector = TcgenScaleVectorSize::Vec4X,
      .instruction = {k64, TcgenMmaKind::MxF4NvF4},
      .scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx4, 4},
      .scale_b_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::FourXN, 4},
      .a_packing = TcgenMxInputPacking::TmemPairedFourBit,
      .b_packing = TcgenMxInputPacking::SharedPairedFourBit,
  };
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm103a = base::find_target_profile("sm_103a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm103a);
  facts.target = sm100a->identity;
  facts.ptx_version = checker::PtxVersion{8, 7};
  const auto ordinary = check_tcgen_mxnv_known_operation(facts);
  EXPECT_TRUE(ordinary.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(ordinary.supplied_facts_ok());
  EXPECT_EQ(ordinary.scale_a_factor_count, 4);
  EXPECT_FALSE(contains(ordinary.violations, TcgenMxNvViolation::Target));
  EXPECT_TRUE(
      contains(ordinary.missing, TcgenMxNvObligation::ALivePackingContents));
  auto bad = facts;
  bad.scale_selector = TcgenScaleVectorSize::Absent;
  EXPECT_TRUE(contains(check_tcgen_mxnv_known_operation(bad).violations,
                       TcgenMxNvViolation::ScaleSelector));
  bad = facts;
  bad.scale_selector = TcgenScaleVectorSize::Vec2X;
  EXPECT_TRUE(contains(check_tcgen_mxnv_known_operation(bad).violations,
                       TcgenMxNvViolation::ScaleType));
  bad = facts;
  bad.instruction.bits |= 1U << 29;
  EXPECT_FALSE(check_tcgen_mxnv_known_operation(bad)
                   .instruction_fields.defined_fields_ok());
  bad = facts;
  bad.instruction.bits |= 2U << 29;
  EXPECT_TRUE(contains(check_tcgen_mxnv_known_operation(bad).violations,
                       TcgenMxNvViolation::ScaleAId));

  constexpr uint32_t k96 = (2U << 27) | (4U << 17) | (1U << 7) | (1U << 10) |
                           (2U << 29) | (1U << 31);
  facts.group = TcgenCtaGroup::Two;
  facts.instruction.bits = k96;
  facts.scale_selector = TcgenScaleVectorSize::Block16;
  facts.scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx6, 4};
  facts.scale_b_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::SixXN, 4};
  facts.target = sm103a->identity;
  facts.ptx_version = checker::PtxVersion{8, 8};
  const auto exceptional = check_tcgen_mxnv_known_operation(facts);
  EXPECT_TRUE(exceptional.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(exceptional.supplied_facts_ok());
  EXPECT_EQ(exceptional.scale_a_factor_count, 6);
  EXPECT_FALSE(contains(exceptional.violations, TcgenMxNvViolation::Target));
  bad = facts;
  bad.scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx6, 2};
  EXPECT_TRUE(contains(check_tcgen_mxnv_known_operation(bad).violations,
                       TcgenMxNvViolation::ScaleAAlignment));
  bad = facts;
  bad.scale_selector = TcgenScaleVectorSize::Vec4X;
  const auto unresolved = check_tcgen_mxnv_known_operation(bad);
  EXPECT_EQ(unresolved.scale_a_factor_count, 4);
  EXPECT_FALSE(unresolved.scale_a_layout.has_value());
  EXPECT_TRUE(
      contains(unresolved.missing, TcgenMxNvObligation::ScaleALayoutRule));
  EXPECT_TRUE(contains(unresolved.violations, TcgenMxNvViolation::Target));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

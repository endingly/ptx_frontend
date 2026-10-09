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

/** Wrap one MX4 statement in a declared, target-qualified module. */
std::string mx4_source(std::string_view body,
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
std::optional<syntax_ast::AstModule> parse_mx4(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Check source-to-owned-module resolution for one candidate. */
bool accepts_mx4(std::string_view source) {
  auto ast = parse_mx4(source);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Test one enum in a report without interpreting missing facts as success. */
template <typename T>
  requires std::is_enum_v<T>
bool contains(const std::vector<T>& values, T item) {
  return std::find(values.begin(), values.end(), item) != values.end();
}

/** Check both A placements, source omission, and the narrow selector gates. */
TEST(TcgenMmaMx4, SourceTargetsAndExclusions) {
  for (unsigned group : {1u, 2u}) {
    for (bool shared_a : {false, true}) {
      const std::string args = " [%d], " +
                               std::string(shared_a ? "%ad" : "[%a]") +
                               ", %bd, %i, [%sa], [%sb], !%p;";
      const std::string prefix =
          "tcgen05.mma.cta_group::" + std::to_string(group) +
          ".kind::mxf4.block_scale";
      for (auto suffix : {"", ".scale_vec::2X"}) {
        const auto instruction = prefix + suffix + args;
        SCOPED_TRACE(instruction);
        EXPECT_TRUE(accepts_mx4(mx4_source(instruction)));
      }
      EXPECT_TRUE(accepts_mx4(
          mx4_source(prefix + ".block32" + args, "sm_100f", "8.8")));
      EXPECT_TRUE(accepts_mx4(mx4_source(prefix + args, "sm_110a", "9.0")));
      EXPECT_FALSE(accepts_mx4(
          mx4_source(prefix + ".scale_vec::2X" + args, "sm_110a", "9.0")));
      EXPECT_FALSE(accepts_mx4(
          mx4_source(prefix + ".block32" + args, "sm_100a", "8.6")));
    }
  }
  for (std::string_view bad : {
           "tcgen05.mma.cta_group::1.kind::mxf4 [%d], %ad, %bd, %i, [%sa], "
           "[%sb], %p;",
           "tcgen05.mma.cta_group::1.kind::mxf4.block_scale.scale_vec::1X "
           "[%d], %ad, %bd, %i, [%sa], [%sb], %p;",
           "tcgen05.mma.cta_group::1.kind::mxf4.block_scale [%d], %ad, "
           "%bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::mxf4.block_scale [%d], %ad, "
           "%bd, %i, [%sa], [%sb], {%a}, %p;",
           "tcgen05.mma.cta_group::1.kind::mxf4.block_scale [%d], %ad, "
           "%bd, %i, [%sa], [%sb], %p, 1;",
       }) {
    SCOPED_TRACE(bad);
    EXPECT_FALSE(accepts_mx4(mx4_source(bad)));
  }
}

/** Keep width-compatible Tensor Memory addresses independent of signedness. */
TEST(TcgenMmaMx4, TensorMemoryAddressWidthCompatibility) {
  constexpr std::string_view statement =
      "tcgen05.mma.cta_group::1.kind::mxf4.block_scale "
      "[%d], [%a], %bd, %i, [%sa], [%sb], %p;";
  for (std::string_view role : {"d", "a", "sa", "sb"}) {
    for (std::string_view type : {"u32", "s32", "f32", "pred", "b64"}) {
      auto source = mx4_source(statement);
      const std::string declared = ".reg .b32 %" + std::string(role) + ";";
      const std::string replacement =
          ".reg ." + std::string(type) + " %" + std::string(role) + ";";
      const auto offset = source.find(declared);
      ASSERT_NE(offset, std::string::npos);
      source.replace(offset, declared.size(), replacement);
      SCOPED_TRACE(replacement);
      EXPECT_EQ(accepts_mx4(source), type == "u32" || type == "s32");
    }
  }
}

/** Retain scale roles and source locations after the syntax tree dies. */
TEST(TcgenMmaMx4, OwnedSourceAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast =
        parse_mx4(mx4_source("tcgen05.mma.cta_group::1.kind::mxf4.block_scale "
                             "[%d], %ad, %bd, %i, [%sa], [%sb], %p;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaMxf4*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  const auto view = tcgen_mma_mx4_view(*form);
  ASSERT_TRUE(view);
  EXPECT_EQ(view->group, TcgenCtaGroup::One);
  EXPECT_TRUE(view->a_shared.has_value());
  EXPECT_EQ(view->scale_selector->value, TcgenScaleVectorSize::Absent);
  EXPECT_TRUE(view->scale_selector->locs.empty());
  EXPECT_EQ(view->scale_a, &form->scale_a.value);
  EXPECT_EQ(view->scale_b, &form->scale_b.value);
  ASSERT_EQ(form->scale_a.locs.size(), 1U);
  ASSERT_EQ(form->scale_b.locs.size(), 1U);
  form->scale_vector_size.value = TcgenScaleVectorSize::Vec2X;
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

/** Check Tables 42/47/59/60 without equating source to live descriptor bits. */
TEST(TcgenMmaMx4, KnownKAndScaleRows) {
  constexpr uint32_t k64 = (1U << 27) | (2U << 17) | (1U << 23) | (1U << 7) |
                           (1U << 10) | (2U << 29);
  TcgenMx4KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .scale_selector = TcgenScaleVectorSize::Absent,
      .instruction = {k64, TcgenMmaKind::MxF4},
      .scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx2, 2},
      .scale_b_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::TwoXN, 2},
      .a_packing = TcgenMxInputPacking::TmemPairedFourBit,
      .b_packing = TcgenMxInputPacking::SharedPairedFourBit,
  };
  const auto ordinary = check_tcgen_mx4_known_operation(facts);
  EXPECT_TRUE(ordinary.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(ordinary.supplied_facts_ok());
  ASSERT_TRUE(ordinary.scale_a_layout);
  EXPECT_EQ(ordinary.scale_a_factor_count, 2);
  EXPECT_EQ(ordinary.scale_b_factor_count, 2);
  EXPECT_EQ(ordinary.required_a_packing,
            TcgenMxInputPacking::TmemPairedFourBit);
  EXPECT_TRUE(
      contains(ordinary.missing, TcgenMx4Obligation::ALivePackingContents));
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm103a = base::find_target_profile("sm_103a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm103a);
  facts.target = sm100a->identity;
  facts.ptx_version = checker::PtxVersion{8, 6};
  EXPECT_FALSE(contains(check_tcgen_mx4_known_operation(facts).violations,
                        TcgenMx4Violation::Target));
  facts.scale_selector = TcgenScaleVectorSize::Vec2X;
  EXPECT_FALSE(contains(check_tcgen_mx4_known_operation(facts).violations,
                        TcgenMx4Violation::Target));
  facts.scale_selector = TcgenScaleVectorSize::Block32;
  EXPECT_TRUE(contains(check_tcgen_mx4_known_operation(facts).violations,
                       TcgenMx4Violation::Target));

  constexpr uint32_t k96 = (2U << 27) | (4U << 17) | (1U << 23) | (1U << 7) |
                           (1U << 10) | (2U << 29) | (1U << 31);
  facts.group = TcgenCtaGroup::Two;
  facts.instruction.bits = k96;
  facts.scale_selector = TcgenScaleVectorSize::Absent;
  facts.scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx3, 4};
  facts.scale_b_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::ThreeXN, 4};
  facts.target = sm103a->identity;
  facts.ptx_version = checker::PtxVersion{8, 8};
  const auto exceptional = check_tcgen_mx4_known_operation(facts);
  EXPECT_TRUE(exceptional.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(exceptional.supplied_facts_ok());
  EXPECT_EQ(exceptional.scale_a_factor_count, 3);
  EXPECT_EQ(exceptional.scale_b_factor_count, 3);
  EXPECT_FALSE(contains(exceptional.violations, TcgenMx4Violation::Target));
  EXPECT_FALSE(
      contains(exceptional.violations, TcgenMx4Violation::ScaleAAlignment));
  auto bad = facts;
  bad.scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx3, 8};
  EXPECT_FALSE(contains(check_tcgen_mx4_known_operation(bad).violations,
                        TcgenMx4Violation::ScaleAAlignment));
  bad.scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx3, 2};
  EXPECT_TRUE(contains(check_tcgen_mx4_known_operation(bad).violations,
                       TcgenMx4Violation::ScaleAAlignment));
  bad.scale_a_facts = TcgenMxScaleRoleFacts{TcgenMxScaleLayoutId::Mx3, 0};
  EXPECT_TRUE(contains(check_tcgen_mx4_known_operation(bad).violations,
                       TcgenMx4Violation::ScaleAAlignment));
  bad = facts;
  bad.instruction.bits = (k96 & ~(3U << 29)) | (1U << 29);
  EXPECT_FALSE(check_tcgen_mx4_known_operation(bad)
                   .instruction_fields.defined_fields_ok());
  bad = facts;
  bad.scale_selector = TcgenScaleVectorSize::Vec2X;
  const auto unresolved = check_tcgen_mx4_known_operation(bad);
  EXPECT_EQ(unresolved.scale_a_factor_count, 2);
  EXPECT_FALSE(unresolved.scale_a_layout.has_value());
  EXPECT_TRUE(
      contains(unresolved.missing, TcgenMx4Obligation::ScaleALayoutRule));
  EXPECT_TRUE(contains(unresolved.violations, TcgenMx4Violation::Target));
  bad = facts;
  bad.target = sm100a->identity;
  EXPECT_TRUE(contains(check_tcgen_mx4_known_operation(bad).violations,
                       TcgenMx4Violation::Target));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

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
  facts.a_lane_half = 0;
  facts.d_lane_half = 0;
  EXPECT_EQ(check_tcgen_mxnv_known_operation(facts).path_layout, 'A');
  facts.a_lane_half = 16;
  EXPECT_TRUE(contains(check_tcgen_mxnv_known_operation(facts).violations,
                       TcgenMxNvViolation::ALaneHalf));
  facts.a_lane_half = 0;
  facts.d_lane_half = 16;
  EXPECT_TRUE(contains(check_tcgen_mxnv_known_operation(facts).violations,
                       TcgenMxNvViolation::DLaneHalf));
  facts.d_lane_half = 0;
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

/** Known NV K96 block32/block16 rows share field domains but not factor layouts. */
TcgenMxNvKnownFacts mxnv_k96_facts(bool block16) {
  const auto target = base::find_target_profile("sm_103a");
  EXPECT_TRUE(target);
  TcgenMxNvKnownFacts facts{
      .group = TcgenCtaGroup::Two,
      .a_in_tmem = true,
      .scale_selector = block16 ? TcgenScaleVectorSize::Block16
                                : TcgenScaleVectorSize::Block32,
      .instruction = {(2U << 27) | (4U << 17) | (block16 ? 0U : 1U << 23) |
                          (1U << 7) | (1U << 10) | (1U << 31),
                      TcgenMmaKind::MxF4NvF4},
      .scale_a_facts =
          TcgenMxScaleRoleFacts{
              block16 ? TcgenMxScaleLayoutId::Mx6 : TcgenMxScaleLayoutId::Mx3,
              4},
      .scale_b_facts =
          TcgenMxScaleRoleFacts{block16 ? TcgenMxScaleLayoutId::SixXN
                                        : TcgenMxScaleLayoutId::ThreeXN,
                                4},
      .a_packing = TcgenMxInputPacking::TmemPairedFourBit,
      .b_packing = TcgenMxInputPacking::SharedPairedFourBit,
      .a_lane_half = 0,
      .d_lane_half = 0,
      .ptx_version = checker::PtxVersion{9, 3}};
  if (target)
    facts.target = target->identity;
  return facts;
}

/** Encoded IDs, role layouts, and byte guarantees are three independent facts. */
TEST(TcgenMmaMxNv, K96EncodedIdsAndIndependentLayoutFacts) {
  for (const bool block16 : {false, true}) {
    const auto baseline = mxnv_k96_facts(block16);
    constexpr uint32_t a_mask = 3U << 29;
    constexpr uint32_t b_mask = 3U << 4;
    for (const bool a : {true, false}) {
      for (const uint32_t code : {0U, 1U, 2U, 3U})
        for (const uint8_t alignment : {4, 8}) {
          auto facts = baseline;
          facts.instruction.bits =
              (baseline.instruction.bits & ~(a_mask | b_mask)) |
              (code << (a ? 29 : 4));
          facts.scale_a_facts->subcolumn_alignment_bytes = alignment;
          facts.scale_b_facts->subcolumn_alignment_bytes = alignment;
          const auto report = check_tcgen_mxnv_known_operation(facts);
          EXPECT_EQ(report.instruction_fields.defined_fields_ok(),
                    code == 0 || code == 2);
          EXPECT_EQ(report.supplied_facts_ok(), code == 0 || code == 2);
          EXPECT_EQ(report.scale_a_factor_count, block16 ? 6 : 3);
          EXPECT_EQ(report.scale_b_factor_count, block16 ? 6 : 3);
          ASSERT_TRUE(report.scale_a_layout);
          EXPECT_EQ(report.scale_a_layout->id_alignment_policy,
                    TcgenScaleIdAlignmentPolicy::LayoutDefinedPlacement);
          EXPECT_FALSE(
              contains(report.violations, TcgenMxNvViolation::ScaleAAlignment));
          EXPECT_FALSE(
              contains(report.violations, TcgenMxNvViolation::ScaleBAlignment));
          if (code == 1 || code == 3) {
            ASSERT_EQ(report.instruction_fields.violations.size(), 1u);
            EXPECT_EQ(report.instruction_fields.violations.front().kind,
                      TcgenDescriptorViolation::ScaleId);
            EXPECT_EQ(report.instruction_fields.violations.front().field_mask,
                      a ? a_mask : b_mask);
          }
          EXPECT_TRUE(contains(report.missing,
                               TcgenMxNvObligation::ALivePackingContents));
          EXPECT_TRUE(contains(report.missing,
                               TcgenMxNvObligation::BLivePackingContents));
        }
      for (const uint8_t alignment : {0, 2, 3}) {
        auto facts = baseline;
        facts.instruction.bits |= (2U << 29) | (2U << 4);
        (a ? facts.scale_a_facts : facts.scale_b_facts)
            ->subcolumn_alignment_bytes = alignment;
        const auto report = check_tcgen_mxnv_known_operation(facts);
        EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
        ASSERT_EQ(report.violations.size(), 1u);
        EXPECT_EQ(report.violations.front(),
                  a ? TcgenMxNvViolation::ScaleAAlignment
                    : TcgenMxNvViolation::ScaleBAlignment);
      }
      auto wrong = baseline;
      (a ? wrong.scale_a_facts : wrong.scale_b_facts)->layout_id =
          a ? TcgenMxScaleLayoutId::Mx2 : TcgenMxScaleLayoutId::TwoXN;
      const auto layout = check_tcgen_mxnv_known_operation(wrong);
      EXPECT_TRUE(layout.instruction_fields.defined_fields_ok());
      ASSERT_EQ(layout.violations.size(), 1u);
      EXPECT_EQ(layout.violations.front(),
                a ? TcgenMxNvViolation::ScaleALayout
                  : TcgenMxNvViolation::ScaleBLayout);
      for (unsigned missing = 0; missing < 3; ++missing) {
        auto partial = baseline;
        auto& role = a ? partial.scale_a_facts : partial.scale_b_facts;
        if (missing == 0)
          role.reset();
        else if (missing == 1)
          role->layout_id.reset();
        else
          role->subcolumn_alignment_bytes.reset();
        const auto report = check_tcgen_mxnv_known_operation(partial);
        EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
        EXPECT_TRUE(report.supplied_facts_ok());
        EXPECT_TRUE(
            contains(report.missing, a ? TcgenMxNvObligation::ScaleALayout
                                       : TcgenMxNvObligation::ScaleBLayout));
        EXPECT_FALSE(contains(report.violations,
                              a ? TcgenMxNvViolation::ScaleAAlignment
                                : TcgenMxNvViolation::ScaleBAlignment));
      }
    }
    for (const uint8_t alignment : {4, 8, 16}) {
      auto both = baseline;
      both.instruction.bits |= (2U << 29) | (2U << 4);
      both.scale_a_facts->subcolumn_alignment_bytes = alignment;
      both.scale_b_facts->subcolumn_alignment_bytes = alignment;
      EXPECT_TRUE(check_tcgen_mxnv_known_operation(both).supplied_facts_ok());
    }
    for (const auto selector :
         {TcgenScaleVectorSize::Vec2X, TcgenScaleVectorSize::Vec4X}) {
      auto vector = baseline;
      vector.instruction.bits |= 1U << 23;
      vector.scale_selector = selector;
      const auto unresolved = check_tcgen_mxnv_known_operation(vector);
      EXPECT_TRUE(unresolved.instruction_fields.defined_fields_ok());
      EXPECT_TRUE(contains(unresolved.violations, TcgenMxNvViolation::Target));
      EXPECT_FALSE(
          contains(unresolved.violations, TcgenMxNvViolation::ScaleType));
      EXPECT_TRUE(
          contains(unresolved.missing, TcgenMxNvObligation::ScaleALayoutRule));
      EXPECT_TRUE(
          contains(unresolved.missing, TcgenMxNvObligation::ScaleBLayoutRule));
      EXPECT_FALSE(unresolved.scale_a_layout);
      EXPECT_EQ(unresolved.scale_a_factor_count,
                selector == TcgenScaleVectorSize::Vec2X ? 2 : 4);
    }
    auto wrong_target = baseline;
    wrong_target.target = base::find_target_profile("sm_100a")->identity;
    EXPECT_TRUE(
        contains(check_tcgen_mxnv_known_operation(wrong_target).violations,
                 TcgenMxNvViolation::Target));
    auto wrong_group = baseline;
    wrong_group.group = TcgenCtaGroup::One;
    EXPECT_TRUE(
        contains(check_tcgen_mxnv_known_operation(wrong_group).violations,
                 TcgenMxNvViolation::Shape));
    auto wrong_m = baseline;
    wrong_m.instruction.bits =
        (baseline.instruction.bits & ~(3U << 27)) | (1U << 27);
    EXPECT_TRUE(contains(check_tcgen_mxnv_known_operation(wrong_m).violations,
                         TcgenMxNvViolation::Shape));
    auto wrong_kind = baseline;
    wrong_kind.instruction.kind = TcgenMmaKind::MxF8F6F4;
    EXPECT_TRUE(
        contains(check_tcgen_mxnv_known_operation(wrong_kind).violations,
                 TcgenMxNvViolation::Kind));
  }
}

/** Source idesc stays opaque while copied caller words are checked independently. */
TEST(TcgenMmaMxNv, K96SourceAndCallerWordStayIndependent) {
  for (const bool block16 : {false, true}) {
    const std::string statement =
        "tcgen05.mma.cta_group::2.kind::mxf4nvf4.block_scale" +
        std::string(block16 ? ".block16 " : ".block32 ") +
        "[%d], [%a], %bd, %i, [%sa], [%sb], %p;";
    const std::string initialize =
        ".reg .b64 %wide; .reg .b32 %alt;"
        "mov.b32 %d,0;mov.b32 %a,0;mov.b32 %i,0;mov.b32 %sa,0;mov.b32 %sb,0;"
        "mov.b64 %ad,0;mov.b64 %bd,0;mov.pred %p,1;mov.b64 %wide,0;mov.b32 "
        "%alt,0;";
    std::optional<ResolvedModule> owned;
    {
      const auto source = mxnv_source(initialize + statement, "sm_103a");
      auto ast = parse_mxnv(source);
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result) << result.error().front().message;
      owned = std::move(*result);
    }
    auto& body = owned->functions.front().body;
    auto* form = dynamic_cast<Tcgen05MmaMxf4nvf4*>(body[body.size() - 2].get());
    ASSERT_NE(form, nullptr);
    ASSERT_TRUE(tcgen_mma_mxnv_view(*form));
    EXPECT_EQ(tcgen_mma_mxnv_view(*form)->instruction.source,
              &form->idesc.value);
    EXPECT_EQ(form->idesc.value.declared_type, ScalarType::B32);
    EXPECT_EQ(form->cta_group.value, TcgenCtaGroup::Two);
    auto facts = mxnv_k96_facts(block16);
    EXPECT_TRUE(check_tcgen_mxnv_known_operation(facts).supplied_facts_ok());
    facts.instruction.bits |= 1U << 29;
    EXPECT_FALSE(check_tcgen_mxnv_known_operation(facts)
                     .instruction_fields.defined_fields_ok());
    EXPECT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
    facts = mxnv_k96_facts(block16);
    auto copied_facts = facts;
    copied_facts.instruction.bits |= (2U << 29) | (2U << 4);
    EXPECT_TRUE(
        check_tcgen_mxnv_known_operation(copied_facts).supplied_facts_ok());
    const auto good_ast =
        parse_mxnv(mxnv_source(initialize + statement, "sm_103a"));
    ASSERT_TRUE(good_ast);
    const auto saved = form->idesc;
    form->idesc.value = dynamic_cast<MovScalar&>(*body[8]).dst_register->value;
    const auto bad_role = validateModule(
        *good_ast, *owned, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(bad_role);
    EXPECT_TRUE(std::ranges::any_of(bad_role.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::OperandTypeMismatch;
    }));
    form->idesc = saved;
    form->idesc.value = dynamic_cast<MovScalar&>(*body[9]).dst_register->value;
    EXPECT_TRUE(validateModule(*good_ast, *owned,
                               ModuleValidationPolicy::RequireCompleteContext));
    form->idesc = saved;
    form->idesc.value.symbol_id = binding::SymbolId{.value = 999999u};
    const auto bad_identity =
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(bad_identity);
    EXPECT_TRUE(std::ranges::any_of(bad_identity.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
    form->idesc = saved;
    auto changed = initialize + statement;
    changed.replace(changed.find("cta_group::2"), 12, "cta_group::1");
    const auto replacement = parse_mxnv(mxnv_source(changed, "sm_103a"));
    ASSERT_TRUE(replacement);
    const auto mismatch = validateModule(
        *replacement, *owned, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
    ResolvedModule copied = *owned;
    ResolvedModule assigned;
    assigned = copied;
    ResolvedModule moved = std::move(assigned);
    EXPECT_TRUE(
        validateModule(moved, ModuleValidationPolicy::RequireCompleteContext));
    auto& copied_form = dynamic_cast<Tcgen05MmaMxf4nvf4&>(
        *moved.functions.front().body[body.size() - 2]);
    const auto view = tcgen_mma_mxnv_view(copied_form);
    ASSERT_TRUE(view);
    EXPECT_EQ(view->instruction.source, &copied_form.idesc.value);
    EXPECT_NE(view->instruction.source, &form->idesc.value);
    auto clone = copied_form.clone();
    EXPECT_EQ(dynamic_cast<Tcgen05MmaMxf4nvf4&>(*clone).idesc, form->idesc);
    ResolvedFunction function_copy = owned->functions.front();
    ResolvedFunction function_assigned;
    function_assigned = function_copy;
    EXPECT_EQ(dynamic_cast<Tcgen05MmaMxf4nvf4&>(
                  *function_assigned.body[body.size() - 2])
                  .scale_a,
              form->scale_a);
    EXPECT_TRUE(
        check_tcgen_mxnv_known_operation(copied_facts).supplied_facts_ok());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

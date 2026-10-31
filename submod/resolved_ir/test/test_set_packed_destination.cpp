#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/set.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Emit each packed integer-result family while keeping source and predicate legal. */
std::string destination_source(unsigned variant, std::string_view dtype,
                               std::string_view declaration,
                               std::string_view operand = "dst") {
  const bool boolean = variant % 2 != 0;
  const bool bfloat = variant >= 2;
  return ".version 9.3\n.target sm_90\n.entry kernel() { .reg ." +
         std::string(declaration) +
         " dst; .reg .b32 a,b,bits; .reg .pred p; "
         ".reg .s32 signed_value; .reg .u32 unsigned_value; .reg .f32 "
         "float_value; "
         ".reg .f16x2 packed; .reg .b64 wide; .reg .v2 .b32 V<2>; "
         ".reg .v4 .s32 S; .reg .v2 .u32 U; set.eq" +
         std::string(boolean ? ".and" : "") + (bfloat ? "" : ".ftz") + "." +
         std::string(dtype) + (bfloat ? ".bf16x2 " : ".f16x2 ") +
         std::string(operand) + ",a,b" + (boolean ? ",!p;" : ";") + " }";
}
/** Resolve a draft after the source buffer and syntax AST have been destroyed. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> destination_draft(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}

TEST(SetPackedDestination, RejectsFloatingKindsForEveryIntegerDtypeBranch) {
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  for (unsigned variant = 0; variant < 4; ++variant) {
    for (const auto dtype : {"u32", "s32"}) {
      SCOPED_TRACE(variant);
      SCOPED_TRACE(dtype);
      const auto baseline =
          test_helpers::parseModule(destination_source(variant, dtype, "b32"));
      ASSERT_TRUE(baseline && baseline.diagnostics.empty());
      ASSERT_TRUE(resolveAndValidateModule(*baseline));
      for (const auto type : {"f32", "f16x2"}) {
        SCOPED_TRACE(type);
        const auto ast =
            test_helpers::parseModule(destination_source(variant, dtype, type));
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        auto draft = resolveModuleOnly(*ast);
        ASSERT_TRUE(draft);
        const auto direct =
            draft->functions.front().body.front()->check(context);
        ASSERT_FALSE(direct);
        ASSERT_EQ(direct.error().size(), 1u);
        EXPECT_EQ(direct.error().front().kind,
                  checker::CheckDiagnosticKind::OperandTypeMismatch);
        EXPECT_FALSE(validateModule(*ast, *draft));
        EXPECT_FALSE(resolveAndValidateModule(*ast));
        auto owned =
            destination_draft(destination_source(variant, dtype, type));
        ASSERT_TRUE(owned);
        auto copied = *owned;
        copied.functions.front().body.front() =
            copied.functions.front().body.front()->clone();
        EXPECT_FALSE(validateModule(copied));
      }
    }
  }
}
/** Borrow the selected concrete fields; the instruction owns all referenced state. */
struct DestinationFields {
  /** Destination identity, declaration dtype and source ranges. */
  WithLocs<ResolvedRegisterRef>& dst;
  /** Written or coherently mutated logical U32/S32 result dtype. */
  WithLocs<ScalarType>& dtype;
};
/** Select only the four admitted integer-result packed classes. */
DestinationFields destination_fields(Instruction& instruction,
                                     unsigned variant) {
  if (variant == 0) {
    auto& value = dynamic_cast<SetHalfIntegerF16x2&>(instruction);
    return {value.dst, value.dtype};
  }
  if (variant == 1) {
    auto& value = dynamic_cast<SetHalfIntegerF16x2Boolean&>(instruction);
    return {value.dst, value.dtype};
  }
  if (variant == 2) {
    auto& value = dynamic_cast<SetHalfIntegerBf16x2&>(instruction);
    return {value.dst, value.dtype};
  }
  auto& value = dynamic_cast<SetHalfIntegerBf16x2Boolean&>(instruction);
  return {value.dst, value.dtype};
}
/** Return a strictly checked module after source/parser/AST lifetime has ended. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> destination_checked(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}

TEST(SetPackedDestination, RetainsLogicalDtypeAndClosedIntegerBitCarriers) {
  const std::array<std::string_view, 4> names{
      "HalfIntegerF16x2", "HalfIntegerF16x2Boolean", "HalfIntegerBf16x2",
      "HalfIntegerBf16x2Boolean"};
  for (unsigned variant = 0; variant < 4; ++variant) {
    for (const auto dtype : {"u32", "s32"}) {
      for (const auto type : {"b32", "s32", "u32"}) {
        SCOPED_TRACE(variant);
        SCOPED_TRACE(dtype);
        SCOPED_TRACE(type);
        auto owned =
            destination_checked(destination_source(variant, dtype, type));
        ASSERT_TRUE(owned);
        ResolvedModule copied(*owned);
        ResolvedModule assigned;
        assigned = copied;
        ResolvedFunction function(copied.functions.front());
        ResolvedFunction function_assigned;
        function_assigned = function;
        ASSERT_NE(function.body.front().get(),
                  copied.functions.front().body.front().get());
        ASSERT_NE(function_assigned.body.front().get(),
                  function.body.front().get());
        assigned.functions.front() = function_assigned;
        assigned.functions.front().body.front() =
            assigned.functions.front().body.front()->clone();
        auto fields = destination_fields(
            *assigned.functions.front().body.front(), variant);
        EXPECT_EQ(fields.dtype.value, std::string_view(dtype) == "u32"
                                          ? ScalarType::U32
                                          : ScalarType::S32);
        EXPECT_EQ(
            fields.dst.value.declared_type,
            base::find_scalar_type_metadata("." + std::string(type))->type);
        EXPECT_TRUE(validateModule(assigned));
      }
    }
    const auto& descriptor = set_resolved_descriptor();
    const auto found =
        std::ranges::find_if(descriptor.variants, [&](const auto& candidate) {
          return candidate.variant_name == names[variant];
        });
    ASSERT_NE(found, descriptor.variants.end());
    ASSERT_EQ(found->operand_layouts.size(), 1u);
    const auto& bindings = found->operand_layouts.front().bindings;
    const auto destination = std::ranges::find_if(
        bindings,
        [](const auto& binding) { return binding.target_field_id == "dst"; });
    ASSERT_NE(destination, bindings.end());
    EXPECT_EQ(destination->type_expression.kind,
              checker::OperandTypeExpressionKind::ModifierField);
    EXPECT_EQ(destination->type_expression.modifier_field_id, "dtype");
    EXPECT_EQ(destination->type_expression.fixed_scalar_type,
              ScalarType::Invalid);
    EXPECT_EQ(destination->register_width_policy,
              base::ScalarTypeSizePolicy::SameWidth);
  }
}

TEST(SetPackedDestination, RejectsWidthsWholeVectorsAndIndependentBadSources) {
  for (unsigned variant = 0; variant < 4; ++variant) {
    for (const auto dtype : {"u32", "s32"}) {
      for (const auto type :
           {"b16", "u16", "b64", "b128", "s64", "u64", "pred", "bf16x2"}) {
        SCOPED_TRACE(variant);
        SCOPED_TRACE(dtype);
        SCOPED_TRACE(type);
        const auto ast =
            test_helpers::parseModule(destination_source(variant, dtype, type));
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        EXPECT_FALSE(resolveAndValidateModule(*ast));
        auto draft =
            destination_draft(destination_source(variant, dtype, type));
        if (draft)
          EXPECT_FALSE(validateModule(*draft));
      }
      for (const auto operand : {"V1", "S", "{bits,unsigned_value}"}) {
        const auto ast = test_helpers::parseModule(
            destination_source(variant, dtype, "b32", operand));
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        EXPECT_FALSE(resolveAndValidateModule(*ast));
      }
    }
    auto source = destination_source(variant, "u32", "b32");
    const auto operands = source.find("dst,a,b");
    ASSERT_NE(operands, std::string::npos);
    source.replace(operands, std::string("dst,a,b").size(), "dst,packed,b");
    auto ast = test_helpers::parseModule(source);
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_EQ(resolveAndValidateModule(*ast).has_value(), variant < 2);
    for (const auto operand : {"float_value", "wide", "0"}) {
      source = destination_source(variant, "u32", "b32");
      const auto position = source.find("dst,a,b");
      source.replace(position, std::string("dst,a,b").size(),
                     "dst," + std::string(operand) + ",b");
      ast = test_helpers::parseModule(source);
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    if (variant % 2) {
      source = destination_source(variant, "u32", "b32");
      const auto predicate = source.find(",!p;");
      ASSERT_NE(predicate, std::string::npos);
      source.replace(predicate, 4, ",a;");
      ast = test_helpers::parseModule(source);
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
}

TEST(SetPackedDestination, OwnsComponentsCompactMembersAndCoherentRetargets) {
  for (unsigned variant = 0; variant < 4; ++variant) {
    for (const auto component : {"V1.y", "S.z", "U.y"}) {
      auto owned = destination_checked(
          destination_source(variant, "u32", "b32", component));
      ASSERT_TRUE(owned);
      auto copied = *owned;
      copied.functions.front().body.front() =
          copied.functions.front().body.front()->clone();
      auto fields =
          destination_fields(*copied.functions.front().body.front(), variant);
      auto& ref = fields.dst.value;
      ASSERT_TRUE(ref.symbol_id);
      ASSERT_TRUE(ref.component);
      EXPECT_FALSE(ref.vector_width);
      EXPECT_EQ(ref.component->origin,
                RegisterComponentOrigin::ExplicitSelector);
      EXPECT_EQ(ref.component->lane,
                std::string_view(component) == "S.z" ? 2 : 1);
      EXPECT_EQ(ref.parameterized_index, std::string_view(component) == "V1.y"
                                             ? std::optional<uint32_t>{1}
                                             : std::nullopt);
      EXPECT_EQ(copied.symbols.symbol(*ref.symbol_id).name,
                std::string_view(component) == "V1.y"  ? "V"
                : std::string_view(component) == "S.z" ? "S"
                                                       : "U");
      ASSERT_FALSE(fields.dst.locs.empty());
      EXPECT_EQ(ref.component->range, fields.dst.locs.front());
      EXPECT_TRUE(validateModule(copied));
      const auto saved = ref;
      ref.component->lane = ref.component->declaration_width;
      EXPECT_FALSE(validateModule(copied));
      ref = saved;
      ++ref.component->range.end.column;
      EXPECT_FALSE(validateModule(copied));
      ref = saved;
      ref.parameterized_index = 2;
      EXPECT_FALSE(validateModule(copied));
      ref = saved;
      ref.component.reset();
      ref.vector_width = std::string_view(component) == "S.z" ? 4 : 2;
      ref.spelling =
          std::string(component).substr(0, std::string(component).find('.'));
      EXPECT_FALSE(validateModule(copied));
    }
    const auto source = destination_source(variant, "u32", "b32");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto owned = destination_checked(source);
    ASSERT_TRUE(owned);
    for (const auto name : {"bits", "signed_value", "unsigned_value",
                            "float_value", "packed", "wide"}) {
      auto retarget = *owned;
      auto fields =
          destination_fields(*retarget.functions.front().body.front(), variant);
      const auto symbol = retarget.symbols.lookup(
          retarget.functions.front().declaration_scope, name);
      ASSERT_TRUE(symbol);
      const auto type =
          std::string_view(name) == "bits"             ? ScalarType::B32
          : std::string_view(name) == "signed_value"   ? ScalarType::S32
          : std::string_view(name) == "unsigned_value" ? ScalarType::U32
          : std::string_view(name) == "float_value"    ? ScalarType::F32
          : std::string_view(name) == "packed"         ? ScalarType::F16x2
                                                       : ScalarType::B64;
      fields.dst.value =
          ResolvedRegisterRef{.spelling = name,
                              .register_class = ResolvedRegisterClass::General,
                              .symbol_id = symbol->symbol,
                              .declared_type = type};
      const bool legal = type == ScalarType::B32 || type == ScalarType::S32 ||
                         type == ScalarType::U32;
      for (const auto dtype : {ScalarType::U32, ScalarType::S32}) {
        fields.dtype.value = dtype;
        const auto checked = validateModule(retarget);
        EXPECT_EQ(checked.has_value(), legal);
        if (!legal) {
          ASSERT_FALSE(checked);
          EXPECT_TRUE(
              std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
                return diagnostic.kind ==
                       checker::CheckDiagnosticKind::OperandTypeMismatch;
              }));
        }
        EXPECT_EQ(validateModule(*ast, retarget).has_value(), legal);
      }
      if (legal) {
        fields.dst.value.declared_type =
            type == ScalarType::B32 ? ScalarType::U32 : ScalarType::B32;
        EXPECT_FALSE(validateModule(retarget));
      }
    }
    auto replacement = test_helpers::parseModule(
        destination_source(variant, "u32", "b32", "bits"));
    ASSERT_TRUE(replacement && replacement.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*replacement));
    const auto stale = validateModule(*replacement, *owned);
    ASSERT_FALSE(stale);
    ASSERT_EQ(stale.error().size(), 1u);
    EXPECT_EQ(stale.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

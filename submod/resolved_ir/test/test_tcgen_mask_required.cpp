#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <string>
#include <string_view>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** One existing non-WS unscaled MMA family, with no source-permission changes. */
struct RequiredMaskCase {
  /** Canonical identity for finite model/runtime joins. */
  std::string_view name;
  /** Written kind suffix and its existing exact class. */
  std::string_view kind;
  /** Whether the independent sparse metadata address precedes idesc. */
  bool sparse;
  /** Whether optional D-scale source syntax exists for this kind. */
  bool scale_eligible;
  /** Existing generated alternative identity. */
  InstructionKind identity;
};
constexpr std::array required_mask_cases{
    RequiredMaskCase{"tcgen05_mma_f16", "f16", false, true,
                     Tcgen05MmaF16::kind},
    RequiredMaskCase{"tcgen05_mma_tf32", "tf32", false, true,
                     Tcgen05MmaTf32::kind},
    RequiredMaskCase{"tcgen05_mma_i8", "i8", false, false, Tcgen05MmaI8::kind},
    RequiredMaskCase{"tcgen05_mma_f8f6f4", "f8f6f4", false, false,
                     Tcgen05MmaF8f6f4::kind},
    RequiredMaskCase{"tcgen05_mma_sp_f16", "f16", true, true,
                     Tcgen05MmaSpF16::kind},
    RequiredMaskCase{"tcgen05_mma_sp_tf32", "tf32", true, true,
                     Tcgen05MmaSpTf32::kind},
    RequiredMaskCase{"tcgen05_mma_sp_f8f6f4", "f8f6f4", true, false,
                     Tcgen05MmaSpF8f6f4::kind},
    RequiredMaskCase{"tcgen05_mma_sp_i8", "i8", true, false,
                     Tcgen05MmaSpI8::kind}};
/** Build only the source role spellings; register and descriptor contents stay opaque. */
std::string required_mask_instruction(const RequiredMaskCase& item,
                                      unsigned group, bool shared, bool scale,
                                      bool mask = true, unsigned count = 0) {
  std::string text = "tcgen05.mma" + std::string(item.sparse ? ".sp" : "") +
                     ".cta_group::" + std::to_string(group) +
                     ".kind::" + std::string(item.kind) + " [d]," +
                     (shared ? std::string("ad") : "[a]") + ",bd,";
  if (item.sparse)
    text += "[sp],";
  text += "i,";
  if (mask) {
    text += "{";
    const unsigned lanes = count ? count : (group == 1 ? 4 : 8);
    for (unsigned index = 0; index < lanes; ++index)
      text += (index ? ",m" : "m") + std::to_string(index);
    text += "},";
  }
  return text + "p" + (scale ? ",15" : "") + ";";
}
/** A complete initialized scalar-carrier module isolates mask syntax from other roles. */
std::string required_mask_module(std::string_view instruction) {
  std::string text =
      ".version 9.3\n.target sm_100a\n.address_size 64\n.entry kernel(){"
      ".reg .b32 d,a,i,sp,m<9>; .reg .b64 ad,bd; .reg .pred p;"
      "mov.b32 d,0;mov.b32 a,0;mov.b32 i,0;mov.b32 sp,0;"
      "mov.b64 ad,0;mov.b64 bd,0;mov.pred p,1;";
  for (unsigned index = 0; index < 9; ++index)
    text += "mov.b32 m" + std::to_string(index) + ",0;";
  return text + std::string(instruction) + "}";
}
/** Construct an exact qualified context with copied source diagnostic range. */
checker::Context required_mask_context(SourceRange range = {}) {
  const auto profile = base::find_target_profile("sm_100a");
  EXPECT_TRUE(profile);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
      .instruction_range = range};
}
/** Resolve only after source, parser and AST owners have actually left scope. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_required_mask(
    std::string text) {
  const auto ast = test_helpers::parseModule(text);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}
/** Borrow required member storage and its current layout identity from one live form. */
struct RequiredMaskFields {
  /** Mandatory mask object and source ranges, never an optional slot. */
  WithLocs<ResolvedRegisterVector>& mask;
  /** Owned qualifier controls four versus eight scalar entries. */
  WithLocs<TcgenCtaGroup>& group;
  /** Actual new ordinal; no historical ordinal inference is performed. */
  ResolvedOperandLayoutTag& layout;
};
/** Select the eight existing concrete member types without a new public adapter. */
RequiredMaskFields required_mask_fields(Instruction& instruction) {
  switch (instruction.instruction_kind()) {
    case Tcgen05MmaF16::kind: {
      auto& form = dynamic_cast<Tcgen05MmaF16&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
    }
    case Tcgen05MmaTf32::kind: {
      auto& form = dynamic_cast<Tcgen05MmaTf32&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
    }
    case Tcgen05MmaI8::kind: {
      auto& form = dynamic_cast<Tcgen05MmaI8&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
    }
    case Tcgen05MmaF8f6f4::kind: {
      auto& form = dynamic_cast<Tcgen05MmaF8f6f4&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
    }
    case Tcgen05MmaSpF16::kind: {
      auto& form = dynamic_cast<Tcgen05MmaSpF16&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
    }
    case Tcgen05MmaSpTf32::kind: {
      auto& form = dynamic_cast<Tcgen05MmaSpTf32&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
    }
    case Tcgen05MmaSpF8f6f4::kind: {
      auto& form = dynamic_cast<Tcgen05MmaSpF8f6f4&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
    }
    case Tcgen05MmaSpI8::kind: {
      auto& form = dynamic_cast<Tcgen05MmaSpI8&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
    }
    default:
      ADD_FAILURE() << "Unexpected unscaled MMA kind";
      auto& form = dynamic_cast<Tcgen05MmaF16&>(instruction);
      return {form.disable_output_lane, form.cta_group, form.operand_layout};
  }
}
/** Public borrowed views must expose the required member on every valid new ordinal. */
const ResolvedRegisterVector* borrowed_required_mask(
    const Instruction& instruction) {
  switch (instruction.instruction_kind()) {
    case Tcgen05MmaF16::kind: {
      auto v = tcgen_mma_f16_view(instruction);
      return v ? v->disable_output_lane : nullptr;
    }
    case Tcgen05MmaTf32::kind: {
      auto v = tcgen_mma_tf32_view(instruction);
      return v ? v->disable_output_lane : nullptr;
    }
    case Tcgen05MmaI8::kind: {
      auto v = tcgen_mma_i8_view(instruction);
      return v ? v->disable_output_lane : nullptr;
    }
    case Tcgen05MmaF8f6f4::kind: {
      auto v = tcgen_mma_f8f6f4_view(instruction);
      return v ? v->disable_output_lane : nullptr;
    }
    default: {
      auto v = tcgen_mma_sparse_view(instruction);
      return v ? v->disable_output_lane : nullptr;
    }
  }
}
/** Recheck required vector ownership across actual AST-dead copies and clones. */
void expect_required_mask_lifetime(const ResolvedModule& module,
                                   bool valid = true) {
  const auto& function = module.functions.front();
  const auto context =
      required_mask_context(function.instruction_ranges.back());
  EXPECT_EQ(bool(function.body.back()->check(context)), valid);
  EXPECT_EQ(bool(validateModule(module)), valid);
  ResolvedModule copied(module), assigned;
  assigned = copied;
  EXPECT_EQ(bool(validateModule(copied)), valid);
  EXPECT_EQ(bool(validateModule(assigned)), valid);
  ResolvedFunction function_copy(function), function_assigned;
  function_assigned = function_copy;
  EXPECT_NE(function_copy.body.back().get(), function.body.back().get());
  EXPECT_EQ(bool(function_assigned.body.back()->check(context)), valid);
  assigned.functions.front().body.back() = function_copy.body.back()->clone();
  EXPECT_EQ(bool(validateModule(assigned)), valid);
}
TEST(TcgenRequiredMask, RejectsOmissionBesideAllFortyEightMaskedControls) {
  size_t cells = 0;
  for (const auto& item : required_mask_cases)
    for (const unsigned group : {1u, 2u})
      for (const bool shared : {false, true})
        for (const bool scale : {false, true}) {
          if (scale && !item.scale_eligible)
            continue;
          SCOPED_TRACE(std::string(item.name) +
                       " group=" + std::to_string(group) +
                       " shared=" + std::to_string(shared) +
                       " scale=" + std::to_string(scale));
          const auto control = test_helpers::parseModule(required_mask_module(
              required_mask_instruction(item, group, shared, scale)));
          ASSERT_MODULE_PARSE_SUCCEEDS(control);
          const auto valid = resolveAndValidateModule(*control);
          ASSERT_TRUE(valid) << valid.error().front().message;
          EXPECT_EQ(valid->functions.front().body.back()->instruction_kind(),
                    item.identity);
          const auto text = required_mask_module(
              required_mask_instruction(item, group, shared, scale, false));
          const auto ast = test_helpers::parseModule(text);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          EXPECT_FALSE(resolveModuleOnly(*ast));
          EXPECT_FALSE(resolveAndValidateModule(*ast));
          std::cout << "OMITTED " << item.name << " group=" << group
                    << " shared=" << shared << " scale=" << scale
                    << " resolution-rejected; owned=N/A\n";
          ++cells;
        }
  EXPECT_EQ(cells, 48u);
}
TEST(TcgenRequiredMask, OwnsAllSurvivorsWithMandatoryMembersAndNewOrdinals) {
  size_t cells = 0;
  for (const auto& item : required_mask_cases)
    for (const unsigned group : {1u, 2u})
      for (const bool shared : {true, false})
        for (const bool scale : {false, true}) {
          if (scale && !item.scale_eligible)
            continue;
          const auto text = required_mask_module(
              required_mask_instruction(item, group, shared, scale));
          const auto ast = test_helpers::parseModule(text);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          ASSERT_TRUE(resolveAndValidateModule(*ast));
          auto owned = owned_required_mask(text);
          ASSERT_TRUE(owned);
          ASSERT_TRUE(validateModule(*owned));
          EXPECT_TRUE(validateModule(*ast, *owned));
          auto& instruction = *owned->functions.front().body.back();
          auto fields = required_mask_fields(instruction);
          const unsigned ordinal = item.scale_eligible
                                       ? (shared ? 0u : 2u) + (scale ? 1u : 0u)
                                       : (shared ? 0u : 1u);
          EXPECT_EQ(fields.layout.value, ordinal);
          EXPECT_EQ(instruction.instruction_kind(), item.identity);
          ASSERT_EQ(fields.mask.value.elements.size(), group == 1 ? 4u : 8u);
          EXPECT_EQ(fields.mask.locs.size(), fields.mask.value.elements.size());
          EXPECT_EQ(borrowed_required_mask(instruction), &fields.mask.value);
          for (size_t index = 0; index < fields.mask.value.elements.size();
               ++index) {
            ASSERT_TRUE(fields.mask.value.elements[index]);
            const auto& ref = *fields.mask.value.elements[index];
            EXPECT_TRUE(ref.symbol_id);
            EXPECT_EQ(ref.declared_type, ScalarType::B32);
            EXPECT_EQ(ref.register_class, ResolvedRegisterClass::General);
            EXPECT_FALSE(ref.vector_width);
            EXPECT_GT(fields.mask.locs[index].start.column, 0);
          }
          expect_required_mask_lifetime(*owned);
          ++cells;
          const std::string layout =
              (shared ? "shared_mask" : "tensor_mask") +
              std::string(item.scale_eligible ? (scale ? "_scale" : "_no_scale")
                                              : "");
          std::cout << "SURVIVOR " << item.name << " layout=" << layout
                    << " group=" << group << " ordinal=" << ordinal << '\n';
        }
  EXPECT_EQ(cells, 48u);
}
TEST(TcgenRequiredMask, RejectsEmptyWrongCountAndMalformedCurrentMembers) {
  for (const auto& item : required_mask_cases)
    for (const unsigned group : {1u, 2u}) {
      auto owned = owned_required_mask(required_mask_module(
          required_mask_instruction(item, group, true, false)));
      ASSERT_TRUE(owned);
      ASSERT_TRUE(validateModule(*owned));
      auto& instruction = *owned->functions.front().body.back();
      auto fields = required_mask_fields(instruction);
      const auto original = fields.mask;
      fields.mask.value.elements.clear();
      fields.mask.locs.clear();
      expect_required_mask_lifetime(*owned, false);
      fields.mask = original;
      for (const unsigned count : {1u, 3u, 5u, 7u, 9u}) {
        const auto ast = test_helpers::parseModule(required_mask_module(
            required_mask_instruction(item, group, true, false, true, count)));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        EXPECT_FALSE(resolveAndValidateModule(*ast));
        fields.mask.value.elements.resize(count,
                                          original.value.elements.front());
        fields.mask.locs.resize(count, original.locs.front());
        expect_required_mask_lifetime(*owned, false);
        fields.mask = original;
      }
      fields.mask.value.elements.front()->register_class =
          ResolvedRegisterClass::Predicate;
      expect_required_mask_lifetime(*owned, false);
      fields.mask = original;
      fields.mask.value.elements.front()->declared_type = ScalarType::B64;
      expect_required_mask_lifetime(*owned, false);
      fields.mask = original;
      fields.mask.value.elements.front()->vector_width = 2;
      expect_required_mask_lifetime(*owned, false);
      fields.mask = original;
      fields.mask.locs.pop_back();
      expect_required_mask_lifetime(*owned, false);
      fields.mask = original;
      const auto ordinal = fields.layout.value;
      fields.layout.value = 99;
      EXPECT_FALSE(borrowed_required_mask(instruction));
      expect_required_mask_lifetime(*owned, false);
      fields.layout.value = ordinal;
      expect_required_mask_lifetime(*owned);
    }
}
TEST(TcgenRequiredMask, PublicNullableF16EntrypointRejectsAbsenceDirectly) {
  for (const unsigned group : {1u, 2u}) {
    auto owned =
        owned_required_mask(required_mask_module(required_mask_instruction(
            required_mask_cases.front(), group, true, false)));
    ASSERT_TRUE(owned);
    auto& f =
        dynamic_cast<Tcgen05MmaF16&>(*owned->functions.front().body.back());
    const auto context = required_mask_context(
        owned->functions.front().instruction_ranges.back());
    auto check = [&](const WithLocs<ResolvedRegisterVector>* mask) {
      return checker::check_tcgen_mma_f16_sources(
          f.cta_group, f.d, nullptr, &*f.a_register, f.b, f.idesc, mask,
          f.enable_input_d, nullptr, context);
    };
    EXPECT_TRUE(check(&f.disable_output_lane));
    const auto absent = check(nullptr);
    ASSERT_FALSE(absent);
    EXPECT_TRUE(std::ranges::any_of(absent.error(), [](const auto& diagnostic) {
      return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation &&
             diagnostic.message.find("requires an output-lane mask") !=
                 std::string::npos;
    }));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

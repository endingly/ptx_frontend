#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/model/surface/suld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/surface/sust.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** A legal data role whose '$' carrier varies independently of address/resources. */
struct DataWidthCase {
  /** Whole instruction; exactly one scalar data leaf is replaced. */
  std::string_view instruction;
  /** Eligible same-width declaration. */
  std::string_view nominal;
  /** Previously accepted wider declaration. */
  std::string_view wider;
};
/** Distinct multimem families plus scalar/vector surface and multimem data paths. */
constexpr std::array data_width_cases{
    DataWidthCase{"multimem.ld_reduce.and.b32 $, [mm];", "b32", "b64"},
    DataWidthCase{"multimem.st.weak.b32 [mm], $;", "b32", "b64"},
    DataWidthCase{"multimem.red.relaxed.gpu.add.u32 [mm], $;", "u32", "u64"},
    DataWidthCase{"multimem.st.async.release.gpu.global.u32 [addr], $;", "u32",
                  "u64"},
    DataWidthCase{"multimem.red.async.release.sys.add.u64 [addr], $;", "u64",
                  "b128"},
    DataWidthCase{"suld.b.1d.b8.trap $, [surf,x];", "b8", "b32"},
    DataWidthCase{"sust.b.1d.b16.trap [surf,x], $;", "b16", "b32"},
    DataWidthCase{"suld.b.1d.v2.b16.trap {$,half}, [surf,x];", "b16", "b32"},
    DataWidthCase{"sust.b.1d.v2.b8.trap [surf,x], {$,1};", "b8", "b32"},
    DataWidthCase{"multimem.ld_reduce.add.acc::f32.v2.f16x2 {$,b}, [mm];",
                  "b32", "b64"},
    DataWidthCase{"multimem.st.v4.f32 [mm], {$,f0,f1,f2};", "f32", "b64"},
};
/** Bind real resources, a 64-bit address and correctly typed unmodified lanes. */
std::string data_width_source(const DataWidthCase& test, std::string_view type,
                              std::string_view operand = "probe",
                              std::string_view extra_declarations = {}) {
  std::string instruction(test.instruction);
  instruction.replace(instruction.find('$'), 1, operand);
  return ".version 9.3\n.target sm_100a\n.address_size 64\n"
         ".global .align 32 .b8 mm[256]; .shared .align 16 .b8 "
         "shared_data[256]; "
         ".global .surfref surf; .entry kernel() { .reg ." +
         std::string(type) +
         " probe; .reg .b64 addr; .reg .s32 x; .reg .b8 byte; .reg .b16 half; "
         ".reg .b32 b; .reg .f32 f0,f1,f2; .reg .u16 mask; "
         ".reg .v2 .b32 V<2>; .reg .b32 bits; .reg .u32 integer; .reg .f32 "
         "floating; "
         ".reg .b64 wide; .reg .v2 .b8 N<2>; .reg .v2 .b16 H<2>; " +
         std::string(extra_declarations) + instruction + " }";
}
/** Exact supported modern target profile, without spelling-based checker policy. */
checker::Context data_width_context() {
  const auto profile = base::find_target_profile("sm_100a");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}
/** Return owned draft data after the original source and syntax lifetime ends. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> data_width_draft(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}

/** Return a strictly checked module after transient source and AST are destroyed. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> data_width_checked(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Borrow the one probe data leaf through the existing reference observer. */
struct ProbeCapture final : detail::IReferenceObserver {
  /** Stable real symbol identity selected before instruction traversal. */
  binding::SymbolId symbol;
  /** Borrow valid only while the observed instruction remains alive. */
  const ResolvedRegisterRef* found = nullptr;
  /** Select the existing symbol without using operand spelling as policy. */
  explicit ProbeCapture(binding::SymbolId identity) : symbol(identity) {}
  /** Match the data leaf's typed identity. */
  void consider(const ResolvedRegisterRef& value) {
    if (value.symbol_id == symbol)
      found = &value;
  }
  /** Observe direct register data. */
  void reg(const ResolvedRegisterRef& value, std::span<const SourceRange>,
           checker::AddressSymbolResolutionPolicy) override {
    consider(value);
  }
  /** Observe a scalar source without repurposing its immediate alternative. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange>,
                  checker::AddressSymbolResolutionPolicy) override {
    if (const auto* ref = std::get_if<ResolvedRegisterRef>(&value))
      consider(*ref);
  }
  /** Observe existing explicit register lanes without scalarizing a named root. */
  void register_vector(const ResolvedRegisterVector& value,
                       std::span<const SourceRange>,
                       checker::AddressSymbolResolutionPolicy) override {
    for (const auto& ref : value.elements)
      if (ref)
        consider(*ref);
  }
  /** Observe register leaves while preserving mixed literal lanes. */
  void value_vector(const ResolvedValueVector& value,
                    std::span<const SourceRange>,
                    checker::AddressSymbolResolutionPolicy) override {
    for (const auto& lane : value.elements)
      if (const auto* ref = std::get_if<ResolvedRegisterRef>(&lane))
        consider(*ref);
  }
};
/** Retarget an existing scalar data leaf to a real differently typed declaration. */
bool retarget_probe(ResolvedModule& module, std::string_view type) {
  const auto scope = module.functions.front().declaration_scope;
  const auto probe = module.symbols.lookup(scope, "probe");
  const auto candidate = module.symbols.lookup(scope, "candidate");
  const auto* metadata =
      base::find_scalar_type_metadata("." + std::string(type));
  if (!probe || !candidate || !metadata)
    return false;
  ProbeCapture capture(probe->symbol);
  module.functions.front().body.front()->visit_references(capture);
  if (capture.found == nullptr)
    return false;
  auto& ref = const_cast<ResolvedRegisterRef&>(*capture.found);
  ref = ResolvedRegisterRef{.spelling = "candidate",
                            .register_class = ResolvedRegisterClass::General,
                            .symbol_id = candidate->symbol,
                            .declared_type = metadata->type};
  return true;
}

TEST(NonLdstDataWidth, RejectsWiderDataAcrossDistinctFamiliesAndLanePaths) {
  for (const auto& test : data_width_cases) {
    SCOPED_TRACE(test.instruction);
    const auto baseline =
        test_helpers::parseModule(data_width_source(test, test.nominal));
    ASSERT_TRUE(baseline && baseline.diagnostics.empty());
    const auto legal = resolveAndValidateModule(*baseline);
    ASSERT_TRUE(legal) << legal.error().front().message;
    const auto ast =
        test_helpers::parseModule(data_width_source(test, test.wider));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto draft = resolveModuleOnly(*ast);
    if (draft) {
      EXPECT_FALSE(
          draft->functions.front().body.front()->check(data_width_context()));
      EXPECT_FALSE(validateModule(*ast, *draft));
    } else {
      ASSERT_FALSE(draft.error().empty());
      const auto& diagnostic = draft.error().front();
      EXPECT_EQ(diagnostic.stage(), ResolveDiagnosticStage::Resolution);
      EXPECT_GT(diagnostic.range.start.line, 0);
      EXPECT_GT(diagnostic.range.start.column, 0);
      EXPECT_NE(diagnostic.range.start, diagnostic.range.end);
      EXPECT_FALSE(diagnostic.message.empty());
    }
    EXPECT_FALSE(resolveAndValidateModule(*ast));
    // Vector layout selection may reject the bad source before a draft exists.
    // A separately strict-valid module supplies the independent owned counterexample.
    auto owned = data_width_checked(
        data_width_source(test, test.nominal, "probe",
                          ".reg ." + std::string(test.wider) + " candidate; "));
    ASSERT_TRUE(owned);
    auto copied = *owned;
    ResolvedModule assigned;
    assigned = copied;
    assigned.functions.front().body.front() =
        assigned.functions.front().body.front()->clone();
    ASSERT_TRUE(retarget_probe(assigned, test.wider));
    const auto direct =
        assigned.functions.front().body.front()->check(data_width_context());
    ASSERT_FALSE(direct);
    EXPECT_TRUE(std::ranges::any_of(direct.error(), [](const auto& diagnostic) {
      return diagnostic.kind ==
             checker::CheckDiagnosticKind::OperandTypeMismatch;
    }));
    const auto bad_owned = validateModule(assigned);
    ASSERT_FALSE(bad_owned);
    EXPECT_TRUE(
        std::ranges::none_of(bad_owned.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
    auto bad_copy = assigned;
    bad_copy.functions.front().body.front() =
        bad_copy.functions.front().body.front()->clone();
    EXPECT_FALSE(validateModule(bad_copy));
  }
}
/** Independent legal/illegal declaration partitions for one logical data kind. */
struct DataPartition {
  /** Complete instruction with one variable scalar data operand. */
  std::string_view instruction;
  /** Same-size eligible declaration spellings. */
  std::vector<std::string_view> allowed;
  /** Wrong width or same-size wrong-kind declaration spellings. */
  std::vector<std::string_view> rejected;
};

TEST(NonLdstDataWidth, PreservesSameSizeKindPartitionsWithoutGlobalBitBans) {
  const std::array partitions{
      DataPartition{"suld.b.1d.b8.trap $, [surf,x];",
                    {"b8", "s8", "u8"},
                    {"b16", "b32", "b64", "b128"}},
      DataPartition{"sust.b.1d.b16.trap [surf,x], $;",
                    {"b16", "s16", "u16", "f16"},
                    {"b8", "b32", "b64", "b128"}},
      DataPartition{"suld.b.1d.b32.trap $, [surf,x];",
                    {"b32", "s32", "u32", "f32", "f16x2"},
                    {"b16", "b64", "b128"}},
      DataPartition{"sust.b.1d.b64.trap [surf,x], $;",
                    {"b64", "s64", "u64", "f64"},
                    {"b32", "b128"}},
      DataPartition{"multimem.red.add.u32 [mm], $;",
                    {"b32", "s32", "u32"},
                    {"f32", "f16x2", "b16", "b64", "b128"}},
      DataPartition{"multimem.st.f32 [mm], $;",
                    {"b32", "f32"},
                    {"s32", "u32", "f16x2", "b16", "b64", "b128"}},
      DataPartition{"multimem.st.f16x2 [mm], $;",
                    {"b32", "f16x2"},
                    {"f32", "s32", "u32", "b16", "b64", "b128"}},
      DataPartition{"multimem.st.bf16x2 [mm], $;",
                    {"b32"},
                    {"f16x2", "f32", "s32", "u32", "b16", "b64", "b128"}},
      DataPartition{"multimem.ld_reduce.add.acc::f32.v2.bf16 {$,half}, [mm];",
                    {"b16"},
                    {"f16", "s16", "u16", "b32", "b64", "b128"}},
      DataPartition{"multimem.st.e4m3x4 [mm], $;",
                    {"b32"},
                    {"f16x2", "f32", "s32", "u32", "b64", "b128"}},
      DataPartition{"multimem.st.async.release.gpu.global.f32 [addr], $;",
                    {"b32", "f32"},
                    {"s32", "u32", "b16", "b64", "b128"}},
  };
  for (const auto& partition : partitions) {
    SCOPED_TRACE(partition.instruction);
    const DataWidthCase test{partition.instruction, "", ""};
    for (const auto type : partition.allowed) {
      SCOPED_TRACE(type);
      auto owned = data_width_checked(data_width_source(test, type));
      ASSERT_TRUE(owned) << owned.error().front().message;
      auto copied = *owned;
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
      EXPECT_TRUE(validateModule(assigned));
    }
    for (const auto type : partition.rejected) {
      SCOPED_TRACE(type);
      const auto ast = test_helpers::parseModule(data_width_source(test, type));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto draft = resolveModuleOnly(*ast);
      if (draft) {
        const auto checked = validateModule(*draft);
        ASSERT_FALSE(checked);
        EXPECT_TRUE(
            std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
              return diagnostic.kind ==
                     checker::CheckDiagnosticKind::OperandTypeMismatch;
            }));
        EXPECT_FALSE(validateModule(*ast, *draft));
      } else {
        ASSERT_FALSE(draft.error().empty());
        const auto& diagnostic = draft.error().front();
        EXPECT_EQ(diagnostic.stage(), ResolveDiagnosticStage::Resolution);
        EXPECT_GT(diagnostic.range.start.line, 0);
        EXPECT_GT(diagnostic.range.start.column, 0);
        EXPECT_NE(diagnostic.range.start, diagnostic.range.end);
        EXPECT_FALSE(diagnostic.message.empty());
      }
      EXPECT_FALSE(resolveAndValidateModule(*ast));
      auto owned = data_width_checked(
          data_width_source(test, partition.allowed.front(), "probe",
                            ".reg ." + std::string(type) + " candidate; "));
      ASSERT_TRUE(owned);
      ASSERT_TRUE(retarget_probe(*owned, type));
      const auto direct =
          owned->functions.front().body.front()->check(data_width_context());
      ASSERT_FALSE(direct);
      EXPECT_TRUE(
          std::ranges::any_of(direct.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
      const auto bad_owned = validateModule(*owned);
      ASSERT_FALSE(bad_owned);
      EXPECT_TRUE(
          std::ranges::none_of(bad_owned.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::ModuleSourceMismatch;
          }));
      auto bad_copy = *owned;
      bad_copy.functions.front().body.front() =
          bad_copy.functions.front().body.front()->clone();
      EXPECT_FALSE(validateModule(bad_copy));
    }
  }
}

/** Borrow a concrete scalar data leaf and its owner's corresponding locations. */
struct DataReference {
  /** Mutable reference whose instruction owner outlives this borrow. */
  ResolvedRegisterRef& value;
  /** Borrowed source locations, without manufacturing provenance. */
  std::span<const SourceRange> locations;
};
/** Cover surface load data and multimem integer reduction input with finite overloads. */
DataReference scalar_data(Instruction& instruction, bool surface) {
  if (surface) {
    auto& slot = dynamic_cast<SuldBData1dV1TrapB8B16&>(instruction).data;
    return {slot.value, slot.locs};
  }
  auto& slot =
      dynamic_cast<MultimemRedIntegerAddScalarDefaultRed&>(instruction).src;
  return {std::get<ResolvedRegisterRef>(slot.value), slot.locs};
}

TEST(NonLdstDataWidth, OwnsNarrowComponentsAndRechecksActualSymbolRetargets) {
  const std::array tests{
      DataWidthCase{"suld.b.1d.b8.trap $, [surf,x];", "b8", "b32"},
      DataWidthCase{"suld.b.1d.b16.trap $, [surf,x];", "b16", "b32"},
      DataWidthCase{"multimem.red.add.u32 [mm], $;", "u32", "u64"},
  };
  for (unsigned index = 0; index < tests.size(); ++index) {
    const auto& test = tests[index];
    const auto component = index == 0 ? "N1.y" : index == 1 ? "H1.y" : "V1.y";
    const auto ast = test_helpers::parseModule(
        data_width_source(test, test.nominal, component));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto owned =
        data_width_checked(data_width_source(test, test.nominal, component));
    ASSERT_TRUE(owned) << owned.error().front().message;
    auto copied = *owned;
    copied.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    auto fields =
        scalar_data(*copied.functions.front().body.front(), index < 2);
    auto& ref = fields.value;
    ASSERT_TRUE(ref.symbol_id);
    ASSERT_TRUE(ref.component);
    EXPECT_EQ(copied.symbols.symbol(*ref.symbol_id).name, index == 0   ? "N"
                                                          : index == 1 ? "H"
                                                                       : "V");
    EXPECT_EQ(ref.parameterized_index, 1);
    EXPECT_EQ(ref.component->lane, 1);
    EXPECT_EQ(ref.component->origin, RegisterComponentOrigin::ExplicitSelector);
    EXPECT_EQ(ref.declared_type, index == 0   ? ScalarType::B8
                                 : index == 1 ? ScalarType::B16
                                              : ScalarType::B32);
    EXPECT_FALSE(ref.vector_width);
    ASSERT_FALSE(fields.locations.empty());
    EXPECT_EQ(ref.component->range, fields.locations.front());
    EXPECT_TRUE(validateModule(copied));
    const auto saved = ref;
    ref.component->lane = 2;
    EXPECT_FALSE(validateModule(copied));
    ref = saved;
    ++ref.component->range.end.column;
    EXPECT_FALSE(validateModule(copied));
    ref = saved;
    ref.parameterized_index = 2;
    EXPECT_FALSE(validateModule(copied));
    ref = saved;
    ref.component.reset();
    ref.vector_width = 2;
    ref.spelling = std::string(component).substr(0, 2);
    EXPECT_FALSE(validateModule(copied));
    const auto whole = test_helpers::parseModule(data_width_source(
        test, test.nominal, std::string(component).substr(0, 2)));
    ASSERT_TRUE(whole && whole.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*whole));
    for (const auto name :
         {"byte", "half", "bits", "integer", "floating", "wide"}) {
      auto retarget = *owned;
      auto target =
          scalar_data(*retarget.functions.front().body.front(), index < 2);
      const auto symbol = retarget.symbols.lookup(
          retarget.functions.front().declaration_scope, name);
      ASSERT_TRUE(symbol);
      const auto type = std::string_view(name) == "byte"      ? ScalarType::B8
                        : std::string_view(name) == "half"    ? ScalarType::B16
                        : std::string_view(name) == "bits"    ? ScalarType::B32
                        : std::string_view(name) == "integer" ? ScalarType::U32
                        : std::string_view(name) == "floating"
                            ? ScalarType::F32
                            : ScalarType::B64;
      target.value =
          ResolvedRegisterRef{.spelling = name,
                              .register_class = ResolvedRegisterClass::General,
                              .symbol_id = symbol->symbol,
                              .declared_type = type};
      const bool legal =
          index == 0   ? type == ScalarType::B8
          : index == 1 ? type == ScalarType::B16
                       : type == ScalarType::B32 || type == ScalarType::U32;
      const auto checked = validateModule(retarget);
      EXPECT_EQ(checked.has_value(), legal);
      EXPECT_EQ(validateModule(*ast, retarget).has_value(), legal);
      if (!legal) {
        ASSERT_FALSE(checked);
        EXPECT_TRUE(
            std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
              return diagnostic.kind ==
                     checker::CheckDiagnosticKind::OperandTypeMismatch;
            }));
      } else {
        target.value.declared_type = ScalarType::B128;
        EXPECT_FALSE(validateModule(retarget));
      }
    }
    const auto name = index == 0 ? "byte" : index == 1 ? "half" : "integer";
    const auto replacement =
        test_helpers::parseModule(data_width_source(test, test.nominal, name));
    ASSERT_TRUE(replacement && replacement.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*replacement));
    const auto stale = validateModule(*replacement, *owned);
    ASSERT_FALSE(stale);
    ASSERT_EQ(stale.error().size(), 1u);
    EXPECT_EQ(stale.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
  }
}

TEST(NonLdstDataWidth, LeavesExplicitWideningAndIndependentBulkOperandsIntact) {
  for (const auto instruction :
       {"ld.global.u8 $, [mm];", "st.global.u8 [mm], $;", "cvt.u8.u8 $, byte;",
        "ldu.global.u8 $, [mm];"}) {
    const auto ast = test_helpers::parseModule(
        data_width_source(DataWidthCase{instruction, "", ""}, "b32"));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto checked = resolveAndValidateModule(*ast);
    EXPECT_TRUE(checked) << checked.error().front().message;
  }
  const auto ast = test_helpers::parseModule(data_width_source(
      DataWidthCase{"multimem.cp.async.bulk.global.shared::cta.bulk_group.cp_"
                    "mask [mm], [shared_data], 16, $;",
                    "", ""},
      "u16"));
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  EXPECT_TRUE(resolveAndValidateModule(*ast));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

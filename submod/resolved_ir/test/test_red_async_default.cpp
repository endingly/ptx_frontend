#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/red.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <string>
#include <string_view>
#include <utility>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** One existing asynchronous reduction operation with its logical source format. */
struct RedDefaultCase {
  /** Stable canonical variant for exact cohort joins. */
  std::string_view variant;
  /** Written operation token without its leading dot. */
  std::string_view operation;
  /** Written and declared scalar source type, without its leading dot. */
  std::string_view type;
  /** Expected logical source type, compared as a typed enum. */
  ScalarType type_value;
  /** Expected generated instruction class, independent of diagnostic spelling. */
  InstructionKind kind;
};
constexpr std::array red_default_cases{
    RedDefaultCase{"red_async_shared_inc_u32", "inc", "u32", ScalarType::U32,
                   RedAsyncSharedIncU32::kind},
    RedDefaultCase{"red_async_shared_dec_u32", "dec", "u32", ScalarType::U32,
                   RedAsyncSharedDecU32::kind},
    RedDefaultCase{"red_async_shared_min_u32", "min", "u32", ScalarType::U32,
                   RedAsyncSharedMinU32::kind},
    RedDefaultCase{"red_async_shared_min_s32", "min", "s32", ScalarType::S32,
                   RedAsyncSharedMinS32::kind},
    RedDefaultCase{"red_async_shared_max_u32", "max", "u32", ScalarType::U32,
                   RedAsyncSharedMaxU32::kind},
    RedDefaultCase{"red_async_shared_max_s32", "max", "s32", ScalarType::S32,
                   RedAsyncSharedMaxS32::kind},
    RedDefaultCase{"red_async_shared_and_b32", "and", "b32", ScalarType::B32,
                   RedAsyncSharedAndB32::kind},
    RedDefaultCase{"red_async_shared_or_b32", "or", "b32", ScalarType::B32,
                   RedAsyncSharedOrB32::kind},
    RedDefaultCase{"red_async_shared_xor_b32", "xor", "b32", ScalarType::B32,
                   RedAsyncSharedXorB32::kind},
    RedDefaultCase{"red_async_shared_add_u32", "add", "u32", ScalarType::U32,
                   RedAsyncSharedAddU32::kind},
    RedDefaultCase{"red_async_shared_add_s32", "add", "s32", ScalarType::S32,
                   RedAsyncSharedAddS32::kind},
    RedDefaultCase{"red_async_shared_add_u64", "add", "u64", ScalarType::U64,
                   RedAsyncSharedAddU64::kind},
};
/** Initialize real matching source and address registers; runtime contents stay opaque. */
std::string red_default_source(const RedDefaultCase& item,
                               bool explicit_completion = true,
                               bool shared_space = false) {
  return ".version 9.3\n.target sm_100\n.entry kernel(){.reg .u64 dst,bar; "
         ".reg ." +
         std::string(item.type) + " src; mov.u64 dst,0; mov.u64 bar,8; mov." +
         std::string(item.type) + " src,1; red.async.relaxed.cluster" +
         (shared_space ? ".shared::cluster" : std::string{}) +
         (explicit_completion ? ".mbarrier::complete_tx::bytes" : "") + "." +
         std::string(item.operation) + "." + std::string(item.type) +
         " [dst],src,[bar];}";
}
/** Keep release mode's two operands and independent MMIO/Sys rule explicit. */
std::string red_release_source(std::string_view type, bool sys, bool global,
                               bool mmio, bool completion) {
  return ".version 9.3\n.target sm_100\n.entry kernel(){.reg .u64 dst; .reg ." +
         std::string(type) + " src; mov.u64 dst,0; mov." + std::string(type) +
         " src,1; red.async" + (mmio ? ".mmio" : std::string{}) + ".release." +
         (sys ? "sys" : "gpu") + (global ? ".global" : "") +
         (completion ? ".mbarrier::complete_tx::bytes" : "") + ".add." +
         std::string(type) + " [dst],src;}";
}
/** Complete recognized modern context shared by direct checker controls. */
const checker::Context red_default_context{
    .target = {.ptx_version = {9, 3}, .sm_version = 100}};
/** Borrow completion and all three operand payloads from one live instruction. */
struct RedDefaultFields {
  /** Owned effective mechanism marker and written token locations. */
  WithLocs<bool>& completion;
  /** Written generic/shared-cluster distinction, independent of the mechanism. */
  WithLocs<AtomicAddressQualifier>& qualifier;
  /** Typed state-space modifier; omitted generic remains distinct. */
  WithLocs<MemoryStateSpace>& space;
  /** Register-based destination and source address locations. */
  WithLocs<ResolvedAddress>& address;
  /** Matching source value and its exact operand locations. */
  WithLocs<RegOrImm>& src;
  /** Required third address remains present even when the qualifier is omitted. */
  WithLocs<ResolvedAddress>& mbarrier;
};
/** Inspect the closed existing family without introducing a generated accessor. */
RedDefaultFields red_default_fields(Instruction& instruction) {
  switch (instruction.instruction_kind()) {
    case RedAsyncSharedIncU32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedIncU32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedDecU32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedDecU32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedMinU32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedMinU32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedMinS32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedMinS32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedMaxU32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedMaxU32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedMaxS32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedMaxS32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedAndB32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedAndB32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedOrB32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedOrB32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedXorB32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedXorB32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedAddU32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedAddU32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedAddS32::kind: {
      auto& f = dynamic_cast<RedAsyncSharedAddS32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    case RedAsyncSharedAddU64::kind: {
      auto& f = dynamic_cast<RedAsyncSharedAddU64&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
    }
    default:
      ADD_FAILURE() << "Unexpected async reduction class";
      auto& f = dynamic_cast<RedAsyncSharedIncU32&>(instruction);
      return {f.completion, f.address_qualifier, f.state_space, f.address,
              f.src,        f.mbarrier};
  }
}
/** Strictly resolve while all source, parser and AST storage dies on return. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_red_default(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Reproject actual payloads through independent module/function copies and clones. */
void expect_red_default_lifetime(const ResolvedModule& module,
                                 bool valid = true) {
  EXPECT_EQ(
      bool(module.functions.front().body.back()->check(red_default_context)),
      valid);
  EXPECT_EQ(bool(validateModule(module)), valid);
  ResolvedModule copied(module), assigned;
  assigned = copied;
  EXPECT_EQ(bool(validateModule(copied)), valid);
  EXPECT_EQ(bool(validateModule(assigned)), valid);
  ResolvedFunction function(module.functions.front()), function_assigned;
  function_assigned = function;
  EXPECT_NE(function.body.back().get(),
            module.functions.front().body.back().get());
  EXPECT_EQ(bool(function_assigned.body.back()->check(red_default_context)),
            valid);
  assigned.functions.front().body.back() = function.body.back()->clone();
  EXPECT_EQ(bool(validateModule(assigned)), valid);
}
TEST(RedAsyncDefault, AdmitsOmissionBesideTwentyFourExplicitControls) {
  for (const auto& item : red_default_cases)
    for (const bool space : {false, true}) {
      SCOPED_TRACE(std::string(item.variant) +
                   (space ? " shared" : " generic"));
      const auto control =
          test_helpers::parseModule(red_default_source(item, true, space));
      ASSERT_MODULE_PARSE_SUCCEEDS(control);
      const auto matched = resolveAndValidateModule(*control);
      ASSERT_TRUE(matched) << matched.error().front().message;
      EXPECT_EQ(matched->functions.front().body.back()->instruction_kind(),
                item.kind);
      const auto ast =
          test_helpers::parseModule(red_default_source(item, false, space));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto draft = resolveModuleOnly(*ast);
      std::cout << item.variant << " space=" << space << " omitted-stage="
                << (draft ? "draft" : "resolution-rejected; owned=N/A") << '\n';
      EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
      EXPECT_TRUE(resolveAndValidateModule(*ast));
    }
}
TEST(RedAsyncDefault, RetainsAllSixtyFourReleaseCandidates) {
  size_t allowed = 0, rejected = 0;
  for (const auto type : {"u32", "s32", "u64", "s64"})
    for (const bool sys : {false, true})
      for (const bool global : {false, true})
        for (const bool mmio : {false, true})
          for (const bool completion : {false, true}) {
            const auto text =
                red_release_source(type, sys, global, mmio, completion);
            SCOPED_TRACE(text);
            const auto ast = test_helpers::parseModule(text);
            ASSERT_MODULE_PARSE_SUCCEEDS(ast);
            const bool valid = !completion && (!mmio || sys);
            const auto module = resolveAndValidateModule(*ast);
            EXPECT_EQ(bool(module), valid);
            std::cout << "RELEASE type=" << type << " sys=" << sys
                      << " global=" << global << " mmio=" << mmio
                      << " completion=" << completion << " valid=" << valid
                      << '\n';
            if (valid) {
              ++allowed;
              ASSERT_TRUE(module);
              EXPECT_TRUE(validateModule(*module));
              auto dead = owned_red_default(text);
              ASSERT_TRUE(dead);
              expect_red_default_lifetime(*dead);
            } else
              ++rejected;
          }
  EXPECT_EQ(allowed, 24u);
  EXPECT_EQ(rejected, 40u);
}
TEST(RedAsyncDefault, ClosesFortyEightSharedProductsAfterAstDeath) {
  size_t count = 0;
  for (const auto& item : red_default_cases)
    for (const bool space : {false, true})
      for (const bool written : {false, true}) {
        const auto text = red_default_source(item, written, space);
        SCOPED_TRACE(text);
        const auto ast = test_helpers::parseModule(text);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        ASSERT_TRUE(resolveModuleOnly(*ast));
        auto module = owned_red_default(text);
        ASSERT_TRUE(module) << module.error().front().message;
        EXPECT_TRUE(validateModule(*ast, *module));
        EXPECT_EQ(module->functions.front().body.back()->instruction_kind(),
                  item.kind);
        auto fields =
            red_default_fields(*module->functions.front().body.back());
        EXPECT_TRUE(fields.completion.value);
        EXPECT_EQ(fields.completion.locs.empty(), !written);
        if (written) {
          ASSERT_EQ(fields.completion.locs.size(), 1u);
          EXPECT_GT(fields.completion.locs.front().start.column, 0);
        }
        EXPECT_EQ(fields.qualifier.value,
                  space ? AtomicAddressQualifier::SharedCluster
                        : AtomicAddressQualifier::Generic);
        EXPECT_EQ(fields.space.value,
                  space ? MemoryStateSpace::Shared : MemoryStateSpace::Generic);
        ASSERT_FALSE(fields.address.locs.empty());
        ASSERT_FALSE(fields.src.locs.empty());
        ASSERT_FALSE(fields.mbarrier.locs.empty());
        const auto& dst =
            std::get<ResolvedRegisterRef>(fields.address.value.base);
        const auto& bar =
            std::get<ResolvedRegisterRef>(fields.mbarrier.value.base);
        const auto& src = std::get<ResolvedRegisterRef>(fields.src.value);
        ASSERT_TRUE(dst.symbol_id && bar.symbol_id && src.symbol_id);
        EXPECT_NE(dst.symbol_id, bar.symbol_id);
        EXPECT_NE(dst.symbol_id, src.symbol_id);
        EXPECT_EQ(src.declared_type, item.type_value);
        EXPECT_EQ(dst.declared_type, ScalarType::U64);
        EXPECT_EQ(bar.declared_type, ScalarType::U64);
        ResolvedModule copied(*module);
        auto copy = red_default_fields(*copied.functions.front().body.back());
        EXPECT_EQ(copy.completion, fields.completion);
        EXPECT_EQ(copy.address.locs, fields.address.locs);
        EXPECT_EQ(copy.src.locs, fields.src.locs);
        EXPECT_EQ(copy.mbarrier.locs, fields.mbarrier.locs);
        EXPECT_EQ(
            std::get<ResolvedRegisterRef>(copy.address.value.base).symbol_id,
            dst.symbol_id);
        EXPECT_EQ(
            std::get<ResolvedRegisterRef>(copy.mbarrier.value.base).symbol_id,
            bar.symbol_id);
        const auto& copied_source =
            std::get<ResolvedRegisterRef>(copy.src.value);
        EXPECT_EQ(copied_source.symbol_id, src.symbol_id);
        EXPECT_EQ(copied_source.declared_type, src.declared_type);
        EXPECT_FALSE(copy.address.value.offset.has_value());
        EXPECT_FALSE(copy.mbarrier.value.offset.has_value());
        expect_red_default_lifetime(*module);
        ++count;
        std::cout << "SHARED " << item.variant << " space=" << space
                  << " completion=" << written << '\n';
      }
  EXPECT_EQ(count, 48u);
}
TEST(RedAsyncDefault,
     EnforcesSingletonValueWithoutReconstructingLexicalHistory) {
  for (const auto& item : red_default_cases) {
    auto module = owned_red_default(red_default_source(item));
    ASSERT_TRUE(module);
    auto fields = red_default_fields(*module->functions.front().body.back());
    const auto locations = fields.completion.locs;
    for (const bool located : {false, true}) {
      fields.completion.value = false;
      fields.completion.locs = located ? locations : decltype(locations){};
      const auto invalid =
          module->functions.front().body.back()->check(red_default_context);
      ASSERT_FALSE(invalid);
      EXPECT_TRUE(
          std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::ModifierValueDomainMismatch;
          }));
      expect_red_default_lifetime(*module, false);
    }
    fields.completion.value = true;
    fields.completion.locs.clear();
    expect_red_default_lifetime(*module);
    const auto original = test_helpers::parseModule(red_default_source(item));
    ASSERT_MODULE_PARSE_SUCCEEDS(original);
    // Retained source identity still matches; current true/empty is a legal omitted control.
    EXPECT_TRUE(validateModule(*original, *module));
    fields.completion.locs = locations;
    expect_red_default_lifetime(*module);
    const auto replacement =
        test_helpers::parseModule(red_default_source(item, false));
    ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
    ASSERT_TRUE(resolveAndValidateModule(*replacement));
    const auto mismatch = validateModule(*replacement, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
  auto ordinary =
      owned_red_default(red_release_source("u32", true, false, true, false));
  ASSERT_TRUE(ordinary);
  auto& form = dynamic_cast<RedAsyncReleaseAddU32&>(
      *ordinary->functions.front().body.back());
  const auto locations = form.mmio.locs;
  form.mmio.locs.clear();
  expect_red_default_lifetime(*ordinary, false);
  form.mmio.locs = locations;
  form.mmio.value = false;
  expect_red_default_lifetime(*ordinary, false);
  form.mmio.locs.clear();
  expect_red_default_lifetime(*ordinary);
}
TEST(RedAsyncDefault, RetainsMechanismOperandsModesAndPublishedTargetFloors) {
  for (const auto& item : red_default_cases)
    for (const bool written : {false, true}) {
      const auto original = red_default_source(item, written);
      const auto body = original.substr(original.find("\n.entry"));
      for (const auto& [header, kind] : std::array{
               std::pair{std::string(".version 8.0\n.target sm_90"),
                         checker::CheckDiagnosticKind::UnsupportedPtxVersion},
               std::pair{std::string(".version 8.1\n.target sm_89"),
                         checker::CheckDiagnosticKind::UnsupportedSmVersion}}) {
        const auto ast = test_helpers::parseModule(header + body);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const auto rejected = resolveAndValidateModule(*ast);
        ASSERT_FALSE(rejected);
        EXPECT_TRUE(std::ranges::any_of(
            rejected.error(), [kind](const auto& diagnostic) {
              return diagnostic.checker_kind == kind;
            }));
      }
      const auto minimum =
          test_helpers::parseModule(".version 8.1\n.target sm_90" + body);
      ASSERT_MODULE_PARSE_SUCCEEDS(minimum);
      ASSERT_TRUE(resolveAndValidateModule(*minimum));
      for (const auto replacement :
           {" [dst],src;}", " [dst],src,[bar],[bar];}"}) {
        auto source = original;
        source.replace(source.rfind(" [dst],src,[bar];}"), 18, replacement);
        const auto ast = test_helpers::parseModule(source);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        EXPECT_FALSE(resolveAndValidateModule(*ast));
      }
    }
  const auto release = red_release_source("u32", true, true, false, false);
  const auto body = release.substr(release.find("\n.entry"));
  const auto minimum =
      test_helpers::parseModule(".version 8.7\n.target sm_100" + body);
  ASSERT_MODULE_PARSE_SUCCEEDS(minimum);
  ASSERT_TRUE(resolveAndValidateModule(*minimum));
  for (const auto& [header, kind] : std::array{
           std::pair{std::string(".version 8.6\n.target sm_100"),
                     checker::CheckDiagnosticKind::UnsupportedPtxVersion},
           std::pair{std::string(".version 8.7\n.target sm_90"),
                     checker::CheckDiagnosticKind::UnsupportedSmVersion}}) {
    const auto ast = test_helpers::parseModule(header + body);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto rejected = resolveAndValidateModule(*ast);
    ASSERT_FALSE(rejected);
    EXPECT_TRUE(
        std::ranges::any_of(rejected.error(), [kind](const auto& diagnostic) {
          return diagnostic.checker_kind == kind;
        }));
  }
  for (const auto suffix :
       {".mbarrier::complete_tx::bytes.mbarrier::complete_tx::bytes.inc.u32",
        ".end_cta.inc.u32", ".inc.u32.mbarrier::complete_tx::bytes"}) {
    auto source = red_default_source(red_default_cases.front(), false);
    const auto start = source.find(".inc.u32");
    source.replace(start, 8, suffix);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

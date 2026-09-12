#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Representative source exercising every owned module handoff contract. */
constexpr std::string_view k_owned_module_fixture = R"ptx(
.version 9.3
.address_size 64
.func callee(.reg .u32 value) { ret; }
.func callee_alias(.reg .u32 value);
.target sm_90a
.entry kernel(.param .u32 input) .maxntid 8 .reqnctapercluster 2, 1 .explicitcluster {
  .reg .u64 %fptr;
  .reg .u32 %index;
  .param .align 8 .u32 local[2];
prototype: .callprototype _ (.reg .u32 value);
targets: .calltargets callee, callee_alias;
branches: .branchtargets done<2>, done0;
  call callee, (7);
  call %fptr, (8), prototype;
  brx.idx %index, branches;
done0:
done1:
  ret;
}
.alias callee_alias, callee;
)ptx";

/** Equivalent handoff fixture whose declarations all have a source target. */
constexpr std::string_view k_complete_context_module_fixture = R"ptx(
.version 9.3
.address_size 64
.target sm_80
.func callee(.reg .u32 value) { ret; }
.func callee_alias(.reg .u32 value);
.target sm_90a
.entry kernel(.param .u32 input) .maxntid 8 .reqnctapercluster 2, 1 .explicitcluster {
  .reg .u64 %fptr;
  .reg .u32 %index;
  .param .align 8 .u32 local[2];
prototype: .callprototype _ (.reg .u32 value);
targets: .calltargets callee, callee_alias;
branches: .branchtargets done<2>, done0;
  call callee, (7);
  call %fptr, (8), prototype;
  brx.idx %index, branches;
done0:
done1:
  ret;
}
.alias callee_alias, callee;
)ptx";

/** Minimal complete-context source whose generated operands carry group members. */
constexpr std::string_view k_member_identity_module_fixture = R"ptx(
.version 9.3
.target sm_80
.entry kernel() {
  .reg .u32 %r<2>;
  mov.u32 %r0, %r1;
  ret;
}
)ptx";

/** Header-only target fixtures isolate strict source-profile diagnostics. */
constexpr std::string_view k_unknown_target_module_fixture = R"ptx(
.version 9.3
.target sm_123a
.entry kernel() { ret; }
)ptx";

/** Missing-version target fixture must not be rejected as an unused prefix region. */
constexpr std::string_view k_missing_version_module_fixture = R"ptx(
.target sm_80
.entry kernel() { ret; }
)ptx";

/** Entry launch metadata fixture supplies every legal entry-only resource contract. */
constexpr std::string_view k_resource_module_fixture = R"ptx(
.version 9.3
.target sm_90
.entry kernel() .reqntid 1 .reqnctapercluster 1 .blocksareclusters { ret; }
)ptx";

/** Numeric `.unified` source values exercise the owned function-attribute contract. */
constexpr std::string_view k_attribute_module_fixture = R"ptx(
.version 9.3
.target sm_90
.func .attribute(.unified(1, 2)) attributed() { ret; }
)ptx";

/** A valid no-return ABI fixture supplies both distinct preservation suffixes. */
constexpr std::string_view k_function_contract_module_fixture = R"ptx(
.version 9.3
.target sm_80
.func target(.param .u32 input) .noreturn .abi_preserve 1 .abi_preserve_control 1 {
  ret;
}
)ptx";

/** Direct and metadata calls retain their ABI contracts without syntax AST state. */
constexpr std::string_view k_call_contract_module_fixture = R"ptx(
.version 9.3
.target sm_80
.func no_args() { ret; }
.func formal(.reg .u32 input) { ret; }
.func (.param .u32 output) returns_u32() { ret; }
.func parameter_target(.param .u32 input) { ret; }
.func trailing(.param .u32 required, .param .b8 bytes[]) { ret; }
.entry caller() {
  .reg .u64 %fptr;
  .reg .u32 %input;
  .param .u32 output, local_input;
prototype: .callprototype _ (.reg .u32 input);
  call no_args;
  call (output), returns_u32, ();
  call parameter_target, (local_input);
  call %fptr, (%input), prototype;
  call trailing, (local_input);
  ret;
}
)ptx";

/** Return the parse result so each test controls fatal parser diagnostics. */
SyntaxModuleParseResult parse_owned_module_fixture(std::string_view source) {
  return test_helpers::parseModule(source);
}

/** Require AST-free validation to reject one deliberately malformed owned module. */
void expect_owned_model_mismatch(
    const ResolvedModule& module,
    ModuleValidationPolicy policy = ModuleValidationPolicy::AvailableContext) {
  const auto validation = validateModule(module, policy);
  ASSERT_FALSE(validation.has_value());
  ASSERT_FALSE(validation.error().empty());
  EXPECT_EQ(validation.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
}

/** Require AST-free validation to report a requested structured diagnostic kind. */
void expect_owned_validation_kind(const ResolvedModule& module,
                                  ModuleValidationPolicy policy,
                                  checker::CheckDiagnosticKind kind) {
  const auto validation = validateModule(module, policy);
  ASSERT_FALSE(validation.has_value());
  const auto diagnostic = std::ranges::find(validation.error(), kind,
                                            &checker::CheckDiagnostic::kind);
  EXPECT_NE(diagnostic, validation.error().end());
}

/** Require AST-free validation to accept one intact owned module. */
void expect_owned_validation_success(const ResolvedModule& module,
                                     ModuleValidationPolicy policy) {
  const auto validation = validateModule(module, policy);
  ASSERT_TRUE(validation.has_value())
      << (validation.has_value() || validation.error().empty()
              ? "Owned fixture did not validate."
              : validation.error().front().message);
}

/** Owned module data remains consumer-usable after source text and AST lifetime end. */
TEST(OwnedModuleHandoff, RetainsContractsAndTypedCallsAfterInputDies) {
  std::optional<ResolvedModule> owned;
  {
    std::string source{k_owned_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  const ResolvedModule& module = *owned;
  ASSERT_EQ(module.header.regions.size(), 2u);
  EXPECT_EQ(module.header.regions[0].version, (checker::PtxVersion{9, 3}));
  EXPECT_TRUE(module.header.regions[0].target_options.empty());
  EXPECT_EQ(module.header.regions[0].address_size_bits, 64u);
  EXPECT_EQ(module.header.regions[1].version, (checker::PtxVersion{9, 3}));
  EXPECT_EQ(module.header.regions[1].target_options,
            (std::vector<std::string>{"sm_90a"}));

  ASSERT_EQ(module.functions.size(), 3u);
  const ResolvedFunction& callee = module.functions[0];
  const ResolvedFunction& callee_alias = module.functions[1];
  const ResolvedFunction& kernel = module.functions[2];
  ASSERT_TRUE(callee.source_region.has_value());
  EXPECT_EQ(*callee.source_region, 0u);
  ASSERT_EQ(callee.contract.signature.parameters.size(), 1u);
  EXPECT_EQ(callee.contract.signature.parameters.front().scalar_type,
            base::ScalarType::U32);
  EXPECT_EQ(callee_alias.contract.canonical_function,
            callee.contract.canonical_function);
  ASSERT_EQ(module.function_aliases.size(), 1u);
  const ResolvedFunctionAlias& alias = module.function_aliases.front();
  EXPECT_EQ(alias.name, "callee_alias");
  EXPECT_EQ(alias.aliasee_name, "callee");
  EXPECT_EQ(alias.canonical_function, callee.contract.canonical_function);
  EXPECT_EQ(alias.source_region, 1u);

  ASSERT_TRUE(kernel.source_region.has_value());
  EXPECT_EQ(*kernel.source_region, 1u);
  EXPECT_EQ(kernel.source_target, "sm_90a");
  EXPECT_EQ(kernel.source_version, (checker::PtxVersion{9, 3}));
  ASSERT_EQ(kernel.contract.resources.size(), 3u);
  const ResolvedKernelResourceContract& max_threads =
      kernel.contract.resources[0];
  EXPECT_EQ(max_threads.kind, ResolvedKernelResourceKind::MaxNtid);
  EXPECT_EQ(max_threads.explicit_value_count, 1u);
  EXPECT_EQ(max_threads.values, (std::vector<uint32_t>{8, 1, 1}));
  const ResolvedKernelResourceContract& cluster = kernel.contract.resources[1];
  EXPECT_EQ(cluster.kind, ResolvedKernelResourceKind::ReqNctaPerCluster);
  EXPECT_EQ(cluster.explicit_value_count, 2u);
  EXPECT_EQ(cluster.values, (std::vector<uint32_t>{2, 1, 1}));
  EXPECT_EQ(kernel.contract.resources[2].kind,
            ResolvedKernelResourceKind::ExplicitCluster);
  EXPECT_TRUE(kernel.contract.resources[2].values.empty());

  ASSERT_EQ(kernel.parameter_declarations.size(), 2u);
  const ResolvedParameterDeclaration& local = kernel.parameter_declarations[1];
  EXPECT_EQ(local.role, ParameterDeclarationRole::BodyLocal);
  EXPECT_EQ(local.scalar_type, base::ScalarType::U32);
  EXPECT_EQ(local.alignment, 8u);
  EXPECT_TRUE(local.explicit_alignment);
  EXPECT_EQ(local.array_extents, (std::vector<std::optional<uint64_t>>{2u}));
  EXPECT_EQ(local.byte_extent, 8u);

  ASSERT_EQ(kernel.call_target_sets.size(), 1u);
  const ResolvedCallTargetSetContract& target_set =
      kernel.call_target_sets.front();
  ASSERT_EQ(target_set.targets.size(), 2u);
  EXPECT_EQ(target_set.targets[0].name, "callee");
  EXPECT_EQ(target_set.targets[1].name, "callee_alias");
  EXPECT_EQ(target_set.targets[0].canonical_function,
            target_set.targets[1].canonical_function);
  EXPECT_EQ(target_set.targets[0].canonical_function,
            callee.contract.canonical_function);
  EXPECT_EQ(target_set.signature, callee.contract.signature);
  ASSERT_EQ(kernel.call_prototypes.size(), 1u);
  const ResolvedCallPrototypeContract& prototype =
      kernel.call_prototypes.front();
  ASSERT_EQ(prototype.signature.parameters.size(), 1u);
  EXPECT_EQ(prototype.signature.parameters.front().scalar_type,
            base::ScalarType::U32);

  ASSERT_EQ(kernel.branch_target_sets.size(), 1u);
  const ResolvedBranchTargetSetContract& branches =
      kernel.branch_target_sets.front();
  ASSERT_EQ(branches.targets.size(), 3u);
  EXPECT_EQ(branches.targets[0].name, "done0");
  EXPECT_EQ(branches.targets[1].name, "done1");
  EXPECT_EQ(branches.targets[2].name, "done0");
  EXPECT_NE(branches.targets[0].symbol_id, branches.targets[1].symbol_id);
  EXPECT_EQ(branches.targets[0].symbol_id, branches.targets[2].symbol_id);

  ASSERT_EQ(kernel.body.size(), 4u);
  ASSERT_EQ(kernel.instruction_ranges.size(), kernel.body.size());
  ASSERT_EQ(kernel.instruction_opcodes.size(), kernel.body.size());
  EXPECT_EQ(kernel.instruction_opcodes.back(), "ret");
  EXPECT_NE(kernel.instruction_ranges.back(), SourceRange{});
  const auto& direct = std::get<Call::Direct::TargetInputOperands>(
      std::get<Call::Direct>(std::get<Call>(kernel.body[0]).variant).operands);
  const auto& direct_literal = std::get<ResolvedCallLiteral>(
      direct.arguments.value.values.front().value);
  ASSERT_TRUE(direct_literal.value.has_value());
  EXPECT_EQ(direct_literal.value->type, base::ScalarType::U32);
  EXPECT_EQ(direct_literal.value->bits, 7u);
  const auto& indirect = std::get<Call::Direct::TargetInputMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(kernel.body[1]).variant).operands);
  const auto& indirect_literal = std::get<ResolvedCallLiteral>(
      indirect.arguments.value.values.front().value);
  ASSERT_TRUE(indirect_literal.value.has_value());
  EXPECT_EQ(indirect_literal.value->type, base::ScalarType::U32);
  EXPECT_EQ(indirect_literal.value->bits, 8u);

  EXPECT_TRUE(validateModule(module, ModuleValidationPolicy::AvailableContext));
  const auto strict_validation =
      validateModule(module, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(strict_validation.has_value());
  ASSERT_FALSE(strict_validation.error().empty());
  EXPECT_EQ(strict_validation.error().front().kind,
            checker::CheckDiagnosticKind::MissingValidationContext);
}

/** Fully targeted modules pass strict owned validation after all parser state dies. */
TEST(OwnedModuleHandoff, ValidatesCompleteContextWithoutAst) {
  std::optional<ResolvedModule> owned;
  {
    std::string source{k_complete_context_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  const auto validation =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_TRUE(validation.has_value())
      << (validation.has_value() || validation.error().empty()
              ? "Owned fixture did not validate."
              : validation.error().front().message);
}

/** Generated operand members retain checked group identities after AST destruction. */
TEST(OwnedModuleHandoff, RejectsMutatedGeneratedOperandMemberIdentities) {
  std::optional<ResolvedModule> owned;
  {
    std::string source{k_member_identity_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  ResolvedModule& module = *owned;
  ASSERT_EQ(module.functions.size(), 1u);
  ResolvedFunction& kernel = module.functions.front();
  ASSERT_EQ(kernel.body.size(), 2u);
  ASSERT_TRUE(
      validateModule(module, ModuleValidationPolicy::RequireCompleteContext));

  Mov& mov = std::get<Mov>(kernel.body.front());
  Mov::Scalar& scalar = std::get<Mov::Scalar>(mov.variant);
  auto& operands = std::get<Mov::Scalar::ScalarOperands>(scalar.operands);
  ResolvedRegisterRef& source =
      std::get<ResolvedRegisterRef>(operands.src.value);
  ASSERT_EQ(source.parameterized_index, 1u);
  ASSERT_TRUE(source.symbol_id.has_value());

  const auto original_parameterized_index = source.parameterized_index;
  source.parameterized_index = 2u;
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  source.parameterized_index = original_parameterized_index;

  const auto original_symbol_id = source.symbol_id;
  source.symbol_id = kernel.symbol_id;
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  source.symbol_id = original_symbol_id;

  source.symbol_id =
      binding::SymbolId{.value = std::numeric_limits<uint32_t>::max()};
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  source.symbol_id = original_symbol_id;
}

/** Strict validation reports only modeled target and version context omissions. */
TEST(OwnedModuleHandoff, RejectsUnknownOrVersionlessHeaderTargetWithoutAst) {
  std::optional<ResolvedModule> unknown_target;
  {
    std::string source{k_unknown_target_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    unknown_target.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(unknown_target.has_value());
  expect_owned_validation_kind(*unknown_target,
                               ModuleValidationPolicy::RequireCompleteContext,
                               checker::CheckDiagnosticKind::UnknownTarget);

  std::optional<ResolvedModule> missing_version;
  {
    std::string source{k_missing_version_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    missing_version.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(missing_version.has_value());
  expect_owned_validation_kind(
      *missing_version, ModuleValidationPolicy::RequireCompleteContext,
      checker::CheckDiagnosticKind::MissingValidationContext);
}

/** Header provenance permits only PTX's explicit version and target rules. */
TEST(OwnedModuleHandoff, RejectsMalformedOwnedHeaderProvenance) {
  std::optional<ResolvedModule> owned;
  {
    std::string source{k_member_identity_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  ResolvedModule& module = *owned;
  ASSERT_EQ(module.header.regions.size(), 2u);
  expect_owned_validation_success(
      module, ModuleValidationPolicy::RequireCompleteContext);
  ResolvedSourceTargetRegion& target = module.header.regions[1];

  const auto original_version_provenance = target.version_provenance;
  target.version_provenance = SourceConfigurationProvenance::Defaulted;
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  target.version_provenance = original_version_provenance;

  const auto original_target_provenance = target.target_provenance;
  target.target_provenance = SourceConfigurationProvenance::Defaulted;
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  target.target_provenance = original_target_provenance;

  const auto original_address_size = target.address_size_bits;
  const auto original_address_provenance = target.address_size_provenance;
  target.address_size_bits = 64u;
  target.address_size_provenance = SourceConfigurationProvenance::Defaulted;
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  target.address_size_bits = original_address_size;
  target.address_size_provenance = original_address_provenance;

  target.version_provenance = static_cast<SourceConfigurationProvenance>(255);
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  target.version_provenance = original_version_provenance;
}

/** Entry-only resources reject duplicated, invalid, and device-function payloads. */
TEST(OwnedModuleHandoff, RejectsMalformedOwnedResourceContracts) {
  std::optional<ResolvedModule> owned;
  {
    std::string source{k_resource_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  ResolvedFunction& kernel = owned->functions.front();
  expect_owned_validation_success(
      *owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_EQ(kernel.contract.resources.size(), 2u);

  kernel.contract.resources.push_back(kernel.contract.resources.front());
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  kernel.contract.resources.pop_back();

  const auto original_kind = kernel.contract.resources.front().kind;
  kernel.contract.resources.front().kind =
      static_cast<ResolvedKernelResourceKind>(255);
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  kernel.contract.resources.front().kind = original_kind;

  const bool original_is_entry = kernel.is_entry;
  const bool original_signature_entry = kernel.contract.signature.is_entry;
  kernel.is_entry = false;
  kernel.contract.signature.is_entry = false;
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  kernel.is_entry = original_is_entry;
  kernel.contract.signature.is_entry = original_signature_entry;
}

/** Function attributes retain only one numeric `.unified` contract. */
TEST(OwnedModuleHandoff, RejectsMalformedOwnedFunctionAttributes) {
  std::optional<ResolvedModule> owned;
  {
    std::string source{k_attribute_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  ResolvedFunction& function = owned->functions.front();
  expect_owned_validation_success(
      *owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_EQ(function.contract.attributes.size(), 1u);
  ResolvedFunctionAttribute& attribute = function.contract.attributes.front();
  ASSERT_EQ(attribute.kind, ResolvedFunctionAttributeKind::Unified);
  ASSERT_EQ(attribute.values.size(), 2u);

  const auto original_kind = attribute.kind;
  attribute.kind = ResolvedFunctionAttributeKind::Managed;
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  attribute.kind = original_kind;

  function.contract.attributes.push_back(attribute);
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  function.contract.attributes.pop_back();

  // ``push_back`` may have reallocated the vector, so reacquire its surviving
  // element before performing the remaining independent mutations.
  ResolvedFunctionAttribute& restored_attribute =
      function.contract.attributes.front();
  restored_attribute.kind = static_cast<ResolvedFunctionAttributeKind>(255);
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  restored_attribute.kind = original_kind;

  const auto original_values = restored_attribute.values;
  restored_attribute.values.pop_back();
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  restored_attribute.values = original_values;

  restored_attribute.values.front() = "not-a-number";
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  restored_attribute.values = original_values;
}

/** Duplicated function flags and ABI suffix variants remain internally coherent. */
TEST(OwnedModuleHandoff, RejectsMalformedOwnedFunctionContractFlags) {
  std::optional<ResolvedModule> owned;
  {
    std::string source{k_function_contract_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  ResolvedFunction& function = owned->functions.front();
  expect_owned_validation_success(
      *owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_TRUE(function.contract.is_noreturn);
  ASSERT_TRUE(function.contract.abi_preserve.has_value());
  ASSERT_TRUE(function.contract.abi_preserve_control.has_value());

  const bool original_is_entry = function.is_entry;
  function.is_entry = !original_is_entry;
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  function.is_entry = original_is_entry;

  const bool original_noreturn = function.contract.is_noreturn;
  function.contract.is_noreturn = !original_noreturn;
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  function.contract.is_noreturn = original_noreturn;

  const auto original_returns = function.contract.signature.return_parameters;
  ASSERT_FALSE(function.contract.signature.parameters.empty());
  function.contract.signature.return_parameters.push_back(
      function.contract.signature.parameters.front());
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  function.contract.signature.return_parameters = original_returns;

  const bool original_preserve_variant =
      function.contract.abi_preserve->control_registers;
  function.contract.abi_preserve->control_registers = true;
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  function.contract.abi_preserve->control_registers = original_preserve_variant;

  const bool original_control_variant =
      function.contract.abi_preserve_control->control_registers;
  function.contract.abi_preserve_control->control_registers = false;
  expect_owned_model_mismatch(*owned,
                              ModuleValidationPolicy::RequireCompleteContext);
  function.contract.abi_preserve_control->control_registers =
      original_control_variant;
}

/** Owned call ABI checks reject mutation while retaining legal trailing byte omission. */
TEST(OwnedModuleHandoff, RejectsMalformedOwnedCallContractsWithoutAst) {
  std::optional<ResolvedModule> owned;
  {
    std::string source{k_call_contract_module_fixture};
    const auto parsed = parse_owned_module_fixture(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.has_value() || resolved.error().empty()
                ? "Source fixture did not resolve."
                : resolved.error().front().message);
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  ResolvedModule& module = *owned;
  ASSERT_EQ(module.functions.size(), 6u);
  ResolvedFunction& no_args = module.functions[0];
  ResolvedFunction& formal = module.functions[1];
  ResolvedFunction& returns_u32 = module.functions[2];
  ResolvedFunction& caller = module.functions[5];
  ASSERT_EQ(caller.body.size(), 6u);
  expect_owned_validation_success(
      module, ModuleValidationPolicy::RequireCompleteContext);

  auto& target_only = std::get<Call::Direct::TargetOperands>(
      std::get<Call::Direct>(std::get<Call>(caller.body[0]).variant).operands);
  ASSERT_TRUE(target_only.target.value.symbol_id.has_value());
  const auto original_target = target_only.target.value.symbol_id;
  target_only.target.value.symbol_id = formal.symbol_id;
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  target_only.target.value.symbol_id = original_target;
  EXPECT_EQ(original_target, no_args.symbol_id);

  ASSERT_EQ(returns_u32.contract.signature.return_parameters.size(), 1u);
  const auto original_returns =
      returns_u32.contract.signature.return_parameters;
  returns_u32.contract.signature.return_parameters.push_back(
      returns_u32.contract.signature.return_parameters.front());
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  returns_u32.contract.signature.return_parameters = original_returns;

  const auto original_return_type =
      returns_u32.contract.signature.return_parameters.front().scalar_type;
  returns_u32.contract.signature.return_parameters.front().scalar_type =
      base::ScalarType::U64;
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  returns_u32.contract.signature.return_parameters.front().scalar_type =
      original_return_type;

  auto& parameter_call = std::get<Call::Direct::TargetInputOperands>(
      std::get<Call::Direct>(std::get<Call>(caller.body[2]).variant).operands);
  ASSERT_EQ(parameter_call.arguments.value.values.size(), 1u);
  auto& parameter_actual = std::get<ResolvedCallParameterRef>(
      parameter_call.arguments.value.values.front().value);
  ASSERT_EQ(parameter_actual.declared_type, base::ScalarType::U32);
  const auto original_parameter_type = parameter_actual.declared_type;
  parameter_actual.declared_type = base::ScalarType::U64;
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  parameter_actual.declared_type = original_parameter_type;

  auto& metadata_call = std::get<Call::Direct::TargetInputMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(caller.body[3]).variant).operands);
  ASSERT_EQ(metadata_call.arguments.value.values.size(), 1u);
  ASSERT_EQ(caller.call_prototypes.size(), 1u);
  const auto original_prototypes = caller.call_prototypes;
  caller.call_prototypes.clear();
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  caller.call_prototypes = original_prototypes;

  const auto original_prototype_signature =
      caller.call_prototypes.front().signature;
  caller.call_prototypes.front().signature.parameters.clear();
  expect_owned_model_mismatch(module,
                              ModuleValidationPolicy::RequireCompleteContext);
  caller.call_prototypes.front().signature = original_prototype_signature;

  ResolvedFunction& trailing = module.functions[4];
  ASSERT_EQ(trailing.contract.signature.parameters.size(), 2u);
  EXPECT_TRUE(trailing.contract.signature.parameters.back().is_array);
  EXPECT_FALSE(
      trailing.contract.signature.parameters.back().array_extent.has_value());
  const auto& trailing_call = std::get<Call::Direct::TargetInputOperands>(
      std::get<Call::Direct>(std::get<Call>(caller.body[4]).variant).operands);
  EXPECT_EQ(trailing_call.arguments.value.values.size(), 1u);
  expect_owned_validation_success(
      module, ModuleValidationPolicy::RequireCompleteContext);
}

/** AST-free validation rejects mutated public data without reading parser state. */
TEST(OwnedModuleHandoff, RejectsMalformedOwnedContractsWithoutAst) {
  const auto parsed = parse_owned_module_fixture(k_owned_module_fixture);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved.has_value())
      << (resolved.has_value() || resolved.error().empty()
              ? "Source fixture did not resolve."
              : resolved.error().front().message);
  ResolvedModule module = std::move(*resolved);
  ASSERT_EQ(module.functions.size(), 3u);
  ResolvedFunction& kernel = module.functions[2];

  const auto original_ranges = kernel.instruction_ranges;
  kernel.instruction_ranges.clear();
  expect_owned_model_mismatch(module);
  kernel.instruction_ranges = original_ranges;

  auto& direct = std::get<Call::Direct::TargetInputOperands>(
      std::get<Call::Direct>(std::get<Call>(kernel.body[0]).variant).operands);
  auto& direct_literal = std::get<ResolvedCallLiteral>(
      direct.arguments.value.values.front().value);
  ASSERT_TRUE(direct_literal.value.has_value());
  const base::ScalarType original_type = direct_literal.value->type;
  direct_literal.value->type = base::ScalarType::U64;
  expect_owned_model_mismatch(module);
  direct_literal.value->type = original_type;

  const auto original_arguments = direct.arguments.value.values;
  direct.arguments.value.values.clear();
  expect_owned_model_mismatch(module);
  direct.arguments.value.values = original_arguments;

  ASSERT_EQ(kernel.branch_target_sets.size(), 1u);
  ResolvedBranchTargetSetContract& branches = kernel.branch_target_sets.front();
  const binding::SymbolId original_label = branches.targets.front().symbol_id;
  branches.targets.front().symbol_id = branches.symbol_id;
  expect_owned_model_mismatch(module);
  branches.targets.front().symbol_id = original_label;

  ASSERT_EQ(kernel.call_target_sets.size(), 1u);
  ResolvedCallTargetSetContract& targets = kernel.call_target_sets.front();
  const binding::SymbolId original_target = targets.targets.front().symbol_id;
  targets.targets.front().symbol_id = targets.symbol_id;
  expect_owned_model_mismatch(module);
  targets.targets.front().symbol_id = original_target;

  ASSERT_EQ(kernel.contract.resources.size(), 3u);
  const auto original_values = kernel.contract.resources.front().values;
  kernel.contract.resources.front().values.pop_back();
  expect_owned_model_mismatch(module);
  kernel.contract.resources.front().values = original_values;

  kernel.contract.resources.front().values[1] = 2u;
  expect_owned_model_mismatch(module);
  kernel.contract.resources.front().values = original_values;

  const auto original_address_size = module.header.regions[1].address_size_bits;
  module.header.regions[1].address_size_bits = 16u;
  expect_owned_model_mismatch(module);
  module.header.regions[1].address_size_bits = original_address_size;

  const auto original_region = kernel.source_region;
  kernel.source_region = module.header.regions.size();
  expect_owned_model_mismatch(module);
  kernel.source_region = original_region;
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

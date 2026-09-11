#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

const Add::IntegerNoSat& resolvedIntegerAdd(
    const ResolvedInstruction& instruction) {
  return std::get<Add::IntegerNoSat>(std::get<Add>(instruction).variant);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov::Scalar& mov) {
  return std::get<Mov::Scalar::ScalarOperands>(mov.operands);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov& mov) {
  return scalarMovOperands(std::get<Mov::Scalar>(mov.variant));
}

const Mov::Scalar::PackOperands& packMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

Mov::Scalar::PackOperands& packMovOperands(Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

const Mov::Scalar::UnpackOperands& unpackMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::UnpackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

TEST(ResolvedModule, ResolvesClusterSpecialRegisterFamilies) {
  const auto resolve_scalar = [](std::string_view instruction) {
    const auto ast = test_helpers::parseInstruction(instruction);
    if (!ast || !ast.diagnostics.empty()) {
      ADD_FAILURE() << (ast.diagnostics.empty()
                            ? "PTX source did not produce a syntax instruction."
                            : ast.diagnostics.front().message);
      return std::expected<ResolvedInstruction, ResolveDiagnostic>{
          std::unexpected(ResolveDiagnostic{.message = "instruction parse failed"})};
    }
    return resolveInstruction(*ast);
  };
  for (const std::string_view source : {
           "mov.pred %p0, %is_explicit_cluster;",
           "mov.u32 %r0, %cluster_ctarank;",
           "mov.u32 %r0, %cluster_nctarank;",
           "mov.u32 %r0, %clusterid.x;",
           "mov.u32 %r0, %clusterid.y;",
           "mov.u32 %r0, %clusterid.z;",
           "mov.u32 %r0, %nclusterid.x;",
           "mov.u32 %r0, %nclusterid.y;",
           "mov.u32 %r0, %nclusterid.z;",
           "mov.u32 %r0, %cluster_ctaid.x;",
           "mov.u32 %r0, %cluster_ctaid.y;",
           "mov.u32 %r0, %cluster_ctaid.z;",
           "mov.u32 %r0, %cluster_nctaid.x;",
           "mov.u32 %r0, %cluster_nctaid.y;",
           "mov.u32 %r0, %cluster_nctaid.z;",
       }) {
    const auto resolved = resolve_scalar(source);
    ASSERT_TRUE(resolved.has_value())
        << source << ": " << resolved.error().message;
  }

  const auto explicit_cluster =
      resolve_scalar("mov.pred %p0, %is_explicit_cluster;");
  ASSERT_TRUE(explicit_cluster.has_value())
      << explicit_cluster.error().message;
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = checker::TargetInfo{.ptx_version = {7, 8},
                                    .sm_version = 90,
                                    .capabilities = cluster_capabilities},
  };
  EXPECT_TRUE(checker::check(std::get<Mov>(*explicit_cluster), supported)
                  .has_value());
  const auto& special_source = std::get<ResolvedSpecialRegisterRef>(
      std::get<Mov::Pred>(std::get<Mov>(*explicit_cluster).variant)
          .src.value);
  EXPECT_EQ(special_source.id, base::lookup("%is_explicit_cluster")->id);
  auto old_ptx = supported;
  old_ptx.target.ptx_version = {7, 7};
  const auto rejected =
      checker::check(std::get<Mov>(*explicit_cluster), old_ptx);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  auto old_sm = supported;
  old_sm.target.sm_version = 80;
  const auto sm_rejected =
      checker::check(std::get<Mov>(*explicit_cluster), old_sm);
  ASSERT_FALSE(sm_rejected.has_value());
  EXPECT_EQ(sm_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  EXPECT_FALSE(resolve_scalar("mov.pred %p0, %cluster_ctarank;").has_value());
  for (const std::string_view source : {
           R"ptx(.entry kernel() {
  .reg .u32 %r<3>;
  @%is_explicit_cluster add.u32 %r0, %r1, %r2;
})ptx",
           R"ptx(.entry kernel() {
  .reg .u32 %r<3>;
  @%cluster_ctarank add.u32 %r0, %r1, %r2;
})ptx",
       }) {
    const auto parsed_module_1 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
    EXPECT_FALSE(resolveModule(*parsed_module_1).has_value()) << source;
  }

  PtxSyntaxParser invalid_parser("mov.u32 %r0, %clusterid.w;");
  const auto invalid_ast = invalid_parser.parseInstruction();
  ASSERT_TRUE(invalid_ast.has_value())
      << invalid_ast.diagnostics.front().message;
  const auto invalid = resolveInstruction(*invalid_ast);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().message,
            "Unknown special register '%clusterid.w'.");
}

TEST(ResolvedModule, ResolvesV4ClusterSpecialRegisterMoves) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .v4 .b32 %r0, %r1, %r2, %r3;
  mov.v4.u32 %r0, %clusterid;
  mov.v4.u32 %r1, %nclusterid;
  mov.v4.u32 %r2, %cluster_ctaid;
  mov.v4.u32 %r3, %cluster_nctaid;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  for (const auto& instruction : body) {
    const auto& vector =
        std::get<Mov::V4U32>(std::get<Mov>(instruction).variant);
    EXPECT_EQ(vector.dst.value.register_ref.vector_width, 4u);
    EXPECT_EQ(vector.dst.value.register_ref.declared_type, ScalarType::B32);
    EXPECT_EQ(base::metadata(vector.src.value.id).vector_width, 4u);
  }

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = checker::TargetInfo{.ptx_version = {7, 8},
                                    .sm_version = 90,
                                    .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Mov>(body[0]), supported).has_value());
  auto old_ptx = supported;
  old_ptx.target.ptx_version = {7, 7};
  const auto ptx_rejected = checker::check(std::get<Mov>(body[0]), old_ptx);
  ASSERT_FALSE(ptx_rejected.has_value());
  EXPECT_EQ(ptx_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  auto old_sm = supported;
  old_sm.target.sm_version = 80;
  const auto sm_rejected = checker::check(std::get<Mov>(body[0]), old_sm);
  ASSERT_FALSE(sm_rejected.has_value());
  EXPECT_EQ(sm_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  const auto reject = [](std::string_view declaration,
                         std::string_view source) {
    const auto parsed = parseModule(fmt::format(R"ptx(
.entry kernel() {{
  {}
  mov.v4.u32 %r0, {};
}}
)ptx",
                                                 declaration, source));
    if (!parsed || !parsed.diagnostics.empty()) {
      ADD_FAILURE() << (parsed.diagnostics.empty()
                            ? "PTX source did not produce a syntax module."
                            : parsed.diagnostics.front().message);
      return std::expected<ResolvedModule, ModuleResolveDiagnostics>{
          std::unexpected(ModuleResolveDiagnostics{})};
    }
    return resolveModule(*parsed);
  };
  EXPECT_FALSE(reject(".reg .u32 %r0;", "%clusterid").has_value());
  EXPECT_FALSE(reject(".reg .v4 .u32 %r0;", "%clusterid.x").has_value());
  EXPECT_FALSE(reject(".reg .v4 .u32 %r0;", "%cluster_ctarank").has_value());

  for (const std::string_view source : {
           R"ptx(.entry kernel() {
  .reg .u32 %r0;
  mov.u32 %r0, %clusterid;
})ptx",
           R"ptx(.entry kernel() {
  .reg .u8 %b<4>;
  mov.b32 {%b0, %b1, %b2, %b3}, %clusterid;
})ptx",
           R"ptx(.entry kernel() {
  .reg .u64 %rd0;
  cvta.global.u64 %rd0, %clusterid;
})ptx",
       }) {
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    EXPECT_FALSE(resolveModule(*parsed_module_3).has_value()) << source;
  }
}

TEST(ResolvedModule, ChecksModuleTargetAvailabilityWithCatalogProfiles) {
  constexpr std::string_view no_target = R"ptx(
.version 7.8
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx";
  const auto parsed_module_1 = parseModule(no_target);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto parsed_module_2 = parseModule(R"ptx(
.version 7.8
.target sm_80
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto unavailable = checkModuleAvailability(*parsed_module_2,
                                                   *resolved);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 2u);
  for (const auto& diagnostic : unavailable.error()) {
    EXPECT_EQ(diagnostic.kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
  }

  const auto parsed_module_3 = parseModule(R"ptx(
.version 7.8
.target sm_90a, texmode_independent
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto supported = checkModuleAvailability(*parsed_module_3,
                                                 *resolved);
  EXPECT_TRUE(supported.has_value());

  const auto parsed_module_4 = parseModule(R"ptx(
.version 7.8
.target sm_123a
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto unknown = checkModuleAvailability(*parsed_module_4,
                                               *resolved);
  ASSERT_FALSE(unknown.has_value());
  ASSERT_EQ(unknown.error().size(), 1u);
  EXPECT_EQ(unknown.error().front().kind,
            checker::CheckDiagnosticKind::UnknownTarget);

  const auto parsed_module_5 = parseModule(R"ptx(
.version 7.8
.target sm_123a
.entry kernel() { ret; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto pipeline_unknown = resolveModule(*parsed_module_5);
  ASSERT_FALSE(pipeline_unknown.has_value());
  ASSERT_EQ(pipeline_unknown.error().size(), 1u);
  EXPECT_EQ(pipeline_unknown.error().front().message,
            "Unknown validation target 'sm_123a'.");
}

TEST(ResolvedModule, KeepsClusterModuleMetadataBoundToSm90AndCapability) {
  constexpr std::string_view body = R"ptx(
.entry required() .reqnctapercluster 2, 1 .explicitcluster { ret; }
.entry maximum() .maxclusterrank 8 { ret; }
.entry clustered() .reqntid 1 .reqnctapercluster 1 .blocksareclusters { ret; }
)ptx";
  const auto parsed_module_1 = parseModule(
      std::string(".version 9.0\n") + std::string(body));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto for_target = [body](std::string_view target) {
    return parseModule(std::string(".version 9.0\n.target ") +
                       std::string(target) + "\n" + std::string(body));
  };

  const auto sm90a = for_target("sm_90a");
  ASSERT_MODULE_PARSE_SUCCEEDS(sm90a);
  EXPECT_TRUE(checkModuleAvailability(*sm90a, *resolved)
                  .has_value());
  const auto sm90 = for_target("sm_90");
  ASSERT_MODULE_PARSE_SUCCEEDS(sm90);
  EXPECT_TRUE(checkModuleAvailability(*sm90, *resolved)
                  .has_value());
  const auto sm80 = for_target("sm_80");
  ASSERT_MODULE_PARSE_SUCCEEDS(sm80);
  const auto checked = checkModuleAvailability(*sm80, *resolved);
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 5u);
  for (const auto& diagnostic : checked.error())
    EXPECT_EQ(diagnostic.kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
}

TEST(ResolvedModule, ChecksClusterCapabilityAcrossModernInstructionSlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 16 .b8 response[16];
.shared .align 8 .b64 barrier[2];
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  mov.u32 %r0, %cluster_ctarank;
  barrier.cluster.arrive.release;
  mapa.shared::cluster.u32 %r0, %r1, 0;
  getctarank.shared::cluster.u32 %r0, %r1;
  mbarrier.expect_tx.shared::cluster.b64 [barrier], 1;
  mbarrier.arrive.release.cluster.shared::cluster.b64 _, [barrier], 1;
  ld.global.relaxed.cluster.u32 %r0, [global_value];
  st.global.relaxed.cluster.u32 [global_value], %r0;
  fence.proxy.async.shared::cluster;
  clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 [response], [barrier];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  const auto check = [&ast](const ResolvedInstruction& instruction,
                            const checker::TargetInfo& target) {
    return std::visit(
        [&target, &ast](const auto& resolved_instruction) {
          return checker::check(
              resolved_instruction,
              checker::Context{.target = target, .instruction_range = ast.range});
        },
        instruction);
  };
  const auto sm100a = base::find_target_profile("sm_100a");
  ASSERT_TRUE(sm100a.has_value());
  const checker::TargetInfo supported{
      .ptx_version = {8, 6},
      .sm_version = 100,
      .enabled_family_features = sm100a->enabled_family_features,
      .identity = sm100a->identity,
      .capabilities = sm100a->capabilities,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(check(instruction, supported).has_value());

  const checker::TargetInfo no_cluster{.ptx_version = {8, 6}, .sm_version = 100};
  for (const auto& instruction : body) {
    const auto checked = check(instruction, no_cluster);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
  }
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::TargetInfo synthetic_sm80{
      .ptx_version = {8, 6}, .sm_version = 80, .capabilities = cluster_capabilities};
  for (const auto& instruction : body) {
    const auto checked = check(instruction, synthetic_sm80);
    ASSERT_FALSE(checked.has_value());
    EXPECT_TRUE(checked.error().front().kind ==
                    checker::CheckDiagnosticKind::UnsupportedSmVersion ||
                checked.error().front().kind ==
                    checker::CheckDiagnosticKind::UnsupportedAvailability);
  }

  const auto parsed_module_2 = parseModule(R"ptx(
.shared .align 8 .b64 barrier[2];
.entry kernel() { mbarrier.arrive.release.cta.shared.b64 _, [barrier]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto cta = resolveModule(*parsed_module_2);
  ASSERT_TRUE(cta.has_value()) << cta.error().front().message;
  EXPECT_TRUE(checker::check(
                  std::get<Mbarrier>(cta->functions.front().body.front()),
                  checker::Context{.target = {.ptx_version = {8, 0}, .sm_version = 90}})
                  .has_value());
}

TEST(ResolvedModule, AppliesFamilyProfilesThroughProductionAvailability) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 9.2
.entry kernel() {
  .reg .b32 %r<3>;
  add.u8x4 %r0, %r1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto parsed_module_2 = parseModule(R"ptx(
.version 9.2
.target sm_120f
.entry kernel() {
  .reg .b32 %r<3>;
  add.u8x4 %r0, %r1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto sm120f = checkModuleAvailability(*parsed_module_2, *resolved);
  EXPECT_TRUE(sm120f.has_value());

  const auto parsed_module_3 = parseModule(R"ptx(
.version 9.2
.target sm_100f
.entry kernel() {
  .reg .b32 %r<3>;
  add.u8x4 %r0, %r1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto sm100f = resolveModule(*parsed_module_3);
  ASSERT_FALSE(sm100f.has_value());
  EXPECT_EQ(sm100f.error().back().message,
            "Instruction variant 'PackedOptionalSat' requires target family "
            "'sm_120f'.");
}

TEST(ResolvedModule, AppliesTargetProfilesInSourceOrder) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 9.3
.target sm_80
.entry first() { ret; }
.target sm_90
.entry cluster() {
  .reg .u32 %r;
  { mov.u32 %r, %cluster_ctarank; }
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto supported = resolveModule(*parsed_module_1);
  ASSERT_TRUE(supported.has_value()) << supported.error().front().message;
  EXPECT_EQ(supported->functions.size(), 2u);

  const auto parsed_module_2 = parseModule(R"ptx(
.version 9.3
.target sm_90
.entry first() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
.target sm_80
.entry cluster() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto rejected = resolveModule(*parsed_module_2);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().message,
            "Operand value '%cluster_ctarank' has no matching availability "
            "clause.");
}

TEST(ResolvedModule, ClearsUnknownTargetAndKeepsFunctionIndicesAligned) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 9.3
.target sm_90
.entry first() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
.target sm_123a
.entry skipped() { ret; }
.target sm_80
.entry rejected() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto rejected = resolveModule(ast);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].message,
            "Unknown validation target 'sm_123a'.");
  EXPECT_EQ(rejected.error()[0].range,
            std::get<syntax_ast::AstTargetDirective>(ast.items[3]).range);
  EXPECT_EQ(rejected.error()[1].message,
            "Operand value '%cluster_ctarank' has no matching availability "
            "clause.");
}

TEST(ResolvedModule, SharesDirectiveAvailabilityAcrossFunctionsAndPrototypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 9.3
.func alias_fn(.param .u32 input) .noreturn;
.func target(.param .u32 input) .noreturn .abi_preserve 1 .abi_preserve_control 1 {
prototype: .callprototype _ .noreturn .abi_preserve 1 .abi_preserve_control 1;
}
.alias alias_fn, target;
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto parsed_module_2 = parseModule(R"ptx(
.version 9.3
.target sm_30
.func alias_fn(.param .u32 input) .noreturn;
.func target(.param .u32 input) .noreturn .abi_preserve 1 .abi_preserve_control 1 {
prototype: .callprototype _ .noreturn .abi_preserve 1 .abi_preserve_control 1;
}
.alias alias_fn, target;
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto sm30 = checkModuleAvailability(*parsed_module_2, *resolved);
  ASSERT_FALSE(sm30.has_value());
  ASSERT_EQ(sm30.error().size(), 4u);
  for (const auto& diagnostic : sm30.error()) {
    EXPECT_EQ(diagnostic.kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
  }

  const auto parsed_module_3 = parseModule(R"ptx(
.version 9.3
.target sm_80
.func alias_fn(.param .u32 input) .noreturn;
.func target(.param .u32 input) .noreturn .abi_preserve 1 .abi_preserve_control 1 {
prototype: .callprototype _ .noreturn .abi_preserve 1 .abi_preserve_control 1;
}
.alias alias_fn, target;
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto sm80 = checkModuleAvailability(*parsed_module_3, *resolved);
  EXPECT_TRUE(sm80.has_value());
}

TEST(ResolvedModule, ChecksNestedInstructionModuleAvailability) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 0.9
.entry kernel() {
  .reg .u32 %r<3>;
  { add.u32 %r0, %r1, %r2; }
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto parsed_module_2 = parseModule(R"ptx(
.version 0.9
.target sm_80
.entry kernel() {
  .reg .u32 %r<3>;
  { add.u32 %r0, %r1, %r2; }
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto unavailable = checkModuleAvailability(*parsed_module_2,
                                                   *resolved);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
}

TEST(ResolvedModule, ChecksAttributeModuleTargetAvailability) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 8.0
.global .attribute(.unified(1, 2)) .u32 managed;
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto parsed_module_2 = parseModule(R"ptx(
.version 8.0
.target sm_80
.global .attribute(.unified(1, 2)) .u32 managed;
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto sm80 = checkModuleAvailability(*parsed_module_2,
                                            *resolved);
  ASSERT_FALSE(sm80.has_value());
  ASSERT_EQ(sm80.error().size(), 1u);
  EXPECT_EQ(sm80.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  const auto parsed_module_3 = parseModule(R"ptx(
.version 8.0
.target sm_90
.global .attribute(.unified(1, 2)) .u32 managed;
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto sm90 = checkModuleAvailability(*parsed_module_3,
                                            *resolved);
  EXPECT_TRUE(sm90.has_value());

  const auto parsed_module_4 = parseModule(R"ptx(
.target sm_123a
.global .attribute(.unified(1, 2)) .u32 managed;
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto unknown_without_version =
      checkModuleAvailability(*parsed_module_4,
                              *resolved);
  ASSERT_FALSE(unknown_without_version.has_value());
  ASSERT_EQ(unknown_without_version.error().size(), 1u);
  EXPECT_EQ(unknown_without_version.error().front().kind,
            checker::CheckDiagnosticKind::UnknownTarget);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

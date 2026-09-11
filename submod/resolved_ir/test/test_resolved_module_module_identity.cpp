#include <gtest/gtest-spi.h>
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

TEST(ResolvedModule, CarriesFunctionAndRegisterSymbolIdentity) {
  EXPECT_FATAL_FAILURE(
      {
        SyntaxModuleParseResult missing_module;
        ASSERT_MODULE_PARSE_SUCCEEDS(missing_module);
      },
      "PTX source did not produce a syntax module.");

  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u32 %named;
  add.u32 %named, %r1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  const ResolvedFunction& function = resolved->functions.front();
  EXPECT_EQ(function.name, "kernel");
  EXPECT_TRUE(function.is_entry);
  EXPECT_FALSE(function.is_prototype);
  EXPECT_EQ(resolved->symbols.symbol(function.symbol_id).name, "kernel");
  EXPECT_FALSE(
      resolved->symbols.symbol(function.symbol_id).parameterized_count);
  EXPECT_TRUE(resolved->symbols.symbol(function.symbol_id).function_is_entry);
  ASSERT_EQ(function.body.size(), 1u);

  const Add::IntegerNoSat& add = resolvedIntegerAdd(function.body.front());
  EXPECT_FALSE(std::get<Add>(function.body.front()).execution_predicate);
  const ResolvedRegisterRef& dst = add.dst.value;
  const auto& src1 = std::get<ResolvedRegisterRef>(add.src1.value);
  const auto& src2 = std::get<ResolvedRegisterRef>(add.src2.value);

  ASSERT_TRUE(dst.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*dst.symbol_id).name, "%named");
  EXPECT_FALSE(dst.index.has_value());
  EXPECT_FALSE(dst.parameterized_index.has_value());
  EXPECT_EQ(dst.declared_type, ScalarType::U32);

  ASSERT_TRUE(src1.symbol_id.has_value());
  ASSERT_TRUE(src2.symbol_id.has_value());
  EXPECT_EQ(src1.symbol_id, src2.symbol_id);
  EXPECT_EQ(resolved->symbols.symbol(*src1.symbol_id).name, "%r");
  EXPECT_EQ(src1.index, 1u);
  EXPECT_EQ(src2.index, 2u);
  EXPECT_EQ(src1.parameterized_index, 1u);
  EXPECT_EQ(src2.parameterized_index, 2u);
  EXPECT_EQ(src1.declared_type, ScalarType::U32);
}

TEST(ResolvedModule, RecordsFunctionLabelInstructionBoundaries) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
first:
second:
  bra first;
before_nested:
  {
nested:
    bra nested;
  }
before_final:
  bra before_final;
trailing:
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const ResolvedFunction& function = resolved->functions.front();
  ASSERT_EQ(function.body.size(), 3u);
  ASSERT_EQ(function.label_positions.size(), 6u);
  EXPECT_EQ(function.label_positions[0].instruction_offset, 0u);
  EXPECT_EQ(function.label_positions[1].instruction_offset, 0u);
  EXPECT_EQ(function.label_positions[2].instruction_offset, 1u);
  EXPECT_EQ(function.label_positions[3].instruction_offset, 1u);
  EXPECT_EQ(function.label_positions[4].instruction_offset, 2u);
  EXPECT_EQ(function.label_positions[5].instruction_offset,
            function.body.size());

  constexpr std::array<std::string_view, 6> label_names{
      "first", "second", "before_nested", "nested", "before_final", "trailing"};
  for (size_t index = 0; index != function.label_positions.size(); ++index) {
    const binding::Symbol& label =
        resolved->symbols.symbol(function.label_positions[index].symbol_id);
    EXPECT_EQ(label.kind, binding::SymbolKind::Label);
    EXPECT_EQ(label.name, label_names[index]);
  }

  const auto& first =
      std::get<Bra::Direct>(std::get<Bra>(function.body[0]).variant);
  const auto& nested =
      std::get<Bra::Direct>(std::get<Bra>(function.body[1]).variant);
  const auto& before_final =
      std::get<Bra::Direct>(std::get<Bra>(function.body[2]).variant);
  ASSERT_TRUE(first.target.value.symbol_id.has_value());
  ASSERT_TRUE(nested.target.value.symbol_id.has_value());
  ASSERT_TRUE(before_final.target.value.symbol_id.has_value());
  EXPECT_EQ(function.label_positions[0].symbol_id,
            *first.target.value.symbol_id);
  EXPECT_EQ(function.label_positions[3].symbol_id,
            *nested.target.value.symbol_id);
  EXPECT_EQ(function.label_positions[4].symbol_id,
            *before_final.target.value.symbol_id);
}

TEST(ResolvedModule, ReportsRegistersMissingFromTheBoundScope) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  add.u32 %dst, %missing, %dst;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Unresolved instruction operand '%missing'.");
}

TEST(ResolvedModule, ResolvesNestedBlocksInSourceOrder) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %value;
  {
    .reg .u32 %value;
    add.u32 %value, %value, %value;
  }
  add.u32 %value, %value, %value;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  const auto& inner = resolvedIntegerAdd(body[0]);
  const auto& outer = resolvedIntegerAdd(body[1]);
  const auto& inner_value = inner.dst.value;
  const auto& outer_value = outer.dst.value;
  ASSERT_TRUE(inner_value.symbol_id.has_value());
  ASSERT_TRUE(outer_value.symbol_id.has_value());
  EXPECT_NE(inner_value.symbol_id, outer_value.symbol_id);
}

TEST(ResolvedModule, ResolvesFunctionLocalControlTargetsInsideNestedBlocks) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  {
    .reg .u32 %index;
    .reg .u64 %function_pointer;
label:
prototype: .callprototype _;
branches: .branchtargets label;
    bra label;
    brx.idx %index, branches;
    call %function_pointer, prototype;
  }
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto function_scope =
      *resolved->symbols.symbol(resolved->functions.front().symbol_id).owned_scope;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& branch = std::get<Bra::Direct>(
      std::get<Bra>(body[0]).variant);
  ASSERT_TRUE(branch.target.value.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*branch.target.value.symbol_id).scope,
            function_scope);
  const auto& indexed = std::get<Brx::Idx>(std::get<Brx>(body[1]).variant);
  ASSERT_TRUE(indexed.tlist.value.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*indexed.tlist.value.symbol_id).scope,
            function_scope);
  const auto& call = std::get<Call::Direct::TargetMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(body[2]).variant).operands);
  const auto& metadata =
      std::get<ResolvedIndirectMetadataRef>(call.metadata.value);
  ASSERT_TRUE(metadata.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*metadata.symbol_id).scope,
            function_scope);
}

TEST(ResolvedModule, DoesNotStageCallsAcrossNestedBlockBoundaries) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func callee(.param .u32 input);
.entry caller() {
  .reg .u32 %value;
  .param .u32 outer_staging;
  st.param.u32 [outer_staging], %value;
  {
    .param .u32 inner_staging;
    st.param.u32 [inner_staging], %value;
    call callee, (inner_staging);
  }
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "A function-local .param argument store must be in the "
            "contiguous block immediately before a call that uses it.");
}

TEST(ResolvedModule, DistinguishesSpecialRegistersFromMissingDeclarations) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  add.u32 %dst, %laneid, %dst;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Special register '%laneid' is not supported by this resolved "
            "operand.");
}

TEST(ResolvedModule, ResolvesAndChecksSpecialRegisterMetadata) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  mov.u32 %dst, %laneid;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto& u32 = std::get<Mov::Scalar>(mov.variant);
  const auto& scalar = scalarMovOperands(u32);
  const auto& special = std::get<ResolvedSpecialRegisterRef>(scalar.src.value);
  EXPECT_EQ(special.spelling, "%laneid");
  EXPECT_EQ(special.id.kind, base::SpecialRegisterKind::LaneId);
  EXPECT_FALSE(special.component.has_value());
  const auto special_info = base::metadata(special.id);
  EXPECT_EQ(special_info.element_type, ScalarType::U32);
  EXPECT_EQ(special_info.vector_width, 1u);
  EXPECT_EQ(special_info.minimum_ptx_major, 1u);
  EXPECT_EQ(special_info.minimum_ptx_minor, 3u);
  EXPECT_EQ(special_info.minimum_sm, 0u);

  const checker::Context too_old{
      .target =
          checker::TargetInfo{
              .ptx_version = checker::PtxVersion{1, 2},
              .sm_version = 10,
          },
      .instruction_range = scalar.src.locs.front(),
  };
  const auto rejected = checker::check(mov, too_old);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().message,
            "Operand value '%laneid' requires PTX ISA >= 1.3, but target PTX "
            "ISA is 1.2.");

  checker::Context supported = too_old;
  supported.target.ptx_version = checker::PtxVersion{1, 3};
  EXPECT_TRUE(checker::check(mov, supported).has_value());
}

TEST(ResolvedModule, ChecksSpecialRegisterSmAndTypeRequirements) {
  PtxSyntaxParser cluster_parser("mov.u32 %r0, %cluster_ctarank;");
  const auto cluster_ast = cluster_parser.parseInstruction();
  ASSERT_TRUE(cluster_ast.has_value())
      << cluster_ast.diagnostics.front().message;
  const auto cluster_resolved = resolveInstruction(*cluster_ast);
  ASSERT_TRUE(cluster_resolved.has_value()) << cluster_resolved.error().message;
  const auto& cluster_mov = std::get<Mov>(*cluster_resolved);
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const auto cluster_check = checker::check(
      cluster_mov, checker::Context{
                       .target =
                           checker::TargetInfo{
                               .ptx_version = checker::PtxVersion{7, 8},
                               .sm_version = 80,
                               .capabilities = cluster_capabilities,
                           },
                       .instruction_range = cluster_ast->range,
                   });
  ASSERT_FALSE(cluster_check.has_value());
  ASSERT_EQ(cluster_check.error().size(), 1u);
  EXPECT_EQ(cluster_check.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  PtxSyntaxParser timer_parser("mov.u32 %r0, %globaltimer_hi;");
  const auto timer_ast = timer_parser.parseInstruction();
  ASSERT_TRUE(timer_ast.has_value()) << timer_ast.diagnostics.front().message;
  const auto timer_resolved = resolveInstruction(*timer_ast);
  ASSERT_TRUE(timer_resolved.has_value()) << timer_resolved.error().message;
  const auto& timer_mov = std::get<Mov>(*timer_resolved);
  const checker::Context timer_too_old_ptx{
      .target = checker::TargetInfo{
          .ptx_version = checker::PtxVersion{3, 0},
          .sm_version = 30,
      },
      .instruction_range = timer_ast->range,
  };
  const auto timer_ptx_rejected = checker::check(timer_mov, timer_too_old_ptx);
  ASSERT_FALSE(timer_ptx_rejected.has_value());
  EXPECT_EQ(timer_ptx_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  auto timer_too_old_sm = timer_too_old_ptx;
  timer_too_old_sm.target.ptx_version = checker::PtxVersion{3, 1};
  timer_too_old_sm.target.sm_version = 20;
  const auto timer_sm_rejected = checker::check(timer_mov, timer_too_old_sm);
  ASSERT_FALSE(timer_sm_rejected.has_value());
  EXPECT_EQ(timer_sm_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  timer_too_old_sm.target.sm_version = 30;
  EXPECT_TRUE(checker::check(timer_mov, timer_too_old_sm).has_value());

  PtxSyntaxParser wide_parser("mov.u32 %r0, %clock64;");
  const auto wide_ast = wide_parser.parseInstruction();
  ASSERT_TRUE(wide_ast.has_value()) << wide_ast.diagnostics.front().message;
  const auto wide_resolved = resolveInstruction(*wide_ast);
  ASSERT_TRUE(wide_resolved.has_value()) << wide_resolved.error().message;
  const auto wide_check =
      checker::check(std::get<Mov>(*wide_resolved),
                     checker::Context{
                         .target =
                             checker::TargetInfo{
                                 .ptx_version = checker::PtxVersion{9, 3},
                                 .sm_version = 120,
                             },
                         .instruction_range = wide_ast->range,
                     });
  ASSERT_FALSE(wide_check.has_value());
  ASSERT_EQ(wide_check.error().size(), 1u);
  EXPECT_EQ(wide_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(wide_check.error().front().message,
            "Special-register operand 'src' has declared type 'U64' but "
            "instruction type source 'type' is 'U32'.");
}

TEST(ResolvedModule, ResolvesAndChecksSmemAndGraphSpecialRegisters) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<5>;
  .reg .u32 %r<4>;
  .reg .u64 %rd;
  mov.b32 %b0, %reserved_smem_offset_begin;
  mov.b32 %b1, %reserved_smem_offset_end;
  mov.b32 %b2, %reserved_smem_offset_cap;
  mov.b32 %b3, %reserved_smem_offset_0;
  mov.b32 %b4, %reserved_smem_offset_1;
  mov.u32 %r0, %total_smem_size;
  mov.u32 %r1, %dynamic_smem_size;
  mov.u32 %r2, %aggr_smem_size;
  mov.u64 %rd, %current_graph_exec;
  mov.u32 %r3, %current_graph_exec;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  constexpr std::array<std::string_view, 3> capabilities{
      "reserved_smem", "aggregate_smem", "graph_exec"};

  constexpr std::array spellings{
      "%reserved_smem_offset_begin", "%reserved_smem_offset_end",
      "%reserved_smem_offset_cap", "%reserved_smem_offset_0",
      "%reserved_smem_offset_1", "%total_smem_size", "%dynamic_smem_size",
      "%aggr_smem_size", "%current_graph_exec",
  };
  for (size_t index = 0; index < spellings.size(); ++index) {
    const auto& special = std::get<ResolvedSpecialRegisterRef>(
        scalarMovOperands(std::get<Mov>(body[index])).src.value);
    EXPECT_EQ(special.spelling, spellings[index]);
    ASSERT_TRUE(base::lookup(spellings[index]).has_value());
    EXPECT_EQ(special.id, base::lookup(spellings[index])->id);
  }

  const auto check_at = [&](size_t index, checker::PtxVersion version,
                            uint32_t sm) {
    return checker::check(std::get<Mov>(body[index]),
                          checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = version,
                                      .sm_version = sm,
                                      .capabilities = capabilities,
                                  },
                              .instruction_range = ast.range,
                          });
  };
  struct AvailabilityBoundary {
    size_t instruction;
    checker::PtxVersion supported;
    checker::PtxVersion too_old_ptx;
    uint32_t minimum_sm;
  };
  for (const AvailabilityBoundary boundary : {
           AvailabilityBoundary{0, {7, 6}, {7, 5}, 80},
           AvailabilityBoundary{5, {4, 1}, {4, 0}, 20},
           AvailabilityBoundary{7, {8, 1}, {8, 0}, 90},
           AvailabilityBoundary{8, {8, 0}, {7, 9}, 50},
       }) {
    EXPECT_TRUE(check_at(boundary.instruction, boundary.supported,
                         boundary.minimum_sm)
                    .has_value());
    const auto ptx_rejected = check_at(boundary.instruction,
                                       boundary.too_old_ptx,
                                       boundary.minimum_sm);
    ASSERT_FALSE(ptx_rejected.has_value());
    const auto info = base::metadata(
        std::get<ResolvedSpecialRegisterRef>(
            scalarMovOperands(std::get<Mov>(body[boundary.instruction]))
                .src.value)
            .id);
    EXPECT_EQ(ptx_rejected.error().front().kind,
              info.required_capability.empty()
                  ? checker::CheckDiagnosticKind::UnsupportedPtxVersion
                  : checker::CheckDiagnosticKind::UnsupportedAvailability);
    const auto sm_rejected = check_at(boundary.instruction, boundary.supported,
                                      boundary.minimum_sm - 1);
    ASSERT_FALSE(sm_rejected.has_value());
    EXPECT_EQ(sm_rejected.error().front().kind,
              info.required_capability.empty()
                  ? checker::CheckDiagnosticKind::UnsupportedSmVersion
                  : checker::CheckDiagnosticKind::UnsupportedAvailability);
  }

  const auto wrong_type = check_at(9, {9, 3}, 100);
  ASSERT_FALSE(wrong_type.has_value());
  EXPECT_EQ(wrong_type.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ResolvesScalarSpecialRegisterComponentsOnly) {
  PtxSyntaxParser component_parser("mov.u32 %r0, %tid.x;");
  const auto component_ast = component_parser.parseInstruction();
  ASSERT_TRUE(component_ast.has_value())
      << component_ast.diagnostics.front().message;
  const auto component_resolved = resolveInstruction(*component_ast);
  ASSERT_TRUE(component_resolved.has_value())
      << component_resolved.error().message;
  const auto& component = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(*component_resolved)).src.value);
  EXPECT_EQ(component.spelling, "%tid.x");
  EXPECT_EQ(component.id.kind, base::SpecialRegisterKind::Tid);
  EXPECT_EQ(component.component, base::VectorComponent::X);
  const auto component_info = base::metadata(component.id);
  EXPECT_EQ(component_info.vector_width, 4u);
  EXPECT_EQ(component_info.minimum_ptx_major, 2u);

  PtxSyntaxParser vector_parser("mov.u32 %r0, %tid;");
  const auto vector_ast = vector_parser.parseInstruction();
  ASSERT_TRUE(vector_ast.has_value())
      << vector_ast.diagnostics.front().message;
  const auto vector_resolved = resolveInstruction(*vector_ast);
  ASSERT_FALSE(vector_resolved.has_value());
  EXPECT_EQ(vector_resolved.error().message,
            "Special register '%tid' is a vector; select a scalar component.");
}


}  // namespace
}  // namespace ptx_frontend::resolved_ir

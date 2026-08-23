#include <gtest/gtest.h>

#include <string_view>

#include "ptx_ir/resolved/ptx_resolved_ir.hpp"
#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

syntax_ast::AstModule parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value()) << module.error().message;
  return std::move(*module);
}

const Add::IntegerNoSat& resolvedIntegerAdd(
    const ResolvedInstruction& instruction) {
  return std::get<Add::IntegerNoSat>(std::get<Add>(instruction).variant);
}

TEST(ResolvedModule, CarriesFunctionAndRegisterSymbolIdentity) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u32 %named;
  add.u32 %named, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  const ResolvedFunction& function = resolved->functions.front();
  EXPECT_EQ(function.name, "kernel");
  EXPECT_TRUE(function.is_entry);
  EXPECT_FALSE(function.is_prototype);
  EXPECT_EQ(resolved->symbols.symbol(function.symbol_id).name, "kernel");
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

TEST(ResolvedModule, ReportsRegistersMissingFromTheBoundScope) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  add.u32 %dst, %missing, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Unresolved instruction operand '%missing'.");
}

TEST(ResolvedModule, DistinguishesSpecialRegistersFromMissingDeclarations) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  add.u32 %dst, %laneid, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Special register '%laneid' is not supported by this resolved "
            "operand.");
}

TEST(ResolvedModule, ResolvesAndChecksSpecialRegisterMetadata) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  mov.u32 %dst, %laneid;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto& u32 = std::get<Mov::U32>(mov.variant);
  const auto& special = std::get<ResolvedSpecialRegisterRef>(u32.src.value);
  EXPECT_EQ(special.spelling, "%laneid");
  EXPECT_EQ(special.info.element_type, ScalarType::U32);
  EXPECT_EQ(special.info.vector_width, 1u);
  EXPECT_EQ(special.info.minimum_ptx_major, 1u);
  EXPECT_EQ(special.info.minimum_ptx_minor, 3u);
  EXPECT_EQ(special.info.minimum_sm, 0u);

  const checker::Context too_old{
      .target =
          checker::TargetInfo{
              .ptx_version = checker::PtxVersion{1, 2},
              .sm_version = 10,
          },
      .instruction_range = u32.src.locs.front(),
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
  ASSERT_TRUE(cluster_ast.has_value()) << cluster_ast.error().message;
  const auto cluster_resolved = resolveInstruction(*cluster_ast);
  ASSERT_TRUE(cluster_resolved.has_value()) << cluster_resolved.error().message;
  const auto& cluster_mov = std::get<Mov>(*cluster_resolved);
  const auto cluster_check = checker::check(
      cluster_mov, checker::Context{
                       .target =
                           checker::TargetInfo{
                               .ptx_version = checker::PtxVersion{7, 8},
                               .sm_version = 80,
                           },
                       .instruction_range = cluster_ast->range,
                   });
  ASSERT_FALSE(cluster_check.has_value());
  ASSERT_EQ(cluster_check.error().size(), 1u);
  EXPECT_EQ(cluster_check.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  PtxSyntaxParser wide_parser("mov.u32 %r0, %clock64;");
  const auto wide_ast = wide_parser.parseInstruction();
  ASSERT_TRUE(wide_ast.has_value()) << wide_ast.error().message;
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
            "instruction type source 'fixed scalar type' is 'U32'.");
}

TEST(ResolvedModule, ResolvesScalarSpecialRegisterComponentsOnly) {
  PtxSyntaxParser component_parser("mov.u32 %r0, %tid.x;");
  const auto component_ast = component_parser.parseInstruction();
  ASSERT_TRUE(component_ast.has_value()) << component_ast.error().message;
  const auto component_resolved = resolveInstruction(*component_ast);
  ASSERT_TRUE(component_resolved.has_value())
      << component_resolved.error().message;
  const auto& component = std::get<ResolvedSpecialRegisterRef>(
      std::get<Mov::U32>(std::get<Mov>(*component_resolved).variant).src.value);
  EXPECT_EQ(component.spelling, "%tid.x");
  EXPECT_EQ(component.info.vector_width, 1u);
  EXPECT_EQ(component.info.minimum_ptx_major, 2u);

  PtxSyntaxParser vector_parser("mov.u32 %r0, %tid;");
  const auto vector_ast = vector_parser.parseInstruction();
  ASSERT_TRUE(vector_ast.has_value()) << vector_ast.error().message;
  const auto vector_resolved = resolveInstruction(*vector_ast);
  ASSERT_FALSE(vector_resolved.has_value());
  EXPECT_EQ(vector_resolved.error().message,
            "Special register '%tid' is a vector; select a scalar component.");
}

TEST(ResolvedModule, ResolvesBoundSymbolsAndAddressBases) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u64 %rd<2>;
  .reg .u32 %r<3>;
  mov.u64 %rd0, global_value;
  ld.u32 %r0, [global_value+4];
  ld.u32 %r1, [%rd0-8];
  ld.u32 %r2, [240];
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);

  const auto& mov = std::get<Mov>(body[0]);
  const auto& mov_symbol =
      std::get<ResolvedSymbolRef>(std::get<Mov::U64>(mov.variant).src.value);
  ASSERT_TRUE(mov_symbol.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*mov_symbol.symbol_id).name,
            "global_value");
  EXPECT_EQ(mov_symbol.state_space, syntax_ast::AstStateSpace::Global);
  EXPECT_EQ(mov_symbol.declared_type, ScalarType::U32);

  const auto& symbol_address =
      std::get<Ld::GenericU32>(std::get<Ld>(body[1]).variant).address.value;
  const auto& symbol_base = std::get<ResolvedSymbolRef>(symbol_address.base);
  EXPECT_EQ(symbol_base.symbol_id, mov_symbol.symbol_id);
  ASSERT_TRUE(symbol_address.offset.has_value());
  EXPECT_EQ(symbol_address.offset->operation,
            ResolvedAddressOffsetOperator::Add);
  EXPECT_EQ(symbol_address.offset->value.type, ScalarType::S64);
  EXPECT_EQ(symbol_address.offset->value.bits, 4u);

  const auto& register_address =
      std::get<Ld::GenericU32>(std::get<Ld>(body[2]).variant).address.value;
  const auto& register_base =
      std::get<ResolvedRegisterRef>(register_address.base);
  ASSERT_TRUE(register_base.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*register_base.symbol_id).name, "%rd");
  EXPECT_EQ(register_base.parameterized_index, 0u);
  ASSERT_TRUE(register_address.offset.has_value());
  EXPECT_EQ(register_address.offset->operation,
            ResolvedAddressOffsetOperator::Subtract);
  EXPECT_EQ(register_address.offset->value.bits, 8u);

  const auto& immediate_address =
      std::get<Ld::GenericU32>(std::get<Ld>(body[3]).variant).address.value;
  const auto& immediate_base =
      std::get<ResolvedImmediate>(immediate_address.base);
  EXPECT_EQ(immediate_base.type, ScalarType::U64);
  EXPECT_EQ(immediate_base.bits, 240u);
}

TEST(ResolvedModule, ChecksGenericLoadAvailability) {
  PtxSyntaxParser parser("ld.u32 %r0, [%rd0+4];");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;
  const auto resolved = resolveInstruction(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& load = std::get<Ld>(*resolved);

  const auto rejected =
      checker::check(load, checker::Context{
                               .target =
                                   checker::TargetInfo{
                                       .ptx_version = checker::PtxVersion{1, 5},
                                       .sm_version = 10,
                                   },
                               .instruction_range = ast->range,
                           });
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  EXPECT_TRUE(
      checker::check(load,
                     checker::Context{
                         .target =
                             checker::TargetInfo{
                                 .ptx_version = checker::PtxVersion{2, 0},
                                 .sm_version = 20,
                             },
                         .instruction_range = ast->range,
                     })
          .has_value());
}

TEST(ResolvedModule, ResolvesMovRegisterImmediateAndSymbolOffsetSources) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u64 %rd<2>;
  mov.u32 %r0, %r1;
  mov.u32 %r2, 42;
  mov.u64 %rd0, global_value+8;
  mov.u64 %rd1, %clock64;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);

  const auto& register_source = std::get<ResolvedRegisterRef>(
      std::get<Mov::U32>(std::get<Mov>(body[0]).variant).src.value);
  EXPECT_EQ(register_source.spelling, "%r1");
  EXPECT_EQ(register_source.parameterized_index, 1u);
  EXPECT_EQ(register_source.declared_type, ScalarType::U32);

  const auto& immediate_source = std::get<ResolvedImmediate>(
      std::get<Mov::U32>(std::get<Mov>(body[1]).variant).src.value);
  EXPECT_EQ(immediate_source.type, ScalarType::U32);
  EXPECT_EQ(immediate_source.bits, 42u);

  const auto& address_source = std::get<ResolvedAddress>(
      std::get<Mov::U64>(std::get<Mov>(body[2]).variant).src.value);
  const auto& symbol = std::get<ResolvedSymbolRef>(address_source.base);
  ASSERT_TRUE(symbol.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*symbol.symbol_id).name, "global_value");
  ASSERT_TRUE(address_source.offset.has_value());
  EXPECT_EQ(address_source.offset->operation,
            ResolvedAddressOffsetOperator::Add);
  EXPECT_EQ(address_source.offset->value.type, ScalarType::S64);
  EXPECT_EQ(address_source.offset->value.bits, 8u);

  const auto& special_source = std::get<ResolvedSpecialRegisterRef>(
      std::get<Mov::U64>(std::get<Mov>(body[3]).variant).src.value);
  EXPECT_EQ(special_source.spelling, "%clock64");
  EXPECT_EQ(special_source.info.element_type, ScalarType::U64);
}

TEST(ResolvedModule, ChecksMovRegisterSourceType) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  mov.u32 %r0, %rd0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto checked =
      checker::check(mov, checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = checker::PtxVersion{9, 2},
                                      .sm_version = 120,
                                  },
                              .instruction_range = ast.range,
                          });
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, RejectsU32MovSymbolAddressSource) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  mov.u32 %r0, global_value;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "This mov variant does not accept the resolved source operand "
            "shape.");
}

TEST(ResolvedModule, KeepsStandaloneAddressAndSymbolIdentityOpen) {
  PtxSyntaxParser mov_parser("mov.u64 %rd0, global_value;");
  const auto mov_ast = mov_parser.parseInstruction();
  ASSERT_TRUE(mov_ast.has_value()) << mov_ast.error().message;
  const auto mov_resolved = resolveInstruction(*mov_ast);
  ASSERT_TRUE(mov_resolved.has_value()) << mov_resolved.error().message;
  const auto& symbol = std::get<ResolvedSymbolRef>(
      std::get<Mov::U64>(std::get<Mov>(*mov_resolved).variant).src.value);
  EXPECT_EQ(symbol.spelling, "global_value");
  EXPECT_FALSE(symbol.symbol_id.has_value());
  EXPECT_FALSE(symbol.state_space.has_value());

  PtxSyntaxParser load_parser("ld.u32 %r0, [%rd0+4];");
  const auto load_ast = load_parser.parseInstruction();
  ASSERT_TRUE(load_ast.has_value()) << load_ast.error().message;
  const auto load_resolved = resolveInstruction(*load_ast);
  ASSERT_TRUE(load_resolved.has_value()) << load_resolved.error().message;
  const auto& address =
      std::get<Ld::GenericU32>(std::get<Ld>(*load_resolved).variant)
          .address.value;
  const auto& base = std::get<ResolvedRegisterRef>(address.base);
  EXPECT_EQ(base.spelling, "%rd0");
  EXPECT_FALSE(base.symbol_id.has_value());
}

TEST(ResolvedModule, RejectsUnbracketedLoadAddress) {
  PtxSyntaxParser parser("ld.u32 %r0, %rd0+4;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;
  const auto invalid_address = resolveInstruction(*ast);
  ASSERT_FALSE(invalid_address.has_value());
  EXPECT_EQ(invalid_address.error().message,
            "Expected a bracketed address operand.");
}

TEST(ResolvedModule, RejectsNonRegisterSymbolsInRegisterOperands) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  .local .u32 value;
  add.u32 %dst, value, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Symbol 'value' is not a .reg variable.");
}

TEST(ResolvedModule, CheckerUsesDeclarationBoundRegisterTypes) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %wide;
  .reg .u32 %r<2>;
  add.u32 %wide, %r0, %r1;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const ResolvedFunction& function = resolved->functions.front();
  const Add& add = std::get<Add>(function.body.front());
  const auto& syntax_instruction =
      std::get<syntax_ast::AstFunction>(ast.items.front()).body.back();

  const auto result = checker::check(
      add,
      checker::Context{
          .target =
              checker::TargetInfo{
                  .ptx_version = checker::PtxVersion{9, 0},
                  .sm_version = 100,
              },
          .instruction_range =
              std::get<syntax_ast::AstInstruction>(syntax_instruction).range,
      });

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(result.error().front().message,
            "Register operand 'dst' has declared type 'U64' but instruction "
            "type source 'type' is 'U32'.");
}

TEST(ResolvedModule, BindsPredicateOperandsByDeclarationType) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  bar.red.and.pred %condition, 0, !%condition;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& bar = std::get<Bar>(resolved->functions.front().body.front());
  const auto& reduction = std::get<Bar::RedAndPred>(bar.variant);
  const auto& operands =
      std::get<Bar::RedAndPred::WithoutThreadCountOperands>(reduction.operands);
  ASSERT_TRUE(operands.dst.value.register_ref.symbol_id.has_value());
  EXPECT_EQ(operands.dst.value.register_ref.symbol_id,
            operands.predicate.value.register_ref.symbol_id);
  EXPECT_EQ(operands.dst.value.register_ref.declared_type, ScalarType::Pred);
  EXPECT_TRUE(operands.predicate.value.negated);
}

TEST(ResolvedModule, PreservesAndBindsExecutionPredicate) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  .reg .u32 %r<3>;
  @!%condition add.u32 %r0, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& add = std::get<Add>(resolved->functions.front().body.front());
  ASSERT_TRUE(add.execution_predicate.has_value());
  const ResolvedPredicate& predicate = add.execution_predicate->value;
  EXPECT_TRUE(predicate.negated);
  EXPECT_EQ(predicate.register_ref.spelling, "%condition");
  EXPECT_EQ(predicate.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(predicate.register_ref.declared_type, ScalarType::Pred);
  ASSERT_TRUE(predicate.register_ref.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*predicate.register_ref.symbol_id).name,
            "%condition");

  const auto& function = std::get<syntax_ast::AstFunction>(ast.items.front());
  const auto& instruction =
      std::get<syntax_ast::AstInstruction>(function.body.back());
  ASSERT_TRUE(instruction.predicate.has_value());
  ASSERT_EQ(add.execution_predicate->locs.size(), 1u);
  EXPECT_EQ(add.execution_predicate->locs.front(),
            instruction.predicate->range);
}

TEST(ResolvedModule, RejectsNonPredicateExecutionGuard) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %condition;
  .reg .u32 %r<3>;
  @%condition add.u32 %r0, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Expected a predicate register, but '%condition' is declared "
            "'.u32'.");
}

TEST(ResolvedModule, ResolvesPredicatedDirectBranchTarget) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  @!%condition bra.uni done;
done:
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 1u);
  const auto& bra = std::get<Bra>(resolved->functions.front().body.front());
  ASSERT_TRUE(bra.execution_predicate.has_value());
  EXPECT_TRUE(bra.execution_predicate->value.negated);

  const auto& direct = std::get<Bra::Direct>(bra.variant);
  EXPECT_TRUE(direct.uni.value);
  EXPECT_EQ(direct.target.value.spelling, "done");
  ASSERT_TRUE(direct.target.value.symbol_id.has_value());
  const binding::Symbol& label =
      resolved->symbols.symbol(*direct.target.value.symbol_id);
  EXPECT_EQ(label.kind, binding::SymbolKind::Label);
  EXPECT_EQ(label.name, "done");
  const binding::Symbol& function_symbol =
      resolved->symbols.symbol(resolved->functions.front().symbol_id);
  ASSERT_TRUE(function_symbol.owned_scope.has_value());
  EXPECT_EQ(label.scope, *function_symbol.owned_scope);
  ASSERT_EQ(direct.target.locs.size(), 1u);

  const checker::Context check_context{
      .target =
          checker::TargetInfo{
              .ptx_version = checker::PtxVersion{1, 0},
              .sm_version = 0,
          },
      .instruction_range = direct.target.locs.front(),
  };
  const auto checked = checker::check(bra, check_context);
  EXPECT_TRUE(checked.has_value());
}

TEST(ResolvedModule, StandaloneBranchTargetRemainsUnbound) {
  PtxSyntaxParser parser("bra target;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;

  const auto resolved = resolveInstruction(*ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& bra = std::get<Bra>(*resolved);
  EXPECT_FALSE(bra.execution_predicate.has_value());
  const auto& direct = std::get<Bra::Direct>(bra.variant);
  EXPECT_FALSE(direct.uni.value);
  EXPECT_TRUE(direct.uni.locs.empty());
  EXPECT_EQ(direct.target.value.spelling, "target");
  EXPECT_FALSE(direct.target.value.symbol_id.has_value());
}

TEST(ResolvedModule, StandaloneResolutionRemainsDeclarationFree) {
  PtxSyntaxParser parser("@!%p7 add.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;

  const auto resolved = resolveInstruction(*ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& instruction = std::get<Add>(*resolved);
  ASSERT_TRUE(instruction.execution_predicate.has_value());
  EXPECT_TRUE(instruction.execution_predicate->value.negated);
  EXPECT_EQ(instruction.execution_predicate->value.register_ref.index, 7u);
  EXPECT_FALSE(instruction.execution_predicate->value.register_ref.symbol_id
                   .has_value());
  const Add::IntegerNoSat& add = resolvedIntegerAdd(*resolved);
  EXPECT_FALSE(add.dst.value.symbol_id.has_value());
  EXPECT_FALSE(add.dst.value.declared_type.has_value());
  EXPECT_EQ(add.dst.value.index, 0u);
}

TEST(ResolvedModule, RunsDeclarationSemanticsBeforeInstructionResolution) {
  const auto ast = parseModule(R"ptx(
.global .u32 values[2] = {1, 2, 3};
.entry kernel() { ret; }
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Initializer dimension contains 3 elements but its declared "
            "extent is 2.");
}

TEST(ResolvedModule, ResolvesACompatibleFunctionDefinitionScope) {
  const auto ast = parseModule(R"ptx(
.func helper(.reg .u32 input);
.func helper(.reg .u32 input) {
  .reg .u32 %result;
  .reg .u32 %source;
  add.u32 %result, %source, 1;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  EXPECT_TRUE(resolved->functions.front().is_prototype);
  EXPECT_TRUE(resolved->functions.front().body.empty());
  EXPECT_FALSE(resolved->functions.back().is_prototype);
  ASSERT_EQ(resolved->functions.back().body.size(), 1u);
  const auto& add =
      std::get<Add>(resolved->functions.back().body.front()).variant;
  const auto& integer = std::get<Add::IntegerNoSat>(add);
  EXPECT_TRUE(integer.dst.value.symbol_id.has_value());
  EXPECT_TRUE(
      std::get<ResolvedRegisterRef>(integer.src1.value).symbol_id.has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

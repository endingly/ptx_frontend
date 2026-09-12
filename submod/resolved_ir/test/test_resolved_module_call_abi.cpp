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

TEST(ResolvedModule, ResolvesPredicatedDirectBranchTarget) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  @!%condition bra.uni done;
done:
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

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
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

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

TEST(ResolvedModule, ResolvesIndexedBranchTargetSetInCurrentFunction) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %index;
targets: .branchtargets done;
  brx.idx.uni %index, targets;
done:
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 1u);
  const auto& brx = std::get<Brx>(resolved->functions.front().body.front());
  const auto& indexed = std::get<Brx::Idx>(brx.variant);
  EXPECT_TRUE(indexed.uni.value);
  EXPECT_EQ(indexed.index.value.declared_type, ScalarType::U32);
  EXPECT_EQ(indexed.tlist.value.spelling, "targets");
  ASSERT_TRUE(indexed.tlist.value.symbol_id.has_value());
  const auto& target_set =
      resolved->symbols.symbol(*indexed.tlist.value.symbol_id);
  EXPECT_EQ(target_set.kind, binding::SymbolKind::BranchTargetSet);

  const checker::Context too_old{
      .target = {.ptx_version = checker::PtxVersion{5, 9}, .sm_version = 30},
      .instruction_range = indexed.tlist.locs.front(),
  };
  EXPECT_FALSE(checker::check(brx, too_old).has_value());
  const checker::Context too_old_sm{
      .target = {.ptx_version = checker::PtxVersion{6, 0}, .sm_version = 29},
      .instruction_range = indexed.tlist.locs.front(),
  };
  EXPECT_FALSE(checker::check(brx, too_old_sm).has_value());
  const checker::Context supported{
      .target = {.ptx_version = checker::PtxVersion{6, 0}, .sm_version = 30},
      .instruction_range = indexed.tlist.locs.front(),
  };
  EXPECT_TRUE(checker::check(brx, supported).has_value());
}

TEST(ResolvedModule, RejectsIndexedBranchTargetSetDeclaredAfterUse) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 6.0
.target sm_30
.entry kernel() {
  .reg .u32 %index;
  brx.idx %index, targets;
  targets: .branchtargets done;
done:
  ret;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_TRUE(std::ranges::any_of(
      resolved.error(), [](const ResolveDiagnostic& diagnostic) {
        return diagnostic.declaration_kind ==
                   declaration_semantics::DeclarationDiagnosticKind::
                       UnresolvedMetadataTarget &&
               diagnostic.message.find("must be defined before") !=
                   std::string::npos;
      }));
}

TEST(ResolvedModule, IndexedBranchRequiresU32IndexRegister) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %index;
targets: .branchtargets done;
  brx.idx %index, targets;
done:
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& brx = std::get<Brx>(resolved->functions.front().body.front());
  const checker::Context context{
      .target = {.ptx_version = checker::PtxVersion{6, 0}, .sm_version = 30},
      .instruction_range = SourceRange{},
  };
  EXPECT_FALSE(checker::check(brx, context).has_value());
}

TEST(ResolvedModule, ResolvesDirectCallGroupsAndPreservesBindings) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func callee();
.func callee_empty();
.func (.param .u32 result) callee_full(
    .param .u32 input0, .param .u32 input1, .param .s32 literal);
.entry caller() {
  .reg .pred %p;
  .reg .u32 %out, %arg;
  .param .u32 parameter;
  call callee;
  call callee_empty, ();
  @%p call.uni (%out), callee_full, (%arg, parameter, -4);
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.back().body;
  ASSERT_EQ(body.size(), 3u);

  const auto& target_only = std::get<Call>(body[0]);
  const auto& target_payload = std::get<Call::Direct::TargetOperands>(
      std::get<Call::Direct>(target_only.variant).operands);
  EXPECT_EQ(target_payload.target.value.spelling, "callee");
  ASSERT_TRUE(target_payload.target.value.symbol_id.has_value());

  const auto& empty_inputs = std::get<Call>(body[1]);
  const auto& input_payload = std::get<Call::Direct::TargetInputOperands>(
      std::get<Call::Direct>(empty_inputs.variant).operands);
  EXPECT_TRUE(input_payload.arguments.value.values.empty());
  ASSERT_EQ(input_payload.arguments.locs.size(), 1u);

  Call call = std::get<Call>(body[2]);
  auto& direct = std::get<Call::Direct>(call.variant);
  ASSERT_TRUE(call.execution_predicate.has_value());
  EXPECT_TRUE(direct.uni.value);
  const auto& return_payload =
      std::get<Call::Direct::ReturnTargetInputOperands>(direct.operands);
  EXPECT_EQ(return_payload.return_value.value.spelling, "%out");
  ASSERT_TRUE(return_payload.return_value.value.symbol_id.has_value());
  ASSERT_EQ(return_payload.arguments.value.values.size(), 3u);
  const auto& parameter = std::get<ResolvedCallParameterRef>(
      return_payload.arguments.value.values[1].value);
  EXPECT_EQ(parameter.state_space, syntax_ast::AstStateSpace::Parameter);
  const auto& literal = std::get<ResolvedCallLiteral>(
      return_payload.arguments.value.values[2].value);
  EXPECT_EQ(literal.spelling, "-4");
  ASSERT_EQ(return_payload.arguments.value.values[2].locs.size(), 1u);

  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = return_payload.target.locs.front(),
  };
  EXPECT_TRUE(checker::check(call, context).has_value());

  direct.operand_layout = ResolvedOperandLayoutTag{0};
  const auto payload_mismatch = checker::check(call, context);
  ASSERT_FALSE(payload_mismatch.has_value());
  EXPECT_EQ(payload_mismatch.error().back().kind,
            checker::CheckDiagnosticKind::OperandLayoutPayloadMismatch);

  direct.operand_layout = ResolvedOperandLayoutTag{99};
  const auto invalid_tag = checker::check(call, context);
  ASSERT_FALSE(invalid_tag.has_value());
  EXPECT_EQ(invalid_tag.error().back().kind,
            checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
}

TEST(ResolvedModule, ChecksDirectCallAbiForMixedParametersAndLiterals) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func (.param .u32 result) callee(
    .reg .u32 register_input, .param .u32 parameter_input,
    .param .s16 literal_input, .reg .u32 parameterized_register_input);
.entry caller() {
  .reg .u32 register_argument;
  .reg .u32 %r<1>;
  .param .u32 parameter_argument, return_argument;
  call (return_argument), callee,
      (register_argument, parameter_argument, -4, %r0);
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

TEST(ResolvedModule, ReportsDirectCallAbiArityMismatches) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func (.param .u32 return0, .param .u32 return1) callee(
    .param .u32 input0, .param .u32 input1);
.entry caller() {
  .param .u32 return0, input0;
  call (return0), callee, (input0);
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 2u);
  EXPECT_EQ(resolved.error()[0].message,
            "Direct call to 'callee' has 1 return argument but callee requires "
            "2.");
  EXPECT_EQ(resolved.error()[1].message,
            "Direct call to 'callee' has 1 input argument but callee requires "
            "2.");
}

TEST(ResolvedModule, ReportsDirectCallAbiPropertyAndLiteralMismatches) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func scalar(.param .u32 input);
.func bytes(.param .align 16 .b8 input[8]);
.func literal(.param .u16 input);
.entry caller() {
  .reg .v2 .u32 vector_argument;
  .reg .b8 register_byte;
  .param .align 16 .b8 wrong_size[4];
  .param .align 8 .b8 weak_array_alignment[8];
  call scalar, (vector_argument);
  call bytes, (register_byte);
  call bytes, (wrong_size);
  call bytes, (weak_array_alignment);
  call literal, (1.5);
  call literal, (65536);
  call bytes, (1);
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 7u);
  EXPECT_EQ(resolved.error()[0].message,
            "Direct call input argument 1 for 'scalar' has type or vector "
            "shape mismatch.");
  EXPECT_EQ(resolved.error()[1].message,
            "Direct call input argument 1 for 'bytes' has call argument "
            "state-space mismatch.");
  EXPECT_EQ(
      resolved.error()[2].message,
      "Direct call input argument 1 for 'bytes' has array size mismatch.");
  EXPECT_EQ(resolved.error()[3].message,
            "Direct call input argument 1 for 'bytes' has array alignment "
            "mismatch.");
  EXPECT_EQ(resolved.error()[4].message,
            "Decimal floating literal '1.5' is incompatible with scalar type "
            "'U16'.");
  EXPECT_EQ(resolved.error()[5].message,
            "Integer literal '65536' is out of range for scalar type 'U16'.");
  EXPECT_EQ(resolved.error()[6].message,
            "Direct call input argument 1 for 'bytes' has call argument "
            "state-space mismatch.");
}

TEST(ResolvedModule, AcceptsDirectCallsAcrossFunctionLifecycles) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 8.0
.target sm_80
.func prototype_only(.reg .u32 input);
.extern .func external(.reg .u32 input);
.func defined_before(.reg .u32 input) { }
.func bytes(.param .align 8 .b8 input[]);
.func immediate(.reg .u8 high, .reg .s8 low, .reg .f32 float);
.entry caller() {
  .reg .u32 %r;
  .param .align 8 .b8 blob[8];
  call prototype_only, (%r);
  call external, (%r);
  call defined_before, (%r);
  call defined_after, (%r);
  call renamed, (%r);
  call bytes, (blob);
  call immediate, (255, -128, 0f3f800000);
}
.func defined_after(.reg .u32 input) { }
.func renamed(.reg .u32 declaration_name);
.func renamed(.reg .u32 definition_name) {
  .reg .u32 %self;
  call renamed, (%self);
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

TEST(ResolvedModule, ReportsDirectCallArityAndElementRanges) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func (.param .u32 result) one_return();
.func no_returns();
.func needs_input(.reg .u32 input);
.func no_inputs();
.func signed(.reg .s8 input);
.entry caller() {
  .reg .u32 narrow;
  .reg .u64 wide;
  .param .u32 return_value;
  call one_return;
  call (return_value), no_returns, ();
  call needs_input;
  call no_inputs, (narrow);
  call needs_input, (wide);
  call signed, (128);
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 6u);
  EXPECT_EQ(resolved.error()[0].message,
            "Direct call to 'one_return' has 0 return arguments but callee "
            "requires 1.");
  EXPECT_EQ(resolved.error()[1].message,
            "Direct call to 'no_returns' has 1 return argument but callee "
            "requires 0.");
  EXPECT_EQ(resolved.error()[2].message,
            "Direct call to 'needs_input' has 0 input arguments but callee "
            "requires 1.");
  EXPECT_EQ(resolved.error()[3].message,
            "Direct call to 'no_inputs' has 1 input argument but callee "
            "requires 0.");
  EXPECT_EQ(resolved.error()[4].message,
            "Direct call input argument 1 for 'needs_input' has type or "
            "vector shape mismatch.");
  EXPECT_EQ(resolved.error()[5].message,
            "Integer literal '128' is out of range for scalar type 'S8'.");

  const auto& caller = std::get<syntax_ast::AstFunction>(ast.items[5]);
  const auto& extra_inputs =
      std::get<syntax_ast::AstInstruction>(caller.body[6]);
  const auto& type_mismatch =
      std::get<syntax_ast::AstInstruction>(caller.body[7]);
  const auto& extra_group =
      std::get<syntax_ast::AstCallParameterList>(extra_inputs.operands[1]);
  const auto& wide_group =
      std::get<syntax_ast::AstCallParameterList>(type_mismatch.operands[1]);
  EXPECT_EQ(resolved.error()[3].range, extra_group.range);
  EXPECT_EQ(resolved.error()[4].range,
            std::get<syntax_ast::AstIdentifierRef>(wide_group.parameters[0])
                .syntax.range);
}

TEST(ResolvedModule, RejectsLocalAndGlobalDirectCallActuals) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .u32 global_value;
.func needs_input(.reg .u32 input);
.entry caller() {
  .local .u32 local_value;
  call needs_input, (local_value);
  call needs_input, (global_value);
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 2u);
  EXPECT_EQ(resolved.error()[0].message,
            "Call parameter 'local_value' must name a .reg or .param "
            "variable.");
  EXPECT_EQ(resolved.error()[1].message,
            "Call parameter 'global_value' must name a .reg or .param "
            "variable.");
}

TEST(ResolvedModule, EnforcesPtx93CallParameterContexts) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func (.param .u32 result) callee(.param .u32 input0, .param .u32 input1);
.entry caller(.param .u32 entry_input) {
  .reg .pred %p;
  .reg .u32 %r0;
  .param .u32 input0, input1, output;
  ld.param::entry.u32 %r0, [entry_input];
  ld.param.u32 %r0, [entry_input];
  st.param::func.u32 [input0], %r0;
  st.param.u32 [input1], %r0;
  @%p call.uni (output), callee, (input0, input1);
  ld.param::func.u32 %r0, [output];
}
.func device(.param .u32 input) {
  .reg .u32 %r0;
  ld.param::func.u32 %r0, [input];
  ld.param.u32 %r0, [input];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& caller = resolved->functions[1].body;
  ASSERT_EQ(caller.size(), 6u);
  const auto& entry_load =
      std::get<Ld::ExplicitScalar>(std::get<Ld>(caller[0]).variant);
  EXPECT_EQ(entry_load.address.value.parameter_qualifier,
            ParameterAddressQualifier::Entry);
  const auto& staged_store =
      std::get<St::ExplicitScalar>(std::get<St>(caller[2]).variant);
  EXPECT_EQ(staged_store.address.value.parameter_qualifier,
            ParameterAddressQualifier::Function);
  const auto& default_load =
      std::get<Ld::ExplicitScalar>(std::get<Ld>(caller[1]).variant);
  EXPECT_EQ(default_load.address.value.parameter_qualifier,
            ParameterAddressQualifier::Default);
  const auto& call = std::get<Call>(caller[4]);
  EXPECT_TRUE(call.execution_predicate.has_value());
  EXPECT_TRUE(std::get<Call::Direct>(call.variant).uni.value);
  const auto& return_load =
      std::get<Ld::ExplicitScalar>(std::get<Ld>(caller[5]).variant);
  EXPECT_EQ(return_load.address.value.parameter_qualifier,
            ParameterAddressQualifier::Function);
}

TEST(ResolvedModule, RejectsInvalidPtx93CallParameterContexts) {
  const auto resolve_source = [](std::string_view body) {
    const auto parsed = parseModule(fmt::format(R"ptx(
.func (.param .u32 result) callee(.param .u32 input);
.entry caller(.param .u32 entry_input) {{
  .reg .pred %p;
  .reg .u32 %r0;
  .param .u32 input, other, output;
  {}
}}
.func device(.param .u32 input) {{
  .reg .u32 %r0;
  ld.param::entry.u32 %r0, [input];
}}
)ptx",
                                                body));
    if (!parsed || !parsed.diagnostics.empty()) {
      ADD_FAILURE() << (parsed.diagnostics.empty()
                            ? "PTX source did not produce a syntax module."
                            : parsed.diagnostics.front().message);
      return std::expected<ResolvedModule, ModuleResolveDiagnostics>{
          std::unexpected(ModuleResolveDiagnostics{})};
    }
    return resolveModule(*parsed);
  };

  const auto entry_as_function =
      resolve_source("ld.param::func.u32 %r0, [entry_input];");
  ASSERT_FALSE(entry_as_function.has_value());
  EXPECT_EQ(entry_as_function.error().front().message,
            ".param::func may access only a device-function parameter or "
            "function-local call parameter.");

  const auto device_as_entry = resolve_source("mov.u32 %r0, %r0;");
  ASSERT_FALSE(device_as_entry.has_value());
  EXPECT_EQ(device_as_entry.error().front().message,
            ".param::entry may access only a kernel entry input parameter.");

  const auto predicated_store = resolve_source(
      "@%p st.param.u32 [input], %r0;\n  call (output), callee, (input);");
  ASSERT_FALSE(predicated_store.has_value());
  EXPECT_EQ(predicated_store.error().front().message,
            "A function-local .param argument store cannot be predicated.");

  const auto non_adjacent_store = resolve_source(
      "st.param.u32 [input], %r0;\n  mov.u32 %r0, %r0;\n  call (output), "
      "callee, (input);");
  ASSERT_FALSE(non_adjacent_store.has_value());
  EXPECT_EQ(non_adjacent_store.error().front().message,
            "A function-local .param argument store must be in the contiguous "
            "block immediately before a call that uses it.");

  const auto wrong_call_argument = resolve_source(
      "st.param.u32 [input], %r0;\n  call (output), callee, (other);");
  ASSERT_FALSE(wrong_call_argument.has_value());
  EXPECT_EQ(wrong_call_argument.error().front().message,
            "A function-local .param argument store must be in the contiguous "
            "block immediately before a call that uses it.");

  const auto predicated_return_load = resolve_source(
      "call (output), callee, (input);\n  @%p ld.param.u32 %r0, [output];");
  ASSERT_FALSE(predicated_return_load.has_value());
  EXPECT_EQ(predicated_return_load.error().front().message,
            "A function-local .param return load cannot be predicated.");

  const auto non_adjacent_return_load = resolve_source(
      "call (output), callee, (input);\n  mov.u32 %r0, %r0;\n  ld.param.u32 "
      "%r0, [output];");
  ASSERT_FALSE(non_adjacent_return_load.has_value());
  EXPECT_EQ(non_adjacent_return_load.error().front().message,
            "A function-local .param return load must be in the contiguous "
            "block immediately after a call that returns it.");

  const auto parsed_module_2 = parseModule(R"ptx(
.entry caller() {
  .reg .u32 %r0;
  .param .u32 output;
  st.param::entry.u32 [output], %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto entry_store = resolveModule(*parsed_module_2);
  EXPECT_FALSE(entry_store.has_value());
}

TEST(ResolvedModule, ResolvesIndirectCallsWithFunctionLocalMetadata) {
  const auto& syntax = Call::get_syntax_descriptor();
  const auto& resolved_descriptor = Call::get_resolved_descriptor();
  const auto& checker_descriptor = Call::get_checker_descriptor();
  ASSERT_EQ(syntax.variants[0].operand_layouts.size(), 6u);
  EXPECT_EQ(syntax.variants[0].operand_layouts[0].kind,
            check_end::OperandLayoutKind::Call);
  EXPECT_EQ(syntax.variants[0].operand_layouts[3].kind,
            check_end::OperandLayoutKind::IndirectCall);
  EXPECT_EQ(syntax.variants[0].operand_layouts[3].slots[0].allowed_shapes,
            check_end::OperandSyntaxShape::CallTarget);
  EXPECT_EQ(syntax.variants[0].operand_layouts[3].slots[1].allowed_shapes,
            check_end::OperandSyntaxShape::CallTargetSet);
  EXPECT_EQ(resolved_descriptor.variants[0]
                .operand_layouts[3]
                .bindings[0]
                .allowed_shapes,
            checker::OperandShape::IndirectCallee);
  EXPECT_EQ(
      resolved_descriptor.variants[0].operand_layouts[3].fields[0].value_kind,
      check_end::ResolvedValueKind::IndirectCallee);
  for (size_t index = 3; index != 6; ++index) {
    const auto& availability =
        checker_descriptor.variants[0].operand_layouts[index].availability;
    EXPECT_EQ(availability.minimum_ptx_version.major, 2u);
    EXPECT_EQ(availability.minimum_ptx_version.minor, 1u);
    EXPECT_EQ(availability.minimum_sm_version, 20u);
  }

  const auto parsed_module_1 = parseModule(R"ptx(
.entry caller() {
  .reg .u64 %fptr;
  call %fptr;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& indirect = *parsed_module_1;
  const auto indirect_resolved = resolveModule(indirect);
  ASSERT_FALSE(indirect_resolved.has_value());
  EXPECT_EQ(indirect_resolved.error().front().message,
            "Indirect call register targets require a function-local "
            ".callprototype or .calltargets metadata operand.");

  const auto parsed_module_2 = parseModule(R"ptx(
.func maybe_callee(.reg .u32 input);
.func another_callee(.reg .u32 value);
.entry caller() {
  .reg .u64 %fptr;
  .reg .u32 %result, %input;
empty_prototype: .callprototype _;
targets: .calltargets maybe_callee, another_callee;
returning_prototype: .callprototype (.reg .u32 result) _ (.reg .u32 input);
  call %fptr, empty_prototype;
  call %fptr, (%input), targets;
  call (%result), %fptr, (%input), returning_prototype;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto& indirect_forms = *parsed_module_2;
  const auto resolved = resolveModule(indirect_forms);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto caller_scope =
      *resolved->symbols.symbol(resolved->functions[2].symbol_id).owned_scope;
  const auto fptr = resolved->symbols.lookup(caller_scope, "%fptr");
  const auto empty_prototype =
      resolved->symbols.lookup(caller_scope, "empty_prototype");
  ASSERT_TRUE(fptr.has_value());
  const auto targets = resolved->symbols.lookup(caller_scope, "targets");
  const auto returning_prototype =
      resolved->symbols.lookup(caller_scope, "returning_prototype");
  ASSERT_TRUE(empty_prototype.has_value());
  ASSERT_TRUE(targets.has_value());
  ASSERT_TRUE(returning_prototype.has_value());
  const auto& body = resolved->functions[2].body;
  ASSERT_EQ(body.size(), 3u);
  const auto& first = std::get<Call::Direct::TargetMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(body[0]).variant).operands);
  const auto& second = std::get<Call::Direct::TargetInputMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(body[1]).variant).operands);
  const auto& third = std::get<Call::Direct::ReturnTargetInputMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(body[2]).variant).operands);
  const auto& first_target = std::get<ResolvedRegisterRef>(first.target.value);
  ASSERT_TRUE(first_target.symbol_id.has_value());
  EXPECT_EQ(first_target.symbol_id, fptr->symbol);
  const auto& first_metadata =
      std::get<ResolvedIndirectMetadataRef>(first.metadata.value);
  EXPECT_EQ(first_metadata.symbol_id, empty_prototype->symbol);
  EXPECT_EQ(first_metadata.declaration_kind,
            binding::SymbolKind::CallPrototype);
  const auto& second_metadata =
      std::get<ResolvedIndirectMetadataRef>(second.metadata.value);
  EXPECT_EQ(second_metadata.symbol_id, targets->symbol);
  EXPECT_EQ(second_metadata.declaration_kind,
            binding::SymbolKind::CallTargetSet);
  const auto& second_target =
      std::get<ResolvedRegisterRef>(second.target.value);
  ASSERT_TRUE(second_target.symbol_id.has_value());
  EXPECT_EQ(second_target.symbol_id, fptr->symbol);
  const auto& third_target = std::get<ResolvedRegisterRef>(third.target.value);
  ASSERT_TRUE(third_target.symbol_id.has_value());
  EXPECT_EQ(third_target.symbol_id, fptr->symbol);
  const auto& third_metadata =
      std::get<ResolvedIndirectMetadataRef>(third.metadata.value);
  EXPECT_EQ(third_metadata.symbol_id, returning_prototype->symbol);
  EXPECT_EQ(third_metadata.declaration_kind,
            binding::SymbolKind::CallPrototype);

  const checker::Context old_target{
      .target = {.ptx_version = {2, 0}, .sm_version = 19},
      .instruction_range = indirect_forms.range,
  };
  const checker::Context supported_target{
      .target = {.ptx_version = {2, 1}, .sm_version = 20},
      .instruction_range = indirect_forms.range,
  };
  const auto rejected = checker::check(std::get<Call>(body[0]), old_target);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      checker::check(std::get<Call>(body[0]), supported_target).has_value());
}

TEST(ResolvedModule, ReportsIndirectCallAbiMismatches) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func target(.reg .u32 input);
.func another_target(.reg .u32 value);
.entry caller() {
  .reg .u64 %fptr, %wide;
  .reg .b8 %byte;
arity: .callprototype (.reg .u32 result) _;
targets: .calltargets target, another_target;
bytes: .callprototype _ (.param .align 16 .b8 expected[8]);
literal: .callprototype _ (.param .u16 expected);
  call %fptr, arity;
  call %fptr, (%wide), targets;
  call %fptr, (%byte), bytes;
  call %fptr, (65536), literal;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 4u);
  EXPECT_EQ(resolved.error()[0].message,
            "Indirect call via metadata 'arity' has 0 return arguments but "
            "callee requires 1.");
  EXPECT_EQ(resolved.error()[1].message,
            "Indirect call via metadata 'targets' input argument 1 has type "
            "or vector shape mismatch.");
  EXPECT_EQ(resolved.error()[2].message,
            "Indirect call via metadata 'bytes' input argument 1 has call "
            "argument state-space mismatch.");
  EXPECT_EQ(resolved.error()[3].message,
            "Integer literal '65536' is out of range for scalar type 'U16'.");
}

TEST(ResolvedModule, IsolatesSameNamedIndirectMetadataByFunctionScope) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func first() {
  .reg .u64 %fptr;
metadata: .callprototype _ (.reg .u32 input);
  call %fptr, metadata;
}
.func second() {
  .reg .u64 %fptr;
metadata: .callprototype _;
  call %fptr, metadata;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Indirect call via metadata 'metadata' has 0 input "
            "arguments but callee requires 1.");
}

TEST(ResolvedModule, RejectsEntryAsDirectCallTarget) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry callee() {}
.entry caller() {
  call callee;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      resolved.error().front().message,
      "Direct call target 'callee' must name a device .func, not an .entry.");
}

TEST(ResolvedModule, StandaloneDirectCallRemainsUnbound) {
  PtxSyntaxParser parser("call callee, (argument, 4);");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  const auto resolved = resolveInstruction(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& call = std::get<Call>(*resolved);
  const auto& payload = std::get<Call::Direct::TargetInputOperands>(
      std::get<Call::Direct>(call.variant).operands);
  EXPECT_FALSE(payload.target.value.symbol_id.has_value());
  ASSERT_EQ(payload.arguments.value.values.size(), 2u);
  EXPECT_FALSE(std::get<ResolvedCallParameterRef>(
                   payload.arguments.value.values.front().value)
                   .symbol_id.has_value());
}

TEST(ResolvedModule, StandaloneResolutionRemainsDeclarationFree) {
  PtxSyntaxParser parser("@!%p7 add.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

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
  const auto parsed_module_1 = parseModule(R"ptx(
.global .u32 values[2] = {1, 2, 3};
.entry kernel() { ret; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Initializer dimension contains 3 elements but its declared "
            "extent is 2.");
}

TEST(ResolvedModule, ResolvesACompatibleFunctionDefinitionScope) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func helper(.reg .u32 input);
.func helper(.reg .u32 input) {
  .reg .u32 %result;
  .reg .u32 %source;
  add.u32 %result, %source, 1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

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

TEST(ResolvedModule, ResolvesSameModuleAliasCallsToCanonicalSignature) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 9.3
.func alias_fn(.param .u32 input);
.func target(.param .u32 input) {}
.alias alias_fn, target;
.entry kernel() {
  .reg .u32 %r;
  call alias_fn, (%r);
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

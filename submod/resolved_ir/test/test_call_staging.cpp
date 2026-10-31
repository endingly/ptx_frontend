#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a complete call fixture; syntax failures cannot satisfy rejection tests. */
std::optional<syntax_ast::AstModule> parseCallModule(std::string_view body) {
  const std::string source = std::string{R"ptx(
.version 9.3
.target sm_80
.address_size 64
.file 1 "staging.ptx"
.extern .func (.param .b32 output) take2(.param .b32 x, .param .b32 y);
.extern .func noop();
.entry k() {
  .reg .b32 %r<3>;
  .reg .pred %p;
)ptx"} + std::string{body} + "\n}";
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    ADD_FAILURE() << "Call fixture must parse without diagnostics.";
    return std::nullopt;
  }
  return std::move(*ast);
}

/** Hoisted and interleaved declarations preserve instructions and bound arguments. */
TEST(CallStaging, PreservesCallsAcrossOrdinaryDeclarations) {
  for (const std::string_view body : {
           R"ptx(
  .param .b32 a, b, result;
  st.param.b32 [a], %r0;
  st.param.b32 [b], %r1;
  call (result), take2, (a, b);
  ld.param.b32 %r0, [result];
  ld.param.b32 %r1, [result];
)ptx",
           R"ptx(
  .param .b32 a;
  st.param.b32 [a], %r0;
  .param .b32 b;
  st.param.b32 [b], %r1;
  .param .b32 result;
  call (result), take2, (a, b);
  .reg .b32 %temporary;
  ld.param.b32 %r0, [result];
  .local .b32 scratch;
  ld.param.b32 %r1, [result];
)ptx",
           R"ptx(
  .param .b32 a;
  st.param.b32 [a], %r0;
  .loc 1 20 0
  .param .b32 b;
  .pragma "nounroll";
  st.param.b32 [b], %r1;
  .param .b32 result;
  .loc 1 21 0
  call (result), take2, (a, b);
  .param .b32 unused;
  .pragma "nounroll";
  ld.param.b32 %r0, [result];
  .reg .b32 %temporary;
  .loc 1 22 0
  ld.param.b32 %r1, [result];
)ptx"}) {
    SCOPED_TRACE(body);
    const auto ast = parseCallModule(body);
    ASSERT_TRUE(ast);
    const auto module = resolveModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    ASSERT_EQ(module->functions.size(), 3u);
    const auto& function = module->functions.back();
    ASSERT_EQ(function.body.size(), 5u);
    EXPECT_TRUE(dynamic_cast<const StExplicitScalar*>(function.body[0].get()) !=
                nullptr);
    EXPECT_TRUE(dynamic_cast<const StExplicitScalar*>(function.body[1].get()) !=
                nullptr);
    EXPECT_TRUE(dynamic_cast<const LdExplicitScalar*>(function.body[3].get()) !=
                nullptr);
    EXPECT_TRUE(dynamic_cast<const LdExplicitScalar*>(function.body[4].get()) !=
                nullptr);
    const auto* call = dynamic_cast<const CallDirect*>(function.body[2].get());
    ASSERT_NE(call, nullptr);
    ASSERT_TRUE(call->return_value.has_value());
    ASSERT_TRUE(call->arguments.has_value());
    const auto scope = module->symbols.symbol(function.symbol_id).owned_scope;
    ASSERT_TRUE(scope);
    const auto result = module->symbols.lookup(*scope, "result");
    ASSERT_TRUE(result);
    EXPECT_EQ(call->return_value->value.symbol_id, result->symbol);
    ASSERT_EQ(call->arguments->value.values.size(), 2u);
    for (size_t index = 0; index < 2; ++index) {
      const auto symbol =
          module->symbols.lookup(*scope, index == 0 ? "a" : "b");
      ASSERT_TRUE(symbol);
      const auto& argument = std::get<ResolvedCallParameterRef>(
          call->arguments->value.values[index].value);
      EXPECT_EQ(argument.symbol_id, symbol->symbol);
    }
  }
}

/** Every intervening executed instruction interrupts transfer adjacency. */
TEST(CallStaging, RejectsInstructionAndControlBoundaries) {
  for (const bool before_call : {true, false}) {
    for (const std::string_view gap :
         {"mov.b32 %r2, %r2;", "@%p mov.b32 %r2, %r2;",
          "bra resume; resume:", "ret;", "call noop;"}) {
      SCOPED_TRACE(before_call ? "argument store" : "return load");
      SCOPED_TRACE(gap);
      const std::string body =
          std::string{".param .b32 a, b, result;\nst.param.b32 [a], %r0;\n"} +
          (before_call ? std::string{gap} : "") +
          "\ncall (result), take2, (a, b);\n" +
          (before_call ? "" : std::string{gap}) +
          "\nld.param.b32 %r1, [result];\ndone:";
      const auto ast = parseCallModule(body);
      ASSERT_TRUE(ast);
      const auto& function =
          std::get<syntax_ast::AstFunction>(ast->items.back());
      std::optional<SourceRange> offending_range;
      for (const auto& item : function.body) {
        const auto* instruction =
            std::get_if<syntax_ast::AstInstruction>(&item);
        if (instruction &&
            instruction->opcode.syntax.text == (before_call ? "st" : "ld"))
          offending_range = instruction->range;
      }
      ASSERT_TRUE(offending_range);
      const auto module = resolveModule(*ast);
      ASSERT_FALSE(module);
      ASSERT_EQ(module.error().size(), 1u);
      EXPECT_EQ(module.error().front().stage(),
                ResolveDiagnosticStage::Resolution);
      EXPECT_EQ(module.error().front().range, *offending_range);
      EXPECT_EQ(
          module.error().front().message,
          before_call
              ? "A function-local .param argument store must be in the "
                "contiguous block immediately before a call that uses it."
              : "A function-local .param return load must be in the "
                "contiguous block immediately after a call that returns it.");
    }
  }
}

/** Declarations must not hide illegal predication of either staging operation. */
TEST(CallStaging, RejectsPredicationAcrossDeclarations) {
  for (const bool store : {true, false}) {
    SCOPED_TRACE(store ? "argument store" : "return load");
    const std::string body =
        std::string{".param .b32 a, b, result;\n"} + (store ? "@%p " : "") +
        "st.param.b32 [a], %r0;\n" +
        ".reg .b32 %temporary;\ncall (result), take2, (a, b);\n" +
        ".param .b32 unused;\n" + (store ? "" : "@%p ") +
        "ld.param.b32 %r1, [result];";
    const auto ast = parseCallModule(body);
    ASSERT_TRUE(ast);
    const auto& function = std::get<syntax_ast::AstFunction>(ast->items.back());
    std::optional<SourceRange> predicate_range;
    for (const auto& item : function.body) {
      if (const auto* instruction =
              std::get_if<syntax_ast::AstInstruction>(&item);
          instruction && instruction->predicate)
        predicate_range = instruction->predicate->range;
    }
    ASSERT_TRUE(predicate_range);
    const auto module = resolveModule(*ast);
    ASSERT_FALSE(module);
    ASSERT_EQ(module.error().size(), 1u);
    EXPECT_EQ(module.error().front().stage(),
              ResolveDiagnosticStage::Resolution);
    EXPECT_EQ(module.error().front().range, *predicate_range);
    EXPECT_EQ(
        module.error().front().message,
        store ? "A function-local .param argument store cannot be predicated."
              : "A function-local .param return load cannot be predicated.");
  }
}

/** Shadowed objects stay distinct; a visible outer return object may cross braces. */
TEST(CallStaging, KeepsNestedSequencesWithinTheirScope) {
  const auto ast = parseCallModule(R"ptx(
  .param .b32 result;
  {
    .param .b32 a, b;
    st.param.b32 [a], %r0;
    .param .b32 result;
    call (result), take2, (a, b);
    .reg .b32 %temporary;
    ld.param.b32 %temporary, [result];
  }
)ptx");
  ASSERT_TRUE(ast);
  const auto module = resolveModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  const auto& function = module->functions.back();
  ASSERT_EQ(function.body.size(), 3u);
  const auto scope = module->symbols.symbol(function.symbol_id).owned_scope;
  ASSERT_TRUE(scope);
  const auto outer = module->symbols.lookup(*scope, "result");
  ASSERT_TRUE(outer);
  const auto& call = dynamic_cast<const CallDirect&>(*function.body[1]);
  ASSERT_TRUE(call.return_value.has_value());
  ASSERT_TRUE(call.return_value->value.symbol_id);
  EXPECT_NE(call.return_value->value.symbol_id, outer->symbol);
  const auto& load = dynamic_cast<const LdExplicitScalar&>(*function.body[2]);
  EXPECT_EQ(std::get<ResolvedSymbolRef>(load.address.value.base).symbol_id,
            call.return_value->value.symbol_id);

  const auto crossed_ast = parseCallModule(R"ptx(
  .param .b32 a, b, result;
  call (result), take2, (a, b);
  {
    .reg .b32 %temporary;
    ld.param.b32 %temporary, [result];
  }
)ptx");
  ASSERT_TRUE(crossed_ast);
  const auto crossed = resolveModule(*crossed_ast);
  ASSERT_TRUE(crossed) << crossed.error().front().message;
  EXPECT_TRUE(validateModule(*crossed));
}

/** Nonexecuting lexical and metadata statements do not create instruction gaps. */
TEST(CallStaging, PreservesVisibleObjectsAcrossTransparentStatements) {
  for (const auto gap :
       {"resume:", "{ }", "{ .reg .b32 %inner; }",
        "prototype: .callprototype _;", "callees: .calltargets take2;",
        "branches: .branchtargets done;",
        ".loc 1 20 0\n.pragma \"nounroll\";"}) {
    for (bool before : {true, false}) {
      SCOPED_TRACE(gap);
      auto body = std::string(".param .b32 a, b, result;\n") +
                  "{ st.param.b32 [a], %r0; }\n" + (before ? gap : "") +
                  "\n@%p call (result), take2, (a, b);\n" +
                  (before ? "" : gap) +
                  "\n{ { ld.param.b32 %r1, [result]; } }\ndone:";
      const auto ast = parseCallModule(body);
      ASSERT_TRUE(ast);
      auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module) << module.error().front().message;
      EXPECT_TRUE(resolveAndValidateModule(*ast));
      EXPECT_TRUE(validateModule(*module));
      auto copy = *module;
      for (auto& instruction : copy.functions.back().body)
        instruction = instruction->clone();
      EXPECT_TRUE(validateModule(copy));
    }
  }
}

/** Supply equal-sized aggregate inputs/returns and independently bound local arrays. */
std::optional<syntax_ast::AstModule> parseTransferModule(
    std::string_view body) {
  const auto source = std::string(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.extern .func (.param .align 16 .b8 output[16]) transfer(.param .align 16 .b8 input[16]);
.entry kernel() {
.param .align 16 .b8 a[16], b[16], result[16];
.reg .b32 %r<4>;
.reg .b8 %byte;
.reg .u32 idx;
.reg .pred %p;
)ptx") + std::string(body) +
                      "\n}";
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    ADD_FAILURE() << "Transfer fixture must parse without diagnostics.";
    return std::nullopt;
  }
  return std::move(*parsed);
}

/** Byte-address and named element forms share identity while preserving source shape. */
TEST(CallStaging, NormalizesArrayAliasesAcrossAllResolutionAndOwnedPaths) {
  for (const auto input : {"[a]", "a[0]", "[a+4]", "a[4]", "a[idx]", "a[idx+4]",
                           "[a-4]", "a[-4]"}) {
    for (const auto output :
         {"[result]", "result[0]", "[result+4]", "result[4]", "result[idx]"}) {
      SCOPED_TRACE(input);
      SCOPED_TRACE(output);
      const bool dynamic_input =
          std::string_view(input).find("idx") != std::string_view::npos;
      const bool dynamic_output =
          std::string_view(output).find("idx") != std::string_view::npos;
      const auto body =
          std::string(dynamic_input ? "st.param.b8 " : "st.param.b32 ") +
          input + (dynamic_input ? ", %byte;\n" : ", %r0;\n") +
          "call (result), transfer, (a);\n" +
          (dynamic_output ? "ld.param.b8 %byte, " : "ld.param.b32 %r1, ") +
          output + ";";
      std::optional<ResolvedModule> owned;
      {
        const auto ast = parseTransferModule(body);
        ASSERT_TRUE(ast);
        auto module = resolveModuleOnly(*ast);
        ASSERT_TRUE(module) << module.error().front().message;
        EXPECT_TRUE(resolveModule(*ast));
        EXPECT_TRUE(resolveAndValidateModule(*ast));
        owned = std::move(*module);
      }
      EXPECT_TRUE(validateModule(*owned));
      auto copy = *owned;
      for (auto& instruction : copy.functions.back().body)
        instruction = instruction->clone();
      EXPECT_TRUE(validateModule(copy));
    }
  }
}

/** Predication and adjacency apply to every local object even at a symbolic index. */
TEST(CallStaging, RejectsArrayAliasPredicatesWrongObjectsAndInstructionGaps) {
  for (const auto input : {"[a+4]", "a[4]", "a[idx]"}) {
    for (const auto output : {"[result+4]", "result[4]", "result[idx]"}) {
      for (int failure = 0; failure < 6; ++failure) {
        SCOPED_TRACE(failure);
        const bool dynamic_input =
            std::string_view(input).find("idx") != std::string_view::npos;
        const bool dynamic_output =
            std::string_view(output).find("idx") != std::string_view::npos &&
            failure != 5;
        auto body =
            std::string(failure == 0 ? "@%p " : "") +
            (dynamic_input ? "st.param.b8 " : "st.param.b32 ") + input +
            (dynamic_input ? ", %byte;\n" : ", %r0;\n") +
            (failure == 1 ? "mov.b32 %r2, %r2;\n" : "") +
            "call (result), transfer, (" + (failure == 2 ? "b" : "a") + ");\n" +
            (failure == 3 ? "mov.b32 %r2, %r2;\n" : "") +
            (failure == 4 ? "@%p " : "") +
            (dynamic_output ? "ld.param.b8 %byte, " : "ld.param.b32 %r1, ") +
            (failure == 5 ? "b[4]" : output) + ";";
        const auto ast = parseTransferModule(body);
        ASSERT_TRUE(ast);
        EXPECT_FALSE(resolveModuleOnly(*ast));
        EXPECT_FALSE(resolveModule(*ast));
        EXPECT_FALSE(resolveAndValidateModule(*ast));
      }
    }
  }
}

/** Revalidation derives staging facts anew from mutable owned instructions. */
TEST(CallStaging, RechecksOwnedArrayPredicateOrderIdentityAndNormalizedBytes) {
  auto owned = [] {
    const auto ast = parseTransferModule(
        "st.param.b32 a[4], %r0; @%p call (result), transfer, (a); "
        "ld.param.b32 %r1, result[4];");
    EXPECT_TRUE(ast);
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(owned) << owned.error().front().message;
  for (int mutation = 0; mutation < 6; ++mutation) {
    auto copy = *owned;
    auto& body = copy.functions.back().body;
    auto& store = dynamic_cast<StExplicitScalar&>(*body[0]);
    if (mutation == 0)
      store.execution_predicate = body[1]->execution_predicate;
    if (mutation == 1)
      std::swap(body[0], body[1]);
    if (mutation == 2)
      store.address.value.named_index->byte_displacement += 4;
    if (mutation == 3) {
      const auto& call = dynamic_cast<const CallDirect&>(*body[1]);
      store.address.value.base = std::get<ResolvedSymbolRef>(
          dynamic_cast<const LdExplicitScalar&>(*body[2]).address.value.base);
      EXPECT_TRUE(call.return_value);
    }
    if (mutation == 4)
      store.state_space.value = MemoryStateSpace::Global;
    if (mutation == 5) {
      auto& call = dynamic_cast<CallDirect&>(*body[1]);
      auto& argument = std::get<ResolvedCallParameterRef>(
          call.arguments->value.values[0].value);
      const auto scope =
          copy.symbols.symbol(copy.functions.back().symbol_id).owned_scope;
      ASSERT_TRUE(scope);
      const auto other = copy.symbols.lookup(*scope, "b");
      ASSERT_TRUE(other);
      argument.symbol_id = other->symbol;
      argument.spelling = "b";
    }
    EXPECT_FALSE(validateModule(copy));
  }
}

/** Aggregate fields share one actual, while qualifier and declaration rules remain independent. */
TEST(CallStaging,
     PreservesVectorFieldsAndRejectsAliasQualifierAndScopeMismatches) {
  const auto ast = parseTransferModule(
      "st.param.v2.b32 a[0], {%r0,%r1}; st.param.b32 [a+8], %r2; "
      "call (result), transfer, (a); ld.param.v2.b32 {%r0,%r1}, result[0]; "
      "ld.param.b32 %r2, [result+8];");
  ASSERT_TRUE(ast);
  EXPECT_TRUE(resolveAndValidateModule(*ast));
  for (const auto address : {"[a]", "a[0]", "a[idx]"}) {
    const auto invalid =
        parseTransferModule(std::string("st.param::entry.b8 ") + address +
                            ", %byte; call (result), transfer, (a);");
    ASSERT_TRUE(invalid);
    EXPECT_FALSE(resolveModuleOnly(*invalid));
    EXPECT_FALSE(resolveAndValidateModule(*invalid));
  }
  for (const auto body :
       {"{ .param .b32 a, b; st.param.b32 [a], %r0; } call (result), take2, "
        "(a,b);",
        ".param .b32 a,b,result; { .param .b32 a; st.param.b32 [a], %r0; } "
        "call (result), take2, (a,b);",
        ".param .b32 a,b,result; call (result), take2, (a,b); { .param .b32 "
        "result; ld.param.b32 %r0,[result]; }",
        ".param .b32 group<2>, result; st.param.b32 [group0], %r0; call "
        "(result), take2, (group1,group1);",
        ".param .b32 group<2>, result; call (group0), take2, (result,result); "
        "ld.param.b32 %r0,[group1];"}) {
    SCOPED_TRACE(body);
    const auto invalid = parseCallModule(body);
    ASSERT_TRUE(invalid);
    EXPECT_FALSE(resolveModuleOnly(*invalid));
    EXPECT_FALSE(resolveAndValidateModule(*invalid));
  }
}

/** A coherent in-range compact member remains distinct from its neighboring member. */
TEST(CallStaging, RechecksOwnedCompactMemberObjectIdentity) {
  auto owned = [] {
    const auto ast = parseCallModule(
        ".param .b32 group<2>, result; st.param.b32 [group0], %r0; "
        "call (result), take2, (group0,group0); ld.param.b32 %r1,[result];");
    EXPECT_TRUE(ast);
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto copy = *owned;
  auto& store = dynamic_cast<StExplicitScalar&>(*copy.functions.back().body[0]);
  auto& base = std::get<ResolvedSymbolRef>(store.address.value.base);
  ASSERT_EQ(base.parameterized_index, 0u);
  base.parameterized_index = 1u;
  EXPECT_FALSE(validateModule(copy));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

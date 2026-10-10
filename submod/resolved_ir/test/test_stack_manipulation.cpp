#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/stack_manipulation/alloca.gen.hpp>
#include <ptx_frontend/resolved_ir/model/stack_manipulation/stackrestore.gen.hpp>
#include <ptx_frontend/resolved_ir/model/stack_manipulation/stacksave.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Build an owned module while destroying all source syntax before returning. */
std::optional<ResolvedModule> stack_module(std::string_view body,
                                           std::string_view version = "7.3",
                                           std::string_view target = "52",
                                           std::string_view address = "64",
                                           bool entry = true) {
  const auto parsed = test_helpers::parseModule(
      ".version " + std::string(version) + "\n.target sm_" +
      std::string(target) + "\n.address_size " + std::string(address) + "\n" +
      (entry ? ".entry k()" : ".func k()") + R"ptx( {
  .reg .b32 %r<4>;
  .reg .s64 %rd<4>;
  .reg .pred %p;
  .reg .f32 %f;
)ptx" +
      std::string(body) + "\n}\n");
  if (!parsed || !parsed.diagnostics.empty())
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}
/** Validate all eight source forms, guards, integer carriers and local roles. */
TEST(StackManipulation, AllSourceFormsSurviveAstDestruction) {
  for (const bool entry : {true, false}) {
    for (const std::string_view address : {"32", "64"}) {
      auto module = stack_module(R"ptx(
@%p stacksave.u32 %r0;
stacksave.u64 %rd0;
stackrestore.u32 %r0;
stackrestore.u64 %rd0;
alloca.u32 %r1, %r2;
alloca.u32 %r1, 0, 8;
alloca.u64 %rd1, %rd2;
alloca.u64 %rd1, 16, 8;
)ptx",
                                 "7.3", "52", address, entry);
      ASSERT_TRUE(module);
      const auto checked = validateModule(
          *module, ModuleValidationPolicy::RequireCompleteContext);
      ASSERT_TRUE(checked) << (checked ? "" : checked.error().front().message);
      const auto& body = module->functions.front().body;
      ASSERT_EQ(body.size(), 8u);
      const auto& saved = dynamic_cast<const StacksaveU32&>(*body[0]);
      EXPECT_EQ(saved.stack_descriptor()->operation, StackOperation::Save);
      EXPECT_TRUE(saved.execution_predicate);
      EXPECT_EQ(
          saved.dst.value.enclosing_function_kind,
          entry ? EnclosingFunctionKind::Entry : EnclosingFunctionKind::Device);
      const auto& allocated = dynamic_cast<const AllocaU32&>(*body[4]);
      EXPECT_EQ(allocated.dst.value.address_state_space,
                base::DeclarationStateSpace::Local);
      EXPECT_EQ(allocated.stack_descriptor()->width, 32);
      EXPECT_EQ(allocated.stack_descriptor()->default_alignment, 8u);
    }
  }
}
/** Keep unsigned use-width conversion for byte counts, including zero. */
TEST(StackManipulation, SizeUsesOrdinaryUnsignedConversion) {
  auto module = stack_module(
      "alloca.u32 %r0, -1; alloca.u32 %r0, 4294967296; alloca.u64 %rd0, "
      "18446744073709551615;");
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  const auto& body = module->functions.front().body;
  const auto& negative = std::get<ResolvedImmediate>(
      dynamic_cast<const AllocaU32&>(*body[0]).size.value);
  EXPECT_EQ(negative.bits, 0xffffffffu);
  EXPECT_TRUE(negative.is_negative);
  const auto& wrapped = std::get<ResolvedImmediate>(
      dynamic_cast<const AllocaU32&>(*body[1]).size.value);
  EXPECT_EQ(wrapped.bits, 0u);
  EXPECT_EQ(wrapped.integer_source_bits, 4294967296ull);
}
/** Enforce exact source kinds, stack widths, and alignment constraints. */
TEST(StackManipulation, RejectsInvalidOperands) {
  for (const std::string_view body :
       {"stacksave.u32 1;", "stackrestore.u32 0;", "stacksave.u32 %rd0;",
        "stacksave.u32 %f;", "stacksave.u32 %p;", "stacksave.u32 %tid.x;",
        "alloca.u32 %r0, %rd0;", "alloca.u32 %r0, %f;",
        "alloca.u32 %r0, 8, %r1;", "alloca.u32 %r0, 8, 0;",
        "alloca.u32 %r0, 8, 3;", "alloca.u32 %r0, 8, -1;",
        "alloca.u32 %r0, 8, 8388609;", "alloca.u32 %r0, 8, 4294967296;",
        "alloca.u32 %r0, 8, 4294967299;"}) {
    SCOPED_TRACE(body);
    auto module = stack_module(body);
    EXPECT_TRUE(!module || !validateModule(*module));
  }
  for (const std::string_view align :
       {"1", "8", "8388608", "4294967297", "-4294967295"}) {
    auto module =
        stack_module("alloca.u32 %r0, 8, " + std::string(align) + ";");
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
  }
}
/** Apply PTX 7.3 and sm_52 independently to every stack operation. */
TEST(StackManipulation, Availability) {
  for (const std::string_view body :
       {"stacksave.u32 %r0;", "stackrestore.u64 %rd0;", "alloca.u32 %r0, 8;"}) {
    auto old_ptx = stack_module(body, "7.2");
    ASSERT_TRUE(old_ptx);
    const auto ptx_check = validateModule(*old_ptx);
    ASSERT_FALSE(ptx_check);
    EXPECT_EQ(ptx_check.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    auto old_sm = stack_module(body, "7.3", "50");
    ASSERT_TRUE(old_sm);
    const auto sm_check = validateModule(*old_sm);
    ASSERT_FALSE(sm_check);
    EXPECT_EQ(sm_check.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
}
/** Recheck mutated payload roles and full-module function ownership. */
TEST(StackManipulation, RejectsForgedOwnedPayloads) {
  const auto fixture = "stacksave.u32 %r0; alloca.u32 %r1, 16, 8;";
  for (int mutation = 0; mutation != 7; ++mutation) {
    auto module = stack_module(fixture);
    ASSERT_TRUE(module);
    auto& save =
        dynamic_cast<StacksaveU32&>(*module->functions.front().body[0]);
    auto& alloc = dynamic_cast<AllocaU32&>(*module->functions.front().body[1]);
    switch (mutation) {
      case 0:
        save.dst.value.enclosing_function_kind = EnclosingFunctionKind::Unknown;
        break;
      case 1:
        save.dst.value.function_scope.reset();
        break;
      case 2:
        save.dst.value.enclosing_function_kind = EnclosingFunctionKind::Device;
        break;
      case 3:
        save.dst.value.register_ref.symbol_id.reset();
        break;
      case 4:
        save.dst.value.register_ref.declared_type = ScalarType::F32;
        break;
      case 5:
        alloc.dst.value.address_state_space =
            base::DeclarationStateSpace::Global;
        break;
      case 6:
        alloc.operand_layout.value = 0;
        break;
    }
    EXPECT_FALSE(validateModule(*module));
  }
}
/** Keep standalone unknown provenance honest while preserving all eight forms. */
TEST(StackManipulation, StandaloneFragmentBoundary) {
  for (const std::string_view source :
       {"stacksave.u32 %r0;", "stacksave.u64 %rd0;", "stackrestore.u32 %r0;",
        "stackrestore.u64 %rd0;", "alloca.u32 %r0, 0;", "alloca.u32 %r0, 8, 1;",
        "alloca.u64 %rd0, %size0;", "alloca.u64 %rd0, 16, 8;"}) {
    SCOPED_TRACE(source);
    auto ast = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
    auto resolved = resolveInstruction(*ast);
    ASSERT_TRUE(resolved) << resolved.error().message;
    const checker::Context context{
        .target = {.ptx_version = {7, 3}, .sm_version = 52}};
    EXPECT_TRUE((*resolved)->check(context));
  }
  auto ast = test_helpers::parseInstruction("stacksave.u32 %r0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  auto resolved = resolveInstruction(*ast);
  ASSERT_TRUE(resolved);
  auto& save = dynamic_cast<StacksaveU32&>(**resolved);
  EXPECT_EQ(save.dst.value.enclosing_function_kind,
            EnclosingFunctionKind::Unknown);
  EXPECT_FALSE(save.dst.value.function_scope);
  save.dst.value.enclosing_function_kind = EnclosingFunctionKind::Entry;
  EXPECT_FALSE(
      save.check({.target = {.ptx_version = {7, 3}, .sm_version = 52}}));
}
/** Explicit binding context can retain register identity without a function scope. */
TEST(StackManipulation, ExplicitPartialContextRemainsAFragment) {
  auto parsed = test_helpers::parseModule(R"ptx(
.version 7.3
.target sm_52
.entry k() { .reg .u32 %r0; stacksave.u32 %r0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto bound = binding::bindSymbols(*parsed);
  ASSERT_TRUE(bound.diagnostics.empty());
  const auto function = bound.table.lookup(bound.table.moduleScope(), "k");
  ASSERT_TRUE(function);
  const auto scope = bound.table.symbol(function->symbol).owned_scope;
  ASSERT_TRUE(scope);
  const ResolveContext partial{
      .symbols = bound.table, .scope = *scope, .function_is_entry = true};
  auto ast = test_helpers::parseInstruction("stacksave.u32 %r0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
  auto resolved = resolveInstruction(*ast, partial);
  ASSERT_TRUE(resolved);
  auto& token = dynamic_cast<StacksaveU32&>(**resolved).dst.value;
  EXPECT_TRUE(token.register_ref.symbol_id);
  EXPECT_EQ(token.enclosing_function_kind, EnclosingFunctionKind::Unknown);
  EXPECT_FALSE(token.function_scope);
  const checker::Context context{
      .target = {.ptx_version = {7, 3}, .sm_version = 52}};
  EXPECT_TRUE((*resolved)->check(context));
  auto module = resolveModuleOnly(*parsed);
  ASSERT_TRUE(module);
  auto& owned_token =
      dynamic_cast<StacksaveU32&>(*module->functions[0].body[0]).dst.value;
  owned_token = token;
  EXPECT_FALSE(validateModule(*module));
  // Individual checking cannot prove a binding without the owning symbol table.
  token.enclosing_function_kind = EnclosingFunctionKind::Entry;
  token.function_scope = *scope;
  token.register_ref.symbol_id.reset();
  EXPECT_TRUE((*resolved)->check(context));
  owned_token = token;
  EXPECT_FALSE(validateModule(*module));
}

/** Reject binding and ownership copied from a distinct real function. */
TEST(StackManipulation, CrossFunctionContextAndBinding) {
  auto parsed = test_helpers::parseModule(R"ptx(
.version 7.3
.target sm_52
.address_size 64
.func a() { .reg .u32 %r; stacksave.u32 %r; }
.func b() { .reg .u32 %r; stacksave.u32 %r; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto module = resolveModuleOnly(*parsed);
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  auto& first = dynamic_cast<StacksaveU32&>(*module->functions[0].body[0]);
  const auto& other =
      dynamic_cast<const StacksaveU32&>(*module->functions[1].body[0]);
  const auto original = first.dst.value;
  first.dst.value.function_scope = other.dst.value.function_scope;
  EXPECT_FALSE(validateModule(*module));
  first.dst.value = original;
  first.dst.value.register_ref = other.dst.value.register_ref;
  EXPECT_FALSE(validateModule(*module));
  first.dst.value = other.dst.value;
  EXPECT_FALSE(validateModule(*module));
}
/** Validate alignment provenance and both directions of source presence. */
TEST(StackManipulation, AlignmentProvenanceAndPresence) {
  for (int mutation = 0; mutation != 10; ++mutation) {
    auto module = stack_module("alloca.u32 %r0, 16, 8;");
    ASSERT_TRUE(module);
    auto& alloc = dynamic_cast<AllocaU32&>(*module->functions[0].body[0]);
    ASSERT_TRUE(alloc.alignment);
    switch (mutation) {
      case 0:
        alloc.alignment->value.integer_source_bits.reset();
        break;
      case 1:
        alloc.alignment->value.integer_source_bits = 3;
        break;
      case 2:
        alloc.alignment->value.bits = 1;
        break;
      case 3:
        alloc.alignment->value.type = ScalarType::U64;
        break;
      case 4:
        alloc.alignment->value.is_negative = true;
        break;
      case 5:
        alloc.alignment.reset();
        break;
      case 6:
        alloc.operand_layout.value = 0;
        break;
      case 7:
        alloc.size.value =
            ResolvedImmediate{.bits = 0, .type = ScalarType::F32};
        break;
      case 8:
        alloc.size.value = ResolvedImmediate{
            .bits = 0, .type = ScalarType::U32, .integer_source_bits = 1};
        break;
      case 9:
        alloc.size.value = ResolvedRegisterRef{
            .spelling = "%r",
            .register_class = ResolvedRegisterClass::General,
            .declared_type = ScalarType::U32,
            .vector_width = 2};
        break;
    }
    EXPECT_FALSE(validateModule(*module));
  }
  auto module = stack_module("alloca.u32 %r0, 16;");
  ASSERT_TRUE(module);
  auto& alloc = dynamic_cast<AllocaU32&>(*module->functions[0].body[0]);
  alloc.alignment = WithLocs<ResolvedImmediate>{ResolvedImmediate{
      .bits = 8, .type = ScalarType::U32, .integer_source_bits = 8}};
  EXPECT_FALSE(validateModule(*module));
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

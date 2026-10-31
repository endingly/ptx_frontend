#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_instruction_catalogue.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a small module whose body and source ranges are owned by the result. */
std::optional<ResolvedModule> owned_add_module() {
  PtxSyntaxParser parser(R"ptx(
.version 9.0
.target sm_80
.entry kernel() {
  .reg .u32 %r<3>;
  add.u32 %r0, %r1, %r2;
}
)ptx");
  auto parsed = parser.parseModule();
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveAndValidateModule(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Resolve opaque and numeric inputs without retaining parser or AST storage. */
std::optional<ResolvedModule> owned_opaque_module() {
  PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_80, texmode_independent
.address_size 64
.entry kernel(.param .align 8 .texref textures[2],
              .param .surfref surface, .param .samplerref sampler,
              .param .u32 number) {
  .reg .u32 %r<3>;
  add.u32 %r0, %r1, %r2;
  ret;
}
)ptx");
  auto parsed = parser.parseModule();
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveAndValidateModule(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Compare every owned resource field and independent instruction ownership. */
void expect_opaque_copy(const ResolvedFunction& copied,
                        const ResolvedFunction& original) {
  ASSERT_EQ(copied.opaque_entry_parameters.size(), 3u);
  ASSERT_EQ(copied.opaque_entry_parameters.size(),
            original.opaque_entry_parameters.size());
  for (std::size_t index = 0; index < original.opaque_entry_parameters.size();
       ++index) {
    SCOPED_TRACE(index);
    const auto& expected = original.opaque_entry_parameters[index];
    const auto& actual = copied.opaque_entry_parameters[index];
    EXPECT_EQ(actual.symbol_id, expected.symbol_id);
    EXPECT_EQ(actual.scope_id, expected.scope_id);
    EXPECT_EQ(actual.kind, expected.kind);
    EXPECT_EQ(actual.explicit_alignment, expected.explicit_alignment);
    EXPECT_EQ(actual.is_array, expected.is_array);
    EXPECT_EQ(actual.array_extent, expected.array_extent);
    EXPECT_EQ(actual.range, expected.range);
  }
  EXPECT_EQ(copied.symbol_id, original.symbol_id);
  EXPECT_EQ(copied.declaration_scope, original.declaration_scope);
  EXPECT_EQ(copied.source_identity, original.source_identity);
  EXPECT_EQ(copied.instruction_ranges, original.instruction_ranges);
  EXPECT_EQ(copied.instruction_opcodes, original.instruction_opcodes);
  ASSERT_EQ(copied.body.size(), original.body.size());
  for (std::size_t index = 0; index < original.body.size(); ++index) {
    ASSERT_NE(copied.body[index], nullptr);
    ASSERT_NE(original.body[index], nullptr);
    EXPECT_NE(copied.body[index].get(), original.body[index].get());
    EXPECT_EQ(copied.body[index]->instruction_kind(),
              original.body[index]->instruction_kind());
  }
  const auto* actual_add =
      dynamic_cast<const AddIntegerNoSat*>(copied.body[0].get());
  const auto* expected_add =
      dynamic_cast<const AddIntegerNoSat*>(original.body[0].get());
  ASSERT_NE(actual_add, nullptr);
  ASSERT_NE(expected_add, nullptr);
  EXPECT_EQ(actual_add->dst.value.spelling, expected_add->dst.value.spelling);
}

TEST(ResolvedFunctionOwnership, RetainsOpaqueParametersAcrossValueCopies) {
  auto original = owned_opaque_module();
  ASSERT_TRUE(original);
  const auto& function = original->functions.front();
  ASSERT_EQ(function.opaque_entry_parameters.size(), 3u);
  EXPECT_EQ(function.opaque_entry_parameters[0].kind,
            base::OpaqueResourceKind::Texture);
  EXPECT_EQ(function.opaque_entry_parameters[1].kind,
            base::OpaqueResourceKind::Surface);
  EXPECT_EQ(function.opaque_entry_parameters[2].kind,
            base::OpaqueResourceKind::Sampler);
  EXPECT_EQ(function.opaque_entry_parameters[0].explicit_alignment, 8u);
  EXPECT_TRUE(function.opaque_entry_parameters[0].is_array);
  EXPECT_EQ(function.opaque_entry_parameters[0].array_extent, 2u);
  EXPECT_FALSE(function.opaque_entry_parameters[1].is_array);
  EXPECT_FALSE(function.opaque_entry_parameters[1].explicit_alignment);
  EXPECT_FALSE(function.opaque_entry_parameters[1].array_extent);
  ASSERT_EQ(function.parameter_declarations.size(), 1u);

  ResolvedFunction copied(function);
  expect_opaque_copy(copied, function);
  ResolvedFunction assigned;
  assigned = function;
  expect_opaque_copy(assigned, function);
  const auto* assigned_body = assigned.body.front().get();
  assigned = assigned;
  expect_opaque_copy(assigned, function);
  EXPECT_EQ(assigned.body.front().get(), assigned_body);
  ResolvedFunction moved(std::move(copied));
  expect_opaque_copy(moved, function);
  copied = std::move(moved);
  expect_opaque_copy(copied, function);

  ResolvedModule module_copy(*original);
  expect_opaque_copy(module_copy.functions.front(), function);
  EXPECT_TRUE(validateModule(module_copy));
  ResolvedModule module_assigned;
  module_assigned = *original;
  expect_opaque_copy(module_assigned.functions.front(), function);
  EXPECT_TRUE(validateModule(module_assigned));
  module_assigned = module_assigned;
  EXPECT_TRUE(validateModule(module_assigned));
  ResolvedModule module_moved(std::move(module_copy));
  expect_opaque_copy(module_moved.functions.front(), function);
  EXPECT_TRUE(validateModule(module_moved));
  module_copy = std::move(module_moved);
  EXPECT_TRUE(validateModule(module_copy));
  EXPECT_TRUE(validateModule(*original));
}

/** A failing clone makes copy assignment's rollback observable. */
class ThrowOnClone final : public Instruction {
 public:
  /** Own no predicate or concrete fields; this is only a copy-failure probe. */
  ThrowOnClone() = default;
  /** Fail after an earlier source element has already cloned. */
  std::unique_ptr<Instruction> clone() const override {
    throw std::runtime_error("clone failure");
  }
  /** Satisfy the interface; this probe is never checked as PTX. */
  InstructionKind instruction_kind() const noexcept override {
    return InstructionKind::AddIntegerNoSat;
  }
  /** Satisfy the interface; this probe is never checked as PTX. */
  checker::CheckResult check(const checker::Context&) const override {
    return {};
  }
  /** Satisfy the interface; this probe has no references. */
  void visit_references(detail::IReferenceObserver&) const override {}
};

TEST(ResolvedFunctionOwnership, CopiesEveryNonNullInstructionIndependently) {
  auto module = owned_add_module();
  ASSERT_TRUE(module);
  ASSERT_EQ(module->functions.size(), 1u);
  auto& original = module->functions.front();
  ASSERT_EQ(original.body.size(), 1u);
  auto* first = dynamic_cast<AddIntegerNoSat*>(original.body.front().get());
  ASSERT_NE(first, nullptr);

  EXPECT_TRUE(original.opaque_entry_parameters.empty());
  ResolvedFunction copied = original;
  EXPECT_TRUE(copied.opaque_entry_parameters.empty());
  ASSERT_EQ(copied.body.size(), 1u);
  auto* second = dynamic_cast<AddIntegerNoSat*>(copied.body.front().get());
  ASSERT_NE(second, nullptr);
  EXPECT_NE(second, first);
  EXPECT_EQ(copied.instruction_ranges, original.instruction_ranges);
  EXPECT_EQ(copied.instruction_opcodes, original.instruction_opcodes);
  EXPECT_EQ(copied.source_identity, original.source_identity);
  second->dst.value.spelling = "%changed";
  EXPECT_EQ(first->dst.value.spelling, "%r0");

  copied.body.emplace_back();
  ResolvedFunction copied_with_null = copied;
  ASSERT_EQ(copied_with_null.body.size(), 2u);
  EXPECT_EQ(copied_with_null.body.back(), nullptr);
  EXPECT_NE(copied_with_null.body.front(), copied.body.front());
}

TEST(ResolvedFunctionOwnership, AssignmentHasStrongGuaranteeOnCloneFailure) {
  auto module = owned_add_module();
  ASSERT_TRUE(module);
  auto target = module->functions.front();
  const auto* target_instruction = target.body.front().get();
  const auto name = target.name;
  const auto ranges = target.instruction_ranges;

  auto source = module->functions.front();
  source.name = "different";
  source.body.push_back(std::make_unique<ThrowOnClone>());
  EXPECT_THROW(target = source, std::runtime_error);
  ASSERT_EQ(target.body.size(), 1u);
  EXPECT_EQ(target.body.front().get(), target_instruction);
  EXPECT_EQ(target.name, name);
  EXPECT_EQ(target.instruction_ranges, ranges);
  EXPECT_EQ(dynamic_cast<AddIntegerNoSat*>(target.body.front().get())
                ->dst.value.spelling,
            "%r0");
  target = target;
  ASSERT_EQ(target.body.size(), 1u);
  EXPECT_EQ(dynamic_cast<AddIntegerNoSat*>(target.body.front().get())
                ->dst.value.spelling,
            "%r0");
}

TEST(ResolvedFunctionOwnership, BodyGrowthKeepsBorrowedInstructionStable) {
  auto module = owned_add_module();
  ASSERT_TRUE(module);
  auto& function = module->functions.front();
  auto* instruction = function.body.front().get();
  auto* field = &dynamic_cast<AddIntegerNoSat&>(*instruction).dst;
  for (int index = 0; index < 64; ++index)
    function.body.push_back(instruction->clone());
  EXPECT_EQ(function.body.front().get(), instruction);
  EXPECT_EQ(&dynamic_cast<AddIntegerNoSat&>(*function.body.front()).dst, field);
  EXPECT_NE(function.body.back().get(), instruction);
}

TEST(ResolvedFunctionOwnership, NullBodyEntryFailsOwnedValidation) {
  auto module = owned_add_module();
  ASSERT_TRUE(module);
  module->functions.front().body.front().reset();
  const auto checked = validateModule(*module);
  ASSERT_FALSE(checked);
  ASSERT_FALSE(checked.error().empty());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

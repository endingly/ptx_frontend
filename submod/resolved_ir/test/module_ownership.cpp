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

  ResolvedFunction copied = original;
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

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a source module and release the AST before returning its body. */
std::optional<ResolvedModule> owned_add_module() {
  auto parsed = test_helpers::parseModule(R"ptx(
.version 9.0
.target sm_80
.entry kernel() {
  .reg .u32 %r<3>;
  add.u32 %r0, %r1, %r2;
}
)ptx");
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveAndValidateModule(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Exact clones own independent payloads; moved pointers preserve borrows. */
TEST(OwnedInstruction, DeepCopyMoveAndVectorGrowth) {
  EXPECT_EQ(sizeof(std::unique_ptr<Instruction>), sizeof(void*));
  auto module = owned_add_module();
  ASSERT_TRUE(module);
  auto& body = module->functions.front().body;
  ASSERT_EQ(body.size(), 1u);
  const auto* original =
      dynamic_cast<const AddIntegerNoSat*>(body.front().get());
  ASSERT_NE(original, nullptr);
  EXPECT_EQ(body.front()->opcode_name(), "add");
  EXPECT_EQ(dynamic_cast<const CallDirect*>(body.front().get()), nullptr);

  auto copy = body.front()->clone();
  auto* copied = dynamic_cast<AddIntegerNoSat*>(copy.get());
  ASSERT_NE(copied, nullptr);
  EXPECT_NE(copied, original);
  copied->type.value = ScalarType::S32;
  EXPECT_EQ(original->type.value, ScalarType::U32);
  auto self_copy = copy->clone();
  EXPECT_NE(self_copy.get(), copy.get());

  const auto* borrowed = copy.get();
  auto moved = std::move(copy);
  EXPECT_EQ(copy, nullptr);
  EXPECT_EQ(moved.get(), borrowed);
  std::vector<std::unique_ptr<Instruction>> owners;
  owners.push_back(std::move(moved));
  EXPECT_EQ(moved, nullptr);
  for (int index = 0; index < 64; ++index)
    owners.push_back(body.front()->clone());
  EXPECT_EQ(owners.front().get(), borrowed);

  auto assigned = body.front()->clone();
  ASSERT_NE(dynamic_cast<AddIntegerNoSat*>(assigned.get()), nullptr);
  EXPECT_NE(assigned.get(), original);
  assigned = std::move(owners.front());
  EXPECT_EQ(assigned.get(), borrowed);
  EXPECT_EQ(owners.front(), nullptr);
  EXPECT_TRUE(validateModule(*module));
}

/** A null body slot is validated without an invalid virtual call. */
TEST(OwnedInstruction, EmptyStateAndOwnedValidation) {
  std::unique_ptr<Instruction> empty;
  EXPECT_EQ(empty, nullptr);
  auto module = owned_add_module();
  ASSERT_TRUE(module);
  detail::IReferenceObserver no_op;
  module->functions.front().body.front()->visit_references(no_op);
  module->functions.front().body.front() = std::move(empty);
  const auto checked = validateModule(*module);
  ASSERT_FALSE(checked);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

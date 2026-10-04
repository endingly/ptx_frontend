

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a source module and release its AST before returning owned payloads. */
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

/** Exercise independent copies and address-stable moves of typed payloads. */
TEST(OwnedInstruction, DeepCopyMoveAndVectorGrowth) {
  EXPECT_EQ(sizeof(OwnedInstruction), 2 * sizeof(void*));
  auto module = owned_add_module();
  ASSERT_TRUE(module);
  ASSERT_EQ(module->functions.front().body.size(), 1u);
  const OwnedInstruction& original = module->functions.front().body.front();
  const Add* original_add = original.get_if<Add>();
  ASSERT_NE(original_add, nullptr);
  EXPECT_EQ(original.opcode_name(), "add");
  EXPECT_EQ(original.get_if<Call>(), nullptr);

  OwnedInstruction copy(original);
  ASSERT_NE(copy.get_if<Add>(), nullptr);
  EXPECT_NE(copy.get_if<Add>(), original_add);
  auto& copied_variant =
      std::get<Add::IntegerNoSat>(copy.get_if<Add>()->variant);
  copied_variant.type.value = ScalarType::S32;
  EXPECT_EQ(std::get<Add::IntegerNoSat>(original_add->variant).type.value,
            ScalarType::U32);
  copy = copy;
  EXPECT_NE(copy.get_if<Add>(), original_add);

  const Add* borrowed = copy.get_if<Add>();
  OwnedInstruction moved(std::move(copy));
  EXPECT_FALSE(copy);
  EXPECT_EQ(copy.opcode_name(), "");
  EXPECT_EQ(copy.get_if<Add>(), nullptr);
  EXPECT_EQ(moved.get_if<Add>(), borrowed);

  std::vector<OwnedInstruction> owners;
  owners.push_back(std::move(moved));
  EXPECT_FALSE(moved);
  for (int index = 0; index < 64; ++index)
    owners.push_back(original);
  EXPECT_EQ(owners.front().get_if<Add>(), borrowed);

  OwnedInstruction assigned;
  assigned = original;
  ASSERT_NE(assigned.get_if<Add>(), nullptr);
  EXPECT_NE(assigned.get_if<Add>(), original_add);
  const Add* assigned_borrow = assigned.get_if<Add>();
  assigned = std::move(assigned);
  EXPECT_EQ(assigned.get_if<Add>(), assigned_borrow);
  assigned = std::move(owners.front());
  EXPECT_EQ(assigned.get_if<Add>(), borrowed);
  EXPECT_FALSE(owners.front());
  EXPECT_TRUE(validateModule(*module));
}

/** Reject empty owners safely during standalone queries and owned validation. */
TEST(OwnedInstruction, EmptyStateAndOwnedValidation) {
  OwnedInstruction empty;
  EXPECT_FALSE(empty);
  EXPECT_EQ(empty.opcode_name(), "");
  EXPECT_EQ(empty.get_if<Add>(), nullptr);
  const checker::Context context{};
  const auto empty_check = empty.check(context);
  ASSERT_FALSE(empty_check);
  EXPECT_EQ(empty_check.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
  empty.visit_references(
      detail::OwnedReferenceSink{.state = nullptr, .accept = nullptr});
  auto module = owned_add_module();
  ASSERT_TRUE(module);
  module->functions.front().body.front().visit_references(
      detail::OwnedReferenceSink{.state = nullptr, .accept = nullptr});
  module->functions.front().body.front() = std::move(empty);
  const auto checked = validateModule(*module);
  ASSERT_FALSE(checked);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

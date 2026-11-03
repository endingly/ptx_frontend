#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Source type and operation select three real scalar immediate consumers. */
enum class SignConsumer { Signed, Unsigned, Bit, WideBit, SignedToWide };
/** Return a strict owned module after destroying parser, text and syntax AST. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_sign_source(
    SignConsumer consumer = SignConsumer::Signed,
    std::string_view literal = "1") {
  const bool wide = consumer == SignConsumer::WideBit ||
                    consumer == SignConsumer::SignedToWide;
  const std::string operation =
      consumer == SignConsumer::Bit || consumer == SignConsumer::WideBit
          ? std::string(wide ? "mov.b64" : "mov.b32")
          : std::string(wide                               ? "cvt.u64.s32"
                        : consumer == SignConsumer::Signed ? "cvt.u32.s32"
                                                           : "cvt.u32.u32");
  const std::string source =
      ".version 9.3\n.target sm_75\n.entry kernel(){.reg ." +
      std::string(wide ? "b64" : "b32") + " d; " + operation + " d," +
      std::string(literal) + ";}";
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** A scalar immediate and its ranges borrow from their live owning instruction. */
struct SignSlot {
  /** Mutable current typed word; no source text is retained by this borrow. */
  ResolvedImmediate& immediate;
  /** Original operand ranges with the same instruction lifetime. */
  std::span<const SourceRange> locations;
};
/** Access the two existing concrete source wrappers without a new operand abstraction. */
SignSlot sign_slot(Instruction& instruction) {
  if (auto* cvt = dynamic_cast<CvtUnroundedBase*>(&instruction))
    return {std::get<ResolvedImmediate>(cvt->src.value), cvt->src.locs};
  auto& mov = dynamic_cast<MovScalar&>(instruction);
  return {std::get<ResolvedImmediate>(mov.src_mov_source->value),
          mov.src_mov_source->locs};
}
/** Complete fixture context for direct checks independent of AST storage. */
const checker::Context sign_context{
    .target = {.ptx_version = {9, 3}, .sm_version = 75}};
/** Require one origin-sign diagnostic at the actual operand, never an association failure. */
void expect_sign_failure(
    const checker::CheckResult& result, SourceRange range,
    std::string_view message = "negative flag inconsistent") {
  ASSERT_FALSE(result);
  ASSERT_EQ(result.error().size(), 1u);
  const auto& diagnostic = result.error().front();
  EXPECT_EQ(diagnostic.kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
  EXPECT_EQ(diagnostic.range, range);
  EXPECT_NE(diagnostic.message.find(message), std::string::npos);
}
/** Recheck seven independently owned routes and retain one precise diagnostic at each. */
void expect_invalid_sign_ownership(const ResolvedModule& module,
                                   SourceRange range) {
  expect_sign_failure(
      module.functions.front().body.front()->check(sign_context), range);
  expect_sign_failure(validateModule(module), range);
  ResolvedModule copied(module), assigned;
  assigned = copied;
  expect_sign_failure(validateModule(copied), range);
  expect_sign_failure(validateModule(assigned), range);
  ResolvedFunction function(copied.functions.front()), function_assigned;
  function_assigned = function;
  EXPECT_NE(function.body.front().get(),
            module.functions.front().body.front().get());
  expect_sign_failure(function.body.front()->check(sign_context), range);
  expect_sign_failure(function_assigned.body.front()->check(sign_context),
                      range);
  assigned.functions.front().body.front() = function.body.front()->clone();
  expect_sign_failure(validateModule(assigned), range);
}
/** Preserve valid source words through module/function copies, assignments and clones. */
void expect_valid_sign_ownership(const ResolvedModule& module) {
  EXPECT_TRUE(module.functions.front().body.front()->check(sign_context));
  EXPECT_TRUE(validateModule(module));
  ResolvedModule copied(module), assigned;
  assigned = copied;
  ResolvedFunction function(module.functions.front()), function_assigned;
  function_assigned = function;
  EXPECT_TRUE(function_assigned.body.front()->check(sign_context));
  assigned.functions.front().body.front() = function.body.front()->clone();
  EXPECT_TRUE(validateModule(copied));
  EXPECT_TRUE(validateModule(assigned));
}
TEST(OwnedIntegerSourceSign, RejectsNegativeFlagForKnownPositiveSource) {
  auto owned = owned_sign_source();
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto slot = sign_slot(*owned->functions.front().body.front());
  auto& value = slot.immediate;
  ASSERT_EQ(value.type, ScalarType::S32);
  ASSERT_EQ(value.bits, 1u);
  ASSERT_EQ(value.integer_source_bits, 1u);
  ASSERT_FALSE(value.is_negative);
  ASSERT_FALSE(slot.locations.empty());
  const auto range = slot.locations.front();
  EXPECT_GT(range.start.line, 0);
  EXPECT_GT(range.start.column, 0);
  expect_valid_sign_ownership(*owned);
  value.is_negative = true;
  expect_invalid_sign_ownership(*owned, range);
  EXPECT_EQ(value.bits, 1u);
  EXPECT_EQ(value.integer_source_bits, 1u);
  EXPECT_EQ(slot.locations.front(), range);
  value.is_negative = false;
  expect_valid_sign_ownership(*owned);
}
TEST(OwnedIntegerSourceSign,
     ChecksOriginalSignRatherThanUseWidthOrDestinationType) {
  constexpr std::array<uint64_t, 5> sources{0, 1, 0x7fffffffffffffffULL,
                                            0x8000000000000000ULL,
                                            0xffffffffffffffffULL};
  for (const auto consumer :
       {SignConsumer::Signed, SignConsumer::Unsigned, SignConsumer::Bit}) {
    auto saved = owned_sign_source(consumer);
    ASSERT_TRUE(saved);
    for (const auto source : sources)
      for (const bool negative : {false, true}) {
        ResolvedModule module(*saved);
        auto slot = sign_slot(*module.functions.front().body.front());
        slot.immediate.bits = source & 0xffffffffULL;
        slot.immediate.integer_source_bits = source;
        slot.immediate.is_negative = negative;
        if (negative && (source >> 63) == 0)
          expect_invalid_sign_ownership(module, slot.locations.front());
        else
          expect_valid_sign_ownership(module);
      }
  }
}
TEST(OwnedIntegerSourceSign,
     RetainsSignedNegativeUnsignedAndNarrowedPositiveSources) {
  for (const auto consumer :
       {SignConsumer::Signed, SignConsumer::Unsigned, SignConsumer::Bit,
        SignConsumer::WideBit, SignConsumer::SignedToWide}) {
    for (const auto literal : {"-1", "0xffffffffffffffffU", "-4294967295"}) {
      SCOPED_TRACE(literal);
      auto module = owned_sign_source(consumer, literal);
      ASSERT_TRUE(module) << module.error().front().message;
      const auto slot = sign_slot(*module->functions.front().body.front());
      const bool negative = std::string_view(literal).starts_with('-');
      const uint64_t original = std::string_view(literal) == "-4294967295"
                                    ? 0xffffffff00000001ULL
                                    : UINT64_MAX;
      EXPECT_EQ(slot.immediate.integer_source_bits, original);
      EXPECT_EQ(slot.immediate.is_negative, negative);
      EXPECT_EQ(slot.immediate.bits, consumer == SignConsumer::WideBit
                                         ? original
                                         : original & 0xffffffffULL);
      expect_valid_sign_ownership(*module);
    }
  }
}
TEST(OwnedIntegerSourceSign,
     PreservesCacheAbsenceAndBitMismatchDiagnosticPrecedence) {
  for (const auto consumer :
       {SignConsumer::Signed, SignConsumer::Unsigned, SignConsumer::Bit}) {
    auto module = owned_sign_source(consumer);
    ASSERT_TRUE(module);
    auto slot = sign_slot(*module->functions.front().body.front());
    slot.immediate.integer_source_bits.reset();
    slot.immediate.is_negative = true;
    expect_valid_sign_ownership(*module);
    slot.immediate.integer_source_bits = 1;
    slot.immediate.bits = 2;
    expect_sign_failure(
        module->functions.front().body.front()->check(sign_context),
        slot.locations.front(), "bits inconsistent");
    expect_sign_failure(validateModule(*module), slot.locations.front(),
                        "bits inconsistent");
    slot.immediate.bits = 1;
    expect_invalid_sign_ownership(*module, slot.locations.front());
    slot.immediate.is_negative = false;
    expect_valid_sign_ownership(*module);
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

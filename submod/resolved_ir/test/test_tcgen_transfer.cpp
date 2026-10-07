#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Spell an exact brace fragment using declared scalar b32 registers. */
std::string fragment(size_t count) {
  std::string result = "{";
  for (size_t lane = 0; lane < count; ++lane) {
    if (lane)
      result += ", ";
    result += "%r" + std::to_string(lane);
  }
  return result + "}";
}

/** Wrap transfer source in a complete module with declaration-bound carriers. */
std::string transfer_source(std::string_view instructions,
                            std::string_view ptx = "9.0",
                            std::string_view target = "sm_110a") {
  return ".version " + std::string(ptx) + "\n.target " + std::string(target) +
         R"ptx(
.address_size 64
.visible .entry kernel() {
  .shared .align 4 .b32 slot;
  .reg .b32 %r<129>;
  .reg .b32 %t;
  .reg .f32 %rf, %red_f;
  .reg .u32 %red_u;
  .reg .s32 %red_s;
  .reg .b16 %short;
  .reg .b64 %wide;
  .reg .pred %p;
)ptx" + std::string(instructions) +
         R"ptx(
  ret;
}
)ptx";
}

/** Parse one complete fixture and return its AST or a test failure. */
std::optional<syntax_ast::AstModule> parse_transfer(std::string_view source,
                                                    bool report = true) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    if (report) {
      ADD_FAILURE() << (ast && !ast.diagnostics.empty()
                            ? ast.diagnostics.front().message
                            : "Transfer module did not parse.");
    }
    return std::nullopt;
  }
  return std::move(*ast);
}

/** Resolve a transfer fixture and optionally report its first diagnostic. */
bool accepts_transfer(std::string_view source, bool report = false) {
  auto ast = parse_transfer(source, report);
  if (!ast)
    return false;
  auto resolved = resolveAndValidateModule(*ast);
  if (!resolved && report) {
    ADD_FAILURE() << (resolved.error().empty()
                          ? "Transfer module rejected without diagnostics."
                          : resolved.error().front().message);
  }
  return resolved.has_value();
}

/** Construct a direct checker context from the exact qualified target. */
checker::Context transfer_context(const ResolvedModule& module) {
  const auto target = base::find_target_profile("sm_110a");
  EXPECT_TRUE(target.has_value());
  return checker::Context{
      .target = {.ptx_version = {9, 0},
                 .sm_version = target->identity.architecture.number,
                 .enabled_family_features = target->enabled_family_features,
                 .identity = target->identity,
                 .capabilities = target->capabilities},
      .instruction_range = module.functions.front().instruction_ranges[0],
  };
}

/** Every Table 52/53 shape/repeat row has its exact fragment cardinality. */
TEST(TcgenTransfer, AcceptsShapeRowsAndPacking) {
  EXPECT_TRUE(accepts_transfer(
      transfer_source(
          "tcgen05.ld.sync.aligned.32x32b.x1.pack::16b.b32 {%r0}, [%t];\n"
          "tcgen05.wait::ld.sync.aligned;\n"),
      true));
  for (const auto& [shape, multiplier, maximum] : {
           std::tuple{"32x32b", 1u, 128u},
           std::tuple{"16x64b", 1u, 128u},
           std::tuple{"16x128b", 2u, 64u},
           std::tuple{"16x256b", 4u, 32u},
           std::tuple{"16x32bx2", 1u, 128u},
       }) {
    for (unsigned repeat : {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}) {
      if (repeat > maximum)
        continue;
      const std::string num = "x" + std::to_string(repeat);
      const size_t count = repeat * multiplier;
      SCOPED_TRACE(std::string(shape) + "." + num);
      const bool split = std::string_view(shape) == "16x32bx2";
      const std::string half = split ? ", -16" : "";
      EXPECT_TRUE(accepts_transfer(transfer_source(
          "tcgen05.ld.sync.aligned." + std::string(shape) + "." + num +
          ".pack::16b.b32 " + fragment(count) + ", [%t]" + half +
          ";\n"
          "tcgen05.wait::ld.sync.aligned;\n")));
      const std::string store_operands =
          std::string("[%t], ") + (split ? "-16, " : "") + fragment(count);
      EXPECT_TRUE(accepts_transfer(
          transfer_source("tcgen05.st.sync.aligned." + std::string(shape) +
                          "." + num + ".unpack::16b.b32 " + store_operands +
                          ";\n"
                          "tcgen05.wait::st.sync.aligned;\n")));
    }
  }
}

/** Neighbor counts and unsupported high repeats do not enter the typed form. */
TEST(TcgenTransfer, RejectsCardinalityNeighborsAndInvalidShapeRepeat) {
  for (const auto& [shape, multiplier, maximum] : {
           std::tuple{"32x32b", 1u, 128u},
           std::tuple{"16x64b", 1u, 128u},
           std::tuple{"16x128b", 2u, 64u},
           std::tuple{"16x256b", 4u, 32u},
           std::tuple{"16x32bx2", 1u, 128u},
       }) {
    for (unsigned repeat : {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}) {
      if (repeat > maximum)
        continue;
      const size_t expected = repeat * multiplier;
      for (size_t count : {expected - 1, expected + 1}) {
        SCOPED_TRACE(std::string(shape) + ".x" + std::to_string(repeat) + ":" +
                     std::to_string(count));
        const std::string half =
            std::string_view(shape) == "16x32bx2" ? ", -16" : "";
        EXPECT_FALSE(accepts_transfer(
            transfer_source("tcgen05.ld.sync.aligned." + std::string(shape) +
                            ".x" + std::to_string(repeat) + ".b32 " +
                            fragment(count) + ", [%t]" + half + ";\n")));
      }
    }
  }
  for (std::string_view suffix : {"16x128b.x128", "16x256b.x64"}) {
    EXPECT_FALSE(accepts_transfer(
        transfer_source("tcgen05.ld.sync.aligned." + std::string(suffix) +
                        ".b32 " + fragment(128) + ", [%t];\n")));
  }
}

/** Both documented reduction orders lower to the same typed result. */
TEST(TcgenTransfer, ReductionOrdersAndControls) {
  for (std::string_view suffix : {
           "min.abs.NaN.f32",
           "f32.min.abs.NaN",
           "max.f32",
           "f32.max",
       }) {
    SCOPED_TRACE(suffix);
    EXPECT_TRUE(accepts_transfer(
        transfer_source("tcgen05.ld.red.sync.aligned.32x32b.x2." +
                        std::string(suffix) + " " + fragment(2) +
                        ", %red_f, [%t];\n"
                        "tcgen05.wait::ld.sync.aligned;\n")));
  }
  for (std::string_view suffix : {"min.u32", "u32.min", "max.s32", "s32.max"}) {
    const std::string red =
        suffix.find("u32") != std::string_view::npos ? "%red_u" : "%red_s";
    EXPECT_TRUE(accepts_transfer(transfer_source(
        "tcgen05.ld.red.sync.aligned.16x32bx2.x2." + std::string(suffix) + " " +
        fragment(2) + ", " + red + ", [%t], 0x100000010U;\n")));
  }
  for (std::string_view suffix :
       {"min.abs.u32", "u32.max.NaN", "min.f32.abs"}) {
    EXPECT_FALSE(accepts_transfer(transfer_source(
        "tcgen05.ld.red.sync.aligned.32x32b.x2." + std::string(suffix) + " " +
        fragment(2) + ", %red_u, [%t];\n")));
  }
}

/** Every reduction type, operation, shape, repeat and float control resolves. */
TEST(TcgenTransfer, AcceptsAllReductionTuples) {
  for (std::string_view shape : {"32x32b", "16x32bx2"}) {
    for (unsigned repeat : {2u, 4u, 8u, 16u, 32u, 64u, 128u}) {
      for (std::string_view op : {"min", "max"}) {
        for (std::string_view type : {"f32", "u32", "s32"}) {
          const unsigned controls = type == "f32" ? 4 : 1;
          for (unsigned mask = 0; mask < controls; ++mask) {
            const std::string qualifiers =
                std::string(op) + (mask & 1 ? ".abs" : "") +
                (mask & 2 ? ".NaN" : "") + "." + std::string(type);
            const std::string red = type == "f32"   ? "%red_f"
                                    : type == "u32" ? "%red_u"
                                                    : "%red_s";
            const std::string half = shape == "16x32bx2" ? ", -16" : "";
            SCOPED_TRACE(std::string(shape) + ".x" + std::to_string(repeat) +
                         "." + qualifiers);
            EXPECT_TRUE(accepts_transfer(transfer_source(
                "tcgen05.ld.red.sync.aligned." + std::string(shape) + ".x" +
                std::to_string(repeat) + "." + qualifiers + " " +
                fragment(repeat) + ", " + red + ", [%t]" + half + ";\n")));
          }
        }
      }
    }
  }
}

/** Qualified introductions stay separate from generic and unrelated targets. */
TEST(TcgenTransfer, ExactTargetGates) {
  constexpr std::string_view load =
      "tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r0}, [%t];\n";
  for (const auto& [ptx, target, accepted] : {
           std::tuple{"8.5", "sm_100a", false},
           std::tuple{"8.6", "sm_100a", true},
           std::tuple{"8.7", "sm_100f", false},
           std::tuple{"8.8", "sm_100f", true},
           std::tuple{"9.0", "sm_110a", true},
           std::tuple{"9.0", "sm_110f", true},
           std::tuple{"9.3", "sm_100", false},
           std::tuple{"9.3", "sm_120a", false},
       }) {
    SCOPED_TRACE(std::string(ptx) + "/" + target);
    EXPECT_EQ(accepts_transfer(transfer_source(load, ptx, target)), accepted);
  }
  const std::string reduction =
      "tcgen05.ld.red.sync.aligned.32x32b.x2.min.u32 " + fragment(2) +
      ", %red_u, [%t];\n";
  EXPECT_FALSE(accepts_transfer(transfer_source(reduction, "8.8", "sm_100f")));
  EXPECT_FALSE(accepts_transfer(transfer_source(reduction, "8.7", "sm_103f")));
  EXPECT_TRUE(accepts_transfer(transfer_source(reduction, "8.8", "sm_103f")));
  EXPECT_TRUE(accepts_transfer(transfer_source(reduction, "9.0", "sm_110a")));
}

/** Simple bracket addresses, split placement and waits have closed syntax. */
TEST(TcgenTransfer, RejectsInvalidOperandPlacementAndWaitControls) {
  for (std::string_view instruction : {
           "tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r0}, %t;",
           "tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r0}, [%t+4];",
           "tcgen05.ld.sync.aligned.16x32bx2.x1.b32 {%r0}, [%t], %r0;",
           "tcgen05.ld.sync.aligned.16x32bx2.x1.b32 {%r0}, [%t], 1.5;",
           "tcgen05.st.sync.aligned.16x32bx2.x1.b32 [%t], {%r0}, 16;",
           "tcgen05.wait::ld.cta_group::1.sync.aligned;",
           "tcgen05.wait::st.sync.aligned %r0;",
       }) {
    SCOPED_TRACE(instruction);
    EXPECT_FALSE(accepts_transfer(transfer_source(instruction)));
  }
  EXPECT_TRUE(
      accepts_transfer(transfer_source("tcgen05.wait::ld.sync.aligned;\n"
                                       "tcgen05.wait::st.sync.aligned;\n")));
}

/** Ungrouped transfers do not impose an invented allocation CTA group. */
TEST(TcgenTransfer, PreservesPerBodyAllocationGroupScope) {
  EXPECT_TRUE(accepts_transfer(transfer_source(
      "tcgen05.alloc.cta_group::1.sync.aligned.b32 [slot], 32;\n"
      "ld.shared.b32 %t, [slot];\n"
      "tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r0}, [%t];\n"
      "tcgen05.wait::ld.sync.aligned;\n"
      "tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t, 32;\n")));
  EXPECT_FALSE(accepts_transfer(transfer_source(
      "tcgen05.alloc.cta_group::1.sync.aligned.b32 [slot], 32;\n"
      "ld.shared.b32 %t, [slot];\n"
      "tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r0}, [%t];\n"
      "tcgen05.dealloc.cta_group::2.sync.aligned.b32 %t, 32;\n")));
}

/** A released AST cannot hide mutated fragment, address, or split metadata. */
TEST(TcgenTransfer, RechecksOwnedFieldsWithoutSourceAst) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_transfer(
        transfer_source("tcgen05.ld.sync.aligned.16x32bx2.x2.b32 " +
                        fragment(2) + ", [%t], -16;\n"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned = std::move(*result);
  }
  ASSERT_TRUE(owned);
  auto& operation = *owned->functions.front().body[0].get();
  auto& selected = dynamic_cast<Tcgen05LdSplit&>(operation);
  const checker::Context context = transfer_context(*owned);
  const auto rejected = [&]() {
    EXPECT_FALSE(operation.check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
  };
  EXPECT_TRUE(operation.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  selected.r.value.elements[0]->register_class =
      ResolvedRegisterClass::Predicate;
  rejected();
  selected.r.value.elements[0]->register_class = ResolvedRegisterClass::General;
  selected.r.value.elements[0]->vector_width = 2;
  rejected();
  selected.r.value.elements[0]->vector_width.reset();
  selected.r.value.elements[0]->declared_type = ScalarType::B16;
  rejected();
  selected.r.value.elements[0]->declared_type = ScalarType::B32;
  selected.r.value.elements[0]->declared_type.reset();
  rejected();
  selected.r.value.elements[0]->declared_type = ScalarType::B32;
  const auto saved_lane = selected.r.value.elements[0];
  selected.r.value.elements[0].reset();
  rejected();
  selected.r.value.elements[0] = saved_lane;
  selected.taddr.value.bracketed = false;
  rejected();
  selected.taddr.value.bracketed = true;
  auto& address = std::get<ResolvedRegisterRef>(selected.taddr.value.value);
  address.vector_width = 2;
  rejected();
  address.vector_width.reset();
  address.declared_type = ScalarType::F32;
  rejected();
  address.declared_type = ScalarType::B32;
  selected.splitoff.value.source_kind =
      static_cast<TcgenIntegerSourceKind>(255);
  rejected();
  selected.splitoff.value.source_kind = TcgenIntegerSourceKind::Signed;
  selected.splitoff.value.source_bits = 0x100000010ULL;
  EXPECT_TRUE(operation.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** Reduction result and immediate address remain checked after AST release. */
TEST(TcgenTransfer, RechecksReductionResultAndConvertedAddress) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_transfer(
        transfer_source("tcgen05.ld.red.sync.aligned.32x32b.x2.min.f32 " +
                        fragment(2) + ", %red_f, [0];\n"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned = std::move(*result);
  }
  ASSERT_TRUE(owned);
  auto& operation = *owned->functions.front().body[0].get();
  auto& selected = dynamic_cast<Tcgen05LdRedFloat&>(operation);
  const checker::Context context = transfer_context(*owned);
  const auto rejected = [&]() {
    EXPECT_FALSE(operation.check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
  };
  selected.redval.value.vector_width = 2;
  rejected();
  selected.redval.value.vector_width.reset();
  selected.redval.value.register_class = ResolvedRegisterClass::Predicate;
  rejected();
  selected.redval.value.register_class = ResolvedRegisterClass::General;
  auto& address = std::get<ResolvedImmediate>(selected.taddr.value.value);
  address.bits = 1;
  rejected();
  address.bits = 0;
  address.integer_source_bits = 1;
  rejected();
  address.integer_source_bits = 0;
  EXPECT_TRUE(operation.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

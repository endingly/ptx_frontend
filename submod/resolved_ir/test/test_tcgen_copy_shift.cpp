#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Build one complete module with declaration-bound copy sources. */
std::string copy_source(std::string_view body, std::string_view ptx = "9.0",
                        std::string_view target = "sm_110a") {
  return ".version " + std::string(ptx) + "\n.target " + std::string(target) +
         R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %t;
  .reg .b64 %desc;
  .reg .u64 %udesc;
  .reg .s64 %sdesc;
  .reg .f64 %fdesc;
  .reg .b32 %short;
  .reg .pred %pred;
  .shared .align 4 .b32 slot;
  .shared .align 8 .b64 barrier;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse one complete source module without retaining it in owned tests. */
std::optional<syntax_ast::AstModule> parse_copy(std::string_view source,
                                                bool report = true) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    if (report)
      ADD_FAILURE() << (ast && !ast.diagnostics.empty()
                            ? ast.diagnostics.front().message
                            : "TCGEN copy/shift module did not parse.");
    return std::nullopt;
  }
  return std::move(*ast);
}

/** Resolve and validate a complete source module under its written target. */
bool accepts_copy(std::string_view source) {
  auto ast = parse_copy(source, false);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Construct a direct checker context from the exact qualified target. */
checker::Context copy_context(const ResolvedModule& module) {
  const auto profile = base::find_target_profile("sm_110a");
  EXPECT_TRUE(profile.has_value());
  return checker::Context{
      .target = {.ptx_version = {9, 0},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
      .instruction_range = module.functions.front().instruction_ranges[0],
  };
}

/** All six pairs and three formats remain available for both CTA groups. */
TEST(TcgenCopyShift, AcceptsClosedCopyRowsAndShiftAliases) {
  for (const auto& [shape, multicast] : {
           std::tuple{"128x256b", ""},
           std::tuple{"4x256b", ""},
           std::tuple{"128x128b", ""},
           std::tuple{"64x128b", ".warpx2::02_13"},
           std::tuple{"64x128b", ".warpx2::01_23"},
           std::tuple{"32x128b", ".warpx4"},
       }) {
    for (unsigned group : {1u, 2u}) {
      for (std::string_view format :
           {"", ".b8x16.b6x16_p32", ".b8x16.b4x16_p64"}) {
        const std::string instruction =
            "tcgen05.cp.cta_group::" + std::to_string(group) + "." + shape +
            multicast + std::string(format) + " [%t], %desc;";
        SCOPED_TRACE(instruction);
        EXPECT_TRUE(accepts_copy(copy_source(instruction)));
      }
    }
  }
  for (unsigned group : {1u, 2u}) {
    const auto digit = std::to_string(group);
    EXPECT_TRUE(accepts_copy(
        copy_source("tcgen05.shift.cta_group::" + digit + ".down [%t];")));
    EXPECT_TRUE(accepts_copy(
        copy_source("tcgen05.shift.down.cta_group::" + digit + " [%t];")));
  }
}

/** Closed source pairs reject all partial and crossed modifiers. */
TEST(TcgenCopyShift, RejectsWrongSourcePairsAndCarriers) {
  for (std::string_view instruction : {
           "tcgen05.cp.cta_group::1.128x256b.warpx4 [%t], %desc;",
           "tcgen05.cp.cta_group::1.64x128b [%t], %desc;",
           "tcgen05.cp.cta_group::1.32x128b.warpx2::02_13 [%t], %desc;",
           "tcgen05.cp.cta_group::1.128x128b.b8x16 [%t], %desc;",
           "tcgen05.cp.cta_group::1.128x128b.b6x16_p32 [%t], %desc;",
           "tcgen05.cp.cta_group::1.128x128b.b8x16.b6x16_p32.b4x16_p64 [%t], "
           "%desc;",
           "tcgen05.cp.cta_group::1.128x128b [%t], %fdesc;",
           "tcgen05.cp.cta_group::1.128x128b [%t], %short;",
           "tcgen05.cp.cta_group::1.128x128b [%t], %pred;",
           "tcgen05.cp.cta_group::1.128x128b %t, %desc;",
           "tcgen05.cp.cta_group::1.128x128b [%t+4], %desc;",
           "tcgen05.shift.cta_group::1.down.31x256b [%t];",
           "tcgen05.shift.cta_group::1.down [%t], %desc;",
       }) {
    SCOPED_TRACE(instruction);
    EXPECT_FALSE(accepts_copy(copy_source(instruction)));
  }
  for (std::string_view descriptor : {"%desc", "%udesc", "%sdesc"})
    EXPECT_TRUE(
        accepts_copy(copy_source("tcgen05.cp.cta_group::1.128x256b [%t], " +
                                 std::string(descriptor) + ";")));
}

/** Copy and shift use different qualified target lists and exact floors. */
TEST(TcgenCopyShift, ChecksQualifiedAvailability) {
  const std::string copy = "tcgen05.cp.cta_group::1.128x256b [%t], %desc;";
  const std::string shift = "tcgen05.shift.cta_group::1.down [%t];";
  for (const auto& [ptx, target, copy_ok, shift_ok] : {
           std::tuple{"8.6", "sm_100a", true, true},
           std::tuple{"8.7", "sm_100f", false, false},
           std::tuple{"8.8", "sm_100f", true, false},
           std::tuple{"8.8", "sm_103a", true, true},
           std::tuple{"8.8", "sm_103f", true, false},
           std::tuple{"9.0", "sm_110a", true, true},
           std::tuple{"9.0", "sm_110f", true, false},
           std::tuple{"9.3", "sm_100", false, false},
           std::tuple{"9.3", "sm_120a", false, false},
       }) {
    SCOPED_TRACE(std::string(ptx) + "/" + target);
    EXPECT_EQ(accepts_copy(copy_source(copy, ptx, target)), copy_ok);
    EXPECT_EQ(accepts_copy(copy_source(shift, ptx, target)), shift_ok);
  }
}

/** Owned descriptor metadata and canonical pair checks survive AST release. */
TEST(TcgenCopyShift, RechecksOwnedCopyMutations) {
  std::optional<ResolvedModule> owned;
  {
    auto ast =
        parse_copy(copy_source("tcgen05.cp.cta_group::1.64x128b.warpx2::02_13"
                               ".b8x16.b6x16_p32 [%t], %desc;"));
    ASSERT_TRUE(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_TRUE(owned);
  auto& instruction = *owned->functions.front().body[0].get();
  auto& form = dynamic_cast<Tcgen05Cp&>(instruction);
  const auto context = copy_context(*owned);
  const auto reject = [&]() {
    EXPECT_FALSE(instruction.check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
  };
  ASSERT_TRUE(form.descriptor_view());
  EXPECT_EQ(form.descriptor_view()->source, &form.s_desc.value);
  EXPECT_TRUE(form.warpx2_02_13.value);
  EXPECT_FALSE(form.warpx2_01_23.value);
  EXPECT_FALSE(form.warpx4.value);
  EXPECT_TRUE(form.dst_format.value);
  EXPECT_TRUE(form.src_b6.value);
  EXPECT_FALSE(form.src_b4.value);
  ASSERT_EQ(form.dst_format.locs.size(), 1);
  ASSERT_EQ(form.src_b6.locs.size(), 1);
  EXPECT_EQ(form.dst_format.locs[0].end, form.src_b6.locs[0].start);
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  form.s_desc.value.vector_width = 2;
  reject();
  form.s_desc.value.vector_width.reset();
  form.s_desc.value.register_class = ResolvedRegisterClass::Predicate;
  reject();
  form.s_desc.value.register_class = ResolvedRegisterClass::General;
  const auto type = form.s_desc.value.declared_type;
  for (const auto wrong : {ScalarType::F64, ScalarType::B32}) {
    form.s_desc.value.declared_type = wrong;
    reject();
  }
  form.s_desc.value.declared_type.reset();
  reject();
  form.s_desc.value.declared_type = type;
  const auto symbol = form.s_desc.value.symbol_id;
  form.s_desc.value.symbol_id.reset();
  form.s_desc.value.declared_type.reset();
  EXPECT_TRUE(instruction.check(context).has_value());
  form.s_desc.value.symbol_id = symbol;
  form.s_desc.value.declared_type = type;
  form.s_desc.value.symbol_id = binding::SymbolId{.value = 999998u};
  EXPECT_FALSE(validateModule(*owned).has_value());
  form.s_desc.value.symbol_id = symbol;
  form.warpx2_02_13.value = false;
  reject();
  form.warpx2_02_13.value = true;
  form.warpx4.value = true;
  reject();
  form.warpx4.value = false;
  form.src_b6.value = false;
  reject();
  form.src_b6.value = true;
  form.shape.value = TcgenDataMovementShape::S128x128b;
  reject();
  form.shape.value = TcgenDataMovementShape::S64x128b;
  form.cta_group.value = TcgenCtaGroup::Two;
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  form.cta_group.value = TcgenCtaGroup::One;
  form.taddr.value.bracketed = false;
  reject();
  form.taddr.value.bracketed = true;
  auto& address = std::get<ResolvedRegisterRef>(form.taddr.value.value);
  address.vector_width = 2;
  reject();
  address.vector_width.reset();
  ++form.operand_layout.value;
  reject();
  --form.operand_layout.value;
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** Both decompression source spellings retain independent source subranges. */
TEST(TcgenCopyShift, PreservesBothWrittenFormatLocations) {
  for (const auto& [suffix, format] : {
           std::tuple{".b6x16_p32", TcgenCopyFormat::B6x16P32},
           std::tuple{".b4x16_p64", TcgenCopyFormat::B4x16P64},
       }) {
    std::optional<ResolvedModule> owned;
    {
      auto ast =
          parse_copy(copy_source("tcgen05.cp.cta_group::1.128x256b.b8x16" +
                                 std::string(suffix) + " [%t], %desc;"));
      ASSERT_TRUE(ast);
      auto resolved = resolveAndValidateModule(*ast);
      ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
      owned = std::move(*resolved);
    }
    ASSERT_TRUE(owned);
    const auto& instruction = *owned->functions.front().body[0].get();
    const auto& form = dynamic_cast<const Tcgen05Cp&>(instruction);
    const auto& source_format =
        format == TcgenCopyFormat::B6x16P32 ? form.src_b6 : form.src_b4;
    ASSERT_EQ(form.dst_format.locs.size(), 1);
    ASSERT_EQ(source_format.locs.size(), 1);
    EXPECT_EQ(form.dst_format.locs[0].end, source_format.locs[0].start);
    EXPECT_TRUE(form.dst_format.value);
    EXPECT_EQ(form.src_b6.value, format == TcgenCopyFormat::B6x16P32);
    EXPECT_EQ(form.src_b4.value, format == TcgenCopyFormat::B4x16P64);
    EXPECT_TRUE(validateModule(*owned).has_value());
  }
}

/** Shift checks converted lane bits, source consistency, and group identity. */
TEST(TcgenCopyShift, RechecksOwnedShiftMutationsAndGroups) {
  for (uint32_t lane : {0u, 16u, 32u}) {
    const auto address = std::to_string(lane << 16);
    EXPECT_EQ(accepts_copy(copy_source("tcgen05.shift.cta_group::1.down [" +
                                       address + "];")),
              lane != 16);
  }
  std::optional<ResolvedModule> owned;
  {
    auto ast =
        parse_copy(copy_source("tcgen05.shift.cta_group::1.down [2097152];"));
    ASSERT_TRUE(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_TRUE(owned);
  auto& instruction = *owned->functions.front().body[0].get();
  auto& form = dynamic_cast<Tcgen05Shift&>(instruction);
  const auto context = copy_context(*owned);
  auto& immediate = std::get<ResolvedImmediate>(form.taddr.value.value);
  immediate.bits = 1048576;
  EXPECT_FALSE(instruction.check(context).has_value());
  EXPECT_FALSE(validateModule(*owned).has_value());
  immediate.bits = 2097152;
  immediate.integer_source_bits = 1;
  EXPECT_FALSE(instruction.check(context).has_value());
  immediate.integer_source_bits = 2097152;
  form.down.value = false;
  EXPECT_FALSE(instruction.check(context).has_value());
  form.down.value = true;
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  EXPECT_FALSE(
      accepts_copy(copy_source("tcgen05.cp.cta_group::1.128x256b [%t], %desc;\n"
                               "tcgen05.shift.cta_group::2.down [%t];")));
  EXPECT_FALSE(accepts_copy(
      copy_source("tcgen05.cp.cta_group::1.128x256b [%t], %desc;\n"
                  "tcgen05.alloc.cta_group::2.sync.aligned.b32 [slot], 32;")));
  EXPECT_FALSE(accepts_copy(copy_source(
      "tcgen05.shift.cta_group::1.down [%t];\n"
      "tcgen05.commit.cta_group::2.mbarrier::arrive::one.b64 [barrier];")));
  EXPECT_TRUE(accepts_copy(copy_source(
      "tcgen05.cp.cta_group::1.128x256b [%t], %desc;\n"
      "tcgen05.shift.cta_group::1.down [%t];\n"
      "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];\n"
      "tcgen05.fence::after_thread_sync;")));
  std::string separate =
      copy_source("tcgen05.cp.cta_group::1.128x256b [%t], %desc;");
  separate += R"ptx(
.visible .entry other() {
  .reg .b32 %other_taddr;
  tcgen05.shift.cta_group::2.down [%other_taddr];
  ret;
}
)ptx";
  EXPECT_TRUE(accepts_copy(separate));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap one tensor read in known descriptor, destination, and barrier storage. */
std::string im2col_module(
    std::string_view instruction,
    std::string_view map = ".global .align 64 .b8 tensor_map[128];",
    std::string_view destination = ".shared .align 16 .b8 dst[1024];",
    std::string_view version = "9.3", std::string_view target = "sm_100a") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + "\n.address_size 64\n" + std::string(map) +
         "\n" + std::string(destination) +
         "\n.shared .align 8 .b64 mbar;\n.entry kernel() {\n"
         ".reg .s32 %r<5>;\n.reg .u16 %u<3>;\n.reg .b16 %b<2>;\n"
         ".reg .s16 %s<2>;\n.reg .b64 %ptr;\n" +
         std::string(instruction) + "\n}\n";
}

/** Build a direct-check context from the repository's exact target catalog. */
checker::Context im2col_context(checker::PtxVersion version = {9, 3},
                                std::string_view target = "sm_100a") {
  const auto profile = base::find_target_profile(target);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = version,
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Resolve into owned IR after every syntax object leaves scope. */
std::optional<ResolvedModule> owned_im2col(
    std::string_view instruction, std::string_view version = "9.3",
    std::string_view target = "sm_100a") {
  const auto parsed = test_helpers::parseModule(
      im2col_module(instruction, ".global .align 64 .b8 tensor_map[128];",
                    ".shared .align 16 .b8 dst[1024];", version, target));
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Borrow the selected im2col information field from either layout encoding. */
template <typename Form>
  requires(
      requires(Form& form) { form.im2col_info; } ||
      requires(Form& form) { form.operands; })
WithLocs<ResolvedTensorIm2colInfo>* mutable_im2col_info(Form& selected) {
  if constexpr (requires { selected.im2col_info; }) {
    if constexpr (requires { selected.im2col_info.has_value(); })
      return selected.im2col_info ? &*selected.im2col_info : nullptr;
    else
      return &selected.im2col_info;
  } else {
    WithLocs<ResolvedTensorIm2colInfo>* info = nullptr;
    std::visit(
        [&](auto& payload) {
          if constexpr (requires { payload.im2col_info; })
            info = &payload.im2col_info;
        },
        selected.operands);
    return info;
  }
}

/** Borrow the tensor operand regardless of optional-info layout storage. */
template <typename Form>
  requires(
      requires(Form& form) { form.tensor; } ||
      requires(Form& form) { form.operands; })
ResolvedTensorOperand* mutable_im2col_tensor(Form& selected) {
  if constexpr (requires { selected.tensor; }) {
    return &selected.tensor.value;
  } else {
    ResolvedTensorOperand* tensor = nullptr;
    std::visit(
        [&](auto& payload) {
          if constexpr (requires { payload.tensor; })
            tensor = &payload.tensor.value;
        },
        selected.operands);
    return tensor;
  }
}

/** All 27 semantic identities accept both info-presence layouts. */
TEST(TensorIm2colInfo, AllTwentySevenModesAndFiftyFourLayouts) {
  constexpr std::array<std::string_view, 3> modes{"im2col", "im2col::w",
                                                  "im2col::w::128"};
  std::set<InstructionKind> alternatives;
  for (const std::string_view topology : {"cluster", "cta", "prefetch"}) {
    for (int rank = 3; rank <= 5; ++rank) {
      std::string coords = "%r0";
      for (int index = 1; index < rank; ++index)
        coords += ", %r" + std::to_string(index);
      for (const auto mode : modes) {
        for (const bool present : {false, true}) {
          const std::string prefix =
              topology == "prefetch"
                  ? "cp.async.bulk.prefetch.tensor." + std::to_string(rank) +
                        "d.L2.global." + std::string(mode) + " [tensor_map, {" +
                        coords + "}]"
                  : "cp.async.bulk.tensor." + std::to_string(rank) +
                        "d.shared::" + std::string(topology) + ".global." +
                        std::string(mode) +
                        ".mbarrier::complete_tx::bytes [dst], [tensor_map, {" +
                        coords + "}], [mbar]";
          const int count = mode == "im2col" ? rank - 2 : 2;
          std::string info = "%u0";
          for (int index = 1; index < count; ++index)
            info += ", %u" + std::to_string(index);
          const std::string instruction =
              prefix + (present ? ", {" + info + "}" : "") + ";";
          auto owned = owned_im2col(instruction);
          ASSERT_TRUE(owned.has_value()) << instruction;
          const auto& copy = owned->functions.front().body.front();
          alternatives.insert(copy->instruction_kind());
          EXPECT_TRUE(copy->check(im2col_context()).has_value()) << instruction;
          EXPECT_TRUE(
              validateModule(*owned,
                             ModuleValidationPolicy::RequireCompleteContext)
                  .has_value())
              << instruction;
          bool inspected = false;
          auto inspect = [&](const auto& selected) {
            inspected = true;
            auto inspect_info = [&](const auto& payload) {
              const auto& tensor = payload.tensor.value;
              const auto expected = mode == "im2col" ? TensorAccessMode::Im2col
                                    : mode == "im2col::w"
                                        ? TensorAccessMode::Im2colW
                                        : TensorAccessMode::Im2colW128;
              EXPECT_EQ(tensor.mode, expected);
              EXPECT_EQ(tensor.coordinates.elements.size(),
                        static_cast<size_t>(rank));
              if constexpr (requires { payload.im2col_info; }) {
                const auto& field = payload.im2col_info;
                if constexpr (requires { field.has_value(); }) {
                  EXPECT_EQ(field.has_value(), present);
                  if (field) {
                    EXPECT_EQ(field->value.elements.size(),
                              static_cast<size_t>(count));
                    EXPECT_TRUE(
                        tensor_im2col_info_role(tensor, field->value, 0));
                  }
                } else {
                  EXPECT_TRUE(present);
                  EXPECT_EQ(field.value.elements.size(),
                            static_cast<size_t>(count));
                  EXPECT_TRUE(tensor_im2col_info_role(tensor, field.value, 0));
                }
              } else {
                EXPECT_FALSE(present);
              }
            };
            if constexpr (requires { selected.operands; })
              std::visit(inspect_info, selected.operands);
            else
              inspect_info(selected);
          };
#define PTX_INSPECT_IM2COL(CLASS)                                    \
  if (const auto* selected = dynamic_cast<const CLASS*>(copy.get())) \
  inspect(*selected)
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor3dSharedClusterIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor3dSharedClusterIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor3dSharedClusterIm2colW128);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor4dSharedClusterIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor4dSharedClusterIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor4dSharedClusterIm2colW128);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor5dSharedClusterIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor5dSharedClusterIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor5dSharedClusterIm2colW128);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor3dSharedCtaIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor3dSharedCtaIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor3dSharedCtaIm2colW128);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor4dSharedCtaIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor4dSharedCtaIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor4dSharedCtaIm2colW128);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor5dSharedCtaIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor5dSharedCtaIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkTensor5dSharedCtaIm2colW128);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor3dIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor3dIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor3dIm2colW128);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor4dIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor4dIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor4dIm2colW128);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor5dIm2col);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor5dIm2colW);
          PTX_INSPECT_IM2COL(CpAsyncBulkPrefetchTensor5dIm2colW128);
#undef PTX_INSPECT_IM2COL
          EXPECT_TRUE(inspected) << instruction;
        }
      }
    }
  }
  EXPECT_EQ(alternatives.size(), 27u);
}

/** Converted U16 values, not original S64/U64 magnitudes, take the bounds. */
TEST(TensorIm2colInfo, NarrowBeforeModeAndRankBounds) {
  struct Case {
    std::string_view mode;
    int rank;
    std::string_view info;
    bool valid;
  };
  constexpr std::array cases{
      Case{"im2col", 3, "65536", true},
      Case{"im2col", 3, "-65536", true},
      Case{"im2col", 3, "65535", true},
      Case{"im2col", 4, "65537, 255", true},
      Case{"im2col", 4, "256, 0", false},
      Case{"im2col", 5, "31, 31, 31", true},
      Case{"im2col", 5, "32, 0, 0", false},
      Case{"im2col::w", 3, "511, 31", true},
      Case{"im2col::w", 3, "512, 0", false},
      Case{"im2col::w", 3, "65536, 0", true},
      Case{"im2col::w::128", 3, "31, 31", true},
      Case{"im2col::w::128", 3, "32, 0", false},
      Case{"im2col::w::128", 3, "0, 32", false},
  };
  for (const auto& item : cases) {
    std::string coords = "0, 0, 0";
    for (int index = 3; index < item.rank; ++index)
      coords += ", 0";
    const auto instruction = "cp.async.bulk.prefetch.tensor." +
                             std::to_string(item.rank) + "d.L2.global." +
                             std::string(item.mode) + " [tensor_map, {" +
                             coords + "}], {" + std::string(item.info) + "};";
    auto owned = owned_im2col(instruction);
    ASSERT_TRUE(owned.has_value()) << instruction;
    const auto& copy = owned->functions.front().body.front();
    EXPECT_EQ(copy->check(im2col_context()).has_value(), item.valid)
        << instruction;
    EXPECT_EQ(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value(),
        item.valid)
        << instruction;
  }
}

/** Mode-specific target clauses do not leak across topologies or families. */
TEST(TensorIm2colInfo, ExactAvailabilityMatrix) {
  struct Case {
    std::string_view topology;
    std::string_view mode;
    checker::PtxVersion version;
    std::string_view target;
    bool valid;
  };
  constexpr std::array cases{
      Case{"cluster", "im2col", {8, 0}, "sm_90", true},
      Case{"cta", "im2col", {8, 0}, "sm_90", false},
      Case{"cta", "im2col", {8, 6}, "sm_90", true},
      Case{"cta", "im2col::w", {8, 6}, "sm_100", true},
      Case{"cta", "im2col::w", {8, 6}, "sm_110", true},
      Case{"cluster", "im2col::w", {8, 6}, "sm_100a", true},
      Case{"cluster", "im2col::w", {8, 6}, "sm_103a", false},
      Case{"cluster", "im2col::w", {8, 8}, "sm_103f", true},
      Case{"cluster", "im2col::w", {9, 3}, "sm_110a", true},
      Case{"prefetch", "im2col::w", {8, 6}, "sm_100", false},
      Case{"prefetch", "im2col::w", {8, 8}, "sm_103a", true},
      Case{"prefetch", "im2col::w", {9, 0}, "sm_110f", true},
      Case{"prefetch", "im2col::w", {9, 3}, "sm_120f", false},
      Case{"cta", "im2col::w::128", {8, 6}, "sm_100", false},
      Case{"cluster", "im2col::w::128", {8, 6}, "sm_100a", true},
      Case{"cluster", "im2col::w::128", {9, 0}, "sm_110a", true},
      Case{"cluster", "im2col::w::128", {8, 8}, "sm_110f", false},
  };
  for (const auto& item : cases) {
    const std::string prefix =
        item.topology == "prefetch"
            ? "cp.async.bulk.prefetch.tensor.3d.L2.global."
            : "cp.async.bulk.tensor.3d.shared::" + std::string(item.topology) +
                  ".global.";
    const std::string end = item.topology == "prefetch"
                                ? " [tensor_map, {0, 0, 0}]"
                                : ".mbarrier::complete_tx::bytes [dst], "
                                  "[tensor_map, {0, 0, 0}], [mbar]";
    const auto instruction = prefix + std::string(item.mode) + end + ";";
    auto owned = owned_im2col(instruction);
    ASSERT_TRUE(owned.has_value()) << instruction;
    const auto& copy = owned->functions.front().body.front();
    EXPECT_EQ(
        copy->check(im2col_context(item.version, item.target)).has_value(),
        item.valid)
        << instruction << " / " << item.target;
  }
}

/** Table 63 adds the 110f family to W-cluster on all three tensor ranks. */
TEST(TensorIm2colInfo, WClusterTable63FamilyEndpoints) {
  /** A source target and PTX version with the expected W-cluster availability. */
  struct Case {
    std::string_view version;
    checker::PtxVersion parsed_version;
    std::string_view target;
    bool valid;
  };
  constexpr std::array cases{
      Case{"8.6", {8, 6}, "sm_100a", true},
      Case{"8.6", {8, 6}, "sm_103a", false},
      Case{"8.8", {8, 8}, "sm_103a", true},
      Case{"8.8", {8, 8}, "sm_103f", true},
      Case{"8.8", {8, 8}, "sm_110a", false},
      Case{"8.9", {8, 9}, "sm_110f", false},
      Case{"9.0", {9, 0}, "sm_110a", true},
      Case{"9.0", {9, 0}, "sm_110f", true},
      Case{"9.0", {9, 0}, "sm_110", false},
      Case{"9.3", {9, 3}, "sm_120a", false},
      Case{"9.3", {9, 3}, "sm_120f", false},
  };
  for (int rank = 3; rank <= 5; ++rank) {
    std::string coordinates = "0, 0, 0";
    for (int index = 3; index < rank; ++index)
      coordinates += ", 0";
    for (const bool present : {false, true}) {
      const std::string instruction =
          "cp.async.bulk.tensor." + std::to_string(rank) +
          "d.shared::cluster.global.im2col::w."
          "mbarrier::complete_tx::bytes [dst], [tensor_map, {" +
          coordinates + "}], [mbar]" + (present ? ", {%u0, %u1}" : "") + ";";
      for (const auto& item : cases) {
        auto owned = owned_im2col(instruction, item.version, item.target);
        ASSERT_TRUE(owned.has_value())
            << instruction << " / " << item.version << " / " << item.target;
        const auto& copy = owned->functions.front().body.front();
        const auto context = im2col_context(item.parsed_version, item.target);
        EXPECT_EQ(copy->check(context).has_value(), item.valid)
            << instruction << " / " << item.version << " / " << item.target;
        EXPECT_EQ(validateModule(*owned,
                                 ModuleValidationPolicy::RequireCompleteContext)
                      .has_value(),
                  item.valid)
            << instruction << " / " << item.version << " / " << item.target;
      }
    }
  }
}

/** Owned information is checked directly and through module validation after
 * the syntax tree has been destroyed. */
TEST(TensorIm2colInfo, OwnedInformationMutationAndReferences) {
  auto owned = owned_im2col(
      "cp.async.bulk.prefetch.tensor.3d.L2.global.im2col "
      "[tensor_map, {0, 0, 0}], {%u0};");
  ASSERT_TRUE(owned.has_value());
  auto& instruction = owned->functions.front().body.front();
  auto& copy = instruction;
  auto* selected = dynamic_cast<CpAsyncBulkPrefetchTensor3dIm2col*>(copy.get());
  ASSERT_NE(selected, nullptr);
  auto* info_field = mutable_im2col_info(*selected);
  auto* tensor_field = mutable_im2col_tensor(*selected);
  ASSERT_NE(info_field, nullptr);
  ASSERT_NE(tensor_field, nullptr);
  auto& info = *info_field;
  auto& tensor = *tensor_field;
  const auto context = im2col_context();
  const auto valid = [&] {
    return copy->check(context).has_value() &&
           validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext)
               .has_value();
  };
  const auto invalid = [&] {
    EXPECT_FALSE(copy->check(context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
  };
  ASSERT_TRUE(valid());
  class ReferenceCounter final : public detail::IReferenceObserver {
   public:
    /** Count borrowed tensor references. */
    void tensor_operand(const ResolvedTensorOperand&,
                        std::span<const SourceRange>,
                        checker::AddressSymbolResolutionPolicy) override {
      ++count;
    }
    /** Count the complete borrowed im2col information payload. */
    void tensor_im2col_info(const ResolvedTensorIm2colInfo&,
                            std::span<const SourceRange>,
                            checker::AddressSymbolResolutionPolicy) override {
      ++count;
    }
    /** Count borrowed info register and immediate references. */
    void reg_or_imm(const RegOrImm&, std::span<const SourceRange>,
                    checker::AddressSymbolResolutionPolicy) override {
      ++count;
    }
    /** Count borrowed address references. */
    void address(const ResolvedAddress&, std::span<const SourceRange>,
                 checker::AddressSymbolResolutionPolicy) override {
      ++count;
    }
    /** Number of references seen. */
    int count = 0;
  } references;
  instruction->visit_references(references);
  EXPECT_GE(references.count, 2);
  EXPECT_EQ(tensor_im2col_info_role(tensor, info.value, 0),
            TensorIm2colInfoRole::OffsetW);
  EXPECT_FALSE(tensor_im2col_info_role(tensor, info.value, 1));

  auto& reg = std::get<ResolvedRegisterRef>(info.value.elements.front());
  const auto original_reg = reg;
  reg.vector_width = 2;
  invalid();
  reg = original_reg;
  reg.register_class = ResolvedRegisterClass::Predicate;
  invalid();
  reg = original_reg;
  reg.declared_type = ScalarType::B32;
  invalid();
  reg = original_reg;
  reg.declared_type.reset();
  invalid();
  reg = original_reg;
  reg.symbol_id.reset();
  reg.declared_type.reset();
  EXPECT_TRUE(copy->check(context).has_value());
  reg = original_reg;

  const auto element_range = info.locs.front();
  info.locs.clear();
  invalid();
  info.locs.push_back(element_range);
  EXPECT_TRUE(valid());
  const auto pack_range = info.value.pack_range;
  info.value.pack_range = {};
  invalid();
  info.value.pack_range = pack_range;
  tensor.mode = TensorAccessMode::Tiled;
  invalid();
  EXPECT_FALSE(tensor_im2col_info_role(tensor, info.value, 0));
  tensor.mode = TensorAccessMode::Im2col;
  tensor.rank = TensorRank::Four;
  invalid();
  tensor.rank = TensorRank::Three;
  EXPECT_TRUE(valid());
}

/** U16 source conversion and value bounds are rechecked on the owned payload. */
TEST(TensorIm2colInfo, OwnedImmediateMutation) {
  auto owned = owned_im2col(
      "cp.async.bulk.prefetch.tensor.3d.L2.global.im2col "
      "[tensor_map, {0, 0, 0}], {65536};");
  ASSERT_TRUE(owned.has_value());
  auto& copy = owned->functions.front().body.front();
  auto* selected = dynamic_cast<CpAsyncBulkPrefetchTensor3dIm2col*>(copy.get());
  ASSERT_NE(selected, nullptr);
  auto* payload = mutable_im2col_info(*selected);
  ASSERT_NE(payload, nullptr);
  auto& value = std::get<ResolvedImmediate>(payload->value.elements.front());
  const auto original = value;
  const auto context = im2col_context();
  ASSERT_TRUE(copy->check(context).has_value());
  EXPECT_EQ(value.bits, 0u);
  EXPECT_EQ(value.integer_source_bits, 65536u);
  const auto invalid = [&] {
    EXPECT_FALSE(copy->check(context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
  };
  value.bits = 1;
  invalid();
  value = original;
  value.integer_source_bits.reset();
  invalid();
  value = original;
  value.type = ScalarType::U32;
  invalid();
  value = original;
  value.bits = 65536;
  invalid();
  value = original;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

/** Both info layouts retain scalar coordinate identity after syntax lifetime. */
TEST(TensorIm2colInfo, OwnedCoordinateRegisterShapesAcrossReadLayouts) {
  for (const bool prefetch : {false, true}) {
    for (const bool with_info : {false, true}) {
      const std::string instruction =
          prefetch ? "cp.async.bulk.prefetch.tensor.3d.L2.global.im2col "
                     "[tensor_map, {%r0, %r1, %r2}]"
                   : "cp.async.bulk.tensor.3d.shared::cluster.global.im2col."
                     "mbarrier::complete_tx::bytes [dst], "
                     "[tensor_map, {%r0, %r1, %r2}], [mbar]";
      auto owned = owned_im2col(instruction + (with_info ? ", {%u0};" : ";"));
      ASSERT_TRUE(owned.has_value()) << instruction;
      auto& copy = owned->functions.front().body.front();
      const auto context = im2col_context();
      auto check_payload = [&](auto& selected) {
        auto& coordinate = std::get<ResolvedRegisterRef>(
            selected.tensor.value.coordinates.elements.front());
        ASSERT_TRUE(coordinate.symbol_id);
        ASSERT_EQ(coordinate.declared_type, ScalarType::S32);
        ASSERT_TRUE(copy->check(context).has_value());
        ASSERT_TRUE(validateModule(
                        *owned, ModuleValidationPolicy::RequireCompleteContext)
                        .has_value());
        const auto saved = coordinate;
        auto check_damage = [&] {
          EXPECT_FALSE(copy->check(context).has_value());
          EXPECT_FALSE(
              validateModule(*owned,
                             ModuleValidationPolicy::RequireCompleteContext)
                  .has_value());
        };
        coordinate.vector_width = 2;
        check_damage();
        coordinate = saved;
        coordinate.register_class = ResolvedRegisterClass::Predicate;
        check_damage();
        coordinate = saved;
        coordinate.declared_type = ScalarType::B16;
        check_damage();
        coordinate = saved;
        coordinate.declared_type.reset();
        check_damage();
        coordinate = saved;
        EXPECT_TRUE(copy->check(context).has_value());
        EXPECT_TRUE(validateModule(
                        *owned, ModuleValidationPolicy::RequireCompleteContext)
                        .has_value());
        coordinate.symbol_id.reset();
        coordinate.declared_type.reset();
        EXPECT_TRUE(copy->check(context).has_value());
        coordinate = saved;
      };
      if (prefetch) {
        auto* selected =
            dynamic_cast<CpAsyncBulkPrefetchTensor3dIm2col*>(copy.get());
        ASSERT_NE(selected, nullptr);
        check_payload(*selected);
      } else {
        auto* selected =
            dynamic_cast<CpAsyncBulkTensor3dSharedClusterIm2col*>(copy.get());
        ASSERT_NE(selected, nullptr);
        check_payload(*selected);
      }
    }
  }
}

/** New read forms recheck all pointer roles before lossy operand projection. */
TEST(TensorIm2colInfo, OwnedReadPointerMutation) {
  auto owned = owned_im2col(
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col."
      "mbarrier::complete_tx::bytes [dst], [tensor_map, {0, 0, 0}], "
      "[mbar], {%u0};");
  ASSERT_TRUE(owned.has_value());
  auto& copy = owned->functions.front().body.front();
  auto* payload =
      dynamic_cast<CpAsyncBulkTensor3dSharedClusterIm2col*>(copy.get());
  ASSERT_NE(payload, nullptr);
  const auto context = im2col_context();
  const auto valid = [&] {
    return copy->check(context).has_value() &&
           validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext)
               .has_value();
  };
  const auto invalid = [&] {
    EXPECT_FALSE(copy->check(context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
  };
  ASSERT_TRUE(valid());
  auto& map = std::get<ResolvedSymbolRef>(
      payload->tensor.value.tensor_map.address.base);
  const auto map_alignment = map.address_alignment;
  map.address_alignment = 32;
  invalid();
  map.address_alignment = map_alignment;
  const auto map_space = map.address_state_space;
  map.address_state_space = base::DeclarationStateSpace::Shared;
  invalid();
  map.address_state_space = map_space;
  auto& dst = std::get<ResolvedSymbolRef>(payload->dst.value.base);
  const auto dst_alignment = dst.address_alignment;
  dst.address_alignment = 8;
  invalid();
  dst.address_alignment = dst_alignment;
  auto& mbar = std::get<ResolvedSymbolRef>(payload->mbar.value.base);
  const auto mbar_alignment = mbar.address_alignment;
  mbar.address_alignment = 4;
  invalid();
  mbar.address_alignment = mbar_alignment;
  ASSERT_TRUE(valid());

  for (auto* address : {&payload->tensor.value.tensor_map.address,
                        &payload->dst.value, &payload->mbar.value}) {
    const auto original_base = address->base;
    address->base =
        ResolvedRegisterRef{.spelling = "%ptr",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B16};
    invalid();
    address->base =
        ResolvedRegisterRef{.spelling = "%ptr",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B64,
                            .vector_width = 2};
    invalid();
    address->base = original_base;
  }
  EXPECT_TRUE(valid());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

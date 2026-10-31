#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/isspacep.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Supply coherent headers while keeping source container width independent. */
std::string address_module(std::string_view declarations,
                           std::string_view operation,
                           uint32_t address_bits = 64) {
  return ".version 9.3\n.target sm_90\n.address_size " +
         std::to_string(address_bits) +
         "\n.entry kernel() { .reg .pred %p; .reg .u32 %r; .reg .u64 %rd; " +
         std::string(declarations) + std::string(operation) + " }";
}
/** Copy the typed source reference before its callback borrow expires. */
struct AddressSourceObserver final : detail::IReferenceObserver {
  /** Owned register metadata; the pred destination uses a different callback. */
  std::optional<ResolvedRegisterRef> source;
  /** Retain the actual declared type and identity for AST-free assertions. */
  void reg(const ResolvedRegisterRef& value, std::span<const SourceRange>,
           checker::AddressSymbolResolutionPolicy) override {
    source = value;
  }
};

/** Every state-space form admits the same finite integer/bit source cohort. */
TEST(IsspacepAddressWidth,
     PreservesBothAddressSizesAndEveryLegalSourceContainer) {
  constexpr std::string_view spaces[] = {
      "global",      "const",           "local", "shared",
      "shared::cta", "shared::cluster", "param", "param::entry"};
  constexpr ScalarType types[] = {ScalarType::B32, ScalarType::B64,
                                  ScalarType::S32, ScalarType::S64,
                                  ScalarType::U32, ScalarType::U64};
  constexpr std::string_view spellings[] = {"b32", "b64", "s32",
                                            "s64", "u32", "u64"};
  for (const auto size : {32u, 64u}) {
    for (const auto space : spaces) {
      for (size_t index = 0; index < std::size(types); ++index) {
        SCOPED_TRACE(space);
        SCOPED_TRACE(spellings[index]);
        const auto source =
            address_module(".reg ." + std::string(spellings[index]) + " a; ",
                           "isspacep." + std::string(space) + " %p, a;", size);
        auto module = [&] {
          const auto parsed = test_helpers::parseModule(source);
          EXPECT_TRUE(parsed && parsed.diagnostics.empty());
          return resolveAndValidateModule(*parsed);
        }();
        ASSERT_TRUE(module) << module.error().front().message;
        auto copy = *module;
        copy.functions.front().body[0] =
            copy.functions.front().body[0]->clone();
        EXPECT_TRUE(validateModule(copy));
        AddressSourceObserver observer;
        copy.functions.front().body[0]->visit_references(observer);
        ASSERT_TRUE(observer.source);
        EXPECT_EQ(observer.source->declared_type, types[index]);
        ASSERT_TRUE(observer.source->symbol_id);
        EXPECT_EQ(copy.symbols.symbol(*observer.source->symbol_id).name, "a");
      }
    }
  }
}

/** Wider data carriers and other register classes cannot become generic addresses. */
TEST(IsspacepAddressWidth, RejectsOtherTypesAndOperandShapes) {
  for (const auto type : {"b8", "b16", "b128", "s8", "s16", "u8", "u16", "f16",
                          "f32", "f64", "f16x2", "pred"}) {
    for (const auto size : {32u, 64u}) {
      for (const auto space :
           {"global", "const", "local", "shared", "shared::cta",
            "shared::cluster", "param", "param::entry"}) {
        SCOPED_TRACE(type);
        SCOPED_TRACE(space);
        const auto parsed = test_helpers::parseModule(
            address_module(".reg ." + std::string(type) + " a; ",
                           "isspacep." + std::string(space) + " %p, a;", size));
        ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
        EXPECT_FALSE(resolveAndValidateModule(*parsed));
      }
    }
  }
  for (const auto operand :
       {"1", "0d0000000000000000", "%tid.x", "{a,a}", "[a]"}) {
    const auto parsed = test_helpers::parseModule(address_module(
        ".reg .u32 a; ", "isspacep.global %p, " + std::string(operand) + ";"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed));
  }
  const auto vector = test_helpers::parseModule(
      address_module(".reg .v2 .u32 a; ", "isspacep.global %p, a;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(vector);
  EXPECT_FALSE(resolveAndValidateModule(*vector));
}

/** Existing compact members and selected scalar aliases keep their real source identity. */
TEST(IsspacepAddressWidth,
     PreservesScalarAliasesAndRejectsOwnedSourceMutation) {
  for (const auto operation :
       {"isspacep.global %p, group1;", "isspacep.global %p, lanes.x;"}) {
    const auto parsed = test_helpers::parseModule(
        address_module(".reg .u32 group<2>; .reg .v2 .u32 lanes; ", operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_TRUE(resolveAndValidateModule(*parsed));
  }
  auto module = [&] {
    const auto parsed = test_helpers::parseModule(
        address_module(".reg .u32 a; .reg .b128 wide; .reg .f64 fp; .reg .u16 "
                       "small; .reg .v2 .u32 whole; ",
                       "isspacep.global %p, a;"));
    EXPECT_TRUE(parsed && parsed.diagnostics.empty());
    return resolveAndValidateModule(*parsed);
  }();
  ASSERT_TRUE(module) << module.error().front().message;
  for (const auto name : {"wide", "fp", "small", "whole"}) {
    auto copy = *module;
    auto& instruction =
        dynamic_cast<IsspacepGlobalU64&>(*copy.functions.front().body[0]);
    auto& reference = instruction.src.value;
    const auto scope =
        copy.symbols.symbol(copy.functions.front().symbol_id).owned_scope;
    ASSERT_TRUE(scope);
    const auto lookup = copy.symbols.lookup(*scope, name);
    ASSERT_TRUE(lookup);
    reference.symbol_id = lookup->symbol;
    reference.spelling = name;
    reference.declared_type =
        name == std::string_view("wide")    ? ScalarType::B128
        : name == std::string_view("fp")    ? ScalarType::F64
        : name == std::string_view("whole") ? ScalarType::U32
                                            : ScalarType::U16;
    if (name == std::string_view("whole"))
      reference.vector_width = 2;
    EXPECT_FALSE(validateModule(copy));
  }
  auto forged = *module;
  dynamic_cast<IsspacepGlobalU64&>(*forged.functions.front().body[0])
      .src.value.declared_type = ScalarType::U64;
  EXPECT_FALSE(validateModule(forged));
}

/** Existing address conversion policies keep their own same-size rules. */
TEST(IsspacepAddressWidth, PreservesOtherAddressRoleAndDestinationControls) {
  for (const auto operation :
       {"cvta.global.u64 %rd, wide;", "mapa.u64 %rd, wide, 0;",
        "isspacep.global %r, %rd;"}) {
    const auto parsed = test_helpers::parseModule(
        address_module(".reg .b128 wide; ", operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed));
  }
}
/** Address-container admission cannot bypass qualifier introduction and target gates. */
TEST(IsspacepAddressWidth, PreservesQualifierVersionAndTargetWindows) {
  for (const auto space : {"param::entry", "shared::cta", "shared::cluster"}) {
    auto source = address_module(".reg .u32 a; ",
                                 "isspacep." + std::string(space) + " %p, a;");
    const bool cluster = space == std::string_view("shared::cluster");
    const auto version =
        space == std::string_view("param::entry") ? "8.2" : "7.7";
    source.replace(source.find("9.3"), 3, cluster ? "7.8" : version);
    source.replace(source.find("sm_90"), 5, cluster ? "sm_80" : "sm_70");
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

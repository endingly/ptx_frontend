#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <ptx_frontend/resolved_ir/model/arithmetic/mul.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/neg.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/setp.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Emit one of the four native packed variants without changing predicate roles. */
std::string packed_operation(unsigned variant,
                             const std::vector<std::string>& data) {
  if (variant == 0)
    return "mul.rn.ftz.sat.f16x2 " + data[0] + "," + data[1] + "," + data[2] +
           ";";
  if (variant == 1)
    return "neg.ftz.f16x2 " + data[0] + "," + data[1] + ";";
  return std::string(variant == 2 ? "setp.eq.ftz.f16x2 "
                                  : "setp.eq.and.ftz.f16x2 ") +
         "%p|%q," + data[0] + "," + data[1] + (variant == 2 ? ";" : ",%c;");
}
/** Own complete modern declaration text, with spare scalar/component retargets. */
std::string packed_source(unsigned variant,
                          const std::vector<std::string>& types,
                          const std::vector<std::string>& operands = {}) {
  std::string source =
      ".version 9.3\n.target sm_90\n.entry k() { .reg .pred %p,%q,%c; ";
  std::vector<std::string> data;
  for (std::size_t index = 0; index < types.size(); ++index) {
    data.push_back("%v" + std::to_string(index));
    source += ".reg ." + types[index] + " " + data.back() + ";";
  }
  source +=
      ".reg .f16x2 native; .reg .b32 bits; .reg .f32 wrong; .reg .v2 .f16x2 "
      "V<2>; .reg .v4 .b32 B; ";
  return source +
         packed_operation(variant, operands.empty() ? data : operands) + " }";
}
/** Resolve/validate while releasing source, parser and AST before returning. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> packed_module(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  auto source_only = resolveModuleOnly(*ast);
  if (!source_only)
    return source_only;
  EXPECT_TRUE(validateModule(*ast, *source_only));
  return resolveAndValidateModule(*ast);
}
/** Borrow only the changed register-data roles from exact generated variants. */
std::vector<ResolvedRegisterRef*> packed_data(Instruction& instruction,
                                              unsigned variant) {
  if (variant == 0) {
    auto& form = dynamic_cast<MulHalfX2&>(instruction);
    return {&form.dst.value, &form.src1.value, &form.src2.value};
  }
  if (variant == 1) {
    auto& form = dynamic_cast<NegF16x2&>(instruction);
    return {&form.dst.value, &form.src.value};
  }
  if (variant == 2) {
    auto& form = dynamic_cast<SetpF16x2&>(instruction);
    return {&form.src1.value, &form.src2.value};
  }
  auto& form = dynamic_cast<SetpF16x2Boolean&>(instruction);
  return {&form.src1.value, &form.src2.value};
}

TEST(NativeF16x2Carriers, AcceptsAllMixedNativeAndBitDataRoles) {
  for (unsigned variant = 0; variant < 4; ++variant) {
    const unsigned count = variant == 0 ? 3 : 2;
    for (unsigned mask = 0; mask < (1u << count); ++mask) {
      SCOPED_TRACE(variant);
      SCOPED_TRACE(mask);
      std::vector<std::string> types;
      for (unsigned index = 0; index < count; ++index)
        types.push_back(mask & (1u << index) ? "f16x2" : "b32");
      auto module = packed_module(packed_source(variant, types));
      ASSERT_TRUE(module) << (module.error().empty()
                                  ? ""
                                  : module.error().front().message);
      auto copied = *module;
      ResolvedModule assigned;
      assigned = copied;
      assigned.functions.front().body[0] =
          assigned.functions.front().body[0]->clone();
      const auto refs =
          packed_data(*assigned.functions.front().body[0], variant);
      ASSERT_EQ(refs.size(), count);
      for (unsigned index = 0; index < count; ++index) {
        ASSERT_TRUE(refs[index]->symbol_id);
        EXPECT_EQ(refs[index]->declared_type,
                  types[index] == "b32" ? ScalarType::B32 : ScalarType::F16x2);
        EXPECT_EQ(assigned.symbols.symbol(*refs[index]->symbol_id).name,
                  "%v" + std::to_string(index));
      }
      EXPECT_TRUE(validateModule(assigned));
    }
  }
}

TEST(NativeF16x2Carriers,
     RejectsEveryWrongDataRoleWithoutWidthOrFormatWidening) {
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  for (unsigned variant = 0; variant < 4; ++variant) {
    const unsigned count = variant == 0 ? 3 : 2;
    for (unsigned role = 0; role < count; ++role) {
      for (const auto type : {"s32", "u32", "f32", "b16", "b64", "b128", "f16",
                              "f64", "pred", "bf16x2", "f32x2"}) {
        SCOPED_TRACE(variant);
        SCOPED_TRACE(role);
        SCOPED_TRACE(type);
        auto types = std::vector<std::string>(count, "f16x2");
        types[role] = type;
        const auto ast =
            test_helpers::parseModule(packed_source(variant, types));
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        auto source_only = resolveModuleOnly(*ast);
        if (source_only) {
          EXPECT_FALSE(validateModule(*ast, *source_only));
          EXPECT_FALSE(validateModule(*source_only));
        }
        EXPECT_FALSE(resolveAndValidateModule(*ast));
        auto valid = packed_module(
            packed_source(variant, std::vector<std::string>(count, "f16x2")));
        ASSERT_TRUE(valid);
        auto& ref =
            *packed_data(*valid->functions.front().body[0], variant)[role];
        const auto* metadata =
            base::find_scalar_type_metadata("." + std::string(type));
        ASSERT_NE(metadata, nullptr);
        ref.declared_type = metadata->type;
        if (std::string_view(type) == "pred")
          ref.register_class = ResolvedRegisterClass::Predicate;
        EXPECT_FALSE(valid->functions.front().body[0]->check(context));
        EXPECT_FALSE(validateModule(*valid));
      }
    }
  }
}

TEST(NativeF16x2Carriers, PreservesComponentsCompactMembersAndOwnedRetargets) {
  for (unsigned variant = 0; variant < 4; ++variant) {
    const unsigned count = variant == 0 ? 3 : 2;
    for (unsigned role = 0; role < count; ++role) {
      for (const auto component : {"V1.y", "B.z"}) {
        auto operands = std::vector<std::string>{"%v0", "%v1"};
        if (count == 3)
          operands.push_back("%v2");
        operands[role] = component;
        auto module = packed_module(packed_source(
            variant, std::vector<std::string>(count, "b32"), operands));
        ASSERT_TRUE(module)
            << (module.error().empty() ? "" : module.error().front().message);
        auto copied = *module;
        copied.functions.front().body[0] =
            copied.functions.front().body[0]->clone();
        auto& ref =
            *packed_data(*copied.functions.front().body[0], variant)[role];
        ASSERT_TRUE(ref.component);
        ASSERT_TRUE(ref.symbol_id);
        EXPECT_FALSE(ref.vector_width);
        EXPECT_EQ(ref.component->origin,
                  RegisterComponentOrigin::ExplicitSelector);
        EXPECT_EQ(ref.component->lane,
                  std::string_view(component) == "V1.y" ? 1 : 2);
        EXPECT_EQ(ref.parameterized_index, std::string_view(component) == "V1.y"
                                               ? std::optional<uint32_t>{1}
                                               : std::nullopt);
        EXPECT_TRUE(validateModule(copied));
        if (ref.parameterized_index) {
          ref.parameterized_index = 0;
          ref.component->base_spelling = "V0";
          ref.spelling = "V0.y";
          EXPECT_TRUE(validateModule(copied));
        }
        ref.component->lane = 4;
        EXPECT_FALSE(validateModule(copied));
      }
      auto module = packed_module(
          packed_source(variant, std::vector<std::string>(count, "b32")));
      ASSERT_TRUE(module);
      for (const auto name : {"native", "bits", "wrong"}) {
        auto copied = *module;
        auto& ref =
            *packed_data(*copied.functions.front().body[0], variant)[role];
        const auto symbol = copied.symbols.lookup(
            copied.functions.front().declaration_scope, name);
        ASSERT_TRUE(symbol);
        ref.symbol_id = symbol->symbol;
        ref.spelling = name;
        ref.declared_type =
            std::string_view(name) == "native" ? ScalarType::F16x2
            : std::string_view(name) == "bits" ? ScalarType::B32
                                               : ScalarType::F32;
        EXPECT_EQ(validateModule(copied).has_value(),
                  std::string_view(name) != "wrong");
      }
      auto whole = *module;
      auto& ref = *packed_data(*whole.functions.front().body[0], variant)[role];
      const auto vector_symbol =
          whole.symbols.lookup(whole.functions.front().declaration_scope, "V1");
      ASSERT_TRUE(vector_symbol);
      ref.symbol_id = vector_symbol->symbol;
      ref.parameterized_index = 1;
      ref.spelling = "V1";
      ref.declared_type = ScalarType::F16x2;
      ref.vector_width = 2;
      EXPECT_FALSE(validateModule(whole));
    }
  }
}

TEST(NativeF16x2Carriers,
     KeepsWholeVectorsLiteralsAndAlternateFormatsSeparate) {
  for (unsigned variant = 0; variant < 4; ++variant) {
    const unsigned count = variant == 0 ? 3 : 2;
    for (unsigned role = 0; role < count; ++role) {
      for (const auto operand : {"V1", "{native,bits}", "0", "0f00000000"}) {
        auto operands = std::vector<std::string>{"%v0", "%v1"};
        if (count == 3)
          operands.push_back("%v2");
        operands[role] = operand;
        const auto ast = test_helpers::parseModule(packed_source(
            variant, std::vector<std::string>(count, "b32"), operands));
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        EXPECT_FALSE(resolveAndValidateModule(*ast));
      }
    }
  }
  for (const auto operation :
       {"setp.eq.bf16x2 %p|%q,%v0,%v1;", "setp.eq.and.bf16x2 %p|%q,%v0,%v1,%c;",
        "neg.bf16x2 %v0,%v1;"}) {
    for (const auto type : {"b32", "f16x2"}) {
      auto source = packed_source(1, {type, type});
      source.replace(source.find("neg.ftz.f16x2"), std::string::npos,
                     std::string(operation) + " }");
      const auto ast = test_helpers::parseModule(source);
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_EQ(resolveAndValidateModule(*ast).has_value(),
                std::string_view(type) == "b32");
    }
  }
}
/** Equal legal carrier classes do not erase supplied-source identity checks. */
TEST(NativeF16x2Carriers,
     KeepsStaleSourceAssociationSeparateFromCarrierCompatibility) {
  for (unsigned variant = 0; variant < 4; ++variant) {
    auto ast = test_helpers::parseModule(packed_source(
        variant, std::vector<std::string>(variant == 0 ? 3 : 2, "f16x2")));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    auto& function = std::get<syntax_ast::AstFunction>(ast->items.back());
    auto& instruction =
        std::get<syntax_ast::AstInstruction>(function.body.back());
    auto& identifier = std::get<syntax_ast::AstIdentifierRef>(
        instruction.operands[variant < 2 ? 0 : 1]);
    identifier.syntax.text = "bits";
    EXPECT_TRUE(resolveAndValidateModule(*ast));
    EXPECT_FALSE(validateModule(*ast, *module));
    EXPECT_TRUE(validateModule(*module));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

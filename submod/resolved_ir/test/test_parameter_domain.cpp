#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Resolve complete text and return independently owned records after source death. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> parameter_module(
    std::string text) {
  const auto parsed = test_helpers::parseModule(text);
  EXPECT_TRUE(parsed && parsed.diagnostics.empty());
  if (!parsed || !parsed.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*parsed);
}
/** Use declarations that do not introduce half arithmetic availability requirements. */
std::string parameter_header() {
  return ".version 9.3\n.target sm_80\n.address_size 64\n";
}

/** Table-defined scalar eligibility and vector size are the same at every role boundary. */
TEST(ParameterDomain, FiniteFundamentalShapesAcrossAllParameterRoles) {
  constexpr ScalarType supported[] = {
      ScalarType::U8,   ScalarType::U16, ScalarType::U32,   ScalarType::U64,
      ScalarType::S8,   ScalarType::S16, ScalarType::S32,   ScalarType::S64,
      ScalarType::B8,   ScalarType::B16, ScalarType::B32,   ScalarType::B64,
      ScalarType::B128, ScalarType::F16, ScalarType::F16x2, ScalarType::F32,
      ScalarType::F64};
  std::array<size_t, 3> accepted{};
  for (const auto& type : base::scalar_type_metadata()) {
    const bool fundamental =
        std::ranges::find(supported, type.type) != std::end(supported);
    EXPECT_EQ(declaration_semantics::parameterScalarType(type.source_spelling)
                  .has_value(),
              fundamental);
    for (size_t shape = 0; shape < 3; ++shape) {
      const uint32_t lanes = shape == 0 ? 1 : shape == 1 ? 2 : 4;
      const bool eligible = fundamental && lanes * type.size_bytes <= 16;
      if (eligible)
        ++accepted[shape];
      const auto declaration = std::string(".param ") +
                               (lanes == 1   ? ""
                                : lanes == 2 ? ".v2 "
                                             : ".v4 ") +
                               std::string(type.source_spelling) + " value";
      for (int role = 0; role < 5; ++role) {
        SCOPED_TRACE(type.source_spelling);
        SCOPED_TRACE(lanes);
        SCOPED_TRACE(role);
        auto source = parameter_header();
        if (role == 0)
          source += ".entry kernel(" + declaration + ") {}";
        if (role == 1)
          source += ".extern .func target(" + declaration + ");";
        if (role == 2)
          source += ".extern .func (" + declaration + ") target();";
        if (role == 3)
          source += ".entry kernel() {" + declaration + ";}";
        if (role == 4)
          source += ".entry kernel() { prototype: .callprototype (" +
                    declaration + ") _ (" + declaration + "); }";
        auto module = parameter_module(source);
        ASSERT_EQ(module.has_value(), eligible)
            << (module || module.error().empty()
                    ? ""
                    : module.error().front().message);
        if (module) {
          auto copy = *module;
          EXPECT_TRUE(validateModule(copy));
          if (role != 4) {
            const auto& record =
                copy.functions.front().parameter_declarations.front();
            EXPECT_EQ(record.scalar_type, type.type);
            EXPECT_EQ(record.vector_width, lanes);
            EXPECT_EQ(record.byte_extent, type.size_bytes * lanes);
            EXPECT_EQ(record.alignment, type.size_bytes * lanes);
          }
        }
      }
    }
  }
  EXPECT_EQ(accepted, (std::array<size_t, 3>{17, 16, 12}));
  EXPECT_FALSE(declaration_semantics::parameterScalarType(".unknown"));
}

/** Vector bytes and natural alignment belong to each object, not array/group allocation. */
TEST(ParameterDomain, RetainsVectorArrayAndCompactMemberLayouts) {
  auto module = parameter_module(parameter_header() + R"ptx(
.entry kernel(.param .v2 .u32 scalar, .param .v2 .u32 array[3]) {
.param .v4 .f16x2 matrix[2][3];
.param .v2 .f16x2 group<3>;
.param .align 32 .v2 .u32 aligned;
})ptx");
  ASSERT_TRUE(module) << module.error().front().message;
  ASSERT_EQ(module->functions.front().parameter_declarations.size(), 5u);
  const auto& records = module->functions.front().parameter_declarations;
  constexpr uint64_t bytes[] = {8, 24, 96, 8, 8};
  constexpr uint64_t alignment[] = {8, 8, 16, 8, 32};
  for (size_t index = 0; index < 5; ++index) {
    EXPECT_EQ(records[index].byte_extent, bytes[index]);
    EXPECT_EQ(records[index].alignment, alignment[index]);
  }
  EXPECT_EQ(records[2].scalar_type, ScalarType::F16x2);
  EXPECT_EQ(records[3].vector_width, 2u);
  EXPECT_EQ(module->symbols.symbol(records[3].symbol_id).parameterized_count,
            3u);
  EXPECT_TRUE(module->storage_declarations.empty());
  auto copy = *module;
  EXPECT_TRUE(validateModule(copy));
  for (int mutation = 0; mutation < 10; ++mutation) {
    auto invalid = *module;
    auto& record = invalid.functions.front().parameter_declarations[0];
    if (mutation == 0)
      record.vector_width = 4;
    if (mutation == 1)
      record.scalar_type = ScalarType::BF16x2;
    if (mutation == 2)
      ++*record.byte_extent;
    if (mutation == 3)
      record.array_extents = {3};
    if (mutation == 4)
      record.alignment = 4;
    if (mutation == 5)
      record.role = ParameterDeclarationRole::DeviceInput;
    if (mutation == 6)
      record.symbol_id = records[1].symbol_id;
    if (mutation == 7)
      record.pointer = call_argument_compatibility::PointerProperties{};
    if (mutation == 8)
      record.scope_id = invalid.symbols.moduleScope();
    if (mutation == 9)
      std::swap(invalid.functions.front().parameter_declarations[0],
                invalid.functions.front().parameter_declarations[1]);
    EXPECT_FALSE(validateModule(invalid));
  }
}

/** Typed packed objects retain scalar or explicit vector ABI shape, never implicit lanes. */
TEST(ParameterDomain, CallsAndTransfersPreserveNativePackedObjects) {
  for (const auto vector : {"", ".v2 ", ".v4 "}) {
    auto source = parameter_header() + ".extern .func (.param " +
                  std::string(vector) + ".f16x2 out) exchange(.param " +
                  vector + ".f16x2 in);\n";
    source += ".entry kernel() { .param " + std::string(vector) +
              ".f16x2 a, result; .reg .b32 %r<4>;\n";
    source += std::string(vector).empty() ? "st.param.b32 [a], %r0;"
              : std::string(vector) == ".v2 "
                  ? "st.param.v2.b32 [a], {%r0,%r1};"
                  : "st.param.v4.b32 [a], {%r0,%r1,%r2,%r3};";
    source += "call (result), exchange, (a);";
    source += std::string(vector).empty() ? "ld.param.b32 %r0, [result];"
              : std::string(vector) == ".v2 "
                  ? "ld.param.v2.b32 {%r0,%r1}, [result];"
                  : "ld.param.v4.b32 {%r0,%r1,%r2,%r3}, [result];";
    source += "}";
    auto module = parameter_module(source);
    ASSERT_TRUE(module) << module.error().front().message;
    auto copy = *module;
    for (auto& instruction : copy.functions.back().body)
      instruction = instruction->clone();
    EXPECT_TRUE(validateModule(copy));
    EXPECT_EQ(
        copy.functions.front().contract.signature.parameters[0].scalar_type,
        ScalarType::F16x2);
    EXPECT_EQ(copy.functions.back().parameter_declarations[0].scalar_type,
              ScalarType::F16x2);
  }
  auto module = parameter_module(parameter_header() + R"ptx(
.extern .func (.reg .v2 .f16x2 out) exchange(.reg .v2 .f16x2 in);
.entry kernel() { .reg .v2 .f16x2 a, result; call (result), exchange, (a); }
)ptx");
  ASSERT_TRUE(module) << module.error().front().message;
  EXPECT_TRUE(validateModule(*module));
}

/** Numeric packed conversion is an explicit reference boundary, not declaration rejection. */
TEST(ParameterDomain, KeepsPackedLiteralRepresentationBoundaryPrecise) {
  const auto exact =
      parameter_module(parameter_header() +
                       ".extern .func take(.param .f16x2 input); "
                       ".entry kernel() { call take, (0f3fc00000); }");
  ASSERT_TRUE(exact) << exact.error().front().message;
  EXPECT_TRUE(validateModule(*exact));
  for (const auto literal : {"1.5", "0d3ff8000000000000", "0x3c003c00", "0"}) {
    auto result = parameter_module(parameter_header() +
                                   ".extern .func take(.param .f16x2 input); "
                                   ".entry kernel() { call take, (" +
                                   literal + "); }");
    ASSERT_FALSE(result);
    ASSERT_FALSE(result.error().empty());
    EXPECT_NE(result.error().front().message.find(
                  "No direct numeric call-literal conversion to packed .f16x2"),
              std::string::npos);
  }
}

/** Byte-unit and scalar-element source forms retain declaration scalar stride. */
TEST(ParameterDomain, NamedVectorParameterArraysKeepScalarStride) {
  auto module = parameter_module(parameter_header() + R"ptx(
.extern .func take(.param .v2 .f16x2 input[2]);
.entry kernel() { .param .v2 .f16x2 a[2]; .reg .b32 %r; st.param.b32 a[1], %r; call take, (a); }
)ptx");
  ASSERT_TRUE(module) << module.error().front().message;
  const auto& address =
      dynamic_cast<const StExplicitScalar&>(*module->functions.back().body[0])
          .address.value;
  ASSERT_TRUE(address.named_index);
  EXPECT_EQ(address.named_index->scalar_stride, 4u);
  EXPECT_EQ(address.named_index->byte_displacement, 4);
  EXPECT_TRUE(validateModule(*module));
}

/** Source and owned totals use vector size, padding and coherent version windows. */
TEST(ParameterDomain, RechecksPaddedEntryLimitsAfterVersionRetargeting) {
  for (const auto version : {"1.4", "1.5", "8.0", "8.1"}) {
    const uint64_t limit = std::string_view(version) == "1.4"   ? 256
                           : std::string_view(version) == "8.1" ? 32764
                                                                : 4352;
    const auto prefix =
        ".version " + std::string(version) + "\n.target " +
        (std::string_view(version).starts_with("1.") ? "sm_13" : "sm_70") +
        "\n";
    for (const auto bytes : {limit, limit + 1}) {
      auto result = parameter_module(prefix + ".entry k(.param .b8 a[" +
                                     std::to_string(bytes) + "]) {}");
      EXPECT_EQ(result.has_value(), bytes == limit);
    }
  }
  auto result = parameter_module(
      ".version 8.1\n.target sm_70\n.entry k(.param .v4 .b8 a[1088], .param "
      ".b8 extra) {}");
  ASSERT_TRUE(result) << result.error().front().message;
  for (auto& region : result->header.regions)
    region.version = checker::PtxVersion{8, 0};
  for (auto& function : result->functions)
    function.source_version = checker::PtxVersion{8, 0};
  const auto invalid = validateModule(*result);
  ASSERT_FALSE(invalid);
  EXPECT_TRUE(std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
    return diagnostic.message.find("Static entry parameter bytes exceed") !=
           std::string::npos;
  }));
  auto padding = parameter_module(
      ".version 1.4\n.target sm_13\n.entry k(.param .b8 a[249], .param .v2 "
      ".u32 b) {}");
  EXPECT_FALSE(padding);
}
/** Direct and metadata calls preserve exact dtype, vector shape, extent and alignment. */
TEST(ParameterDomain, MetadataAndArrayCallsUseExactContracts) {
  for (const auto metadata :
       {"proto: .callprototype (.param .v2 .u32 r) _ (.param .v4 .f16x2 p);",
        "proto: .calltargets exchange;"}) {
    for (const auto declaration :
         {".param .v4 .f16x2 a;", ".param .v2 .f16x2 a;", ".param .v4 .b32 a;",
          ".param .b128 a;", ".param .v4 .f16x2 a[2];"}) {
      const auto source =
          parameter_header() +
          ".extern .func (.param .v2 .u32 result) exchange(.param .v4 .f16x2 "
          "input);\n"
          ".entry kernel() { .reg .b64 target; .param .v2 .u32 result; " +
          declaration + metadata + " call (result), target, (a), proto; }";
      auto module = parameter_module(source);
      const bool matching =
          declaration == std::string_view(".param .v4 .f16x2 a;");
      ASSERT_EQ(module.has_value(), matching)
          << (module || module.error().empty()
                  ? ""
                  : module.error().front().message);
      if (module) {
        auto copy = *module;
        copy.functions.back().body[0] = copy.functions.back().body[0]->clone();
        EXPECT_TRUE(validateModule(copy));
      }
    }
  }
  for (const auto declaration :
       {".param .v2 .u32 a[3];", ".param .v2 .u32 a[2];",
        ".param .align 16 .v2 .u32 a[3];", ".param .v4 .u16 a[3];"}) {
    const auto source =
        parameter_header() +
        ".extern .func take(.param .v2 .u32 input[3]); .entry kernel() { " +
        declaration + " call take, (a); }";
    auto module = parameter_module(source);
    EXPECT_EQ(module.has_value(),
              declaration == std::string_view(".param .v2 .u32 a[3];"));
  }
  auto native = parameter_module(
      parameter_header() +
      ".extern .func (.reg .f16x2 r) take(.reg .f16x2 input); "
      ".entry kernel() { .reg .f16x2 a, result; call (result), take, (a); }");
  ASSERT_TRUE(native) << native.error().front().message;
  EXPECT_TRUE(validateModule(*native));
}

/** Invalid structural combinations and vector-sized products cannot evade source checks. */
TEST(ParameterDomain, RejectsVectorRestrictionsAndCheckedProductOverflow) {
  for (const auto declaration :
       {".entry kernel(.param .v2 .u32 .ptr .global p) {}",
        ".extern .func take(.param .v2 .b8 input[]);",
        ".extern .func take(.reg .v2 .u32 input[2]);",
        ".entry kernel(.param .v4 .u32 a[18446744073709551615U]) {}",
        ".entry kernel() { .param .v4 .u32 a[18446744073709551615U]; }",
        ".global .f16x2 packed = 0; .entry kernel() {}",
        ".const .f16 half = 0; .entry kernel() {}"}) {
    SCOPED_TRACE(declaration);
    EXPECT_FALSE(parameter_module(parameter_header() + declaration));
  }
  auto large =
      parameter_module(parameter_header() +
                       ".entry kernel() { .param .v4 .u32 big[268435456]; }");
  ASSERT_TRUE(large) << large.error().front().message;
  EXPECT_EQ(large->functions.front().parameter_declarations.front().byte_extent,
            uint64_t{1} << 32);
  EXPECT_TRUE(validateModule(*large));
  auto legacy = parameter_module(
      ".version 2.0\n.target sm_20\n.entry kernel(.param .v2 .f16x2 input) "
      "{} ");
  ASSERT_TRUE(legacy) << legacy.error().front().message;
}

/** Source aliases and forged packed immediates cannot overwrite the retained object contract. */
TEST(ParameterDomain, SourceAssociationAndForgedPackedLiteralRemainChecked) {
  auto parsed = test_helpers::parseModule(
      parameter_header() + ".entry kernel(.param .v2 .f16x2 input) {}");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto module = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(module) << module.error().front().message;
  auto& function = std::get<syntax_ast::AstFunction>(parsed->items.back());
  function.parameters[0].vector_type->text = ".v4";
  EXPECT_FALSE(validateModule(*parsed, *module));
  EXPECT_TRUE(validateModule(*module));
  function.parameters[0].vector_type->text = ".v3";
  EXPECT_FALSE(resolveModuleOnly(*parsed));
  auto literal = parameter_module(parameter_header() +
                                  ".extern .func packed(.param .f16x2 input); "
                                  ".extern .func scalar(.param .f32 input); "
                                  ".entry kernel() { .param .f16x2 a; call "
                                  "packed, (a); call scalar, (1.5); }");
  ASSERT_TRUE(literal) << literal.error().front().message;
  auto& packed = dynamic_cast<CallDirect&>(*literal->functions.back().body[0]);
  const auto& scalar =
      dynamic_cast<const CallDirect&>(*literal->functions.back().body[1]);
  auto forged =
      std::get<ResolvedCallLiteral>(scalar.arguments->value.values[0].value);
  ASSERT_TRUE(forged.value);
  forged.value->type = ScalarType::F16x2;
  forged.value->bits = 0x3c003c00;
  packed.arguments->value.values[0].value = forged;
  EXPECT_FALSE(validateModule(*literal));
}

/** Name-only opaque inputs cannot suppress later numeric/vector entry-limit checks. */
TEST(ParameterDomain, InterleavedOpaqueInputsKeepNumericEntryLimits) {
  for (const auto extent : {1087, 1088}) {
    const auto source =
        std::string(".version 8.0\n.target sm_80, texmode_independent\n") +
        ".entry kernel(.param .b8 prefix[4], .param .texref texture, .param "
        ".v4 .b8 tail[" +
        std::to_string(extent) + "]) {}";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_EQ(resolveModuleOnly(*parsed).has_value(), extent == 1087);
    auto checked = resolveAndValidateModule(*parsed);
    ASSERT_EQ(checked.has_value(), extent == 1087);
    if (checked) {
      EXPECT_EQ(checked->functions.front().opaque_entry_parameters.size(), 1u);
      EXPECT_EQ(checked->functions.front().parameter_declarations.size(), 2u);
      EXPECT_TRUE(validateModule(*checked));
      auto source_dead = parameter_module(source);
      ASSERT_TRUE(source_dead);
      EXPECT_TRUE(validateModule(*source_dead));
    }
  }
}

/** Metadata-only vector arrays use the same positive, checked shape/alignment rules. */
TEST(ParameterDomain, RechecksUnusedVectorArrayPrototypeLayout) {
  auto module =
      parameter_module(parameter_header() +
                       ".entry kernel() { proto: .callprototype (.param .v2 "
                       ".u32 r[3]) _ (.param .v4 .f16x2 p[2]); }");
  ASSERT_TRUE(module) << module.error().front().message;
  ASSERT_EQ(module->functions.front().call_prototypes.size(), 1u);
  for (bool returns : {false, true}) {
    for (int mutation = 0; mutation < 6; ++mutation) {
      auto copy = *module;
      auto& signature =
          copy.functions.front().call_prototypes.front().signature;
      auto& formal = returns ? signature.return_parameters.front()
                             : signature.parameters.front();
      if (mutation == 0)
        formal.array_extents = {uint64_t{0}};
      if (mutation == 1)
        formal.array_extents = {std::numeric_limits<uint64_t>::max()};
      if (mutation == 2)
        formal.alignment = uint64_t{0};
      if (mutation == 3)
        formal.alignment = uint64_t{3};
      if (mutation == 4) {
        formal.scalar_type = ScalarType::BF16x2;
        formal.type_spelling = ".bf16x2";
      }
      if (mutation == 5)
        formal.vector_shape =
            static_cast<call_argument_compatibility::CallArgumentVectorShape>(
                255);
      const auto invalid = validateModule(copy);
      ASSERT_FALSE(invalid);
      const std::string_view expected =
          mutation < 4    ? "alignment, array shape or byte extent"
          : mutation == 4 ? "invalid parameter type"
                          : "invalid vector shape";
      EXPECT_TRUE(
          std::ranges::any_of(invalid.error(), [&](const auto& diagnostic) {
            return diagnostic.message.find(expected) != std::string::npos;
          }));
    }
  }
}

/** Unused scalar prototypes cannot bypass array-space and entry-only pointer roles. */
TEST(ParameterDomain, RechecksUnusedScalarPrototypeRoles) {
  for (const auto space : {"reg", "param"}) {
    auto module = parameter_module(
        parameter_header() + ".entry kernel() { proto: .callprototype _ (." +
        space + " .u32 p); }");
    ASSERT_TRUE(module) << module.error().front().message;
    for (int mutation = 0; mutation < 4; ++mutation) {
      if (mutation == 0 && space == std::string_view("param"))
        continue;
      auto copy = *module;
      auto& formal = copy.functions.front()
                         .call_prototypes.front()
                         .signature.parameters.front();
      if (mutation == 0) {
        formal.array_extents = {uint64_t{2}};
      }
      if (mutation == 1) {
        formal.is_pointer = true;
        formal.pointer_alignment = uint64_t{4};
      }
      if (mutation == 2)
        formal.pointed_state_space =
            call_argument_compatibility::PointedStateSpace::Global;
      if (mutation == 3)
        formal.pointer_alignment = uint64_t{4};
      const auto invalid = validateModule(copy);
      ASSERT_FALSE(invalid);
      EXPECT_TRUE(
          std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
            return diagnostic.message.find(
                       "parameter role or pointer attributes") !=
                   std::string::npos;
          }));
    }
  }
}

/** Function tables keep the same native parameter-vector signature equivalence. */
TEST(ParameterDomain, FunctionTablesRetainNativePackedVectorSignatures) {
  for (const auto type : {".v2 .f16x2", ".v4 .f16"}) {
    const auto source = parameter_header() +
                        ".extern .func first(.param .v2 .f16x2 input); .extern "
                        ".func second(.param " +
                        type +
                        " input); "
                        ".global .u64 table[2]={first,second}; "
                        ".entry kernel() { .reg .u64 target; .param .v2 .f16x2 "
                        "a; call target, (a), table; }";
    auto module = parameter_module(source);
    const bool matching = type == std::string_view(".v2 .f16x2");
    ASSERT_EQ(module.has_value(), matching)
        << (module || module.error().empty() ? ""
                                             : module.error().front().message);
    if (module) {
      ASSERT_EQ(module->call_tables.size(), 1u);
      EXPECT_EQ(
          module->call_tables.front().signature.parameters.front().scalar_type,
          ScalarType::F16x2);
      auto copy = *module;
      EXPECT_TRUE(validateModule(copy));
    }
  }
}

/** Aligned entry addition is checked independently from scalar/vector multiplication. */
TEST(ParameterDomain, RejectsEntryPaddingAdditionOverflow) {
  auto invalid =
      parameter_module(parameter_header() +
                       ".entry kernel(.param .b8 bytes[18446744073709551615U], "
                       ".param .v4 .u32 aligned) {}");
  ASSERT_FALSE(invalid);
  EXPECT_TRUE(std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
    return diagnostic.message.find(
               "Aligned entry parameter byte size overflows uint64_t") !=
           std::string::npos;
  }));
}

/** Vector compact members share one declaration while retaining independent actual identities. */
TEST(ParameterDomain, VectorCompactMembersRetainPerObjectCallIdentity) {
  const auto text =
      parameter_header() +
      ".extern .func (.param .v2 .f16x2 r) exchange(.param .v2 .f16x2 p); "
      ".entry kernel() { .param .v2 .f16x2 group<2>; call (group1), exchange, "
      "(group0); }";
  auto module = parameter_module(text);
  const auto original_ast = test_helpers::parseModule(text);
  ASSERT_MODULE_PARSE_SUCCEEDS(original_ast);
  ASSERT_TRUE(module) << module.error().front().message;
  const auto& record = module->functions.back().parameter_declarations.front();
  EXPECT_EQ(module->functions.back().parameter_declarations.size(), 1u);
  EXPECT_EQ(record.byte_extent, 8u);
  EXPECT_EQ(module->symbols.symbol(record.symbol_id).parameterized_count, 2u);
  auto copy = *module;
  copy.functions.back().body[0] = copy.functions.back().body[0]->clone();
  auto& call = dynamic_cast<CallDirect&>(*copy.functions.back().body[0]);
  auto& input =
      std::get<ResolvedCallParameterRef>(call.arguments->value.values[0].value);
  ASSERT_TRUE(call.return_value);
  ASSERT_EQ(call.arguments->value.values[0].locs.size(), 1u);
  /** Saved real argument extent, independent of current spelling/index edits. */
  const auto argument_range = call.arguments->value.values[0].locs.front();
  EXPECT_EQ(input.symbol_id, call.return_value->value.symbol_id);
  EXPECT_EQ(input.parameterized_index, 0u);
  EXPECT_EQ(call.return_value->value.parameterized_index, 1u);
  EXPECT_TRUE(validateModule(copy));
  EXPECT_TRUE(validateModule(*original_ast, copy));
  /** Match the current identity diagnostic without requiring an error count. */
  const auto has_model_mismatch = [argument_range](
                                      const checker::CheckResult& checked,
                                      std::string_view message) {
    return !checked &&
           std::ranges::any_of(checked.error(),
                               [&](const checker::CheckDiagnostic& diagnostic) {
                                 return diagnostic.kind ==
                                            checker::CheckDiagnosticKind::
                                                ModuleSourceMismatch &&
                                        diagnostic.range == argument_range &&
                                        diagnostic.message == message;
                               });
  };
  input.parameterized_index = 1;
  constexpr std::string_view identity_message =
      "Resolved call parameter metadata disagrees with its owned declaration.";
  EXPECT_TRUE(has_model_mismatch(validateModule(copy), identity_message));
  EXPECT_TRUE(has_model_mismatch(validateModule(*original_ast, copy),
                                 identity_message));
  input.spelling = "group1";
  EXPECT_TRUE(validateModule(copy));
  EXPECT_TRUE(validateModule(*original_ast, copy));
  input.parameterized_index = 2;
  input.spelling = "group2";
  constexpr std::string_view bounds_message =
      "Resolved module operand has an invalid parameterized member index.";
  EXPECT_TRUE(has_model_mismatch(validateModule(copy), bounds_message));
  EXPECT_TRUE(
      has_model_mismatch(validateModule(*original_ast, copy), bounds_message));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

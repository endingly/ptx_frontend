#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Build complete source with independently chosen vector declarations and uses. */
std::string namedAggregateSource(std::string_view declarations,
                                 std::string_view body,
                                 std::string_view version = "9.3",
                                 std::string_view target = "sm_90") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + "\n.address_size 64\n.entry kernel() { " +
         std::string(declarations) + std::string(body) + " }";
}

/** Resolve while releasing source, parser, and AST before the caller inspects owned IR. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveNamedAggregate(
    std::string source, bool check_source = true) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return check_source ? resolveAndValidateModule(*ast)
                      : resolveModuleOnly(*ast);
}

/** Locate a whole identifier independently within a unique written instruction. */
SourceRange expectedIdentifierRange(std::string_view source,
                                    std::string_view instruction,
                                    std::string_view identifier) {
  const auto instruction_offset = source.find(instruction);
  const auto relative = instruction.find(identifier);
  if (instruction_offset == std::string_view::npos ||
      relative == std::string_view::npos) {
    ADD_FAILURE() << "Missing independently specified source fixture token.";
    return {};
  }
  SourcePos start{1, 1};
  for (const char character : source.substr(0, instruction_offset + relative)) {
    if (character == '\n') {
      ++start.line;
      start.column = 1;
    } else {
      ++start.column;
    }
  }
  return {start,
          {start.line, start.column + static_cast<int32_t>(identifier.size())}};
}

/** Borrow the existing aggregate payload without changing the selected MOV class. */
WithLocs<ResolvedRegisterVector>& aggregateVector(Instruction& instruction,
                                                  bool unpack) {
  if (auto* scalar = dynamic_cast<MovScalar*>(&instruction))
    return *(unpack ? scalar->dst_register_vector
                    : scalar->src_register_vector);
  auto& wide = dynamic_cast<MovB128PackUnpack&>(instruction);
  return *(unpack ? wide.dst_register_vector : wide.src_register_vector);
}

/** Assert canonical whole-declaration provenance and exact lane order/ranges. */
void expectNamedAggregate(const WithLocs<ResolvedRegisterVector>& vector,
                          size_t arity) {
  ASSERT_EQ(vector.value.source.kind, ResolvedVectorSourceKind::NamedVector);
  ASSERT_TRUE(vector.value.source.whole_base);
  const auto& whole = *vector.value.source.whole_base;
  const auto& base = whole.value.register_ref;
  ASSERT_TRUE(base.symbol_id);
  EXPECT_EQ(base.vector_width, arity);
  EXPECT_FALSE(base.component);
  ASSERT_EQ(whole.locs.size(), 1u);
  EXPECT_EQ(whole.locs.front(), vector.value.source.range);
  ASSERT_EQ(vector.value.elements.size(), arity);
  ASSERT_EQ(vector.locs.size(), arity);
  for (size_t lane = 0; lane < arity; ++lane) {
    ASSERT_TRUE(vector.value.elements[lane]);
    const auto& reference = *vector.value.elements[lane];
    EXPECT_EQ(reference.symbol_id, base.symbol_id);
    EXPECT_EQ(reference.parameterized_index, base.parameterized_index);
    EXPECT_EQ(reference.declared_type, base.declared_type);
    EXPECT_EQ(reference.spelling, base.spelling);
    EXPECT_FALSE(reference.vector_width);
    ASSERT_TRUE(reference.component);
    EXPECT_EQ(reference.component->lane, lane);
    EXPECT_EQ(reference.component->declaration_width, arity);
    EXPECT_EQ(reference.component->origin,
              RegisterComponentOrigin::NamedProjection);
    EXPECT_FALSE(reference.component->selector);
    EXPECT_EQ(reference.component->base_range, vector.value.source.range);
    EXPECT_EQ(reference.component->range, vector.value.source.range);
    EXPECT_EQ(vector.locs[lane], vector.value.source.range);
  }
}

/** Every legal bit width, vector arity, and fundamental lane family shares pack/unpack. */
TEST(NamedAggregateMov, CoversMatchingFundamentalLaneCohortAndBraceControls) {
  /** One exact-size vector declaration family, independent of generated descriptors. */
  struct Cohort {
    /** Selected bit instruction width in bits. */
    unsigned bits;
    /** Real declaration arity; no instruction vector modifier is written. */
    unsigned arity;
    /** Fundamental lane spellings with the same element width. */
    std::initializer_list<std::string_view> types;
  };
  const Cohort cohorts[] = {
      {16, 2, {"b8", "s8", "u8"}},
      {32, 2, {"b16", "s16", "u16", "f16"}},
      {32, 4, {"b8", "s8", "u8"}},
      {64, 2, {"b32", "s32", "u32", "f32", "f16x2"}},
      {64, 4, {"b16", "s16", "u16", "f16"}},
      {128, 2, {"b64", "s64", "u64", "f64"}},
      {128, 4, {"b32", "s32", "u32", "f32", "f16x2"}},
  };
  for (const auto& cohort : cohorts) {
    for (const auto lane_type : cohort.types) {
      const auto bit_type = "b" + std::to_string(cohort.bits);
      std::string braces = "{";
      for (unsigned lane = 0; lane < cohort.arity; ++lane)
        braces += (lane == 0 ? "" : ",") + std::string("%lane") +
                  std::to_string(lane);
      braces += "}";
      const auto source = namedAggregateSource(
          ".reg ." + bit_type + " %q; .reg .v" + std::to_string(cohort.arity) +
              " ." + std::string(lane_type) + " V; .reg ." +
              std::string(lane_type) + " %lane<" +
              std::to_string(cohort.arity) + ">; ",
          "mov." + bit_type + " %q, V; mov." + bit_type + " V, %q; mov." +
              bit_type + " %q, " + braces + "; mov." + bit_type + " " + braces +
              ", %q;");
      SCOPED_TRACE(source);
      for (const auto source_check : {true, false}) {
        auto module = resolveNamedAggregate(source, source_check);
        ASSERT_TRUE(module.has_value()) << module.error().front().message;
        auto& body = module->functions.front().body;
        ASSERT_EQ(body.size(), 4u);
        expectNamedAggregate(aggregateVector(*body[0], false), cohort.arity);
        expectNamedAggregate(aggregateVector(*body[1], true), cohort.arity);
        EXPECT_EQ(aggregateVector(*body[0], false).value.source.range,
                  expectedIdentifierRange(source, "mov." + bit_type + " %q, V;",
                                          "V"));
        EXPECT_EQ(aggregateVector(*body[1], true).value.source.range,
                  expectedIdentifierRange(source, "mov." + bit_type + " V, %q;",
                                          "V"));
        const auto& base_ref = aggregateVector(*body[0], false)
                                   .value.source.whole_base->value.register_ref;
        const auto* metadata =
            base::find_scalar_type_metadata("." + std::string(lane_type));
        ASSERT_NE(metadata, nullptr);
        EXPECT_EQ(base_ref.declared_type, metadata->type);
        const auto scope =
            module->symbols.symbol(module->functions.front().symbol_id)
                .owned_scope;
        ASSERT_TRUE(scope);
        const auto declaration = module->symbols.lookup(*scope, "V");
        ASSERT_TRUE(declaration);
        EXPECT_EQ(base_ref.symbol_id, declaration->symbol);
        EXPECT_EQ(aggregateVector(*body[2], false).value.source.kind,
                  ResolvedVectorSourceKind::BraceList);
        EXPECT_EQ(aggregateVector(*body[3], true).value.source.kind,
                  ResolvedVectorSourceKind::BraceList);
        if (cohort.bits == 128) {
          const auto& pack = dynamic_cast<const MovB128PackUnpack&>(*body[0]);
          EXPECT_EQ(pack.type, ScalarType::B128);
          EXPECT_EQ(pack.operand_layout.value, 0u);
        } else {
          const auto& pack = dynamic_cast<const MovScalar&>(*body[0]);
          EXPECT_EQ(base::scalar_size_of(pack.type.value) * 8u, cohort.bits);
          EXPECT_EQ(pack.operand_layout.value, 1u);
          EXPECT_EQ(
              dynamic_cast<const MovScalar&>(*body[1]).operand_layout.value,
              2u);
        }
        EXPECT_TRUE(validateModule(*module));
        auto copy = *module;
        copy.functions.front().body[0] = body[0]->clone();
        EXPECT_TRUE(validateModule(copy));
      }
    }
  }
}

/** Scalar carriers keep existing same-overall-width bit compatibility. */
TEST(NamedAggregateMov, PreservesScalarCarriersAndOrdinaryScalarVectorForms) {
  /** One instruction width and all same-size fundamental scalar carriers. */
  struct ScalarCohort {
    /** Written aggregate bit type. */
    std::string_view instruction;
    /** Lane type of the matching two-element vector. */
    std::string_view element;
    /** Compatible whole-width scalar declaration types. */
    std::initializer_list<std::string_view> carriers;
  };
  const ScalarCohort cohorts[] = {
      {"b16", "u8", {"b16", "s16", "u16", "f16"}},
      {"b32", "u16", {"b32", "s32", "u32", "f32", "f16x2"}},
      {"b64", "u32", {"b64", "s64", "u64", "f64"}},
      {"b128", "u64", {"b128"}},
  };
  for (const auto& cohort : cohorts) {
    for (const auto scalar : cohort.carriers) {
      const auto module = resolveNamedAggregate(namedAggregateSource(
          ".reg ." + std::string(scalar) + " %q; .reg .v2 ." +
              std::string(cohort.element) + " V; ",
          "mov." + std::string(cohort.instruction) + " %q,V; mov." +
              std::string(cohort.instruction) + " V,%q;"));
      ASSERT_TRUE(module.has_value()) << module.error().front().message;
      EXPECT_TRUE(validateModule(*module));
    }
  }
  const auto ordinary = resolveNamedAggregate(
      namedAggregateSource(".reg .b32 %r; .reg .v2 .u32 V; ",
                           "mov.b32 %r,1; mov.b32 %r,V.x; mov.v2.u32 V,V;"));
  ASSERT_TRUE(ordinary.has_value()) << ordinary.error().front().message;
  EXPECT_EQ(dynamic_cast<const MovScalar&>(*ordinary->functions.front().body[0])
                .operand_layout.value,
            0u);
  EXPECT_TRUE(validateModule(*ordinary));
}

/** Inferred arity follows lexical declarations and parameterized member identity. */
TEST(NamedAggregateMov, RetainsGroupMembersShadowingAndWholeIdentifierRanges) {
  auto module = resolveNamedAggregate(
      namedAggregateSource(".reg .b64 %q; .reg .v2 .u32 %T<2>; ",
                           "mov.b64 %q,%T1; { .reg .v4 .u16 %T<2>; mov.b64 "
                           "%q,%T1; } mov.b64 %T1,%q;"));
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  auto& body = module->functions.front().body;
  auto& outer = aggregateVector(*body[0], false);
  auto& inner = aggregateVector(*body[1], false);
  expectNamedAggregate(outer, 2);
  expectNamedAggregate(inner, 4);
  EXPECT_EQ(
      outer.value.source.whole_base->value.register_ref.parameterized_index,
      1u);
  EXPECT_EQ(
      inner.value.source.whole_base->value.register_ref.parameterized_index,
      1u);
  EXPECT_NE(outer.value.source.whole_base->value.register_ref.symbol_id,
            inner.value.source.whole_base->value.register_ref.symbol_id);
  EXPECT_EQ(outer.value.source.range.end.column -
                outer.value.source.range.start.column,
            3);
  EXPECT_TRUE(validateModule(*module));
}

/** Named source syntax never bypasses aggregate shape or selected availability. */
TEST(NamedAggregateMov, RejectsBadShapesAndKeepsB128GatesAndSinkRules) {
  for (const auto body :
       {"mov.b32 %r,V;", "mov.b64 V,V;", "mov.u64 %q,V;", "mov.b16 %h,W;",
        "mov.b64 %q,%r;", "mov.b128 %wide,V.x;", "mov.b64 %q,missing;",
        "mov.b64 %q,%p;", "mov.b64 %q,%tid;", "mov.b64 %q,{%r,_};",
        "mov.b64 {_,_},%q;"}) {
    SCOPED_TRACE(body);
    EXPECT_FALSE(resolveNamedAggregate(namedAggregateSource(
        ".reg .b16 %h; .reg .b32 %r; .reg .b64 %q; .reg .b128 %wide; "
        ".reg .pred %p; .reg .v2 .u32 V; .reg .v4 .u8 W; ",
        body)));
  }
  for (const auto declaration :
       {".reg .v2 .pred V;", ".reg .v2 .bf16 V;", ".reg .v2 .tf32 V;",
        ".reg .v4 .u64 V;", ".shared .v2 .u32 V;"}) {
    SCOPED_TRACE(declaration);
    EXPECT_FALSE(resolveNamedAggregate(namedAggregateSource(
        ".reg .b64 %q; " + std::string(declaration), "mov.b64 V,%q;")));
  }
  const auto sinks = resolveNamedAggregate(namedAggregateSource(
      ".reg .b64 %q; .reg .b32 %r; ", "mov.b64 {%r,_},%q;"));
  ASSERT_TRUE(sinks.has_value()) << sinks.error().front().message;
  for (const auto& [version, target] :
       {std::pair{"8.3", "sm_70"}, std::pair{"8.2", "sm_70"},
        std::pair{"9.3", "sm_60"}}) {
    const auto module = resolveNamedAggregate(
        namedAggregateSource(".reg .b128 %q; .reg .v2 .b64 V; ",
                             "mov.b128 %q,V; mov.b128 V,%q;", version, target));
    EXPECT_EQ(module.has_value(), std::string_view(version) == "8.3");
  }
  for (const auto source : {"mov.b128 %b0,V;", "mov.b64 V,%rd0;"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value());
    EXPECT_FALSE(resolveInstruction(*ast));
  }
  // A declaration-free symbol address remains an ordinary scalar source.
  PtxSyntaxParser parser("mov.b64 %rd0,V;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value());
  const auto scalar = resolveInstruction(*ast);
  ASSERT_TRUE(scalar.has_value());
  const auto& mov = dynamic_cast<const MovScalar&>(**scalar);
  EXPECT_EQ(mov.operand_layout.value, 0u);
  EXPECT_FALSE(mov.src_register_vector);
  ASSERT_TRUE(mov.src_mov_source);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedSymbolRef>(mov.src_mov_source->value));
}

/** Owned named projections reject forged lane/base/member/order/range provenance. */
TEST(NamedAggregateMov, RejectsForgedOwnedProjectionMetadata) {
  const auto baseline = resolveNamedAggregate(namedAggregateSource(
      ".reg .b64 %q; .reg .v2 .u32 %T<2>; ", "mov.b64 %q,%T1;"));
  ASSERT_TRUE(baseline.has_value()) << baseline.error().front().message;
  for (int mutation = 0; mutation < 15; ++mutation) {
    auto changed = *baseline;
    auto& instruction = *changed.functions.front().body.front();
    auto& vector = aggregateVector(instruction, false);
    auto& value = vector.value;
    if (mutation == 0)
      std::swap(value.elements[0], value.elements[1]);
    else if (mutation == 1)
      value.elements.pop_back();
    else if (mutation == 2)
      value.elements[0]->declared_type = ScalarType::U16;
    else if (mutation == 3)
      value.elements[0]->parameterized_index = 0;
    else if (mutation == 4)
      value.elements[0]->component->declaration_width = 4;
    else if (mutation == 5)
      value.elements[0]->component->origin =
          RegisterComponentOrigin::ExplicitSelector;
    else if (mutation == 6)
      value.source.kind = ResolvedVectorSourceKind::BraceList;
    else if (mutation == 7)
      value.source.whole_base.reset();
    else if (mutation == 8)
      value.source.range = {{1, 1}, {1, 2}};
    else if (mutation == 9)
      value.source.whole_base->locs = {{{1, 1}, {1, 2}}};
    else if (mutation == 10)
      value.source.whole_base->value.register_ref.vector_width = 4;
    else if (mutation == 11)
      value.elements[0]->spelling = "other";
    else if (mutation == 12)
      changed.functions.front().instruction_ranges.front() = {{1, 1}, {1, 2}};
    else if (mutation == 13)
      value.source.whole_base->value.register_ref.symbol_id =
          dynamic_cast<MovScalar&>(instruction).dst_register->value.symbol_id;
    else
      value.elements[0]->symbol_id =
          dynamic_cast<MovScalar&>(instruction).dst_register->value.symbol_id;
    SCOPED_TRACE(mutation);
    EXPECT_FALSE(validateModule(changed));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

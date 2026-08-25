#include <gtest/gtest.h>

#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

syntax_ast::AstModule parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value()) << module.error().message;
  return std::move(*module);
}

const Add::IntegerNoSat& resolvedIntegerAdd(
    const ResolvedInstruction& instruction) {
  return std::get<Add::IntegerNoSat>(std::get<Add>(instruction).variant);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov::Scalar& mov) {
  return std::get<Mov::Scalar::ScalarOperands>(mov.operands);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov& mov) {
  return scalarMovOperands(std::get<Mov::Scalar>(mov.variant));
}

const Mov::Scalar::PackOperands& packMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

Mov::Scalar::PackOperands& packMovOperands(Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

const Mov::Scalar::UnpackOperands& unpackMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::UnpackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

TEST(ResolvedModule, CarriesFunctionAndRegisterSymbolIdentity) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u32 %named;
  add.u32 %named, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  const ResolvedFunction& function = resolved->functions.front();
  EXPECT_EQ(function.name, "kernel");
  EXPECT_TRUE(function.is_entry);
  EXPECT_FALSE(function.is_prototype);
  EXPECT_EQ(resolved->symbols.symbol(function.symbol_id).name, "kernel");
  ASSERT_EQ(function.body.size(), 1u);

  const Add::IntegerNoSat& add = resolvedIntegerAdd(function.body.front());
  EXPECT_FALSE(std::get<Add>(function.body.front()).execution_predicate);
  const ResolvedRegisterRef& dst = add.dst.value;
  const auto& src1 = std::get<ResolvedRegisterRef>(add.src1.value);
  const auto& src2 = std::get<ResolvedRegisterRef>(add.src2.value);

  ASSERT_TRUE(dst.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*dst.symbol_id).name, "%named");
  EXPECT_FALSE(dst.index.has_value());
  EXPECT_FALSE(dst.parameterized_index.has_value());
  EXPECT_EQ(dst.declared_type, ScalarType::U32);

  ASSERT_TRUE(src1.symbol_id.has_value());
  ASSERT_TRUE(src2.symbol_id.has_value());
  EXPECT_EQ(src1.symbol_id, src2.symbol_id);
  EXPECT_EQ(resolved->symbols.symbol(*src1.symbol_id).name, "%r");
  EXPECT_EQ(src1.index, 1u);
  EXPECT_EQ(src2.index, 2u);
  EXPECT_EQ(src1.parameterized_index, 1u);
  EXPECT_EQ(src2.parameterized_index, 2u);
  EXPECT_EQ(src1.declared_type, ScalarType::U32);
}

TEST(ResolvedModule, ReportsRegistersMissingFromTheBoundScope) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  add.u32 %dst, %missing, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Unresolved instruction operand '%missing'.");
}

TEST(ResolvedModule, DistinguishesSpecialRegistersFromMissingDeclarations) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  add.u32 %dst, %laneid, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Special register '%laneid' is not supported by this resolved "
            "operand.");
}

TEST(ResolvedModule, ResolvesAndChecksSpecialRegisterMetadata) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  mov.u32 %dst, %laneid;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto& u32 = std::get<Mov::Scalar>(mov.variant);
  const auto& scalar = scalarMovOperands(u32);
  const auto& special = std::get<ResolvedSpecialRegisterRef>(scalar.src.value);
  EXPECT_EQ(special.spelling, "%laneid");
  EXPECT_EQ(special.id.kind, base::SpecialRegisterKind::LaneId);
  EXPECT_FALSE(special.component.has_value());
  const auto special_info = base::metadata(special.id);
  EXPECT_EQ(special_info.element_type, ScalarType::U32);
  EXPECT_EQ(special_info.vector_width, 1u);
  EXPECT_EQ(special_info.minimum_ptx_major, 1u);
  EXPECT_EQ(special_info.minimum_ptx_minor, 3u);
  EXPECT_EQ(special_info.minimum_sm, 0u);

  const checker::Context too_old{
      .target =
          checker::TargetInfo{
              .ptx_version = checker::PtxVersion{1, 2},
              .sm_version = 10,
          },
      .instruction_range = scalar.src.locs.front(),
  };
  const auto rejected = checker::check(mov, too_old);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().message,
            "Operand value '%laneid' requires PTX ISA >= 1.3, but target PTX "
            "ISA is 1.2.");

  checker::Context supported = too_old;
  supported.target.ptx_version = checker::PtxVersion{1, 3};
  EXPECT_TRUE(checker::check(mov, supported).has_value());
}

TEST(ResolvedModule, ChecksSpecialRegisterSmAndTypeRequirements) {
  PtxSyntaxParser cluster_parser("mov.u32 %r0, %cluster_ctarank;");
  const auto cluster_ast = cluster_parser.parseInstruction();
  ASSERT_TRUE(cluster_ast.has_value()) << cluster_ast.error().message;
  const auto cluster_resolved = resolveInstruction(*cluster_ast);
  ASSERT_TRUE(cluster_resolved.has_value()) << cluster_resolved.error().message;
  const auto& cluster_mov = std::get<Mov>(*cluster_resolved);
  const auto cluster_check = checker::check(
      cluster_mov, checker::Context{
                       .target =
                           checker::TargetInfo{
                               .ptx_version = checker::PtxVersion{7, 8},
                               .sm_version = 80,
                           },
                       .instruction_range = cluster_ast->range,
                   });
  ASSERT_FALSE(cluster_check.has_value());
  ASSERT_EQ(cluster_check.error().size(), 1u);
  EXPECT_EQ(cluster_check.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  PtxSyntaxParser wide_parser("mov.u32 %r0, %clock64;");
  const auto wide_ast = wide_parser.parseInstruction();
  ASSERT_TRUE(wide_ast.has_value()) << wide_ast.error().message;
  const auto wide_resolved = resolveInstruction(*wide_ast);
  ASSERT_TRUE(wide_resolved.has_value()) << wide_resolved.error().message;
  const auto wide_check =
      checker::check(std::get<Mov>(*wide_resolved),
                     checker::Context{
                         .target =
                             checker::TargetInfo{
                                 .ptx_version = checker::PtxVersion{9, 3},
                                 .sm_version = 120,
                             },
                         .instruction_range = wide_ast->range,
                     });
  ASSERT_FALSE(wide_check.has_value());
  ASSERT_EQ(wide_check.error().size(), 1u);
  EXPECT_EQ(wide_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(wide_check.error().front().message,
            "Special-register operand 'src' has declared type 'U64' but "
            "instruction type source 'type' is 'U32'.");
}

TEST(ResolvedModule, ResolvesScalarSpecialRegisterComponentsOnly) {
  PtxSyntaxParser component_parser("mov.u32 %r0, %tid.x;");
  const auto component_ast = component_parser.parseInstruction();
  ASSERT_TRUE(component_ast.has_value()) << component_ast.error().message;
  const auto component_resolved = resolveInstruction(*component_ast);
  ASSERT_TRUE(component_resolved.has_value())
      << component_resolved.error().message;
  const auto& component = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(*component_resolved)).src.value);
  EXPECT_EQ(component.spelling, "%tid.x");
  EXPECT_EQ(component.id.kind, base::SpecialRegisterKind::Tid);
  EXPECT_EQ(component.component, base::VectorComponent::X);
  const auto component_info = base::metadata(component.id);
  EXPECT_EQ(component_info.vector_width, 4u);
  EXPECT_EQ(component_info.minimum_ptx_major, 2u);

  PtxSyntaxParser vector_parser("mov.u32 %r0, %tid;");
  const auto vector_ast = vector_parser.parseInstruction();
  ASSERT_TRUE(vector_ast.has_value()) << vector_ast.error().message;
  const auto vector_resolved = resolveInstruction(*vector_ast);
  ASSERT_FALSE(vector_resolved.has_value());
  EXPECT_EQ(vector_resolved.error().message,
            "Special register '%tid' is a vector; select a scalar component.");
}

TEST(ResolvedModule, ResolvesBoundSymbolsAndAddressBases) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u64 %rd<2>;
  .reg .u32 %r<3>;
  mov.u64 %rd0, global_value;
  ld.u32 %r0, [global_value+4];
  ld.u32 %r1, [%rd0-8];
  ld.u32 %r2, [240];
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);

  const auto& mov = std::get<Mov>(body[0]);
  const auto& mov_symbol =
      std::get<ResolvedSymbolRef>(scalarMovOperands(mov).src.value);
  ASSERT_TRUE(mov_symbol.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*mov_symbol.symbol_id).name,
            "global_value");
  EXPECT_EQ(mov_symbol.declaration_kind, binding::SymbolKind::Variable);
  EXPECT_EQ(mov_symbol.declaration_state_space,
            syntax_ast::AstStateSpace::Global);
  EXPECT_EQ(mov_symbol.address_state_space, syntax_ast::AstStateSpace::Global);
  EXPECT_EQ(mov_symbol.declared_type, ScalarType::U32);

  const auto& symbol_address =
      std::get<Ld::GenericScalar>(std::get<Ld>(body[1]).variant).address.value;
  const auto& symbol_base = std::get<ResolvedSymbolRef>(symbol_address.base);
  EXPECT_EQ(symbol_base.symbol_id, mov_symbol.symbol_id);
  ASSERT_TRUE(symbol_address.offset.has_value());
  EXPECT_EQ(symbol_address.offset->operation,
            ResolvedAddressOffsetOperator::Add);
  EXPECT_EQ(symbol_address.offset->value.type, ScalarType::S64);
  EXPECT_EQ(symbol_address.offset->value.bits, 4u);

  const auto& register_address =
      std::get<Ld::GenericScalar>(std::get<Ld>(body[2]).variant).address.value;
  const auto& register_base =
      std::get<ResolvedRegisterRef>(register_address.base);
  ASSERT_TRUE(register_base.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*register_base.symbol_id).name, "%rd");
  EXPECT_EQ(register_base.parameterized_index, 0u);
  ASSERT_TRUE(register_address.offset.has_value());
  EXPECT_EQ(register_address.offset->operation,
            ResolvedAddressOffsetOperator::Subtract);
  EXPECT_EQ(register_address.offset->value.bits, 8u);

  const auto& immediate_address =
      std::get<Ld::GenericScalar>(std::get<Ld>(body[3]).variant).address.value;
  const auto& immediate_base =
      std::get<ResolvedImmediate>(immediate_address.base);
  EXPECT_EQ(immediate_base.type, ScalarType::U64);
  EXPECT_EQ(immediate_base.bits, 240u);
}

TEST(ResolvedModule, ChecksGenericLoadAvailability) {
  PtxSyntaxParser parser("ld.u32 %r0, [%rd0+4];");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;
  const auto resolved = resolveInstruction(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& load = std::get<Ld>(*resolved);

  const auto rejected =
      checker::check(load, checker::Context{
                               .target =
                                   checker::TargetInfo{
                                       .ptx_version = checker::PtxVersion{1, 5},
                                       .sm_version = 10,
                                   },
                               .instruction_range = ast->range,
                           });
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  EXPECT_TRUE(
      checker::check(load,
                     checker::Context{
                         .target =
                             checker::TargetInfo{
                                 .ptx_version = checker::PtxVersion{2, 0},
                                 .sm_version = 20,
                             },
                         .instruction_range = ast->range,
                     })
          .has_value());
}

TEST(ResolvedModule, ChecksGenericLoadStoreAddressStateSpacePolicy) {
  const auto ast = parseModule(R"ptx(
.const .u32 constant_value;
.global .u32 global_value;
.entry kernel(.param .u32 input) {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  .local .u32 local_value;
  .shared .u32 shared_value;
  ld.u32 %r0, [global_value];
  ld.u32 %r0, [local_value];
  ld.u32 %r0, [shared_value];
  ld.u32 %r0, [constant_value];
  ld.u32 %r0, [input];
  ld.u32 %r0, [%rd0];
  st.u32 [global_value], %r0;
  st.u32 [local_value], %r0;
  st.u32 [shared_value], %r0;
  st.u32 [constant_value], %r0;
  st.u32 [input], %r0;
  st.u32 [%rd0], %r0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 12u);
  const checker::Context generic_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = ast.range,
  };

  for (const size_t index : {0u, 1u, 2u, 5u}) {
    EXPECT_TRUE(checker::check(std::get<Ld>(body[index]), generic_context)
                    .has_value());
  }
  for (const size_t index : {6u, 7u, 8u, 11u}) {
    EXPECT_TRUE(checker::check(std::get<St>(body[index]), generic_context)
                    .has_value());
  }

  auto const_context = generic_context;
  const_context.target.ptx_version = {3, 0};
  const auto old_const_load =
      checker::check(std::get<Ld>(body[3]), const_context);
  ASSERT_FALSE(old_const_load.has_value());
  ASSERT_EQ(old_const_load.error().size(), 1u);
  EXPECT_EQ(old_const_load.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(old_const_load.error().front().range,
            std::get<Ld::GenericScalar>(std::get<Ld>(body[3]).variant)
                .address.locs.front());
  const_context.target.ptx_version = {3, 1};
  EXPECT_TRUE(checker::check(std::get<Ld>(body[3]), const_context).has_value());

  const auto expect_mismatch = [&](const auto& instruction) {
    const auto checked = checker::check(instruction, generic_context);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  };
  expect_mismatch(std::get<Ld>(body[4]));
  expect_mismatch(std::get<St>(body[9]));
  expect_mismatch(std::get<St>(body[10]));
}

TEST(ResolvedModule, ResolvesGenericAndExplicitScalarLoadStoreForms) {
  const auto resolve_standalone = [](std::string_view source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.error().message;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::move(*resolved);
  };

  const auto generic_load = resolve_standalone("ld.u32 %r0, [%rd0];");
  const auto global_load =
      resolve_standalone("ld.global.u32 %r0, [%rd0];");
  const auto generic_store = resolve_standalone("st.u32 [%rd0], %r0;");
  const auto global_store =
      resolve_standalone("st.global.u32 [%rd0], %r0;");

  EXPECT_TRUE(
      std::holds_alternative<Ld::GenericScalar>(
          std::get<Ld>(generic_load).variant));
  EXPECT_TRUE(
      std::holds_alternative<Ld::ExplicitScalar>(
          std::get<Ld>(global_load).variant));
  EXPECT_TRUE(std::holds_alternative<St::GenericScalar>(
      std::get<St>(generic_store).variant));
  EXPECT_TRUE(std::holds_alternative<St::ExplicitScalar>(
      std::get<St>(global_store).variant));
  EXPECT_EQ(
      std::get<Ld::ExplicitScalar>(std::get<Ld>(global_load).variant)
          .state_space.value,
      MemoryStateSpace::Global);
  EXPECT_EQ(
      std::get<St::ExplicitScalar>(std::get<St>(global_store).variant)
          .state_space.value,
      MemoryStateSpace::Global);

  const checker::Context old_target{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
  };
  const auto generic_store_check =
      checker::check(std::get<St>(generic_store), old_target);
  ASSERT_FALSE(generic_store_check.has_value());
  ASSERT_EQ(generic_store_check.error().size(), 2u);
  EXPECT_EQ(generic_store_check.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(generic_store_check.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
}

TEST(ResolvedModule, ResolvesEveryLegalLoadStoreCacheOperator) {
  struct CacheCase {
    std::string_view spelling;
    CacheOperator value;
  };
  constexpr CacheCase load_cases[] = {
      {"ca", CacheOperator::Ca}, {"cg", CacheOperator::Cg},
      {"cs", CacheOperator::Cs}, {"lu", CacheOperator::Lu},
      {"cv", CacheOperator::Cv},
  };
  constexpr CacheCase store_cases[] = {
      {"wb", CacheOperator::Wb}, {"cg", CacheOperator::Cg},
      {"cs", CacheOperator::Cs}, {"wt", CacheOperator::Wt},
  };
  const auto resolve = [](const std::string& source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.error().message;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::pair{std::move(*ast), std::move(*resolved)};
  };
  const checker::Context context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
  };

  for (const auto& cache : load_cases) {
    SCOPED_TRACE(cache.spelling);
    auto [generic_ast, generic_instruction] =
        resolve(fmt::format("ld.{}.u32 %r0, [%rd0];", cache.spelling));
    const auto& generic =
        std::get<Ld::GenericScalar>(std::get<Ld>(generic_instruction).variant);
    EXPECT_EQ(generic.cache.value, cache.value);
    ASSERT_EQ(generic.cache.locs.size(), 1u);
    EXPECT_EQ(generic.cache.locs.front(), generic_ast.modifiers.front().syntax.range);
    EXPECT_TRUE(checker::check(std::get<Ld>(generic_instruction), context)
                    .has_value());

    auto [explicit_ast, explicit_instruction] = resolve(
        fmt::format("ld.global.{}.u32 %r0, [%rd0];", cache.spelling));
    const auto& explicit_load =
        std::get<Ld::ExplicitScalar>(std::get<Ld>(explicit_instruction).variant);
    EXPECT_EQ(explicit_load.cache.value, cache.value);
    ASSERT_EQ(explicit_load.cache.locs.size(), 1u);
    EXPECT_EQ(explicit_load.cache.locs.front(),
              explicit_ast.modifiers[1].syntax.range);
    EXPECT_TRUE(checker::check(std::get<Ld>(explicit_instruction), context)
                    .has_value());
  }

  for (const auto& cache : store_cases) {
    SCOPED_TRACE(cache.spelling);
    auto [generic_ast, generic_instruction] =
        resolve(fmt::format("st.{}.u32 [%rd0], %r0;", cache.spelling));
    const auto& generic =
        std::get<St::GenericScalar>(std::get<St>(generic_instruction).variant);
    EXPECT_EQ(generic.cache.value, cache.value);
    ASSERT_EQ(generic.cache.locs.size(), 1u);
    EXPECT_EQ(generic.cache.locs.front(), generic_ast.modifiers.front().syntax.range);
    EXPECT_TRUE(checker::check(std::get<St>(generic_instruction), context)
                    .has_value());

    auto [explicit_ast, explicit_instruction] = resolve(
        fmt::format("st.global.{}.u32 [%rd0], %r0;", cache.spelling));
    const auto& explicit_store =
        std::get<St::ExplicitScalar>(std::get<St>(explicit_instruction).variant);
    EXPECT_EQ(explicit_store.cache.value, cache.value);
    ASSERT_EQ(explicit_store.cache.locs.size(), 1u);
    EXPECT_EQ(explicit_store.cache.locs.front(),
              explicit_ast.modifiers[1].syntax.range);
    EXPECT_TRUE(checker::check(std::get<St>(explicit_instruction), context)
                    .has_value());
  }
}

TEST(ResolvedModule, ChecksExplicitCacheOperatorAvailabilityAndOmittedBaseline) {
  const auto resolve = [](std::string_view source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.error().message;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::pair{std::move(*ast), std::move(*resolved)};
  };
  const checker::Context old_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
  };
  const checker::Context supported_target{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
  };

  auto [cached_load_ast, cached_load_instruction] =
      resolve("ld.global.ca.u32 %r0, [%rd0];");
  const auto cached_load =
      checker::check(std::get<Ld>(cached_load_instruction), old_target);
  ASSERT_FALSE(cached_load.has_value());
  ASSERT_EQ(cached_load.error().size(), 2u);
  EXPECT_EQ(cached_load.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(cached_load.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(cached_load.error()[0].range,
            cached_load_ast.modifiers[1].syntax.range);
  EXPECT_TRUE(
      checker::check(std::get<Ld>(cached_load_instruction), supported_target)
          .has_value());

  auto [cached_store_ast, cached_store_instruction] =
      resolve("st.global.wb.u32 [%rd0], %r0;");
  const auto cached_store =
      checker::check(std::get<St>(cached_store_instruction), old_target);
  ASSERT_FALSE(cached_store.has_value());
  ASSERT_EQ(cached_store.error().size(), 2u);
  EXPECT_EQ(cached_store.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(cached_store.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(cached_store.error()[0].range,
            cached_store_ast.modifiers[1].syntax.range);
  EXPECT_TRUE(
      checker::check(std::get<St>(cached_store_instruction), supported_target)
          .has_value());

  auto [implicit_load_ast, implicit_load_instruction] =
      resolve("ld.global.u32 %r0, [%rd0];");
  (void)implicit_load_ast;
  EXPECT_TRUE(checker::check(std::get<Ld>(implicit_load_instruction), old_target)
                  .has_value());

  auto [implicit_store_ast, implicit_store_instruction] =
      resolve("st.global.u32 [%rd0], %r0;");
  (void)implicit_store_ast;
  EXPECT_TRUE(
      checker::check(std::get<St>(implicit_store_instruction), old_target)
          .has_value());
}

TEST(ResolvedModule, ResolvesAndChecksLoadStoreScalarTypeFamily) {
  struct ScalarCase {
    std::string_view spelling;
    ScalarType type;
  };
  constexpr ScalarCase scalar_cases[] = {
      {"b8", ScalarType::B8},   {"b16", ScalarType::B16},
      {"b32", ScalarType::B32}, {"b64", ScalarType::B64},
      {"u8", ScalarType::U8},   {"u16", ScalarType::U16},
      {"u32", ScalarType::U32}, {"u64", ScalarType::U64},
      {"s8", ScalarType::S8},   {"s16", ScalarType::S16},
      {"s32", ScalarType::S32}, {"s64", ScalarType::S64},
      {"f32", ScalarType::F32}, {"f64", ScalarType::F64},
  };
  const auto resolve = [](const std::string& source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.error().message;
    const SourceRange type_range = ast->modifiers.back().syntax.range;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::pair{std::move(*resolved), type_range};
  };
  const checker::Context context{
      .target = {.ptx_version = {3, 1}, .sm_version = 20},
  };

  for (const auto& scalar : scalar_cases) {
    SCOPED_TRACE(scalar.spelling);
    auto [generic_load, generic_load_type_range] = resolve(
        fmt::format("ld.{} %r0, [%rd0];", scalar.spelling));
    const auto& generic_load_variant =
        std::get<Ld::GenericScalar>(std::get<Ld>(generic_load).variant);
    EXPECT_EQ(generic_load_variant.type.value, scalar.type);
    ASSERT_EQ(generic_load_variant.type.locs.size(), 1u);
    EXPECT_EQ(generic_load_variant.type.locs.front(), generic_load_type_range);
    EXPECT_TRUE(checker::check(std::get<Ld>(generic_load), context).has_value());

    auto [explicit_load, explicit_load_type_range] = resolve(
        fmt::format("ld.global.{} %r0, [%rd0];", scalar.spelling));
    const auto& explicit_load_variant =
        std::get<Ld::ExplicitScalar>(std::get<Ld>(explicit_load).variant);
    EXPECT_EQ(explicit_load_variant.type.value, scalar.type);
    ASSERT_EQ(explicit_load_variant.type.locs.size(), 1u);
    EXPECT_EQ(explicit_load_variant.type.locs.front(),
              explicit_load_type_range);
    EXPECT_TRUE(
        checker::check(std::get<Ld>(explicit_load), context).has_value());

    auto [generic_store, generic_store_type_range] = resolve(
        fmt::format("st.{} [%rd0], %r0;", scalar.spelling));
    const auto& generic_store_variant =
        std::get<St::GenericScalar>(std::get<St>(generic_store).variant);
    EXPECT_EQ(generic_store_variant.type.value, scalar.type);
    ASSERT_EQ(generic_store_variant.type.locs.size(), 1u);
    EXPECT_EQ(generic_store_variant.type.locs.front(),
              generic_store_type_range);
    EXPECT_TRUE(
        checker::check(std::get<St>(generic_store), context).has_value());

    auto [explicit_store, explicit_store_type_range] = resolve(
        fmt::format("st.global.{} [%rd0], %r0;", scalar.spelling));
    const auto& explicit_store_variant =
        std::get<St::ExplicitScalar>(std::get<St>(explicit_store).variant);
    EXPECT_EQ(explicit_store_variant.type.value, scalar.type);
    ASSERT_EQ(explicit_store_variant.type.locs.size(), 1u);
    EXPECT_EQ(explicit_store_variant.type.locs.front(),
              explicit_store_type_range);
    EXPECT_TRUE(
        checker::check(std::get<St>(explicit_store), context).has_value());
  }
}

TEST(ResolvedModule, ChecksLoadStoreF64Availability) {
  const auto resolve = [](std::string_view source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.error().message;
    const SourceRange type_range = ast->modifiers.back().syntax.range;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::pair{std::move(*resolved), type_range};
  };

  auto [explicit_instruction, explicit_type_range] =
      resolve("ld.global.f64 %fd0, [%rd0];");
  const auto& explicit_load = std::get<Ld>(explicit_instruction);
  const checker::Context sm12_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 12},
  };
  const auto explicit_rejected = checker::check(explicit_load, sm12_context);
  ASSERT_FALSE(explicit_rejected.has_value());
  ASSERT_EQ(explicit_rejected.error().size(), 1u);
  EXPECT_EQ(explicit_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(explicit_rejected.error().front().range, explicit_type_range);

  auto sm13_context = sm12_context;
  sm13_context.target.sm_version = 13;
  EXPECT_TRUE(checker::check(explicit_load, sm13_context).has_value());

  auto [explicit_vector_instruction, explicit_vector_type_range] =
      resolve("ld.global.v2.f64 {%fd0, %fd1}, [%rd0];");
  const auto& explicit_vector_load = std::get<Ld>(explicit_vector_instruction);
  const auto explicit_vector_rejected =
      checker::check(explicit_vector_load, sm12_context);
  ASSERT_FALSE(explicit_vector_rejected.has_value());
  ASSERT_EQ(explicit_vector_rejected.error().size(), 1u);
  EXPECT_EQ(explicit_vector_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(explicit_vector_rejected.error().front().range,
            explicit_vector_type_range);
  EXPECT_TRUE(checker::check(explicit_vector_load, sm13_context).has_value());

  auto [generic_instruction, generic_type_range] =
      resolve("ld.f64 %fd0, [%rd0];");
  const auto& generic_load = std::get<Ld>(generic_instruction);
  const auto& generic_variant =
      std::get<Ld::GenericScalar>(generic_load.variant);
  ASSERT_EQ(generic_variant.type.locs.size(), 1u);
  EXPECT_EQ(generic_variant.type.locs.front(), generic_type_range);
  const checker::Context generic_sm13_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 13},
  };
  const auto generic_rejected =
      checker::check(generic_load, generic_sm13_context);
  ASSERT_FALSE(generic_rejected.has_value());
  ASSERT_EQ(generic_rejected.error().size(), 1u);
  EXPECT_EQ(generic_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  auto generic_sm20_context = generic_sm13_context;
  generic_sm20_context.target.sm_version = 20;
  EXPECT_TRUE(checker::check(generic_load, generic_sm20_context).has_value());
}

TEST(ResolvedModule, ChecksBoundLoadStoreRegisterWidthPolicy) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %u32_value;
  .reg .u64 %u64_value;
  .reg .b64 %b64_value;
  .reg .b128 %b128_value;
  .reg .f64 %f64_value;
  ld.u8 %u32_value, [%rd0];
  ld.global.s16 %u64_value, [%rd0];
  ld.b16 %f64_value, [%rd0];
  ld.global.f32 %b64_value, [%rd0];
  st.u8 [%rd0], %u32_value;
  st.global.s16 [%rd0], %u64_value;
  st.b16 [%rd0], %f64_value;
  st.global.f32 [%rd0], %b64_value;
  ld.u64 %u32_value, [%rd0];
  st.u64 [%rd0], %u32_value;
  ld.f32 %f64_value, [%rd0];
  st.f32 [%rd0], %f64_value;
  ld.u32 %f64_value, [%rd0];
  st.u32 [%rd0], %f64_value;
  ld.u32 %b128_value, [%rd0];
  st.u32 [%rd0], %b128_value;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 16u);
  const checker::Context context{
      .target = {.ptx_version = {8, 3}, .sm_version = 70},
      .instruction_range = ast.range,
  };

  for (size_t index = 0; index < 8; ++index) {
    const auto checked = std::visit(
        [&](const auto& instruction) { return checker::check(instruction, context); },
        body[index]);
    EXPECT_TRUE(checked.has_value());
  }

  for (size_t index = 8; index < body.size(); ++index) {
    const auto checked = std::visit(
        [&](const auto& instruction) { return checker::check(instruction, context); },
        body[index]);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksLegacyLoadStoreRegisterVectors) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.shared .u16 shared_value;
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %r<4>;
  .reg .u16 %h<4>;
  ld.v2.u32 {%r0, %r1}, [%rd0];
  ld.cg.v4.u16 {%h0, %h1, %h2, %h3}, [%rd0];
  ld.global.v2.u32 {%r0, %r1}, [global_value];
  ld.shared.v4.u16 {%h0, %h1, %h2, %h3}, [shared_value];
  st.v2.u32 [%rd0], {%r0, %r1};
  st.wt.v4.u16 [%rd0], {%h0, %h1, %h2, %h3};
  st.global.v2.u32 [global_value], {%r0, %r1};
  st.shared.v4.u16 [shared_value], {%h0, %h1, %h2, %h3};
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);

  const auto& generic_load =
      std::get<Ld::GenericVector>(std::get<Ld>(body[0]).variant);
  EXPECT_EQ(generic_load.vector.value, VectorArity::V2);
  EXPECT_EQ(generic_load.type.value, ScalarType::U32);
  EXPECT_FALSE(generic_load.vector.locs.empty());
  ASSERT_EQ(generic_load.dst.value.elements.size(), 2u);
  EXPECT_EQ(generic_load.dst.value.elements[0]->declared_type,
            ScalarType::U32);

  const auto& cached_load =
      std::get<Ld::GenericVector>(std::get<Ld>(body[1]).variant);
  EXPECT_EQ(cached_load.cache.value, CacheOperator::Cg);
  EXPECT_EQ(cached_load.vector.value, VectorArity::V4);

  const auto& explicit_load =
      std::get<Ld::ExplicitVector>(std::get<Ld>(body[2]).variant);
  EXPECT_EQ(explicit_load.state_space.value, MemoryStateSpace::Global);
  EXPECT_EQ(explicit_load.vector.value, VectorArity::V2);

  const auto& generic_store =
      std::get<St::GenericVector>(std::get<St>(body[4]).variant);
  EXPECT_EQ(generic_store.vector.value, VectorArity::V2);
  ASSERT_EQ(generic_store.src.value.elements.size(), 2u);
  EXPECT_EQ(generic_store.src.value.elements[1]->declared_type,
            ScalarType::U32);

  const auto& cached_store =
      std::get<St::GenericVector>(std::get<St>(body[5]).variant);
  EXPECT_EQ(cached_store.cache.value, CacheOperator::Wt);
  EXPECT_EQ(cached_store.vector.value, VectorArity::V4);

  const checker::Context current{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    const auto checked = std::visit(
        [&](const auto& value) { return checker::check(value, current); },
        instruction);
    EXPECT_TRUE(checked.has_value());
  }

  const checker::Context old_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast.range,
  };
  const auto old_generic = checker::check(std::get<Ld>(body[0]), old_target);
  ASSERT_FALSE(old_generic.has_value());
  EXPECT_EQ(old_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(checker::check(std::get<Ld>(body[2]), old_target).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[6]), old_target).has_value());
}

TEST(ResolvedModule, RejectsInvalidLegacyLoadStoreRegisterVectors) {
  const auto resolve_source = [](std::string_view instruction) {
    const std::string source = fmt::format(R"ptx(
.entry kernel() {{
  .reg .u64 %rd0;
  .reg .u32 %r<4>;
  .reg .u16 %h<4>;
  .reg .f32 %f<4>;
  {}
}}
)ptx",
                                           instruction);
    return resolveModule(parseModule(source));
  };

  const auto v8 = resolve_source("ld.v8.u32 {%r0, %r1}, [%rd0];");
  ASSERT_FALSE(v8.has_value());
  EXPECT_EQ(v8.error().front().message, "Unknown modifier '.v8'.");

  const auto scalar_load = resolve_source("ld.v2.u32 %r0, [%rd0];");
  ASSERT_FALSE(scalar_load.has_value());
  EXPECT_EQ(scalar_load.error().front().message,
            "Operands do not match any layout of instruction variant "
            "'GenericVector'.");

  const auto scalar_store = resolve_source("st.v2.u32 [%rd0], %r0;");
  ASSERT_FALSE(scalar_store.has_value());
  EXPECT_EQ(scalar_store.error().front().message,
            "Operands do not match any layout of instruction variant "
            "'GenericVector'.");

  const auto arity_mismatch =
      resolve_source("ld.v4.u32 {%r0, %r1}, [%rd0];");
  ASSERT_FALSE(arity_mismatch.has_value());
  EXPECT_EQ(arity_mismatch.error().front().message,
            "This vector operand requires 4 elements.");

  const auto overwide_load =
      resolve_source("ld.v4.u64 {%r0, %r1, %r2, %r3}, [%rd0];");
  ASSERT_FALSE(overwide_load.has_value());
  EXPECT_EQ(overwide_load.error().front().message,
            "This vector operand's payload width (256 bits) exceeds the "
            "supported 128 bit limit.");

  const auto sink = resolve_source("st.v2.u32 [%rd0], {%r0, _};");
  ASSERT_FALSE(sink.has_value());
  EXPECT_EQ(sink.error().front().message,
            "The '_' sink is allowed only in a destination vector.");

  const auto overwide_store =
      resolve_source("st.v4.u64 [%rd0], {%r0, %r1, %r2, %r3};");
  ASSERT_FALSE(overwide_store.has_value());
  EXPECT_EQ(overwide_store.error().front().message,
            "This vector operand's payload width (256 bits) exceeds the "
            "supported 128 bit limit.");

  EXPECT_TRUE(resolve_source("ld.v2.u16 {%r0, %r1}, [%rd0];").has_value());

  const auto narrow = resolve_source("ld.v2.u32 {%h0, %h1}, [%rd0];");
  ASSERT_FALSE(narrow.has_value());
  EXPECT_EQ(narrow.error().front().message,
            "Vector element '%h0' has type 'U16' incompatible with this "
            "instruction.");

  const auto kind_mismatch = resolve_source("ld.v2.u32 {%f0, %f1}, [%rd0];");
  ASSERT_FALSE(kind_mismatch.has_value());
  EXPECT_EQ(kind_mismatch.error().front().message,
            "Vector element '%f0' has type 'F32' incompatible with this "
            "instruction.");
}

TEST(ResolvedModule, KeepsNonMemoryRegisterWidthChecksStrict) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %narrow;
  .reg .u64 %wide;
  mov.u32 %wide, %narrow;
  add.u32 %wide, %narrow, %narrow;
  sub.u32 %wide, %narrow, %narrow;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const checker::Context context{
      .target = {.ptx_version = {3, 1}, .sm_version = 20},
      .instruction_range = ast.range,
  };

  for (const auto& instruction : body) {
    const auto checked = std::visit(
        [&](const auto& value) { return checker::check(value, context); },
        instruction);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksBasicExplicitAddressStateSpaces) {
  const auto ast = parseModule(R"ptx(
.const .u32 constant_value;
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  .local .u32 local_value;
  .shared .u32 shared_value;
  ld.const.u32 %r0, [constant_value];
  ld.global.u32 %r0, [global_value];
  ld.local.u32 %r0, [local_value];
  ld.shared.u32 %r0, [shared_value];
  st.global.u32 [global_value], %r0;
  st.local.u32 [local_value], %r0;
  st.shared.u32 [shared_value], %r0;
  ld.shared.u32 %r0, [global_value];
  ld.local.u32 %r0, [%rd0];
  st.shared.u32 [%rd0], %r0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  const auto& syntax_function =
      std::get<syntax_ast::AstFunction>(ast.items[2]);
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast.range,
  };

  constexpr std::array expected_load_spaces{
      MemoryStateSpace::Constant, MemoryStateSpace::Global,
      MemoryStateSpace::Local, MemoryStateSpace::Shared};
  for (size_t index = 0; index < expected_load_spaces.size(); ++index) {
    const auto& load = std::get<Ld>(body[index]);
    const auto& explicit_load = std::get<Ld::ExplicitScalar>(load.variant);
    EXPECT_EQ(explicit_load.state_space.value, expected_load_spaces[index]);
    ASSERT_EQ(explicit_load.state_space.locs.size(), 1u);
    const auto& syntax_instruction =
        std::get<syntax_ast::AstInstruction>(syntax_function.body[4 + index]);
    EXPECT_EQ(explicit_load.state_space.locs.front(),
              syntax_instruction.modifiers.front().syntax.range);
    EXPECT_TRUE(checker::check(load, context).has_value());
  }

  constexpr std::array expected_store_spaces{
      MemoryStateSpace::Global, MemoryStateSpace::Local,
      MemoryStateSpace::Shared};
  for (size_t offset = 0; offset < expected_store_spaces.size(); ++offset) {
    const auto& store = std::get<St>(body[4 + offset]);
    const auto& explicit_store = std::get<St::ExplicitScalar>(store.variant);
    EXPECT_EQ(explicit_store.state_space.value, expected_store_spaces[offset]);
    ASSERT_EQ(explicit_store.state_space.locs.size(), 1u);
    const auto& syntax_instruction = std::get<syntax_ast::AstInstruction>(
        syntax_function.body[8 + offset]);
    EXPECT_EQ(explicit_store.state_space.locs.front(),
              syntax_instruction.modifiers.front().syntax.range);
    EXPECT_TRUE(checker::check(store, context).has_value());
  }

  const auto& mismatch_load = std::get<Ld>(body[7]);
  const auto mismatch = checker::check(mismatch_load, context);
  ASSERT_FALSE(mismatch.has_value());
  ASSERT_EQ(mismatch.error().size(), 1u);
  EXPECT_EQ(mismatch.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  EXPECT_EQ(mismatch.error().front().range,
            std::get<Ld::ExplicitScalar>(mismatch_load.variant)
                .address.locs.front());

  // A register address does not carry a declaration-derived state space, so
  // the explicit qualifier is retained without inventing an effective space.
  EXPECT_TRUE(checker::check(std::get<Ld>(body[8]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[9]), context).has_value());
}

TEST(ResolvedModule, RejectsConstStoreModifier) {
  PtxSyntaxParser parser("st.const.u32 [%rd0], %r0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;
  const auto resolved = resolveInstruction(*ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().message, "Unknown modifier '.const'.");
}

TEST(ResolvedModule, RejectsParamAddressThroughExplicitGlobalLoad) {
  const auto ast = parseModule(R"ptx(
.func device(.param .u32 input) {
  .reg .u32 %r0;
  ld.global.u32 %r0, [input];
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& load =
      std::get<Ld>(resolved->functions.front().body.front());
  const auto checked = checker::check(
      load, checker::Context{
                .target = {.ptx_version = {1, 0}, .sm_version = 0},
                .instruction_range = ast.range,
            });
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
}

TEST(ResolvedModule, ChecksExplicitParameterAddressSemantics) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel(.param .u32 kernel_input) {
  .reg .u32 %r0;
  ld.param.u32 %r0, [kernel_input];
}
.func (.param .u32 result) device(.param .u32 input) {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  ld.param.u32 %r0, [input];
  ld.param.u32 %r0, [%rd0];
  st.param.u32 [result], %r0;
  st.param.u32 [%rd0], %r0;
  ld.param.u32 %r0, [result];
  st.param.u32 [input], %r0;
  ld.param.u32 %r0, [global_value];
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  ASSERT_EQ(resolved->functions[0].body.size(), 1u);
  ASSERT_EQ(resolved->functions[1].body.size(), 7u);

  const checker::Context old_context{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
      .instruction_range = ast.range,
  };
  const checker::Context supported_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = ast.range,
  };

  const auto& kernel_load =
      std::get<Ld>(resolved->functions[0].body.front());
  const auto& kernel_explicit =
      std::get<Ld::ExplicitScalar>(kernel_load.variant);
  EXPECT_EQ(kernel_explicit.state_space.value, MemoryStateSpace::Parameter);
  EXPECT_EQ(kernel_explicit.address.value.enclosing_function_kind,
            EnclosingFunctionKind::Entry);
  EXPECT_TRUE(checker::check(kernel_load, old_context).has_value());

  const auto& syntax_device =
      std::get<syntax_ast::AstFunction>(ast.items[2]);
  const auto& syntax_first_load =
      std::get<syntax_ast::AstInstruction>(syntax_device.body[2]);
  const auto& device_load = std::get<Ld>(resolved->functions[1].body[0]);
  const auto& device_explicit =
      std::get<Ld::ExplicitScalar>(device_load.variant);
  EXPECT_EQ(device_explicit.state_space.value, MemoryStateSpace::Parameter);
  ASSERT_EQ(device_explicit.state_space.locs.size(), 1u);
  EXPECT_EQ(device_explicit.state_space.locs.front(),
            syntax_first_load.modifiers.front().syntax.range);
  EXPECT_EQ(device_explicit.address.value.enclosing_function_kind,
            EnclosingFunctionKind::Device);
  const auto& input_symbol =
      std::get<ResolvedSymbolRef>(device_explicit.address.value.base);
  EXPECT_FALSE(input_symbol.address_availability.has_value());

  const auto expect_old_target = [&](const auto& instruction) {
    const auto checked = checker::check(instruction, old_context);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 2u);
    EXPECT_EQ(checked.error()[0].kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_EQ(checked.error()[1].kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(checker::check(instruction, supported_context).has_value());
  };
  expect_old_target(device_load);
  expect_old_target(std::get<Ld>(resolved->functions[1].body[1]));
  expect_old_target(std::get<St>(resolved->functions[1].body[2]));
  expect_old_target(std::get<St>(resolved->functions[1].body[3]));

  const auto expect_direction_mismatch = [&](const auto& instruction) {
    const auto checked = checker::check(instruction, old_context);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ParameterDirectionMismatch);
  };
  expect_direction_mismatch(std::get<Ld>(resolved->functions[1].body[4]));
  expect_direction_mismatch(std::get<St>(resolved->functions[1].body[5]));

  const auto wrong_space = checker::check(
      std::get<Ld>(resolved->functions[1].body[6]), old_context);
  ASSERT_FALSE(wrong_space.has_value());
  ASSERT_EQ(wrong_space.error().size(), 1u);
  EXPECT_EQ(wrong_space.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
}

TEST(ResolvedModule, ChecksStandaloneExplicitParameterAvailability) {
  const auto resolve_standalone = [](std::string_view source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.error().message;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::move(*resolved);
  };
  const auto load = resolve_standalone("ld.param.u32 %r0, [%rd0];");
  const auto store = resolve_standalone("st.param.u32 [%rd0], %r0;");
  const auto& resolved_load = std::get<Ld>(load);
  const auto& resolved_store = std::get<St>(store);
  EXPECT_EQ(std::get<Ld::ExplicitScalar>(resolved_load.variant)
                .address.value.enclosing_function_kind,
            EnclosingFunctionKind::Unknown);
  EXPECT_EQ(std::get<St::ExplicitScalar>(resolved_store.variant)
                .address.value.enclosing_function_kind,
            EnclosingFunctionKind::Unknown);

  const checker::Context old_context{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
  };
  EXPECT_TRUE(checker::check(resolved_load, old_context).has_value());
  const auto old_store = checker::check(resolved_store, old_context);
  ASSERT_FALSE(old_store.has_value());
  ASSERT_EQ(old_store.error().size(), 2u);
  EXPECT_EQ(old_store.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(old_store.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  auto supported_context = old_context;
  supported_context.target = {.ptx_version = {2, 0}, .sm_version = 20};
  EXPECT_TRUE(checker::check(resolved_store, supported_context).has_value());
}

TEST(ResolvedModule, RejectsNarrowStoreSourceRegisterType) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u16 %r0;
  st.global.u32 [%rd0], %r0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto checked = checker::check(
      std::get<St>(resolved->functions.front().body.front()),
      checker::Context{
          .target = {.ptx_version = {1, 0}, .sm_version = 0},
          .instruction_range = ast.range,
      });
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, RejectsUnbracketedStoreAddress) {
  PtxSyntaxParser parser("st.u32 %rd0, %r0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;
  const auto resolved = resolveInstruction(*ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      resolved.error().message,
      "Operands do not match any layout of instruction variant 'GenericScalar'.");
}

TEST(ResolvedModule, ResolvesMovRegisterImmediateAndSymbolOffsetSources) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u64 %rd<2>;
  mov.u32 %r0, %r1;
  mov.u32 %r2, 42;
  mov.u64 %rd0, global_value+8;
  mov.u64 %rd1, %clock64;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);

  const auto& register_source = std::get<ResolvedRegisterRef>(
      scalarMovOperands(std::get<Mov>(body[0])).src.value);
  EXPECT_EQ(register_source.spelling, "%r1");
  EXPECT_EQ(register_source.parameterized_index, 1u);
  EXPECT_EQ(register_source.declared_type, ScalarType::U32);

  const auto& immediate_source = std::get<ResolvedImmediate>(
      scalarMovOperands(std::get<Mov>(body[1])).src.value);
  EXPECT_EQ(immediate_source.type, ScalarType::U32);
  EXPECT_EQ(immediate_source.bits, 42u);

  const auto& address_source = std::get<ResolvedAddress>(
      scalarMovOperands(std::get<Mov>(body[2])).src.value);
  const auto& symbol = std::get<ResolvedSymbolRef>(address_source.base);
  ASSERT_TRUE(symbol.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*symbol.symbol_id).name, "global_value");
  ASSERT_TRUE(address_source.offset.has_value());
  EXPECT_EQ(address_source.offset->operation,
            ResolvedAddressOffsetOperator::Add);
  EXPECT_EQ(address_source.offset->value.type, ScalarType::S64);
  EXPECT_EQ(address_source.offset->value.bits, 8u);

  const auto& special_source = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(body[3])).src.value);
  EXPECT_EQ(special_source.spelling, "%clock64");
  EXPECT_EQ(special_source.id.kind,
            base::SpecialRegisterKind::Clock64);
  EXPECT_EQ(base::metadata(special_source.id).element_type,
            ScalarType::U64);
}

TEST(ResolvedModule, ChecksMovRegisterSourceType) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  mov.u32 %r0, %rd0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto checked =
      checker::check(mov, checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = checker::PtxVersion{9, 2},
                                      .sm_version = 120,
                                  },
                              .instruction_range = ast.range,
                          });
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ResolvesAndChecksMovScalarTypeFamilies) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b16 %bh0;
  .reg .u16 %uh0;
  .reg .s16 %sh0;
  .reg .b32 %b0;
  .reg .u32 %u0;
  .reg .s32 %s0;
  .reg .f32 %f0;
  .reg .b64 %bd0;
  .reg .f64 %fd0;
  mov.b16 %bh0, %uh0;
  mov.s16 %sh0, %bh0;
  mov.u16 %uh0, 65535;
  mov.b32 %b0, %u0;
  mov.s32 %s0, %b0;
  mov.u32 %u0, %s0;
  mov.f32 %f0, %b0;
  mov.b64 %bd0, %fd0;
  mov.f64 %fd0, %bd0;
  mov.f32 %f0, %u0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  const auto& first = std::get<Mov::Scalar>(std::get<Mov>(body[0]).variant);
  EXPECT_EQ(first.type.value, ScalarType::B16);
  const auto& immediate = std::get<ResolvedImmediate>(
      scalarMovOperands(std::get<Mov>(body[2])).src.value);
  EXPECT_EQ(immediate.type, ScalarType::U16);
  EXPECT_EQ(immediate.bits, 65535u);
  const auto& f64 = std::get<Mov>(body[8]);
  EXPECT_EQ(std::get<Mov::Scalar>(f64.variant).type.value, ScalarType::F64);

  const auto check_at = [&](const Mov& mov, uint32_t sm) {
    return checker::check(mov,
                          checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = checker::PtxVersion{9, 2},
                                      .sm_version = sm,
                                  },
                              .instruction_range = ast.range,
                          });
  };
  for (size_t index = 0; index < 9; ++index)
    EXPECT_TRUE(check_at(std::get<Mov>(body[index]), 13).has_value());

  const auto old_target = check_at(f64, 12);
  ASSERT_FALSE(old_target.has_value());
  ASSERT_EQ(old_target.error().size(), 1u);
  EXPECT_EQ(old_target.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto incompatible = check_at(std::get<Mov>(body[9]), 13);
  ASSERT_FALSE(incompatible.has_value());
  ASSERT_EQ(incompatible.error().size(), 1u);
  EXPECT_EQ(incompatible.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ResolvesAndChecksMovRegisterVectorPackAndUnpack) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u8 %b<4>;
  .reg .u16 %h<2>;
  .reg .b32 %r<2>;
  .reg .b64 %rd<2>;
  .reg .b128 %q0;
  mov.b32 %r0, {%h0, %h1};
  mov.b32 {%b0, %b1, %b2, %b3}, %r0;
  mov.b64 {%r0, _}, %rd0;
  mov.b128 %q0, {%rd0, %rd1};
  mov.b128 {%rd0, %rd1}, %q0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const auto& pack = packMovOperands(std::get<Mov>(body[0]));
  ASSERT_EQ(pack.src.value.elements.size(), 2u);
  ASSERT_TRUE(pack.src.value.elements[0].has_value());
  EXPECT_EQ(pack.src.value.elements[0]->spelling, "%h0");
  EXPECT_EQ(pack.src.value.elements[0]->declared_type, ScalarType::U16);

  const auto& unpack = unpackMovOperands(std::get<Mov>(body[2]));
  ASSERT_EQ(unpack.dst.value.elements.size(), 2u);
  ASSERT_TRUE(unpack.dst.value.elements[0].has_value());
  EXPECT_FALSE(unpack.dst.value.elements[1].has_value());

  const checker::Context current{
      .target =
          checker::TargetInfo{
              .ptx_version = {8, 3},
              .sm_version = 70,
          },
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(
        checker::check(std::get<Mov>(instruction), current).has_value());

  const auto b128_too_old =
      checker::check(std::get<Mov>(body[3]),
                     checker::Context{
                         .target = checker::TargetInfo{.ptx_version = {8, 2},
                                                       .sm_version = 70},
                         .instruction_range = ast.range,
                     });
  ASSERT_FALSE(b128_too_old.has_value());
  EXPECT_EQ(b128_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto b128_sm_too_old =
      checker::check(std::get<Mov>(body[3]),
                     checker::Context{
                         .target = checker::TargetInfo{.ptx_version = {8, 3},
                                                       .sm_version = 69},
                         .instruction_range = ast.range,
                     });
  ASSERT_FALSE(b128_sm_too_old.has_value());
  EXPECT_EQ(b128_sm_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  Mov corrupted = std::get<Mov>(body[0]);
  packMovOperands(corrupted).src.value.elements[0]->declared_type =
      ScalarType::U8;
  const auto corrupted_check = checker::check(corrupted, current);
  ASSERT_FALSE(corrupted_check.has_value());
  EXPECT_EQ(corrupted_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, RejectsInvalidMovRegisterVectorForms) {
  const auto resolve_source = [](std::string_view instruction) {
    const std::string source = fmt::format(R"ptx(
.entry kernel() {{
  .reg .u8 %b<4>;
  .reg .u16 %h<2>;
  .reg .b32 %r0;
  .reg .b128 %q0;
  {}
}}
)ptx",
                                           instruction);
    return resolveModule(parseModule(source));
  };

  const auto non_bit = resolve_source("mov.u32 %r0, {%h0, %h1};");
  ASSERT_FALSE(non_bit.has_value());
  EXPECT_EQ(non_bit.error().front().message,
            "A vector mov requires a bit-size instruction type.");

  const auto source_sink = resolve_source("mov.b32 %r0, {%h0, _};");
  ASSERT_FALSE(source_sink.has_value());
  EXPECT_EQ(source_sink.error().front().message,
            "The '_' sink is allowed only in a destination vector.");

  const auto bad_arity = resolve_source("mov.b32 %r0, {%b0, %b1, %b2};");
  ASSERT_FALSE(bad_arity.has_value());
  EXPECT_EQ(bad_arity.error().front().message,
            "A vector mov requires two or four elements.");

  const auto all_sinks = resolve_source("mov.b32 {_, _}, %r0;");
  ASSERT_FALSE(all_sinks.has_value());
  EXPECT_EQ(all_sinks.error().front().message,
            "A destination vector must contain at least one register.");

  const auto scalar_b128 = resolve_source("mov.b128 %q0, %q0;");
  ASSERT_FALSE(scalar_b128.has_value());
  EXPECT_EQ(scalar_b128.error().front().message,
            "The .b128 mov type is available only for vector pack or unpack "
            "forms.");

  const auto sub_byte = resolve_source("mov.b16 %r0, {%b0, %b1, %b2, %b3};");
  ASSERT_FALSE(sub_byte.has_value());
  EXPECT_EQ(sub_byte.error().front().message,
            "Vector mov elements must be at least eight bits wide.");
}

TEST(ResolvedModule, ChecksLegacySpecialRegisterMoveWidths) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u16 %h0;
  .reg .u32 %r0;
  mov.u16 %h0, %tid.x;
  mov.u32 %r0, %tid.x;
  mov.u16 %h0, %gridid;
  mov.u32 %r0, %gridid;
  mov.u16 %h0, %laneid;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const auto& tid = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(body[0])).src.value);
  const auto& current_tid = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(body[1])).src.value);
  EXPECT_EQ(tid.id, current_tid.id);
  EXPECT_EQ(tid.component, current_tid.component);
  const auto tid_info = base::metadata(tid.id);
  EXPECT_EQ(tid_info.element_type, ScalarType::U32);
  EXPECT_EQ(tid_info.minimum_ptx_major, 2u);
  EXPECT_EQ(tid_info.minimum_ptx_minor, 0u);

  const auto check_at = [&](size_t index, checker::PtxVersion version) {
    return checker::check(std::get<Mov>(body[index]),
                          checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = version,
                                      .sm_version = 10,
                                  },
                              .instruction_range = ast.range,
                          });
  };
  EXPECT_TRUE(check_at(0, {1, 0}).has_value());
  EXPECT_TRUE(check_at(1, {2, 0}).has_value());
  EXPECT_TRUE(check_at(2, {1, 0}).has_value());
  EXPECT_TRUE(check_at(3, {1, 3}).has_value());

  const auto current_tid_too_old = check_at(1, {1, 9});
  ASSERT_FALSE(current_tid_too_old.has_value());
  ASSERT_EQ(current_tid_too_old.error().size(), 1u);
  EXPECT_EQ(current_tid_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto gridid_too_old = check_at(3, {1, 2});
  ASSERT_FALSE(gridid_too_old.has_value());
  ASSERT_EQ(gridid_too_old.error().size(), 1u);
  EXPECT_EQ(gridid_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto laneid_narrow = check_at(4, {9, 2});
  ASSERT_FALSE(laneid_narrow.has_value());
  ASSERT_EQ(laneid_narrow.error().size(), 1u);
  EXPECT_EQ(laneid_narrow.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, RejectsSixteenBitMovAddress) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u16 %h0;
  mov.u16 %h0, global_value;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "A data address requires a 32-bit or 64-bit integer or bit-size "
            "mov type.");
}

TEST(ResolvedModule, ResolvesAndChecksPredicateMove) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p<2>;
  mov.pred %p0, %p1;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto& predicate = std::get<Mov::Pred>(mov.variant);
  EXPECT_FALSE(predicate.dst.value.negated);
  EXPECT_FALSE(predicate.src.value.negated);
  ASSERT_TRUE(predicate.dst.value.register_ref.symbol_id.has_value());
  ASSERT_TRUE(predicate.src.value.register_ref.symbol_id.has_value());
  EXPECT_EQ(
      resolved->symbols.symbol(*predicate.dst.value.register_ref.symbol_id)
          .name,
      "%p");
  EXPECT_EQ(predicate.dst.value.register_ref.parameterized_index, 0u);
  EXPECT_EQ(predicate.src.value.register_ref.parameterized_index, 1u);

  EXPECT_TRUE(
      checker::check(mov,
                     checker::Context{
                         .target =
                             checker::TargetInfo{
                                 .ptx_version = checker::PtxVersion{1, 0},
                                 .sm_version = 0,
                             },
                         .instruction_range = ast.range,
                     })
          .has_value());

  PtxSyntaxParser parser("mov.pred %p0, %p1;");
  const auto standalone_ast = parser.parseInstruction();
  ASSERT_TRUE(standalone_ast.has_value()) << standalone_ast.error().message;
  const auto standalone = resolveInstruction(*standalone_ast);
  ASSERT_TRUE(standalone.has_value()) << standalone.error().message;
  const auto& standalone_predicate =
      std::get<Mov::Pred>(std::get<Mov>(*standalone).variant);
  EXPECT_FALSE(
      standalone_predicate.dst.value.register_ref.symbol_id.has_value());
  EXPECT_FALSE(
      standalone_predicate.src.value.register_ref.symbol_id.has_value());
}

TEST(ResolvedModule, ResolvesU32MovSymbolAddressSource) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  mov.u32 %r0, global_value;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& source = std::get<ResolvedSymbolRef>(
      scalarMovOperands(std::get<Mov>(resolved->functions.front().body.front()))
          .src.value);
  EXPECT_EQ(source.spelling, "global_value");
  EXPECT_EQ(source.declaration_state_space, syntax_ast::AstStateSpace::Global);
  EXPECT_EQ(source.address_state_space, syntax_ast::AstStateSpace::Global);
}

TEST(ResolvedModule, ResolvesKernelAndDeviceFunctionParameterAddresses) {
  const auto ast = parseModule(R"ptx(
.entry kernel(.param .u32 kernel_input) {
  .reg .u32 %r0;
  mov.u32 %r0, kernel_input+4;
}
.func (.param .u32 result) device(.param .u32 input) {
  .reg .u64 %rd<2>;
  mov.u64 %rd0, input;
  mov.u64 %rd1, result;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);

  const auto& kernel_address = std::get<ResolvedAddress>(
      scalarMovOperands(std::get<Mov>(resolved->functions[0].body.front()))
          .src.value);
  const auto& kernel_parameter =
      std::get<ResolvedSymbolRef>(kernel_address.base);
  EXPECT_EQ(kernel_parameter.declaration_kind,
            binding::SymbolKind::InputParameter);
  EXPECT_EQ(kernel_parameter.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(kernel_parameter.address_state_space,
            syntax_ast::AstStateSpace::Parameter);
  ASSERT_TRUE(kernel_address.offset.has_value());
  EXPECT_EQ(kernel_address.offset->value.bits, 4u);

  const auto& device_input = std::get<ResolvedSymbolRef>(
      scalarMovOperands(std::get<Mov>(resolved->functions[1].body[0]))
          .src.value);
  EXPECT_EQ(device_input.declaration_kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(device_input.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(device_input.address_state_space, syntax_ast::AstStateSpace::Local);
  ASSERT_TRUE(device_input.address_availability.has_value());
  EXPECT_EQ(device_input.address_availability->minimum_ptx_version,
            (checker::PtxVersion{2, 0}));
  EXPECT_EQ(device_input.address_availability->minimum_sm_version, 20u);

  const auto& return_parameter = std::get<ResolvedSymbolRef>(
      scalarMovOperands(std::get<Mov>(resolved->functions[1].body[1]))
          .src.value);
  EXPECT_EQ(return_parameter.declaration_kind,
            binding::SymbolKind::ReturnParameter);
  EXPECT_EQ(return_parameter.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(return_parameter.address_state_space,
            syntax_ast::AstStateSpace::Local);
  ASSERT_TRUE(return_parameter.address_availability.has_value());
  EXPECT_EQ(return_parameter.address_availability->minimum_ptx_version,
            (checker::PtxVersion{6, 0}));
  EXPECT_EQ(return_parameter.address_availability->minimum_sm_version, 20u);
}

TEST(ResolvedModule, ChecksDeviceParameterAddressAvailability) {
  const auto ast = parseModule(R"ptx(
.func (.param .u32 result) device(.param .u32 input) {
  .reg .u64 %rd<2>;
  mov.u64 %rd0, input;
  mov.u64 %rd1, result+4;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& input_mov = std::get<Mov>(resolved->functions.front().body[0]);
  const auto& return_mov = std::get<Mov>(resolved->functions.front().body[1]);

  const auto input_rejected =
      checker::check(input_mov, checker::Context{
                                    .target =
                                        checker::TargetInfo{
                                            .ptx_version = {1, 5},
                                            .sm_version = 10,
                                        },
                                    .instruction_range = ast.range,
                                });
  ASSERT_FALSE(input_rejected.has_value());
  ASSERT_EQ(input_rejected.error().size(), 2u);
  EXPECT_EQ(input_rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(input_rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(checker::check(input_mov,
                             checker::Context{
                                 .target =
                                     checker::TargetInfo{
                                         .ptx_version = {2, 0},
                                         .sm_version = 20,
                                     },
                                 .instruction_range = ast.range,
                             })
                  .has_value());

  const auto return_rejected =
      checker::check(return_mov, checker::Context{
                                     .target =
                                         checker::TargetInfo{
                                             .ptx_version = {5, 0},
                                             .sm_version = 10,
                                         },
                                     .instruction_range = ast.range,
                                 });
  ASSERT_FALSE(return_rejected.has_value());
  ASSERT_EQ(return_rejected.error().size(), 2u);
  EXPECT_EQ(return_rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(return_rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(checker::check(return_mov,
                             checker::Context{
                                 .target =
                                     checker::TargetInfo{
                                         .ptx_version = {6, 0},
                                         .sm_version = 20,
                                     },
                                 .instruction_range = ast.range,
                             })
                  .has_value());
}

TEST(ResolvedModule, ResolvesAndChecksFunctionAddresses) {
  const auto ast = parseModule(R"ptx(
.func device() {}
.entry launched() {}
.entry caller() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  mov.u32 %r0, device;
  mov.u64 %rd0, launched;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 3u);
  const auto& device_mov = std::get<Mov>(resolved->functions[2].body[0]);
  const auto& kernel_mov = std::get<Mov>(resolved->functions[2].body[1]);

  const auto& device =
      std::get<ResolvedFunctionRef>(scalarMovOperands(device_mov).src.value);
  ASSERT_TRUE(device.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*device.symbol_id).name, "device");
  EXPECT_FALSE(device.is_entry);
  EXPECT_FALSE(device.address_availability.has_value());
  EXPECT_TRUE(checker::check(device_mov,
                             checker::Context{
                                 .target =
                                     checker::TargetInfo{
                                         .ptx_version = {1, 0},
                                         .sm_version = 0,
                                     },
                                 .instruction_range = ast.range,
                             })
                  .has_value());

  const auto& kernel =
      std::get<ResolvedFunctionRef>(scalarMovOperands(kernel_mov).src.value);
  ASSERT_TRUE(kernel.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*kernel.symbol_id).name, "launched");
  EXPECT_TRUE(kernel.is_entry);
  ASSERT_TRUE(kernel.address_availability.has_value());
  EXPECT_EQ(kernel.address_availability->minimum_ptx_version,
            (checker::PtxVersion{3, 1}));
  EXPECT_EQ(kernel.address_availability->minimum_sm_version, 35u);

  const auto rejected =
      checker::check(kernel_mov, checker::Context{
                                     .target =
                                         checker::TargetInfo{
                                             .ptx_version = {3, 0},
                                             .sm_version = 30,
                                         },
                                     .instruction_range = ast.range,
                                 });
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(checker::check(kernel_mov,
                             checker::Context{
                                 .target =
                                     checker::TargetInfo{
                                         .ptx_version = {3, 1},
                                         .sm_version = 35,
                                     },
                                 .instruction_range = ast.range,
                             })
                  .has_value());
}

TEST(ResolvedModule, PreservesDirectDeviceParameterAddressSpace) {
  const auto ast = parseModule(R"ptx(
.func device(.param .u32 input) {
  .reg .u32 %r0;
  ld.u32 %r0, [input];
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& address =
      std::get<Ld::GenericScalar>(
          std::get<Ld>(resolved->functions.front().body.front()).variant)
          .address.value;
  const auto& parameter = std::get<ResolvedSymbolRef>(address.base);
  EXPECT_EQ(parameter.declaration_kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(parameter.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(parameter.address_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_FALSE(parameter.address_availability.has_value());
}

TEST(ResolvedModule, RejectsLocallyScopedParamVariableAddress) {
  const auto ast = parseModule(R"ptx(
.func device() {
  .param .align 8 .b8 call_argument[8];
  .reg .u64 %rd0;
  mov.u64 %rd0, call_argument;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Symbol 'call_argument' is not an addressable data symbol.");
}

TEST(ResolvedModule, KeepsStandaloneAddressAndSymbolIdentityOpen) {
  PtxSyntaxParser mov_parser("mov.u64 %rd0, global_value;");
  const auto mov_ast = mov_parser.parseInstruction();
  ASSERT_TRUE(mov_ast.has_value()) << mov_ast.error().message;
  const auto mov_resolved = resolveInstruction(*mov_ast);
  ASSERT_TRUE(mov_resolved.has_value()) << mov_resolved.error().message;
  const auto& symbol = std::get<ResolvedSymbolRef>(
      scalarMovOperands(std::get<Mov>(*mov_resolved)).src.value);
  EXPECT_EQ(symbol.spelling, "global_value");
  EXPECT_FALSE(symbol.symbol_id.has_value());
  EXPECT_FALSE(symbol.declaration_kind.has_value());
  EXPECT_FALSE(symbol.declaration_state_space.has_value());
  EXPECT_FALSE(symbol.address_state_space.has_value());

  PtxSyntaxParser load_parser("ld.u32 %r0, [%rd0+4];");
  const auto load_ast = load_parser.parseInstruction();
  ASSERT_TRUE(load_ast.has_value()) << load_ast.error().message;
  const auto load_resolved = resolveInstruction(*load_ast);
  ASSERT_TRUE(load_resolved.has_value()) << load_resolved.error().message;
  const auto& address =
      std::get<Ld::GenericScalar>(std::get<Ld>(*load_resolved).variant)
          .address.value;
  const auto& base = std::get<ResolvedRegisterRef>(address.base);
  EXPECT_EQ(base.spelling, "%rd0");
  EXPECT_FALSE(base.symbol_id.has_value());
}

TEST(ResolvedModule, RejectsUnbracketedLoadAddress) {
  PtxSyntaxParser parser("ld.u32 %r0, %rd0+4;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;
  const auto invalid_address = resolveInstruction(*ast);
  ASSERT_FALSE(invalid_address.has_value());
  EXPECT_EQ(invalid_address.error().message,
            "Expected a bracketed address operand.");
}

TEST(ResolvedModule, RejectsNonRegisterSymbolsInRegisterOperands) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  .local .u32 value;
  add.u32 %dst, value, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Symbol 'value' is not a .reg variable.");
}

TEST(ResolvedModule, CheckerUsesDeclarationBoundRegisterTypes) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %wide;
  .reg .u32 %r<2>;
  add.u32 %wide, %r0, %r1;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const ResolvedFunction& function = resolved->functions.front();
  const Add& add = std::get<Add>(function.body.front());
  const auto& syntax_instruction =
      std::get<syntax_ast::AstFunction>(ast.items.front()).body.back();

  const auto result = checker::check(
      add,
      checker::Context{
          .target =
              checker::TargetInfo{
                  .ptx_version = checker::PtxVersion{9, 0},
                  .sm_version = 100,
              },
          .instruction_range =
              std::get<syntax_ast::AstInstruction>(syntax_instruction).range,
      });

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(result.error().front().message,
            "Register operand 'dst' has declared type 'U64' but instruction "
            "type source 'type' is 'U32'.");
}

TEST(ResolvedModule, BindsPredicateOperandsByDeclarationType) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  bar.red.and.pred %condition, 0, !%condition;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& bar = std::get<Bar>(resolved->functions.front().body.front());
  const auto& reduction = std::get<Bar::RedAndPred>(bar.variant);
  const auto& operands =
      std::get<Bar::RedAndPred::WithoutThreadCountOperands>(reduction.operands);
  ASSERT_TRUE(operands.dst.value.register_ref.symbol_id.has_value());
  EXPECT_EQ(operands.dst.value.register_ref.symbol_id,
            operands.predicate.value.register_ref.symbol_id);
  EXPECT_EQ(operands.dst.value.register_ref.declared_type, ScalarType::Pred);
  EXPECT_TRUE(operands.predicate.value.negated);
}

TEST(ResolvedModule, PreservesAndBindsExecutionPredicate) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  .reg .u32 %r<3>;
  @!%condition add.u32 %r0, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& add = std::get<Add>(resolved->functions.front().body.front());
  ASSERT_TRUE(add.execution_predicate.has_value());
  const ResolvedPredicate& predicate = add.execution_predicate->value;
  EXPECT_TRUE(predicate.negated);
  EXPECT_EQ(predicate.register_ref.spelling, "%condition");
  EXPECT_EQ(predicate.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(predicate.register_ref.declared_type, ScalarType::Pred);
  ASSERT_TRUE(predicate.register_ref.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*predicate.register_ref.symbol_id).name,
            "%condition");

  const auto& function = std::get<syntax_ast::AstFunction>(ast.items.front());
  const auto& instruction =
      std::get<syntax_ast::AstInstruction>(function.body.back());
  ASSERT_TRUE(instruction.predicate.has_value());
  ASSERT_EQ(add.execution_predicate->locs.size(), 1u);
  EXPECT_EQ(add.execution_predicate->locs.front(),
            instruction.predicate->range);
}

TEST(ResolvedModule, RejectsNonPredicateExecutionGuard) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %condition;
  .reg .u32 %r<3>;
  @%condition add.u32 %r0, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Expected a predicate register, but '%condition' is declared "
            "'.u32'.");
}

TEST(ResolvedModule, ResolvesPredicatedDirectBranchTarget) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  @!%condition bra.uni done;
done:
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 1u);
  const auto& bra = std::get<Bra>(resolved->functions.front().body.front());
  ASSERT_TRUE(bra.execution_predicate.has_value());
  EXPECT_TRUE(bra.execution_predicate->value.negated);

  const auto& direct = std::get<Bra::Direct>(bra.variant);
  EXPECT_TRUE(direct.uni.value);
  EXPECT_EQ(direct.target.value.spelling, "done");
  ASSERT_TRUE(direct.target.value.symbol_id.has_value());
  const binding::Symbol& label =
      resolved->symbols.symbol(*direct.target.value.symbol_id);
  EXPECT_EQ(label.kind, binding::SymbolKind::Label);
  EXPECT_EQ(label.name, "done");
  const binding::Symbol& function_symbol =
      resolved->symbols.symbol(resolved->functions.front().symbol_id);
  ASSERT_TRUE(function_symbol.owned_scope.has_value());
  EXPECT_EQ(label.scope, *function_symbol.owned_scope);
  ASSERT_EQ(direct.target.locs.size(), 1u);

  const checker::Context check_context{
      .target =
          checker::TargetInfo{
              .ptx_version = checker::PtxVersion{1, 0},
              .sm_version = 0,
          },
      .instruction_range = direct.target.locs.front(),
  };
  const auto checked = checker::check(bra, check_context);
  EXPECT_TRUE(checked.has_value());
}

TEST(ResolvedModule, StandaloneBranchTargetRemainsUnbound) {
  PtxSyntaxParser parser("bra target;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;

  const auto resolved = resolveInstruction(*ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& bra = std::get<Bra>(*resolved);
  EXPECT_FALSE(bra.execution_predicate.has_value());
  const auto& direct = std::get<Bra::Direct>(bra.variant);
  EXPECT_FALSE(direct.uni.value);
  EXPECT_TRUE(direct.uni.locs.empty());
  EXPECT_EQ(direct.target.value.spelling, "target");
  EXPECT_FALSE(direct.target.value.symbol_id.has_value());
}

TEST(ResolvedModule, StandaloneResolutionRemainsDeclarationFree) {
  PtxSyntaxParser parser("@!%p7 add.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;

  const auto resolved = resolveInstruction(*ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& instruction = std::get<Add>(*resolved);
  ASSERT_TRUE(instruction.execution_predicate.has_value());
  EXPECT_TRUE(instruction.execution_predicate->value.negated);
  EXPECT_EQ(instruction.execution_predicate->value.register_ref.index, 7u);
  EXPECT_FALSE(instruction.execution_predicate->value.register_ref.symbol_id
                   .has_value());
  const Add::IntegerNoSat& add = resolvedIntegerAdd(*resolved);
  EXPECT_FALSE(add.dst.value.symbol_id.has_value());
  EXPECT_FALSE(add.dst.value.declared_type.has_value());
  EXPECT_EQ(add.dst.value.index, 0u);
}

TEST(ResolvedModule, RunsDeclarationSemanticsBeforeInstructionResolution) {
  const auto ast = parseModule(R"ptx(
.global .u32 values[2] = {1, 2, 3};
.entry kernel() { ret; }
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Initializer dimension contains 3 elements but its declared "
            "extent is 2.");
}

TEST(ResolvedModule, ResolvesACompatibleFunctionDefinitionScope) {
  const auto ast = parseModule(R"ptx(
.func helper(.reg .u32 input);
.func helper(.reg .u32 input) {
  .reg .u32 %result;
  .reg .u32 %source;
  add.u32 %result, %source, 1;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  EXPECT_TRUE(resolved->functions.front().is_prototype);
  EXPECT_TRUE(resolved->functions.front().body.empty());
  EXPECT_FALSE(resolved->functions.back().is_prototype);
  ASSERT_EQ(resolved->functions.back().body.size(), 1u);
  const auto& add =
      std::get<Add>(resolved->functions.back().body.front()).variant;
  const auto& integer = std::get<Add::IntegerNoSat>(add);
  EXPECT_TRUE(integer.dst.value.symbol_id.has_value());
  EXPECT_TRUE(
      std::get<ResolvedRegisterRef>(integer.src1.value).symbol_id.has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

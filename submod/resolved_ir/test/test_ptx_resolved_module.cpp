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
  EXPECT_EQ(special.id.kind, special_registers::SpecialRegisterKind::LaneId);
  EXPECT_FALSE(special.component.has_value());
  const auto special_info = special_registers::metadata(special.id);
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
  EXPECT_EQ(component.id.kind, special_registers::SpecialRegisterKind::Tid);
  EXPECT_EQ(component.component, special_registers::VectorComponent::X);
  const auto component_info = special_registers::metadata(component.id);
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
      std::get<Ld::GenericU32>(std::get<Ld>(body[1]).variant).address.value;
  const auto& symbol_base = std::get<ResolvedSymbolRef>(symbol_address.base);
  EXPECT_EQ(symbol_base.symbol_id, mov_symbol.symbol_id);
  ASSERT_TRUE(symbol_address.offset.has_value());
  EXPECT_EQ(symbol_address.offset->operation,
            ResolvedAddressOffsetOperator::Add);
  EXPECT_EQ(symbol_address.offset->value.type, ScalarType::S64);
  EXPECT_EQ(symbol_address.offset->value.bits, 4u);

  const auto& register_address =
      std::get<Ld::GenericU32>(std::get<Ld>(body[2]).variant).address.value;
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
      std::get<Ld::GenericU32>(std::get<Ld>(body[3]).variant).address.value;
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
            special_registers::SpecialRegisterKind::Clock64);
  EXPECT_EQ(special_registers::metadata(special_source.id).element_type,
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

TEST(ResolvedModule, ResolvesAndChecksMovVectorPackAndUnpack) {
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

TEST(ResolvedModule, RejectsInvalidMovVectorForms) {
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
  const auto tid_info = special_registers::metadata(tid.id);
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
      std::get<Ld::GenericU32>(
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
      std::get<Ld::GenericU32>(std::get<Ld>(*load_resolved).variant)
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

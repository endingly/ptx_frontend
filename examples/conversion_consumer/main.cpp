#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** PTX source exercising public conversion, ownership, and validation APIs. */
constexpr std::string_view kFixture = R"ptx(
.version 9.3
.target sm_121a
.address_size 64
.shared .align 8 .u64 cvta_shared_value;
.global .align 4 .b32 atomic_value;
.global .align 8 .b64 atomic_value_64;
.global .align 16 .b8 atomic_vector_value[16];
.visible .entry conversion_consumer(
    .param .u64 .ptr .const .align 8 constant_pointer) {
  .reg .pred %p<2>;
  .reg .u32 %r<2>;
  .reg .u32 %u0;
  .reg .u64 %rd0;
  .reg .u64 %mbar_addr;
  .reg .s32 %s<3>;
  .reg .b32 %b<5>;
  .reg .u64 %uq<2>;
  .reg .s64 %sq<2>;
  .reg .b64 %bq<3>;
  .reg .f32 %f<4>;
  .reg .f64 %fd<2>;
  .reg .b16 %h<2>;
  .reg .b64 %policy;

  isspacep.shared::cluster %p0, %r0;
  cvta.shared::cluster.u64 %rd0, cvta_shared_value+8;
  cvta.to.const.u64 %rd0, %rd0;
  prmt.b32.rc16 %b0, %b1, %b2, %b3;
  prmt.b32.rc16 %b4, %b1, %b2, 0x1;
  cvt.pack.sat.u2.s32.b32 %u0, %s0, %s1, 0x12345678;
  cvt.rmi.s32.f32 %s2, %f0;
  cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.f32 %h0, %f2, %f3, %h1;
  testp.normal.f32 %p0, %f1;
  copysign.f32 %f0, %f1, %f2;
  sin.approx.ftz.f32 %f0, %f1;
  ex2.approx.ftz.bf16 %h0, %h1;
  min.ftz.NaN.xorsign.abs.f32 %f0, %f1, %f2;
  min.ftz.NaN.abs.f32 %f0, %f1, %f2, %f3;
  max.xorsign.abs.f32 %f0, %f1, %f2;
  max.abs.f32 %f0, %f1, %f2, %f3;
  add.f32.f16 %f0, %h1, %f1;
  add.f32.f16 %f0, %h1, 1.0;
  sub.f32.bf16 %f0, %h1, %f1;
  sub.f32.bf16 %f0, %h1, 2.0;
  set.nan.xor.f32.f32 %f0, %f1, %f2, !0;
  selp.s32 %s0, %s1, -1, !%p0;
  selp.u32 %u0, %u0, 0, 2;
  set.num.xor.ftz.f16x2.f16x2 %b3, %b1, %b2, !0;
  set.eq.bf16.f16 %h0, %h1, %h1;
  slct.u32.s32 %u0, %u0, 0, -1;
  slct.ftz.u64.f32 %rd0, %rd0, %rd0, -0.0;
  atom.relaxed.cta.global.add.u32 %u0, [atomic_value], %u0;
  red.relaxed.cta.global.add.u32 [atomic_value], 1;
  atom.global.cas.b32 %b0, [atomic_value], 1, %b1;
  atom.relaxed.cta.global.cas.b32 %b0, [atomic_value], %b1, 2;
  atom.global.inc.u32 %u0, [atomic_value], %u0;
  atom.relaxed.cta.global.exch.b32 %b0, [atomic_value], 1;
  red.global.xor.b32 [atomic_value], %b1;
  atom.global.add.u64 %uq0, [atomic_value_64], %uq1;
  atom.global.relaxed.cta.min.s64 %sq0, [atomic_value_64], 1;
  atom.relaxed.cta.global.cas.b64 %bq0, [atomic_value_64], %bq1, 2;
  red.global.relaxed.cta.xor.b64 [atomic_value_64], %bq1;
  atom.global.add.f32 %f0, [atomic_value], %b0;
  red.global.relaxed.cta.add.f32 [atomic_value], 0f3f800000;
  atom.relaxed.cta.global.add.f64 %fd0, [atomic_value_64], %bq1;
  red.global.add.f64 [atomic_value_64], 1.0;
  atom.global.v2.f16.add.noftz.L2::cache_hint {_, %h1},
      [atomic_vector_value], {%h0, %h1}, %policy;
  red.global.v2.f16.add.noftz.L2::cache_hint
      [atomic_vector_value], {%h0, %h1}, %policy;
  red.async.relaxed.cluster.shared::cluster.mbarrier::complete_tx::bytes.add.u32
      [%rd0], %r0, [%mbar_addr];
  red.async.mmio.release.sys.global.add.u64 [%rd0], %uq0;
  ret;
}
)ptx";

namespace ir = ptx_frontend::resolved_ir;

/** Report a failed public-contract check in both Debug and Release builds. */
bool require(bool condition, std::string_view description) {
  if (!condition)
    std::cerr << "consumer contract failed: " << description << '\n';
  return condition;
}

/** Check that owned validation rejects a deliberate public-IR mutation. */
bool rejectsMutation(const ir::ResolvedModule& module,
                     ir::checker::CheckDiagnosticKind expected,
                     std::string_view description) {
  const auto result = ir::validateModule(
      module, ir::ModuleValidationPolicy::RequireCompleteContext);
  const bool matched = !result && !result.error().empty() &&
                       result.error().front().kind == expected;
  if (!matched && !result && !result.error().empty())
    std::cerr << "observed validation diagnostic: "
              << result.error().front().message << '\n';
  return require(matched, description);
}

/** Inspect the typed instruction contract after all syntax owners are gone. */
bool checkExtendedContract(ir::ResolvedModule& module) {
  if (!require(module.functions.size() == 1, "one owned function"))
    return false;
  auto& body = module.functions.front().body;
  if (!require(body.size() == 47, "all conversion and atomic instructions"))
    return false;

  auto* atom_instruction = std::get_if<ir::Atom>(&body[27]);
  auto* atom =
      atom_instruction
          ? std::get_if<ir::Atom::GlobalAddU32>(&atom_instruction->variant)
          : nullptr;
  auto* red_instruction = std::get_if<ir::Red>(&body[28]);
  auto* red =
      red_instruction
          ? std::get_if<ir::Red::GlobalAddU32>(&red_instruction->variant)
          : nullptr;
  auto* legacy_cas_instruction = std::get_if<ir::Atom>(&body[29]);
  auto* legacy_cas = legacy_cas_instruction
                         ? std::get_if<ir::Atom::GlobalCasB32>(
                               &legacy_cas_instruction->variant)
                         : nullptr;
  auto* modern_cas_instruction = std::get_if<ir::Atom>(&body[30]);
  auto* modern_cas = modern_cas_instruction
                         ? std::get_if<ir::Atom::GlobalCasB32>(
                               &modern_cas_instruction->variant)
                         : nullptr;
  if (!require(
          atom && red && legacy_cas && modern_cas &&
              std::get<0>(atom->operands).dst.value.register_ref.has_value() &&
              legacy_cas->dst.value.register_ref.has_value() &&
              modern_cas->dst.value.register_ref.has_value() &&
              std::holds_alternative<ir::ResolvedRegisterRef>(
                  std::get<0>(atom->operands).src.value) &&
              std::holds_alternative<ir::ResolvedImmediate>(
                  std::get<0>(red->operands).src.value) &&
              std::holds_alternative<ir::ResolvedImmediate>(
                  legacy_cas->compare.value) &&
              std::holds_alternative<ir::ResolvedRegisterRef>(
                  legacy_cas->swap.value) &&
              std::holds_alternative<ir::ResolvedRegisterRef>(
                  modern_cas->compare.value) &&
              std::holds_alternative<ir::ResolvedImmediate>(
                  modern_cas->swap.value),
          "owned atomic register and immediate sources"))
    return false;

  const auto* inc_instruction = std::get_if<ir::Atom>(&body[31]);
  const auto* inc =
      inc_instruction
          ? std::get_if<ir::Atom::GlobalIncU32>(&inc_instruction->variant)
          : nullptr;
  const auto* exch_instruction = std::get_if<ir::Atom>(&body[32]);
  const auto* exch =
      exch_instruction
          ? std::get_if<ir::Atom::GlobalExchB32>(&exch_instruction->variant)
          : nullptr;
  const auto* xor_instruction = std::get_if<ir::Red>(&body[33]);
  const auto* xor_red =
      xor_instruction
          ? std::get_if<ir::Red::GlobalXorB32>(&xor_instruction->variant)
          : nullptr;
  if (!require(inc && exch && xor_red &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       std::get<0>(inc->operands).src.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       std::get<0>(exch->operands).src.value) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       std::get<0>(xor_red->operands).src.value),
               "owned expanded atomic and reduction variants"))
    return false;

  const auto* add_64_instruction = std::get_if<ir::Atom>(&body[34]);
  const auto* add_64 =
      add_64_instruction
          ? std::get_if<ir::Atom::GlobalAddU64>(&add_64_instruction->variant)
          : nullptr;
  const auto* min_64_instruction = std::get_if<ir::Atom>(&body[35]);
  const auto* min_64 =
      min_64_instruction
          ? std::get_if<ir::Atom::GlobalMinS64>(&min_64_instruction->variant)
          : nullptr;
  const auto* cas_64_instruction = std::get_if<ir::Atom>(&body[36]);
  const auto* cas_64 =
      cas_64_instruction
          ? std::get_if<ir::Atom::GlobalCasB64>(&cas_64_instruction->variant)
          : nullptr;
  const auto* red_64_instruction = std::get_if<ir::Red>(&body[37]);
  const auto* red_64 =
      red_64_instruction
          ? std::get_if<ir::Red::GlobalXorB64>(&red_64_instruction->variant)
          : nullptr;
  if (!require(add_64 && min_64 && cas_64 && red_64 &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       std::get<0>(add_64->operands).src.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       std::get<0>(min_64->operands).src.value) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       cas_64->compare.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       cas_64->swap.value) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       std::get<0>(red_64->operands).src.value),
               "owned 64-bit atomic and reduction variants"))
    return false;

  const auto* float_atom = std::get_if<ir::Atom>(&body[38]);
  const auto* float_red = std::get_if<ir::Red>(&body[39]);
  const auto* double_atom = std::get_if<ir::Atom>(&body[40]);
  const auto* double_red = std::get_if<ir::Red>(&body[41]);
  if (!require(float_atom && float_red && double_atom && double_red &&
                   std::holds_alternative<ir::Atom::GlobalAddF32>(
                       float_atom->variant) &&
                   std::holds_alternative<ir::Red::GlobalAddF32>(
                       float_red->variant) &&
                   std::holds_alternative<ir::Atom::GlobalAddF64>(
                       double_atom->variant) &&
                   std::holds_alternative<ir::Red::GlobalAddF64>(
                       double_red->variant),
               "owned float atomic and reduction variants"))
    return false;

  const auto* vector_atom_instruction = std::get_if<ir::Atom>(&body[42]);
  const auto* vector_atom = vector_atom_instruction
                                ? std::get_if<ir::Atom::VectorAddNoftzF16>(
                                      &vector_atom_instruction->variant)
                                : nullptr;
  const auto* vector_red_instruction = std::get_if<ir::Red>(&body[43]);
  const auto* vector_red = vector_red_instruction
                               ? std::get_if<ir::Red::VectorAddNoftzF16>(
                                     &vector_red_instruction->variant)
                               : nullptr;
  if (!require(vector_atom && vector_red &&
                   vector_atom->vector.value == ir::VectorArity::V2 &&
                   vector_red->vector.value == ir::VectorArity::V2 &&
                   vector_atom->cache_hint.value &&
                   vector_red->cache_hint.value &&
                   !std::get<1>(vector_atom->operands)
                        .dst.value.elements.front()
                        .has_value() &&
                   std::get<1>(vector_atom->operands)
                           .cache_policy.value.declared_type ==
                       ptx_frontend::base::ScalarType::B64,
               "owned vector atomic and reduction policy layout"))
    return false;

  const auto* shared_async = std::get_if<ir::Red>(&body[44]);
  const auto* release_async = std::get_if<ir::Red>(&body[45]);
  if (!require(shared_async && release_async &&
                   std::holds_alternative<ir::Red::AsyncSharedAddU32>(
                       shared_async->variant) &&
                   std::holds_alternative<ir::Red::AsyncReleaseAddU64>(
                       release_async->variant) &&
                   shared_async->address_qualifier.value ==
                       ir::AtomicAddressQualifier::SharedCluster &&
                   release_async->address_qualifier.value ==
                       ir::AtomicAddressQualifier::Global &&
                   std::get<ir::Red::AsyncReleaseAddU64>(release_async->variant)
                       .mmio.value,
               "owned asynchronous reduction modes"))
    return false;

  auto* testp = std::get_if<ir::Testp>(&body[8]);
  auto* property =
      testp ? std::get_if<ir::Testp::F32>(&testp->variant) : nullptr;
  if (!require(property && property->property.value ==
                               ptx_frontend::base::TestProperty::Normal,
               "typed testp.normal.f32 property"))
    return false;
  const auto* copysign = std::get_if<ir::Copysign>(&body[9]);
  if (!require(copysign &&
                   std::holds_alternative<ir::Copysign::F32>(copysign->variant),
               "typed copysign.f32 variant"))
    return false;
  const auto* sin = std::get_if<ir::Sin>(&body[10]);
  const auto* sin_f32 =
      sin ? std::get_if<ir::Sin::ApproxF32>(&sin->variant) : nullptr;
  if (!require(sin_f32 && sin_f32->ftz.value,
               "typed transcendental approximation and FTZ"))
    return false;
  const auto* ex2 = std::get_if<ir::Ex2>(&body[11]);
  if (!require(
          ex2 && std::holds_alternative<ir::Ex2::ApproxFtzBf16>(ex2->variant),
          "typed BF16 transcendental variant"))
    return false;

  auto* min_binary_instruction = std::get_if<ir::Min>(&body[12]);
  auto* min_ternary_instruction = std::get_if<ir::Min>(&body[13]);
  auto* max_binary_instruction = std::get_if<ir::Max>(&body[14]);
  auto* max_ternary_instruction = std::get_if<ir::Max>(&body[15]);
  auto* min_binary =
      min_binary_instruction
          ? std::get_if<ir::Min::F32>(&min_binary_instruction->variant)
          : nullptr;
  auto* min_ternary =
      min_ternary_instruction
          ? std::get_if<ir::Min::F32>(&min_ternary_instruction->variant)
          : nullptr;
  auto* max_binary =
      max_binary_instruction
          ? std::get_if<ir::Max::F32>(&max_binary_instruction->variant)
          : nullptr;
  auto* max_ternary =
      max_ternary_instruction
          ? std::get_if<ir::Max::F32>(&max_ternary_instruction->variant)
          : nullptr;
  if (!require(
          min_binary && min_ternary && max_binary && max_ternary &&
              min_binary->operand_layout == ir::ResolvedOperandLayoutTag{0} &&
              min_ternary->operand_layout == ir::ResolvedOperandLayoutTag{1} &&
              max_binary->operand_layout == ir::ResolvedOperandLayoutTag{0} &&
              max_ternary->operand_layout == ir::ResolvedOperandLayoutTag{1} &&
              std::holds_alternative<ir::Min::F32::BinaryOperands>(
                  min_binary->operands) &&
              std::holds_alternative<ir::Min::F32::TernaryOperands>(
                  min_ternary->operands) &&
              std::holds_alternative<ir::Max::F32::BinaryOperands>(
                  max_binary->operands) &&
              std::holds_alternative<ir::Max::F32::TernaryOperands>(
                  max_ternary->operands) &&
              min_binary->ftz.value && min_binary->nan.value &&
              min_binary->xorsign_abs.value && !min_binary->abs.value &&
              min_ternary->ftz.value && min_ternary->nan.value &&
              !min_ternary->xorsign_abs.value && min_ternary->abs.value &&
              max_binary->xorsign_abs.value && !max_binary->abs.value &&
              !max_ternary->xorsign_abs.value && max_ternary->abs.value,
          "typed MIN/MAX modifier and operand layouts"))
    return false;

  auto* add_register_instruction = std::get_if<ir::Add>(&body[16]);
  auto* add_immediate_instruction = std::get_if<ir::Add>(&body[17]);
  auto* sub_register_instruction = std::get_if<ir::Sub>(&body[18]);
  auto* sub_immediate_instruction = std::get_if<ir::Sub>(&body[19]);
  auto* add_register =
      add_register_instruction
          ? std::get_if<ir::Add::MixedF32>(&add_register_instruction->variant)
          : nullptr;
  auto* add_immediate =
      add_immediate_instruction
          ? std::get_if<ir::Add::MixedF32>(&add_immediate_instruction->variant)
          : nullptr;
  auto* sub_register =
      sub_register_instruction
          ? std::get_if<ir::Sub::MixedF32>(&sub_register_instruction->variant)
          : nullptr;
  auto* sub_immediate =
      sub_immediate_instruction
          ? std::get_if<ir::Sub::MixedF32>(&sub_immediate_instruction->variant)
          : nullptr;
  if (!require(add_register && add_immediate && sub_register && sub_immediate &&
                   add_register->input_type.value ==
                       ptx_frontend::base::ScalarType::F16 &&
                   add_immediate->input_type.value ==
                       ptx_frontend::base::ScalarType::F16 &&
                   sub_register->input_type.value ==
                       ptx_frontend::base::ScalarType::BF16 &&
                   sub_immediate->input_type.value ==
                       ptx_frontend::base::ScalarType::BF16,
               "typed mixed ADD/SUB variants"))
    return false;
  const auto* addend_register =
      std::get_if<ir::ResolvedRegisterRef>(&add_register->addend.value);
  const auto* subtrahend_register =
      std::get_if<ir::ResolvedRegisterRef>(&sub_register->subtrahend.value);
  const auto* addend_immediate =
      std::get_if<ir::ResolvedImmediate>(&add_immediate->addend.value);
  const auto* subtrahend_immediate =
      std::get_if<ir::ResolvedImmediate>(&sub_immediate->subtrahend.value);
  if (!require(
          addend_register && addend_register->spelling == "%f1" &&
              addend_register->declared_type ==
                  ptx_frontend::base::ScalarType::F32 &&
              subtrahend_register && subtrahend_register->spelling == "%f1" &&
              subtrahend_register->declared_type ==
                  ptx_frontend::base::ScalarType::F32 &&
              addend_immediate &&
              addend_immediate->type == ptx_frontend::base::ScalarType::F32 &&
              addend_immediate->bits == 0x3F800000u && subtrahend_immediate &&
              subtrahend_immediate->type ==
                  ptx_frontend::base::ScalarType::F32 &&
              subtrahend_immediate->bits == 0x40000000u,
          "mixed register and floating-immediate values"))
    return false;

  auto* set_instruction = std::get_if<ir::Set>(&body[20]);
  auto* set_float =
      set_instruction
          ? std::get_if<ir::Set::FloatBoolean>(&set_instruction->variant)
          : nullptr;
  auto* selp_scalar_instruction = std::get_if<ir::Selp>(&body[21]);
  auto* selp_scalar =
      selp_scalar_instruction
          ? std::get_if<ir::Selp::Scalar>(&selp_scalar_instruction->variant)
          : nullptr;
  auto* selp_u32_instruction = std::get_if<ir::Selp>(&body[22]);
  auto* selp_u32 =
      selp_u32_instruction
          ? std::get_if<ir::Selp::U32>(&selp_u32_instruction->variant)
          : nullptr;
  if (!require(
          set_float &&
              set_float->comparison.value ==
                  ptx_frontend::base::ComparisonOperator::Nan &&
              set_float->boolean.value ==
                  ptx_frontend::base::BooleanOperator::Xor &&
              std::get<ir::ResolvedPredicateConstant>(set_float->combine.value)
                  .value &&
              selp_scalar &&
              selp_scalar->type.value == ptx_frontend::base::ScalarType::S32 &&
              std::get<ir::ResolvedPredicate>(selp_scalar->predicate.value)
                  .negated &&
              selp_u32 &&
              std::get<ir::ResolvedPredicateConstant>(selp_u32->predicate.value)
                  .value,
          "typed SET and both SELP public alternatives"))
    return false;

  auto* set_half_instruction = std::get_if<ir::Set>(&body[23]);
  auto* set_half = set_half_instruction
                       ? std::get_if<ir::Set::HalfNativeF16x2Boolean>(
                             &set_half_instruction->variant)
                       : nullptr;
  auto* set_bfloat_instruction = std::get_if<ir::Set>(&body[24]);
  auto* set_bfloat =
      set_bfloat_instruction
          ? std::get_if<ir::Set::HalfBf16F16>(&set_bfloat_instruction->variant)
          : nullptr;
  if (!require(
          set_half && set_half->ftz.value &&
              set_half->comparison.value ==
                  ptx_frontend::base::ComparisonOperator::Num &&
              std::get<ir::ResolvedPredicateConstant>(set_half->combine.value)
                  .value &&
              set_bfloat &&
              set_bfloat->comparison.value ==
                  ptx_frontend::base::ComparisonOperator::Eq &&
              set_bfloat->dst.value.declared_type ==
                  ptx_frontend::base::ScalarType::B16,
          "typed half/bfloat SET alternatives and owned operands"))
    return false;

  auto* slct_integer_instruction = std::get_if<ir::Slct>(&body[25]);
  auto* slct_integer =
      slct_integer_instruction
          ? std::get_if<ir::Slct::S32>(&slct_integer_instruction->variant)
          : nullptr;
  auto* slct_floating_instruction = std::get_if<ir::Slct>(&body[26]);
  auto* slct_floating =
      slct_floating_instruction
          ? std::get_if<ir::Slct::F32>(&slct_floating_instruction->variant)
          : nullptr;
  if (!require(slct_integer &&
                   slct_integer->dtype.value ==
                       ptx_frontend::base::ScalarType::U32 &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       slct_integer->src_false.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       slct_integer->selector.value) &&
                   slct_floating && slct_floating->ftz.value &&
                   slct_floating->dtype.value ==
                       ptx_frontend::base::ScalarType::U64 &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       slct_floating->selector.value),
               "typed SLCT selector variants and owned numeric operands"))
    return false;

  min_binary->abs.value = true;
  const bool forbidden_binary_modifier = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierNotAllowedForLayout,
      "binary MIN forbids ternary abs modifier");
  min_binary->abs.value = false;
  if (!forbidden_binary_modifier)
    return false;
  max_ternary->xorsign_abs.value = true;
  const bool forbidden_ternary_modifier = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierNotAllowedForLayout,
      "ternary MAX forbids binary xorsign modifier");
  max_ternary->xorsign_abs.value = false;
  if (!forbidden_ternary_modifier)
    return false;
  const auto original_abs = min_ternary->abs;
  min_ternary->abs = ptx_frontend::WithLocs<bool>{false};
  min_ternary->operand_layout = ir::ResolvedOperandLayoutTag{0};
  const bool mismatched_layout = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::OperandLayoutPayloadMismatch,
      "MIN rejects a layout tag/payload mismatch");
  min_ternary->operand_layout = ir::ResolvedOperandLayoutTag{1};
  min_ternary->abs = original_abs;
  if (!mismatched_layout)
    return false;
  property->property.value = ptx_frontend::base::TestProperty::Invalid;
  const bool invalid_property = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "TESTP rejects an invalid typed property");
  property->property.value = ptx_frontend::base::TestProperty::Normal;
  if (!invalid_property)
    return false;
  set_float->comparison.value = ptx_frontend::base::ComparisonOperator::Lo;
  const bool invalid_set_domain = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "SET rejects a comparison outside its floating domain");
  set_float->comparison.value = ptx_frontend::base::ComparisonOperator::Nan;
  if (!invalid_set_domain)
    return false;
  set_half->comparison.value = ptx_frontend::base::ComparisonOperator::Lo;
  const bool invalid_half_domain = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "half SET rejects an integer-only comparison");
  set_half->comparison.value = ptx_frontend::base::ComparisonOperator::Num;
  if (!invalid_half_domain)
    return false;
  slct_floating->dtype.value = ptx_frontend::base::ScalarType::F16;
  const bool invalid_slct_data_type = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "SLCT rejects an unsupported data type");
  slct_floating->dtype.value = ptx_frontend::base::ScalarType::U64;
  if (!invalid_slct_data_type)
    return false;
  selp_scalar->type.value = ptx_frontend::base::ScalarType::F16;
  const bool invalid_selp_domain = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "SELP rejects a type outside its scalar domain");
  selp_scalar->type.value = ptx_frontend::base::ScalarType::S32;
  return invalid_selp_domain &&
         require(ir::validateModule(
                     module, ir::ModuleValidationPolicy::RequireCompleteContext)
                     .has_value(),
                 "restored owned module validates");
}

/**
 * Resolve a module while parser state is alive, then validate the owned model
 * after the source, parser, and syntax AST have left scope.
 */
int runOwnedValidation() {
  /// Owns resolved values after parser-owned state has been destroyed.
  std::optional<ptx_frontend::resolved_ir::ResolvedModule> owned;
  {
    std::string source{kFixture};
    ptx_frontend::PtxSyntaxParser parser{source};
    auto parsed = parser.parseModule();
    if (!parsed) {
      std::cerr << "parseModule failed with " << parsed.diagnostics.size()
                << " diagnostic(s)\n";
      return 1;
    }

    auto resolved = ptx_frontend::resolved_ir::resolveModuleOnly(*parsed);
    if (!resolved) {
      std::cerr << "resolveModuleOnly failed with " << resolved.error().size()
                << " diagnostic(s)\n";
      for (const auto& diagnostic : resolved.error())
        std::cerr << diagnostic.message << '\n';
      return 2;
    }

    owned.emplace(std::move(*resolved));
  }

  if (!require(owned.has_value(), "resolved module survived parser lifetime"))
    return 3;
  const auto checked = ptx_frontend::resolved_ir::validateModule(
      *owned, ptx_frontend::resolved_ir::ModuleValidationPolicy::
                  RequireCompleteContext);
  if (!checked) {
    std::cerr << "owned validateModule failed with " << checked.error().size()
              << " diagnostic(s)\n";
    for (const auto& diagnostic : checked.error())
      std::cerr << diagnostic.message << '\n';
    return 4;
  }
  if (!checkExtendedContract(*owned))
    return 5;
  std::cout << "conversion consumer passed\n";
  return 0;
}

}  // namespace

/** Check the installed public conversion, comparison, and resolved-IR contract. */
int main() {
  using ptx_frontend::base::RoundingMode;
  using ptx_frontend::base::ScalarType;

  static_assert(ScalarType::F16x2 != ScalarType::Invalid);
  static_assert(ScalarType::BF16x2 != ScalarType::Invalid);
  static_assert(ScalarType::U2 != ScalarType::Invalid);
  static_assert(ScalarType::S2 != ScalarType::Invalid);
  static_assert(ScalarType::U4 != ScalarType::Invalid);
  static_assert(ScalarType::S4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m1x2 != ScalarType::Invalid);
  static_assert(ScalarType::E2m3x2 != ScalarType::Invalid);
  static_assert(ScalarType::E3m2x2 != ScalarType::Invalid);
  static_assert(ScalarType::E4m3x4 != ScalarType::Invalid);
  static_assert(ScalarType::E5m2x4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m1x4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m3x4 != ScalarType::Invalid);
  static_assert(ScalarType::E3m2x4 != ScalarType::Invalid);
  static_assert(ScalarType::UE8M0x2 != ScalarType::Invalid);
  static_assert(ScalarType::S2f6x2 != ScalarType::Invalid);
  static_assert(RoundingMode::Rni != RoundingMode::Invalid);
  static_assert(RoundingMode::Rmi != RoundingMode::Invalid);
  static_assert(RoundingMode::Rpi != RoundingMode::Invalid);
  static_assert(RoundingMode::Rna != RoundingMode::Invalid);
  static_assert(RoundingMode::Rs != RoundingMode::Invalid);

  return runOwnedValidation();
}

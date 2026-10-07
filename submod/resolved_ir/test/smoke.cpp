#include <array>
#include <iostream>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <stdexcept>
#include <vector>

using namespace ptx_frontend;
namespace ir = ptx_frontend::resolved_ir;
namespace ir = ptx_frontend::resolved_ir;
namespace ck = ir::checker;
namespace {
/** Fail in every configuration, including NDEBUG builds. */
void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}
/** Parse a standalone instruction retaining source locations. */
syntax_ast::AstInstruction parse(std::string_view text) {
  PtxSyntaxParser parser(text);
  auto ast = parser.parseInstruction();
  require(ast.has_value(), "parse failed");
  return std::move(*ast);
}
/** Resolve directly to the concrete heap object. */
std::unique_ptr<ir::Instruction> resolve(std::string_view text) {
  auto result = ir::resolveAdd(parse(text));
  if (!result)
    throw std::runtime_error(result.error().message);
  return std::move(*result);
}
/** Return a target admitting every current Add form. */
ck::Context context() {
  static constexpr std::array<std::string_view, 1> families{"sm_120f"};
  return {.target = {.ptx_version = {9, 3},
                     .sm_version = 120,
                     .enabled_family_features = families}};
}
/** Check exact leading diagnostic order. */
void first_error(const ck::CheckResult& result, ck::CheckDiagnosticKind kind) {
  require(!result && !result.error().empty(), "expected diagnostic");
  require(result.error().front().kind == kind, "unexpected diagnostic order");
}
/** Copy observations during the visit so borrowed payloads never escape. */
struct References : ir::detail::IReferenceObserver {
  /** Copy of each spelling, in traversal order. */
  std::vector<std::string> spellings;
  /** Copy of the first location of each borrowed field. */
  std::vector<SourceRange> ranges;
  /** Verify reference metadata while retaining owned copies. */
  void record(std::string spelling, std::span<const SourceRange> locations,
              ck::AddressSymbolResolutionPolicy policy) {
    require(
        policy == ck::AddressSymbolResolutionPolicy::PreserveDeclarationSpace,
        "reference policy changed");
    require(!locations.empty(), "reference locations lost");
    spellings.push_back(std::move(spelling));
    ranges.push_back(locations.front());
  }
  /** Consume an execution predicate synchronously. */
  void predicate(const ir::ResolvedPredicate& value,
                 std::span<const SourceRange> locations,
                 ck::AddressSymbolResolutionPolicy policy) override {
    record(value.register_ref.spelling, locations, policy);
  }
  /** Consume a register synchronously. */
  void reg(const ir::ResolvedRegisterRef& value,
           std::span<const SourceRange> locations,
           ck::AddressSymbolResolutionPolicy policy) override {
    record(value.spelling, locations, policy);
  }
  /** Consume the foundation register/immediate value domain synchronously. */
  void reg_or_imm(const ir::RegOrImm& value,
                  std::span<const SourceRange> locations,
                  ck::AddressSymbolResolutionPolicy policy) override {
    const auto* r = std::get_if<ir::ResolvedRegisterRef>(&value);
    record(r ? r->spelling : "immediate", locations, policy);
  }
};
/** Observe only execution guards through the optional interface method. */
struct PredicateOnlyObserver : ir::detail::IReferenceObserver {
  /** Number of predicate events observed during this visit. */
  std::size_t count{};
  /** Check the guard's borrowed value, location, and resolution policy. */
  void predicate(const ir::ResolvedPredicate& value,
                 std::span<const SourceRange> locations,
                 ck::AddressSymbolResolutionPolicy policy) override {
    require(value.register_ref.spelling == "%p0", "predicate value changed");
    require(!locations.empty(), "predicate locations lost");
    require(
        policy == ck::AddressSymbolResolutionPolicy::PreserveDeclarationSpace,
        "predicate policy changed");
    ++count;
  }
};
/** Exercise all exact identities, virtual checking, cloning and references. */
void all_forms() {
  const std::array cases{
      std::pair{"add.f32 %f0, %f1, %f2;", ir::InstructionKind::AddFloatF32},
      std::pair{"add.rp.f32x2 %r0, %r1, %r2;",
                ir::InstructionKind::AddFloatF32x2},
      std::pair{"add.rm.f64 %fd0, %fd1, %fd2;",
                ir::InstructionKind::AddFloatF64},
      std::pair{"add.rn.ftz.sat.f16x2 %r0, %r1, %r2;",
                ir::InstructionKind::AddHalf},
      std::pair{"add.bf16 %r0, %r1, %r2;", ir::InstructionKind::AddBfloat},
      std::pair{"add.rz.f32.bf16.sat %f0, %h1, %f2;",
                ir::InstructionKind::AddMixedF32},
      std::pair{"add.u32 %r0, %r1, 7;", ir::InstructionKind::AddIntegerNoSat},
      std::pair{"add.sat.s32 %r0, %r1, %r2;", ir::InstructionKind::AddSat},
      std::pair{"add.sat.u8x4 %r0, %r1, %r2;",
                ir::InstructionKind::AddPackedOptionalSat},
      std::pair{"add.cc.u32 %r0, %r1, %r2;", ir::InstructionKind::AddCc32},
      std::pair{"add.cc.u64 %rd0, %rd1, %rd2;", ir::InstructionKind::AddCc64}};
  for (auto [text, kind] : cases) {
    auto instruction = resolve(text);
    require(instruction->instruction_kind() == kind, "wrong identity");
    require(instruction->opcode_kind() == ir::Opcode::Add &&
                instruction->opcode_name() == "add",
            "opcode mapping failed");
    auto checked = instruction->check(context());
    if (!checked)
      throw std::runtime_error(std::string(text) + ": " +
                               checked.error().front().message);
    auto clone = instruction->clone();
    require(
        clone.get() != instruction.get() && clone->instruction_kind() == kind,
        "clone identity/allocation failed");
    require(clone->check(context()).has_value(), "clone check failed");
    References refs;
    clone->visit_references(refs);
    require(refs.spellings.size() == 3, "operand traversal failed");
  }
  static_assert(ir::AddCc32::condition_code_effect ==
                ir::ConditionCodeEffect::CarryOut);
  static_assert(ir::AddFloatF32::condition_code_effect ==
                ir::ConditionCodeEffect::None);
}
/** Exercise selection errors, availability, mutable types, and layout identity. */
void negative_cases() {
  for (auto text : {"sub.u32 %r0, %r1, %r2;", "add.invalid %r0, %r1, %r2;",
                    "add.u32.u32 %r0, %r1, %r2;", "add.f16 %h0, %h1, 1.0;"})
    require(!ir::resolveAdd(parse(text)), "invalid input resolved");
  auto packed = resolve("add.sat.u8x4 %r0, %r1, %r2;");
  auto old = context();
  old.target.ptx_version = {9, 1};
  old.target.sm_version = 100;
  auto unavailable = packed->check(old);
  first_error(unavailable, ck::CheckDiagnosticKind::UnsupportedPtxVersion);
  require(unavailable.error().size() == 2 &&
              unavailable.error()[1].kind ==
                  ck::CheckDiagnosticKind::UnsupportedSmVersion,
          "availability order changed");
  auto no_family = context();
  no_family.target.enabled_family_features = {};
  first_error(packed->check(no_family),
              ck::CheckDiagnosticKind::UnsupportedTargetFamily);

  auto instruction = resolve("add.s32 %r0, %r1, 7;");
  auto& add = dynamic_cast<ir::AddIntegerNoSat&>(*instruction);
  std::get<ir::ResolvedImmediate>(add.src2.value).type = ir::ScalarType::F32;
  auto mismatch = add.check(context());
  first_error(mismatch, ck::CheckDiagnosticKind::OperandTypeMismatch);
  require(mismatch.error().front().range == add.src2.locs.front(),
          "diagnostic source lost");
  std::get<ir::ResolvedImmediate>(add.src2.value).type = ir::ScalarType::S32;
  add.dst.value.declared_type = ir::ScalarType::F64;
  first_error(add.check(context()),
              ck::CheckDiagnosticKind::OperandTypeMismatch);
  add.dst.value.declared_type.reset();
  add.operand_layout.value = 1;
  first_error(add.check(context()),
              ck::CheckDiagnosticKind::InvalidOperandLayoutTag);
}
/** Bind actual declarations and retain their identities through direct resolution. */
void declaration_binding() {
  PtxSyntaxParser parser(R"(.version 9.3
.target sm_120
.address_size 64
.visible .entry demo() {
  .reg .u32 %r<3>;
  .reg .pred %p0;
  ret;
})");
  auto module = parser.parseModule();
  require(module.has_value(), "declaration module parse failed");
  auto bound = binding::bindSymbols(*module);
  require(bound.diagnostics.empty(), "declaration binding failed");
  auto function = bound.table.lookup(bound.table.moduleScope(), "demo");
  require(function.has_value(), "function binding missing");
  auto scope = bound.table.symbol(function->symbol).owned_scope;
  require(scope.has_value(), "function scope missing");
  ir::ResolveContext resolve_context{bound.table, *scope, *scope, true, {}};
  auto ast = parse("@%p0 add.u32 %r0, %r1, %r2;");
  auto result = ir::resolveAdd(ast, &resolve_context);
  require(result.has_value(), "bound Add resolution failed");
  auto& add = dynamic_cast<ir::AddIntegerNoSat&>(**result);
  require(
      add.dst.value.symbol_id == bound.table.lookup(*scope, "%r0")->symbol &&
          add.dst.value.parameterized_index == 0 &&
          add.dst.value.declared_type == ir::ScalarType::U32,
      "bound destination contract lost");
  require(add.execution_predicate->value.register_ref.symbol_id ==
              bound.table.lookup(*scope, "%p0")->symbol,
          "predicate binding lost");
  require(add.check(context()).has_value(), "bound Add rejected");
}
/** Verify independent storage and source retention through clone and owner movement. */
void ownership_and_predicate() {
  auto original = resolve("@!%p0 add.u32 %r0, %r1, %r2;");
  require(original->execution_predicate &&
              original->execution_predicate->value.negated,
          "predicate lost");
  require(original->check(context()).has_value(), "valid predicate rejected");
  auto& add = dynamic_cast<ir::AddIntegerNoSat&>(*original);
  add.dst.value.symbol_id = binding::SymbolId{42};
  auto cloned = original->clone();
  auto& copy = dynamic_cast<ir::AddIntegerNoSat&>(*cloned);
  require(copy.dst.value.symbol_id == binding::SymbolId{42},
          "binding identity lost");
  require(
      copy.dst.locs == add.dst.locs && copy.type.locs == add.type.locs &&
          copy.execution_predicate->locs == original->execution_predicate->locs,
      "clone source ranges lost");
  auto* address = original.get();
  auto* field_address = &add.dst;
  std::vector<std::unique_ptr<ir::Instruction>> owners;
  owners.push_back(std::move(original));
  for (int i = 0; i < 32; ++i)
    owners.push_back(cloned->clone());
  require(owners.front().get() == address &&
              &dynamic_cast<ir::AddIntegerNoSat&>(*owners.front()).dst ==
                  field_address,
          "owner growth moved payload");
  add.dst.value.spelling = "%changed";
  add.dst.locs.clear();
  add.type.locs.clear();
  owners.front()->execution_predicate->value.register_ref.spelling =
      "%changed_pred";
  require(copy.dst.value.spelling == "%r0" && !copy.dst.locs.empty() &&
              !copy.type.locs.empty() &&
              copy.execution_predicate->value.register_ref.spelling == "%p0",
          "clone shares mutable state");
  References refs;
  copy.visit_references(refs);
  require(
      refs.spellings == std::vector<std::string>{"%p0", "%r0", "%r1", "%r2"},
      "reference order changed");
  require(refs.ranges[1] == copy.dst.locs.front(), "reference range changed");
  PredicateOnlyObserver predicate_only;
  copy.visit_references(predicate_only);
  require(predicate_only.count == 1, "partial observer missed predicate");
  ir::detail::IReferenceObserver no_op;
  copy.visit_references(no_op);
  copy.execution_predicate->value.register_ref.declared_type =
      ir::ScalarType::U32;
  copy.dst.value.declared_type = ir::ScalarType::F64;
  auto invalid = copy.check(context());
  first_error(invalid, ck::CheckDiagnosticKind::InvalidExecutionPredicate);
  require(invalid.error().size() >= 2 &&
              invalid.error()[1].kind ==
                  ck::CheckDiagnosticKind::OperandTypeMismatch,
          "predicate must precede field diagnostics");
}
}  // namespace
/** Run the dedicated Add-only smoke, independent of the production GTest target. */
int main() {
  try {
    all_forms();
    negative_cases();
    declaration_binding();
    ownership_and_predicate();
    std::cout << "resolved IR ir: 11 forms, negative checks, "
                 "clone/source/reference stability passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

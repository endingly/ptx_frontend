#include <algorithm>
#include <array>
#include <concepts>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#ifdef PTX_SCALAR_MM_CAPTURE_GENERATED_VIEWS
namespace ptx_frontend::resolved_ir::scalar_mm_generated {
/** Current member borrowed only during one enclosed actual generated check. */
const WithLocs<RegOrImm>* expected = nullptr;
/** Selected owner type, independent from mutable payload mirrors. */
ScalarType logical = ScalarType::Invalid;
/** Count actual generated src projections, not hand-built visitor views. */
unsigned observed = 0;
/** Track all pointer, field, cached value and range mirrors synchronously. */
bool mirrors = true;
#define SCALAR_MM_PARAMETERS                                       \
  std::span<const checker::OperandDescriptor> descriptors,         \
      std::span<const checker::FieldView> fields,                  \
      std::span<const checker::OperandView> views,                 \
      std::span<const checker::OperandTypeCompatibilityDescriptor> \
          compatibility, const checker::Context &context,          \
      const MatrixInstructionDescriptor *matrix
/** Forward to the real matching library after test-only interception. */
extern "C" checker::CheckResult original_operands(SCALAR_MM_PARAMETERS) asm(
    "__real__ZN12ptx_frontend11resolved_ir7checker14check_"
    "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
    "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
    "11OperandViewELm18446744073709551615EES2_IKNS1_"
    "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
    "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Link-only shim observing actual generated operands; no production API hook. */
extern "C" checker::CheckResult generated_operands(SCALAR_MM_PARAMETERS) asm(
    "__wrap__ZN12ptx_frontend11resolved_ir7checker14check_"
    "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
    "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
    "11OperandViewELm18446744073709551615EES2_IKNS1_"
    "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
    "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Consume all borrowed arguments before forwarding the unmodified real checker result. */
checker::CheckResult generated_operands(SCALAR_MM_PARAMETERS) {
  if (expected)
    for (const auto& view : views)
      if (view.field_id == "src") {
        ++observed;
        const auto descriptor =
            std::ranges::find(descriptors, std::string_view("src"),
                              &checker::OperandDescriptor::target_field_id);
        const auto type_field = std::ranges::find(
            fields, std::string_view("type"), &checker::FieldView::field_id);
        bool valid = descriptor != descriptors.end() &&
                     type_field != fields.end() &&
                     type_field->scalar_type == logical &&
                     descriptor->type_expression.kind ==
                         checker::OperandTypeExpressionKind::ModifierField &&
                     descriptor->type_expression.modifier_field_id == "type" &&
                     descriptor->register_width_policy ==
                         base::ScalarTypeSizePolicy::SameWidth &&
                     view.locations.data() == expected->locs.data() &&
                     std::ranges::equal(view.locations, expected->locs);
        if (const auto* reg =
                std::get_if<ResolvedRegisterRef>(&expected->value))
          valid = valid &&
                  view.actual_shape == checker::OperandShape::Register &&
                  view.register_ref == reg &&
                  view.register_type == reg->declared_type &&
                  view.register_symbol_id == reg->symbol_id &&
                  view.register_class == reg->register_class &&
                  view.register_vector_width == reg->vector_width &&
                  !view.immediate_type && !view.immediate_bits &&
                  !view.integer_source_bits;
        else if (const auto* word =
                     std::get_if<ResolvedImmediate>(&expected->value))
          valid = valid &&
                  view.actual_shape == checker::OperandShape::Immediate &&
                  !view.register_ref && !view.register_type &&
                  !view.register_symbol_id && !view.register_class &&
                  !view.register_vector_width &&
                  view.immediate_type == word->type &&
                  view.immediate_bits == word->bits &&
                  view.immediate_is_negative == word->is_negative &&
                  view.integer_source_bits == word->integer_source_bits;
        else
          valid = false;
        mirrors = mirrors && valid;
      }
  return original_operands(descriptors, fields, views, compatibility, context,
                           matrix);
}
#undef SCALAR_MM_PARAMETERS
}  // namespace ptx_frontend::resolved_ir::scalar_mm_generated
#endif

namespace {
namespace ir = ptx_frontend::resolved_ir;
/** Exactly the nine public classes whose source field migrates in place. */
template <typename Form>
concept MigratedMultimemForm =
    std::same_as<Form, ir::MultimemStFloatNoneV2DefaultWeak> ||
    std::same_as<Form, ir::MultimemStFloatNoneV2DefaultOrdered> ||
    std::same_as<Form, ir::MultimemRedFloatAddV2DefaultRed> ||
    std::same_as<Form, ir::MultimemStFloatNoneV4DefaultWeak> ||
    std::same_as<Form, ir::MultimemStFloatNoneV4DefaultOrdered> ||
    std::same_as<Form, ir::MultimemRedFloatAddV4DefaultRed> ||
    std::same_as<Form, ir::MultimemStFloatNoneV8DefaultWeak> ||
    std::same_as<Form, ir::MultimemStFloatNoneV8DefaultOrdered> ||
    std::same_as<Form, ir::MultimemRedFloatAddV8DefaultRed>;

/** Compile the existing default-construction/field contract with a value payload. */
template <MigratedMultimemForm Form>
bool acceptsInstalledValueField() {
  static_assert(std::same_as<decltype(Form{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>>);
  static_assert(std::default_initializable<Form> &&
                std::copy_constructible<Form>);
  Form form;
  form.src = ptx_frontend::WithLocs<ir::ResolvedValueVector>{};
  return form.src.value.elements.empty();
}

/** Borrow an actual migrated field without synthetic payload or source fallback. */
ptx_frontend::WithLocs<ir::ResolvedValueVector>& installedValueField(
    ir::Instruction& instruction) {
  if (auto* form =
          dynamic_cast<ir::MultimemStFloatNoneV2DefaultWeak*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<ir::MultimemStFloatNoneV2DefaultOrdered*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<ir::MultimemRedFloatAddV2DefaultRed*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<ir::MultimemStFloatNoneV4DefaultWeak*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<ir::MultimemStFloatNoneV4DefaultOrdered*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<ir::MultimemRedFloatAddV4DefaultRed*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<ir::MultimemStFloatNoneV8DefaultWeak*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<ir::MultimemStFloatNoneV8DefaultOrdered*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<ir::MultimemRedFloatAddV8DefaultRed*>(&instruction))
    return form->src;
  throw std::logic_error("Expected a migrated multimem value field.");
}

/** Inspect callback data only while its real instruction owner is alive. */
struct InstalledValueObserver final : ir::detail::IReferenceObserver {
  /** Expected owner identity, borrowed for this one visit only. */
  const ptx_frontend::WithLocs<ir::ResolvedValueVector>& owner;
  /** Copied result and callback count, never a received span or pointer. */
  bool valid = true;
  size_t calls = 0;
  /** Bind an immutable owner for the synchronous visit. */
  explicit InstalledValueObserver(
      const ptx_frontend::WithLocs<ir::ResolvedValueVector>& value)
      : owner(value) {}
  /** Check every lane, including the eighth, against the actual owned payload. */
  void value_vector(const ir::ResolvedValueVector& value,
                    std::span<const ptx_frontend::SourceRange> ranges,
                    ir::checker::AddressSymbolResolutionPolicy) override {
    ++calls;
    valid = valid && &value == &owner.value &&
            ranges.size() == value.elements.size() &&
            ranges.size() == owner.locs.size();
    if (!valid)
      return;
    for (size_t lane = 0; lane < value.elements.size(); ++lane) {
      valid = valid && ranges[lane] == owner.locs[lane];
      if (const auto* reg =
              std::get_if<ir::ResolvedRegisterRef>(&value.elements[lane])) {
        valid = valid && reg->symbol_id.has_value();
        if (value.source.whole_base) {
          const auto& root = value.source.whole_base->value.register_ref;
          valid = valid && reg->symbol_id == root.symbol_id &&
                  reg->parameterized_index == root.parameterized_index &&
                  reg->component && reg->component->lane == lane &&
                  reg->component->origin ==
                      ir::RegisterComponentOrigin::NamedProjection &&
                  reg->component->base_range == value.source.range;
        }
      } else
        valid =
            valid && ir::valid_value_vector_immediate(
                         std::get<ir::ResolvedImmediate>(value.elements[lane]));
    }
  }
};

/** Exercise applicable numeric/packed32/typedpacked16 data at two, four and eight lanes. */
bool checkInstalledMultimemValues() {
  using ir::ScalarType;
  if (!acceptsInstalledValueField<ir::MultimemStFloatNoneV2DefaultWeak>())
    return false;
  if (!acceptsInstalledValueField<ir::MultimemStFloatNoneV2DefaultOrdered>())
    return false;
  if (!acceptsInstalledValueField<ir::MultimemRedFloatAddV2DefaultRed>())
    return false;
  if (!acceptsInstalledValueField<ir::MultimemStFloatNoneV4DefaultWeak>())
    return false;
  if (!acceptsInstalledValueField<ir::MultimemStFloatNoneV4DefaultOrdered>())
    return false;
  if (!acceptsInstalledValueField<ir::MultimemRedFloatAddV4DefaultRed>())
    return false;
  if (!acceptsInstalledValueField<ir::MultimemStFloatNoneV8DefaultWeak>())
    return false;
  if (!acceptsInstalledValueField<ir::MultimemStFloatNoneV8DefaultOrdered>())
    return false;
  if (!acceptsInstalledValueField<ir::MultimemRedFloatAddV8DefaultRed>())
    return false;
  /** One source program and its expected last word; packed16 uses register seeding. */
  struct Case {
    /** Genuine instruction text under the complete profile below. */
    std::string_view instruction;
    /** Selected word type and exact low-width expected payload. */
    ScalarType type;
    uint64_t word;
    /** True only for typed data without a written literal producer. */
    bool typed_only;
  };
  constexpr std::array cases{
      Case{"multimem.st.global.v2.f32 [mm],{1.5,1.5};", ScalarType::F32,
           0x3fc00000, false},
      Case{"multimem.st.global.v4.f16x2 "
           "[mm],{0f3e003e00,0f3e003e00,0f3e003e00,0f3e003e00};",
           ScalarType::F16x2, 0x3e003e00, false},
      Case{"multimem.st.global.v8.bf16 [mm],{1.5,1.5,1.5,1.5,1.5,1.5,1.5,1.5};",
           ScalarType::BF16, 0x3fc0, false},
      Case{"multimem.st.global.v2.e4m3x2 [mm],{r0,r1};", ScalarType::E4m3x2,
           0x3c3c, true},
      Case{"multimem.st.global.v4.e4m3x2 [mm],{r0,r1,r2,r3};",
           ScalarType::E4m3x2, 0x3c3c, true},
      Case{"multimem.st.global.v8.e4m3x2 [mm],{r0,r1,r2,r3,r4,r5,r6,r7};",
           ScalarType::E4m3x2, 0x3c3c, true}};
  for (const auto& item : cases) {
    std::optional<ir::ResolvedModule> owned;
    {
      const std::string source =
          ".version 9.3\n.target sm_100a\n.address_size 64\n"
          ".global .align 16 .b8 mm[256]; .entry kernel(){.reg .b16 r<8>; " +
          std::string(item.instruction) + "}";
      ptx_frontend::PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto result = ir::resolveAndValidateModule(*ast);
      if (!result || !ir::validateModule(*ast, *result))
        return false;
      owned.emplace(std::move(*result));
    }
    auto copy = *owned;
    auto& field = installedValueField(*copy.functions.front().body.front());
    if (item.typed_only)
      field.value.elements.back() =
          ir::ResolvedImmediate{.bits = item.word, .type = item.type};
    const auto& word =
        std::get<ir::ResolvedImmediate>(field.value.elements.back());
    if (word.bits != item.word || word.type != item.type)
      return false;
    InstalledValueObserver observer(field);
    copy.functions.front().body.front()->visit_references(observer);
    if (!observer.valid || observer.calls != 1 || !ir::validateModule(copy))
      return false;
    copy.functions.front().body.front() =
        copy.functions.front().body.front()->clone();
    auto& cloned = installedValueField(*copy.functions.front().body.front());
    InstalledValueObserver clone_observer(cloned);
    copy.functions.front().body.front()->visit_references(clone_observer);
    if (!clone_observer.valid || clone_observer.calls != 1 ||
        !ir::validateModule(copy))
      return false;
  }
  return true;
}
/** Keep installed named whole-base and projected references joined after source death. */
bool checkInstalledNamedValueRoots() {
  for (const unsigned arity : {2u, 4u}) {
    std::optional<ir::ResolvedModule> owned;
    {
      const std::string source =
          ".version 9.3\n.target sm_100a\n.address_size 64\n.global .align 16 "
          ".b8 mm[256];.entry k(){.reg .v" +
          std::to_string(arity) + " .b32 V<2>;multimem.st.global.v" +
          std::to_string(arity) + ".f32 [mm],V1;}";
      ptx_frontend::PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto result = ir::resolveAndValidateModule(*ast);
      if (!result || !ir::validateModule(*ast, *result))
        return false;
      owned.emplace(std::move(*result));
    }
    auto copied = *owned;
    ir::ResolvedModule assigned;
    assigned = copied;
    auto function = assigned.functions.front();
    assigned.functions.front() = function;
    assigned.functions.front().body.front() = function.body.front()->clone();
    auto& owner = installedValueField(*assigned.functions.front().body.front());
    if (!owner.value.source.whole_base)
      return false;
    InstalledValueObserver observer(owner);
    assigned.functions.front().body.front()->visit_references(observer);
    if (!observer.valid || observer.calls != 1 || !ir::validateModule(assigned))
      return false;
  }
  return true;
}
/** Borrow one of the three migrated scalar packed members from its live owner. */
const ptx_frontend::WithLocs<ir::RegOrImm>& installedScalarPackedSource(
    const ir::Instruction& instruction) {
  if (const auto* form =
          dynamic_cast<const ir::MultimemStFloatPackedNoneScalarDefaultWeak*>(
              &instruction))
    return form->src;
  if (const auto* form = dynamic_cast<
          const ir::MultimemStFloatPackedNoneScalarDefaultOrdered*>(
          &instruction))
    return form->src;
  return dynamic_cast<const ir::MultimemRedFloatPackedAddScalarDefaultRed&>(
             instruction)
      .src;
}

/** Read the existing selected logical modifier without any opcode-dependent interpretation. */
ir::ScalarType installedScalarPackedType(const ir::Instruction& instruction) {
  if (const auto* form =
          dynamic_cast<const ir::MultimemStFloatPackedNoneScalarDefaultWeak*>(
              &instruction))
    return form->type.value;
  if (const auto* form = dynamic_cast<
          const ir::MultimemStFloatPackedNoneScalarDefaultOrdered*>(
          &instruction))
    return form->type.value;
  return dynamic_cast<const ir::MultimemRedFloatPackedAddScalarDefaultRed&>(
             instruction)
      .type.value;
}

/** Synchronously inspect the real scalar source address and location storage. */
struct InstalledScalarPackedObserver final : ir::detail::IReferenceObserver {
  /** Borrowed actual member; the instruction outlives this single traversal. */
  const ptx_frontend::WithLocs<ir::RegOrImm>& owner;
  /** Selected logical owner type, independent of callback payload storage. */
  ir::ScalarType logical;
  /** Number of callbacks for the exact member, without retaining callback payloads. */
  size_t calls = 0;
  /** Whether every copied range comparison matched while callback storage was live. */
  bool valid = true;
  /** Bind a live owner for the synchronous visit. */
  InstalledScalarPackedObserver(
      const ptx_frontend::WithLocs<ir::RegOrImm>& source,
      ir::ScalarType selected)
      : owner(source), logical(selected) {}
  /** Compare real owner and range mirrors without constructing an observer surrogate. */
  void reg_or_imm(const ir::RegOrImm& value,
                  std::span<const ptx_frontend::SourceRange> ranges,
                  ir::checker::AddressSymbolResolutionPolicy) override {
    if (&value != &owner.value)
      return;
    ++calls;
    valid = valid && ranges.size() == owner.locs.size() &&
            ranges.data() == owner.locs.data();
    if (const auto* word = std::get_if<ir::ResolvedImmediate>(&value))
      valid = valid && word->type == logical && word->bits <= 0xffffffffu &&
              !word->integer_source_bits && !word->is_negative;
    else if (const auto* ref = std::get_if<ir::ResolvedRegisterRef>(&value))
      valid = valid && ref->symbol_id.has_value() &&
              ref->declared_type == ir::ScalarType::B32;
    else
      valid = false;
    if (!valid)
      return;
    for (size_t index = 0; index < ranges.size(); ++index)
      valid = valid && ranges[index] == owner.locs[index];
  }
};

/** Exercise all six scalar type branches with actual installed register/immediate members. */
bool checkInstalledScalarPackedMultimem() {
  static_assert(
      std::same_as<
          decltype(ir::MultimemStFloatPackedNoneScalarDefaultWeak{}.src.value),
          ir::RegOrImm>);
  static_assert(
      std::same_as<decltype(ir::MultimemStFloatPackedNoneScalarDefaultOrdered{}
                                .src.value),
                   ir::RegOrImm>);
  static_assert(
      std::same_as<
          decltype(ir::MultimemRedFloatPackedAddScalarDefaultRed{}.src.value),
          ir::RegOrImm>);
  static_assert(std::default_initializable<
                    ir::MultimemStFloatPackedNoneScalarDefaultWeak> &&
                std::default_initializable<
                    ir::MultimemStFloatPackedNoneScalarDefaultOrdered> &&
                std::default_initializable<
                    ir::MultimemRedFloatPackedAddScalarDefaultRed>);
  static_assert(
      std::copy_constructible<ir::MultimemStFloatPackedNoneScalarDefaultWeak> &&
      std::copy_constructible<
          ir::MultimemStFloatPackedNoneScalarDefaultOrdered> &&
      std::copy_constructible<ir::MultimemRedFloatPackedAddScalarDefaultRed>);
  /** Report the actual installed fixture contract that failed. */
  const auto require = [](bool condition, std::string_view contract) {
    if (!condition)
      std::cerr << "scalar packed multimem consumer: " << contract << '\n';
    return condition;
  };
  constexpr std::array patterns{"multimem.st.weak.@ [mm],$;",
                                "multimem.st.release.gpu.@ [mm],$;",
                                "multimem.red.relaxed.gpu.add.@ [mm],$;"};
  constexpr std::array kinds{
      ir::MultimemStFloatPackedNoneScalarDefaultWeak::kind,
      ir::MultimemStFloatPackedNoneScalarDefaultOrdered::kind,
      ir::MultimemRedFloatPackedAddScalarDefaultRed::kind};
  std::optional<ir::ResolvedModule> owned;
  {
    std::string source =
        ".version 9.3\n.target sm_90\n.address_size 64\n"
        ".global .align 16 .b8 mm[256];\n.entry kernel(){ .reg .b32 b; ";
    for (const auto pattern : patterns)
      for (const bool bf : {false, true})
        for (const bool immediate : {false, true}) {
          std::string operation(pattern);
          operation.replace(operation.find('@'), 1, bf ? "bf16x2" : "f16x2");
          operation.replace(
              operation.find('$'), 1,
              immediate ? (bf ? "0f3fc03fc0" : "0f3e003e00") : "b");
          source += operation;
        }
    source += "}";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!require(ast.has_value(), "complete source parses") ||
        !require(ast.diagnostics.empty(), "parser diagnostics are empty"))
      return false;
    auto result = ir::resolveAndValidateModule(*ast);
    if (!require(result.has_value(), "all six branches strictly resolve") ||
        !require(ir::validateModule(*ast, *result).has_value(),
                 "provided AST association validates"))
      return false;
    owned.emplace(std::move(*result));
  }
  if (!require(owned && owned->functions.size() == 1,
               "owned function survives source/AST death") ||
      !require(owned->functions.front().body.size() == 12,
               "all six register/immediate pairs are owned"))
    return false;
  for (size_t index = 0; index < 12; ++index) {
    const auto& pointer = owned->functions.front().body[index];
    if (!require(pointer != nullptr, "actual instruction exists"))
      return false;
    const auto& instruction = *pointer;
    if (!require(instruction.instruction_kind() == kinds[index / 4],
                 "existing exact final class selected"))
      return false;
    const bool bf = (index / 2) % 2 != 0;
    const auto type = bf ? ir::ScalarType::BF16x2 : ir::ScalarType::F16x2;
    if (!require(installedScalarPackedType(instruction) == type,
                 "existing type modifier preserved"))
      return false;
    const auto& source = installedScalarPackedSource(instruction);
    if (!require(!source.locs.empty(), "owned source range retained"))
      return false;
    if (index % 2 != 0) {
      const auto* immediate = std::get_if<ir::ResolvedImmediate>(&source.value);
      if (!require(immediate && immediate->type == type &&
                       immediate->bits == (bf ? 0x3fc03fc0u : 0x3e003e00u) &&
                       !immediate->integer_source_bits &&
                       !immediate->is_negative,
                   "exact packed word/type/origin flags"))
        return false;
    } else {
      const auto* reg = std::get_if<ir::ResolvedRegisterRef>(&source.value);
      if (!require(reg && reg->symbol_id &&
                       reg->declared_type == ir::ScalarType::B32,
                   "existing actual B32 register carrier"))
        return false;
    }
#ifdef PTX_SCALAR_MM_CAPTURE_GENERATED_VIEWS
    const auto profile = ptx_frontend::base::find_target_profile("sm_90");
    if (!profile)
      return false;
    const ir::checker::Context context{
        .target = {.ptx_version = {9, 3},
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities}};
    const auto before = ir::scalar_mm_generated::observed;
    ir::scalar_mm_generated::mirrors = true;
    ir::scalar_mm_generated::logical = type;
    ir::scalar_mm_generated::expected = &source;
    const auto checked = instruction.check(context);
    ir::scalar_mm_generated::expected = nullptr;
    if (!checked || !ir::scalar_mm_generated::mirrors ||
        ir::scalar_mm_generated::observed != before + 1)
      return false;
#endif
    {
      std::string operation(patterns[index / 4]);
      operation.replace(operation.find('@'), 1, bf ? "bf16x2" : "f16x2");
      operation.replace(operation.find('$'), 1,
                        index % 2 ? (bf ? "0f3fc03fc0" : "0f3e003e00") : "%b0");
      operation.replace(operation.find("[mm]"), 4, "[%rd0]");
      ptx_frontend::PtxSyntaxParser factory_parser(operation);
      const auto syntax = factory_parser.parseInstruction();
      if (!syntax || !syntax.diagnostics.empty())
        return false;
      const auto factory = ir::resolveInstruction(*syntax);
      if (!factory || !*factory ||
          (*factory)->instruction_kind() != kinds[index / 4])
        return false;
      const auto& factory_source = installedScalarPackedSource(**factory);
      if (index % 2) {
        const auto* word =
            std::get_if<ir::ResolvedImmediate>(&factory_source.value);
        if (!word || word->type != type ||
            word->bits != (bf ? 0x3fc03fc0u : 0x3e003e00u))
          return false;
      } else if (!std::get_if<ir::ResolvedRegisterRef>(&factory_source.value))
        return false;
    }
    InstalledScalarPackedObserver observer(source, type);
    instruction.visit_references(observer);
    if (!require(observer.calls == 1 && observer.valid,
                 "observer saw the actual member and ranges"))
      return false;
    const auto clone = instruction.clone();
    if (!require(
            clone &&
                clone->instruction_kind() == instruction.instruction_kind() &&
                installedScalarPackedSource(*clone) == source,
            "exact-class clone preserves migrated member"))
      return false;
#ifdef PTX_SCALAR_MM_CAPTURE_GENERATED_VIEWS
    const auto& cloned_source = installedScalarPackedSource(*clone);
    const auto clone_before = ir::scalar_mm_generated::observed;
    ir::scalar_mm_generated::expected = &cloned_source;
    const auto clone_check = clone->check(context);
    ir::scalar_mm_generated::expected = nullptr;
    if (!clone_check || !ir::scalar_mm_generated::mirrors ||
        ir::scalar_mm_generated::observed != clone_before + 1)
      return false;
#endif
  }
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  ir::ResolvedFunction function = copied.functions.front(), function_assigned;
  function_assigned = function;
  assigned.functions.front() = function_assigned;
  owned.reset();
  return require(ir::validateModule(copied).has_value(),
                 "copied module validates after producer death") &&
         require(ir::validateModule(assigned).has_value(),
                 "assigned module validates after producer death");
}

}  // namespace

/** Verify installed multimem form and completion contracts without GPU execution. */
int main() {
  if (!checkInstalledScalarPackedMultimem())
    return 10;
  using namespace ptx_frontend::resolved_ir;
  std::optional<ResolvedModule> module;
  {
    constexpr auto source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.global .align 16 .b8 mm[64];
.shared .align 16 .b8 src[64];
.visible .entry kernel() {
  .reg .u32 %u;
  .reg .f32 %f<4>;
  multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [src], 16;
  multimem.ld_reduce.gpu.add.u32 %u, [mm];
  multimem.st.weak.sys.v4.f32 [mm+16], {%f0,%f1,%f2,%f3};
  multimem.st.u32 [mm], %u;
  multimem.ld_reduce.weak.add.u32 %u, [mm];
  ret;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return 1;
    auto result = resolveAndValidateModule(*ast);
    if (!result || result->functions.front().body.size() != 6)
      return 2;
    module.emplace(std::move(*result));
  }
  const auto& body = module->functions.front().body;
  const auto* form =
      dynamic_cast<const ptx_frontend::resolved_ir::MultimemCpAsyncBulkWeak*>(
          module->functions.front().body.front().get());
  if (!form || form->completion_kind !=
                   ptx_frontend::base::AsyncCompletionKind::BulkGroup)
    return 3;
  const auto* load =
      dynamic_cast<const MultimemLdReduceIntegerAddScalarDefaultWeak*>(
          body[1].get());
  const auto* store =
      dynamic_cast<const MultimemStFloatNoneV4DefaultWeak*>(body[2].get());
  const auto* bare =
      dynamic_cast<const MultimemStIntegerNoneScalarDefaultWeak*>(
          body[3].get());
  const auto* weak =
      dynamic_cast<const MultimemLdReduceIntegerAddScalarDefaultWeak*>(
          body[4].get());
  if (!load || !store || !bare || !weak)
    return 4;
  if (load->semantics.value != MemoryConsistency::Omitted ||
      !load->semantics.locs.empty() || load->scope.value != MemoryScope::Gpu ||
      load->scope.locs.size() != 1)
    return 5;
  if (store->semantics.value != MemoryConsistency::Weak ||
      store->semantics.locs.size() != 1 ||
      store->scope.value != MemoryScope::Sys || store->scope.locs.size() != 1)
    return 6;
  if (bare->semantics.value != MemoryConsistency::Omitted ||
      !bare->semantics.locs.empty() || bare->scope.value != MemoryScope::None ||
      !bare->scope.locs.empty() ||
      weak->semantics.value != MemoryConsistency::Weak ||
      weak->semantics.locs.size() != 1 ||
      weak->scope.value != MemoryScope::None || !weak->scope.locs.empty())
    return 7;
  if (!checkInstalledMultimemValues() || !checkInstalledNamedValueRoots())
    return 9;
  return validateModule(*module) ? 0 : 8;
}

#include <array>
#include <concepts>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

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
}  // namespace

/** Verify installed multimem form and completion contracts without GPU execution. */
int main() {
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

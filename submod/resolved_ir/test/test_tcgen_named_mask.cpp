#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** One existing required mask layout with independent source topology. */
struct NamedMaskCase {
  /** Canonical layout identity for exact finite assertions. */
  std::string_view variant, layout;
  /** Written tensor kind and selected A source placement. */
  std::string_view kind;
  bool sparse, shared, scale;
};
constexpr std::array mask_cases{
    NamedMaskCase{"tcgen05_mma_f16", "shared_mask_no_scale", "f16", false, true,
                  false},
    NamedMaskCase{"tcgen05_mma_f16", "shared_mask_scale", "f16", false, true,
                  true},
    NamedMaskCase{"tcgen05_mma_f16", "tensor_mask_no_scale", "f16", false,
                  false, false},
    NamedMaskCase{"tcgen05_mma_f16", "tensor_mask_scale", "f16", false, false,
                  true},
    NamedMaskCase{"tcgen05_mma_tf32", "shared_mask_no_scale", "tf32", false,
                  true, false},
    NamedMaskCase{"tcgen05_mma_tf32", "shared_mask_scale", "tf32", false, true,
                  true},
    NamedMaskCase{"tcgen05_mma_tf32", "tensor_mask_no_scale", "tf32", false,
                  false, false},
    NamedMaskCase{"tcgen05_mma_tf32", "tensor_mask_scale", "tf32", false, false,
                  true},
    NamedMaskCase{"tcgen05_mma_i8", "shared_mask", "i8", false, true, false},
    NamedMaskCase{"tcgen05_mma_i8", "tensor_mask", "i8", false, false, false},
    NamedMaskCase{"tcgen05_mma_f8f6f4", "shared_mask", "f8f6f4", false, true,
                  false},
    NamedMaskCase{"tcgen05_mma_f8f6f4", "tensor_mask", "f8f6f4", false, false,
                  false},
    NamedMaskCase{"tcgen05_mma_sp_f16", "shared_mask_no_scale", "f16", true,
                  true, false},
    NamedMaskCase{"tcgen05_mma_sp_f16", "shared_mask_scale", "f16", true, true,
                  true},
    NamedMaskCase{"tcgen05_mma_sp_f16", "tensor_mask_no_scale", "f16", true,
                  false, false},
    NamedMaskCase{"tcgen05_mma_sp_f16", "tensor_mask_scale", "f16", true, false,
                  true},
    NamedMaskCase{"tcgen05_mma_sp_tf32", "shared_mask_no_scale", "tf32", true,
                  true, false},
    NamedMaskCase{"tcgen05_mma_sp_tf32", "shared_mask_scale", "tf32", true,
                  true, true},
    NamedMaskCase{"tcgen05_mma_sp_tf32", "tensor_mask_no_scale", "tf32", true,
                  false, false},
    NamedMaskCase{"tcgen05_mma_sp_tf32", "tensor_mask_scale", "tf32", true,
                  false, true},
    NamedMaskCase{"tcgen05_mma_sp_f8f6f4", "shared_mask", "f8f6f4", true, true,
                  false},
    NamedMaskCase{"tcgen05_mma_sp_f8f6f4", "tensor_mask", "f8f6f4", true, false,
                  false},
    NamedMaskCase{"tcgen05_mma_sp_i8", "shared_mask", "i8", true, true, false},
    NamedMaskCase{"tcgen05_mma_sp_i8", "tensor_mask", "i8", true, false, false},
};

/** Build matched initialized controls; opaque descriptor contents are not authenticated. */
std::string namedMaskSource(const NamedMaskCase& item, unsigned group,
                            std::string_view carrier, bool named,
                            bool omit = false, unsigned named_width = 4,
                            std::string_view whole = "V1") {
  std::string source =
      ".version 9.3\n.target sm_100a\n.address_size 64\n.entry kernel(){"
      ".reg .b32 d,a,i,sp; .reg .b64 ad,bd; .reg .pred p; .reg ." +
      std::string(carrier) + " m<8>; .reg .v" + std::to_string(named_width) +
      " ." + std::string(carrier) +
      " V<2>,W<2>;"
      "mov.b32 d,0;mov.b32 a,0;mov.b32 i,0;mov.b32 sp,0;"
      "mov.b64 ad,0;mov.b64 bd,0;mov.pred p,1;";
  for (unsigned lane = 0; lane < 8; ++lane)
    source += "mov.b32 m" + std::to_string(lane) + ",0;";
  for (unsigned lane = 0; lane < named_width; ++lane)
    source += "mov.b32 V1." + std::string(1, "xyzw"[lane]) + ",0;";
  source += "tcgen05.mma" + std::string(item.sparse ? ".sp" : "") +
            ".cta_group::" + std::to_string(group) +
            ".kind::" + std::string(item.kind) + " [d]," +
            (item.shared ? std::string("ad") : "[a]") + ",bd,";
  if (item.sparse)
    source += "[sp],";
  source += "i,";
  if (!omit) {
    if (named)
      source += whole;
    else {
      source += "{";
      for (unsigned lane = 0; lane < (group == 1 ? 4u : 8u); ++lane)
        source += (lane ? ",m" : "m") + std::to_string(lane);
      source += "}";
    }
    source += ",";
  }
  return source + "p" + (item.scale ? ",15" : "") + ";}";
}

/** Remove initializer instructions before parsing to isolate current declaration/type joins.
 * Register identity remains real; this fixture makes no live descriptor-word claim.
 */
std::string uninitializedNamedMaskSource(const NamedMaskCase& item) {
  auto source = namedMaskSource(item, 1, "b32", true);
  const auto begin = source.find("mov.b32 d,0;");
  const auto end = source.find("tcgen05");
  if (begin == std::string::npos || end == std::string::npos || end < begin)
    throw std::logic_error(
        "Expected initialized named mask fixture boundaries.");
  source.erase(begin, end - begin);
  return source;
}

/** Return strict owned modules after parser/AST/source death; returned errors are nonempty.
 * The by-value source owns the actual parser buffer until resolution completes.
 * Fixture setup failures throw descriptively before callers inspect an absent diagnostic.
 */
std::expected<ResolvedModule, ModuleResolveDiagnostics> namedMaskOwned(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    throw std::runtime_error("Named mask fixture did not parse cleanly.");
  auto result = resolveAndValidateModule(*ast);
  if (!result && result.error().empty())
    throw std::runtime_error(
        "Named mask fixture resolution failed without diagnostics.");
  return result;
}

/** Select the actual mandatory member without creating a new public payload adapter. */
WithLocs<ResolvedRegisterVector>* namedMaskField(Instruction& instruction) {
  if (auto* form = dynamic_cast<Tcgen05MmaF16*>(&instruction))
    return &form->disable_output_lane;
  if (auto* form = dynamic_cast<Tcgen05MmaTf32*>(&instruction))
    return &form->disable_output_lane;
  if (auto* form = dynamic_cast<Tcgen05MmaI8*>(&instruction))
    return &form->disable_output_lane;
  if (auto* form = dynamic_cast<Tcgen05MmaF8f6f4*>(&instruction))
    return &form->disable_output_lane;
  if (auto* form = dynamic_cast<Tcgen05MmaSpF16*>(&instruction))
    return &form->disable_output_lane;
  if (auto* form = dynamic_cast<Tcgen05MmaSpTf32*>(&instruction))
    return &form->disable_output_lane;
  if (auto* form = dynamic_cast<Tcgen05MmaSpF8f6f4*>(&instruction))
    return &form->disable_output_lane;
  if (auto* form = dynamic_cast<Tcgen05MmaSpI8*>(&instruction))
    return &form->disable_output_lane;
  return nullptr;
}
/** Find the real MMA among initialized control instructions owned by the function. */
Instruction& namedMaskInstruction(ResolvedModule& module) {
  for (auto& instruction : module.functions.front().body)
    if (namedMaskField(*instruction))
      return *instruction;
  throw std::logic_error("Expected an owned mandatory-mask MMA.");
}

/** Complete target facts isolate mask cardinality from availability deferral. */
checker::Context namedMaskContext() {
  const auto profile = base::find_target_profile("sm_100a");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Synchronous borrowed inspection; no received payload pointer or span is retained. */
struct NamedMaskObserver final : detail::IReferenceObserver {
  /** Actual owner remains alive and unchanged throughout the visit. */
  const WithLocs<ResolvedRegisterVector>& owner;
  /** Copied number of matching callbacks, not a retained borrowed handle. */
  size_t calls = 0;
  /** Bind the current owner's mandatory mask. */
  explicit NamedMaskObserver(const WithLocs<ResolvedRegisterVector>& value)
      : owner(value) {}
  /** Inspect all4/8 leaves and any real named declaration root. */
  void register_vector(const ResolvedRegisterVector& value,
                       std::span<const SourceRange> ranges,
                       checker::AddressSymbolResolutionPolicy) override {
    if (&value != &owner.value)
      return;
    ++calls;
    ASSERT_EQ(ranges.size(), value.elements.size());
    ASSERT_EQ(ranges.size(), owner.locs.size());
    for (size_t lane = 0; lane < value.elements.size(); ++lane) {
      ASSERT_TRUE(value.elements[lane]);
      EXPECT_TRUE(value.elements[lane]->symbol_id);
      EXPECT_EQ(ranges[lane], owner.locs[lane]);
      if (value.source.whole_base) {
        const auto& root = value.source.whole_base->value.register_ref;
        EXPECT_EQ(value.elements[lane]->symbol_id, root.symbol_id);
        ASSERT_TRUE(value.elements[lane]->component);
        EXPECT_EQ(value.elements[lane]->component->origin,
                  RegisterComponentOrigin::NamedProjection);
        EXPECT_EQ(value.elements[lane]->component->lane, lane);
        EXPECT_EQ(value.elements[lane]->component->base_range,
                  value.source.range);
        EXPECT_EQ(ranges[lane], value.source.range);
      }
    }
  }
};
/** Reacquire each actual payload after copies/assignments/clones, with complete checks. */
void expectNamedMaskOwner(ResolvedModule& module) {
  auto& instruction = namedMaskInstruction(module);
  auto* mask = namedMaskField(instruction);
  ASSERT_TRUE(mask);
  NamedMaskObserver observer(*mask);
  instruction.visit_references(observer);
  EXPECT_EQ(observer.calls, 1u);
  EXPECT_TRUE(instruction.check(namedMaskContext()));
  EXPECT_TRUE(validateModule(module));
}

/** Every required layout keeps brace controls and gains group1named/commoncarrier parity. */
TEST(TcgenNamedMask, PreservesTwentyFourLayoutsAndFivePhysicalCarriers) {
  for (const auto& item : mask_cases) {
    for (const auto carrier : {"b32", "u32", "s32", "f32", "f16x2"}) {
      SCOPED_TRACE(std::string(item.variant) + "/" + std::string(item.layout) +
                   "/" + carrier);
      for (const bool named : {false, true}) {
        const auto source = namedMaskSource(item, 1, carrier, named);
        const auto ast = test_helpers::parseModule(source);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        ASSERT_TRUE(resolveModuleOnly(*ast));
        auto module = namedMaskOwned(source);
        ASSERT_TRUE(module) << module.error().front().message;
        EXPECT_TRUE(validateModule(*ast, *module));
        auto copy = *module;
        ResolvedModule assigned;
        assigned = copy;
        auto function = assigned.functions.front();
        assigned.functions.front() = function;
        auto& instruction = namedMaskInstruction(assigned);
        for (auto& owned_instruction : assigned.functions.front().body)
          if (owned_instruction.get() == &instruction) {
            owned_instruction = instruction.clone();
            break;
          }
        expectNamedMaskOwner(copy);
        expectNamedMaskOwner(assigned);
        auto moved = std::move(assigned);
        ResolvedModule move_assigned;
        move_assigned = std::move(moved);
        expectNamedMaskOwner(move_assigned);
      }
      auto group2 = namedMaskOwned(namedMaskSource(item, 2, carrier, false));
      ASSERT_TRUE(group2) << group2.error().front().message;
      auto* mask = namedMaskField(namedMaskInstruction(*group2));
      ASSERT_TRUE(mask);
      EXPECT_EQ(mask->value.elements.size(), 8u);
      expectNamedMaskOwner(*group2);
    }
  }
}

/** Group2hasnoordinarynamedv8, and named support never restores omitted masks. */
TEST(TcgenNamedMask, KeepsExactGroupCardinalityAndMandatoryPresence) {
  for (const auto& item : mask_cases) {
    for (const unsigned group : {1u, 2u}) {
      EXPECT_FALSE(
          namedMaskOwned(namedMaskSource(item, group, "b32", false, true)));
    }
    for (const auto& [group, width] : {std::pair{1u, 2u}, std::pair{2u, 4u}}) {
      const auto ast = test_helpers::parseModule(
          namedMaskSource(item, group, "b32", true, false, width));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      auto draft = resolveModuleOnly(*ast);
      ASSERT_TRUE(draft) << (draft.error().empty()
                                 ? std::string("Named mask draft resolution "
                                               "failed without diagnostics.")
                                 : std::string(draft.error().front().message));
      EXPECT_FALSE(namedMaskInstruction(*draft).check(namedMaskContext()));
      EXPECT_FALSE(validateModule(*draft));
      EXPECT_FALSE(validateModule(*ast, *draft));
      EXPECT_FALSE(resolveAndValidateModule(*ast));
      // An unvalidated draft is not group legality or a deferred-context certificate.
    }
  }
}

/** Actual root/lane facts remain authoritative, with coherent real-container retarget allowed. */
TEST(TcgenNamedMask, RechecksProvenanceAndIndependentSourceAssociation) {
  for (const auto& item : mask_cases) {
    const auto source = namedMaskSource(item, 1, "b32", true);
    auto owner = namedMaskOwned(source);
    ASSERT_TRUE(owner) << owner.error().front().message;
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    for (unsigned mutation = 0; mutation < 7; ++mutation) {
      auto bad = *owner;
      auto& instruction = namedMaskInstruction(bad);
      auto& mask = *namedMaskField(instruction);
      ASSERT_TRUE(mask.value.source.whole_base);
      auto& root = mask.value.source.whole_base->value.register_ref;
      if (mutation == 0)
        root.symbol_id.reset();
      if (mutation == 1)
        root.declared_type = ScalarType::BF16x2;
      if (mutation == 2)
        root.vector_width = 2;
      if (mutation == 3)
        std::swap(mask.value.elements[0], mask.value.elements[1]);
      if (mutation == 4)
        root.component = mask.value.elements[0]->component;
      if (mutation == 5)
        mask.value.source.whole_base.reset();
      if (mutation == 6)
        mask.value.elements.back().reset();
      EXPECT_FALSE(validateModule(bad));
      EXPECT_FALSE(validateModule(*ast, bad));
      expectNamedMaskOwner(*owner);
    }
    const auto other_source =
        namedMaskSource(item, 1, "b32", true, false, 4, "W1");
    auto other = namedMaskOwned(other_source);
    ASSERT_TRUE(other);
    auto retarget = *owner;
    *namedMaskField(namedMaskInstruction(retarget)) =
        *namedMaskField(namedMaskInstruction(*other));
    expectNamedMaskOwner(retarget);
    EXPECT_TRUE(validateModule(*ast, retarget));
    const auto changed = test_helpers::parseModule(other_source);
    ASSERT_MODULE_PARSE_SUCCEEDS(changed);
    EXPECT_TRUE(resolveAndValidateModule(*changed));
    const auto mismatch = validateModule(*changed, *owner);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(std::any_of(
        mismatch.error().begin(), mismatch.error().end(), [](const auto& d) {
          return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
}

/** Lawful scalar initialization keeps wrong physical width separate from mask admission. */
TEST(TcgenNamedMask, RetainsPhysicalWidthAndDeclarationBoundaries) {
  const auto& item = mask_cases.front();
  for (const auto& [carrier, bits] :
       {std::pair{"b16", 16u}, std::pair{"b64", 64u}, std::pair{"f64", 64u}}) {
    auto source =
        namedMaskSource(item, 1, carrier, false, false, bits == 64 ? 2 : 4);
    for (const auto prefix : {"mov.b32 m", "mov.b32 V"}) {
      const std::string from(prefix);
      const std::string to = "mov.b" + std::to_string(bits) + " " + from.back();
      size_t position = 0;
      while ((position = source.find(from, position)) != std::string::npos) {
        source.replace(position, from.size(), to);
        position += to.size();
      }
    }
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  for (const auto& source :
       {namedMaskSource(item, 1, "bf16x2", true),
        namedMaskSource(item, 1, "b32", true, false, 4, "V1.x")}) {
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
    // Invalid source has no strict owner/lifetime success; actual first stage is recorded later.
  }
}
/** Known invalid declarations remain intrinsic when source availability context is absent. */
TEST(TcgenNamedMask, MissingContextDoesNotHideKnownDeclarations) {
  for (const auto& item : mask_cases) {
    auto source = uninitializedNamedMaskSource(item);
    source.erase(0, source.find(".entry"));
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto owner = resolveModuleOnly(*ast);
    ASSERT_TRUE(owner);
    EXPECT_TRUE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*ast, *owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(
        validateModule(*owner, ModuleValidationPolicy::RequireCompleteContext));
    auto* mask = namedMaskField(namedMaskInstruction(*owner));
    ASSERT_TRUE(mask);
    ASSERT_TRUE(mask->value.source.whole_base);
    auto& root = mask->value.source.whole_base->value.register_ref;
    ASSERT_TRUE(root.symbol_id);
    auto& symbol =
        const_cast<binding::Symbol&>(owner->symbols.symbol(*root.symbol_id));
    symbol.type = ".bf16x2";
    root.declared_type = ScalarType::BF16x2;
    for (auto& lane : mask->value.elements) {
      ASSERT_TRUE(lane);
      lane->declared_type = ScalarType::BF16x2;
    }
    EXPECT_FALSE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(
        validateModule(*ast, *owner, ModuleValidationPolicy::AvailableContext));
    symbol.type = ".b32";
    root.declared_type = ScalarType::B32;
    for (auto& lane : mask->value.elements)
      lane->declared_type = ScalarType::B32;
    EXPECT_TRUE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*ast, *owner, ModuleValidationPolicy::AvailableContext));
  }
}

/** Every real lane and the named root retain declaration/provenance/range authority. */
TEST(TcgenNamedMask, RechecksEveryActualLaneAndCoherentDeclaration) {
  for (const auto& item : mask_cases) {
    const auto source = uninitializedNamedMaskSource(item);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto owner = namedMaskOwned(source);
    ASSERT_TRUE(owner);
    for (unsigned lane_index = 0; lane_index < 4; ++lane_index)
      for (unsigned mutation = 0; mutation < 5; ++mutation) {
        auto bad = *owner;
        auto* mask = namedMaskField(namedMaskInstruction(bad));
        ASSERT_TRUE(mask);
        ASSERT_EQ(mask->value.elements.size(), 4u);
        ASSERT_EQ(mask->locs.size(), 4u);
        auto& leaf = mask->value.elements[lane_index];
        ASSERT_TRUE(leaf);
        ASSERT_TRUE(leaf->component);
        if (mutation == 0)
          leaf->declared_type = ScalarType::F64;
        if (mutation == 1)
          leaf->register_class = ResolvedRegisterClass::Predicate;
        if (mutation == 2)
          leaf->component->origin = RegisterComponentOrigin::ExplicitSelector;
        if (mutation == 3)
          leaf->component->range.end.column += 1;
        if (mutation == 4)
          mask->locs[lane_index].end.column += 1;
        EXPECT_FALSE(validateModule(bad));
        EXPECT_FALSE(validateModule(*ast, bad));
      }
    auto bad = *owner;
    auto* mask = namedMaskField(namedMaskInstruction(bad));
    ASSERT_TRUE(mask);
    ASSERT_TRUE(mask->value.source.whole_base);
    auto& root = mask->value.source.whole_base->value.register_ref;
    ASSERT_TRUE(root.symbol_id);
    auto& symbol =
        const_cast<binding::Symbol&>(bad.symbols.symbol(*root.symbol_id));
    symbol.type = ".bf16x2";
    root.declared_type = ScalarType::BF16x2;
    for (auto& lane : mask->value.elements) {
      ASSERT_TRUE(lane);
      lane->declared_type = ScalarType::BF16x2;
    }
    EXPECT_FALSE(validateModule(bad));
    EXPECT_FALSE(validateModule(*ast, bad));
    expectNamedMaskOwner(*owner);
  }
}

/** Borrowed checker mirrors must identify the actual payload and ordered lane addresses. */
TEST(TcgenNamedMask, ActualPayloadAndViewMirrorsMustAgree) {
  const auto& database = tcgen05_resolved_descriptor();
  const auto variant =
      std::find_if(database.variants.begin(), database.variants.end(),
                   [](const auto& v) { return v.variant_name == "MmaF16"; });
  ASSERT_NE(variant, database.variants.end());
  ASSERT_FALSE(variant->operand_layouts.empty());
  const auto& bindings = variant->operand_layouts.front().bindings;
  const auto binding = std::find_if(
      bindings.begin(), bindings.end(),
      [](const auto& b) { return b.target_field_id == "disable_output_lane"; });
  ASSERT_NE(binding, bindings.end());
  for (const auto& item : mask_cases) {
    auto owner = namedMaskOwned(namedMaskSource(item, 1, "b32", true));
    ASSERT_TRUE(owner);
    auto* mask = namedMaskField(namedMaskInstruction(*owner));
    ASSERT_TRUE(mask);
    checker::OperandView view{.field_id = "disable_output_lane",
                              .actual_shape = checker::OperandShape::Vector,
                              .register_vector = &mask->value,
                              .vector_arity = mask->value.elements.size(),
                              .locations = mask->locs};
    ASSERT_EQ(view.vector_arity, 4u);
    for (size_t lane = 0; lane < view.vector_arity; ++lane) {
      ASSERT_TRUE(mask->value.elements[lane]);
      view.vector_element_shapes[lane] = checker::OperandShape::Register;
      view.vector_element_types[lane] = ScalarType::B32;
      view.vector_element_registers[lane] = &*mask->value.elements[lane];
    }
    const auto check = [&] {
      return checker::check_operands(std::span{&*binding, 1}, {},
                                     std::span{&view, 1}, {},
                                     namedMaskContext());
    };
    EXPECT_TRUE(check());
    const auto original = view;
    view.vector_element_registers[0] = view.vector_element_registers[1];
    EXPECT_FALSE(check());
    view = original;
    view.vector_arity = 3;
    EXPECT_FALSE(check());
    view = original;
    view.actual_shape = checker::OperandShape::Register;
    EXPECT_FALSE(check());
    view = original;
    view.vector_element_types[0] = ScalarType::F64;
    EXPECT_FALSE(check());
    view = original;
    EXPECT_TRUE(check());
    expectNamedMaskOwner(*owner);
  }
}

/** Scalar, special, array and unresolved family spellings do not become named vector containers. */
TEST(TcgenNamedMask, SourceContainerIdentityRemainsRequired) {
  const auto& item = mask_cases.front();
  for (const auto operand : {"p", "%tid", "Array", "V", "V2", "V1.x"}) {
    auto source = namedMaskSource(item, 1, "b32", true, false, 4, operand);
    const auto at = source.find(".reg .b32 d");
    ASSERT_NE(at, std::string::npos);
    source.insert(at, ".local .b32 Array[4];");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

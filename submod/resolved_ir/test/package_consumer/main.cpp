#include <ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_call_argument_compatibility.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Verify entry input metadata through only the installed resolved-IR API. */
int check_entry_parameter_metadata() {
  std::optional<ptx_frontend::resolved_ir::ResolvedModule> resolved_module;
  {
    const std::string fixture = R"ptx(
.entry declared(.param .u16 scalar,
                 .param .align 8 .b8 declaration_unsized[]) { }
.entry defined(.param .u32 scalar, .param .align 16 .u64 aligned,
               .param .u64 .ptr generic_pointer,
               .param .u64 .ptr .global .align 32 global_pointer,
               .param .align 8 .b8 values[2 * 4]) { }
.func device_only(.param .u32 ignored) { }
)ptx";
    ptx_frontend::PtxSyntaxParser parser(fixture);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return 31;
    auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
    if (!resolved)
      return 32;
    resolved_module = std::move(*resolved);
  }

  const auto& functions = resolved_module->functions;
  if (functions.size() != 3 || !functions[0].is_entry ||
      functions[0].is_prototype || !functions[1].is_entry ||
      functions[1].is_prototype || functions[2].is_entry ||
      !functions[2].entry_parameters.empty()) {
    return 33;
  }

  const auto& declared = functions[0].entry_parameters;
  const auto& defined = functions[1].entry_parameters;
  if (declared.size() != 2 || defined.size() != 5 ||
      declared[0].type != ".u16" || declared[0].alignment != 2 ||
      declared[0].is_array || declared[0].array_extent || declared[0].pointer ||
      declared[1].type != ".b8" || declared[1].alignment != 8 ||
      !declared[1].is_array || declared[1].array_extent ||
      declared[1].pointer || defined[0].type != ".u32" ||
      defined[0].alignment != 4 || defined[0].is_array ||
      defined[0].array_extent || defined[0].pointer ||
      defined[1].type != ".u64" || defined[1].alignment != 16 ||
      defined[1].is_array || defined[1].array_extent || defined[1].pointer ||
      defined[2].type != ".u64" || defined[2].alignment != 8 ||
      !defined[2].pointer || defined[2].pointer->pointed_state_space ||
      defined[2].pointer->pointed_alignment != 4 || defined[3].type != ".u64" ||
      defined[3].alignment != 8 || !defined[3].pointer ||
      defined[3].pointer->pointed_state_space !=
          ptx_frontend::call_argument_compatibility::PointedStateSpace::
              Global ||
      defined[3].pointer->pointed_alignment != 32 || defined[4].type != ".b8" ||
      defined[4].alignment != 8 || !defined[4].is_array ||
      defined[4].array_extent != 8 || defined[4].pointer) {
    return 34;
  }

  const auto declared_function_scope =
      resolved_module->symbols.symbol(functions[0].symbol_id).owned_scope;
  const auto defined_function_scope =
      resolved_module->symbols.symbol(functions[1].symbol_id).owned_scope;
  if (!declared_function_scope || !defined_function_scope ||
      resolved_module->symbols.symbol(declared[0].symbol_id).name !=
          "scalar" ||
      resolved_module->symbols.symbol(declared[1].symbol_id).name !=
          "declaration_unsized" ||
      resolved_module->symbols.symbol(defined[0].symbol_id).name != "scalar" ||
      resolved_module->symbols.symbol(defined[1].symbol_id).name != "aligned" ||
      resolved_module->symbols.symbol(defined[2].symbol_id).name !=
          "generic_pointer" ||
      resolved_module->symbols.symbol(defined[3].symbol_id).name !=
          "global_pointer" ||
      resolved_module->symbols.symbol(defined[4].symbol_id).name != "values" ||
      resolved_module->symbols.symbol(declared[0].symbol_id).scope !=
          *declared_function_scope ||
      resolved_module->symbols.symbol(defined[0].symbol_id).scope !=
          *defined_function_scope ||
      resolved_module->symbols.symbol(defined[4].symbol_id).scope !=
          *defined_function_scope ||
      declared[0].symbol_id == defined[0].symbol_id) {
    return 35;
  }
  return 0;
}

/** Verify owned storage metadata using only independently included installed headers. */
int check_storage_declaration_metadata() {
  std::optional<ptx_frontend::resolved_ir::ResolvedModule> resolved_module;
  {
    const std::string fixture = R"ptx(
.extern .shared .align 16 .b8 dynamic[];
.global .u32 data[2][3] = {{1, 2}, {3}};
.const .f32 weights[] = {0.5, -0.25};
.global .u64 pointer = generic(data) + 4;
.const .b128 wide[] = {-1, -1U};
.entry storage_kernel() {
  .shared .align 8 .b8 tile[16];
  .local .u16 scratch[2][2];
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(fixture);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return 70;
    auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
    if (!resolved)
      return 71;
    resolved_module = std::move(*resolved);
  }

  using ptx_frontend::base::ScalarType;
  using namespace ptx_frontend::resolved_ir;
  const auto& declarations = resolved_module->storage_declarations;
  if (declarations.size() != 7)
    return 72;
  const auto& dynamic = declarations[0];
  const auto& data = declarations[1];
  const auto& weights = declarations[2];
  const auto& pointer = declarations[3];
  const auto& wide = declarations[4];
  const auto& tile = declarations[5];
  const auto& scratch = declarations[6];
  if (dynamic.space != StorageSpace::Shared ||
      dynamic.declaration_kind != StorageDeclarationKind::External ||
      !dynamic.is_dynamic_shared || dynamic.byte_extent ||
      dynamic.array_extents.size() != 1 || dynamic.array_extents[0] ||
      dynamic.initialization != StorageInitializationKind::External ||
      data.space != StorageSpace::Global ||
      data.element_type != StorageElementType{ScalarType::U32} ||
      data.byte_extent != 24 || data.initializer.size() != 3 ||
      data.initializer[0].byte_offset != 0 ||
      data.initializer[1].byte_offset != 4 ||
      data.initializer[2].byte_offset != 12 ||
      weights.space != StorageSpace::Constant ||
      weights.element_type != StorageElementType{ScalarType::F32} ||
      weights.byte_extent != 8 || weights.initializer.size() != 2 ||
      tile.space != StorageSpace::Shared || tile.byte_extent != 16 ||
      tile.alignment != 8 || scratch.space != StorageSpace::Local ||
      scratch.byte_extent != 8 || !scratch.owner_function) {
    return 73;
  }
  if (pointer.initialization != StorageInitializationKind::Explicit ||
      pointer.initializer.size() != 1) {
    return 74;
  }
  const auto* relocation =
      std::get_if<StorageRelocation>(&pointer.initializer.front().value);
  if (relocation == nullptr || relocation->symbol_id != data.symbol_id ||
      relocation->address_kind != StorageAddressKind::Generic ||
      relocation->addend_bits != 4 || relocation->byte_mask) {
    return 74;
  }
  if (wide.element_type != StorageElementType{ScalarType::B128} ||
      wide.byte_extent != 32 || wide.alignment != 16 ||
      wide.initialization != StorageInitializationKind::Explicit ||
      wide.initializer.size() != 2 || wide.initializer[0].byte_offset != 0 ||
      wide.initializer[1].byte_offset != 16) {
    return 75;
  }
  const auto* signed_value =
      std::get_if<StorageConstant>(&wide.initializer[0].value);
  const auto* unsigned_value =
      std::get_if<StorageConstant>(&wide.initializer[1].value);
  if (signed_value == nullptr || unsigned_value == nullptr ||
      signed_value->bits != std::numeric_limits<uint64_t>::max() ||
      signed_value->high_bits != std::numeric_limits<uint64_t>::max() ||
      unsigned_value->bits != std::numeric_limits<uint64_t>::max() ||
      unsigned_value->high_bits != 0) {
    return 76;
  }
  return 0;
}

/** Verify the complete FMA family through installed parsing and checker APIs. */
int check_fma_contract() {
  using namespace ptx_frontend::resolved_ir;
  constexpr std::string_view fixture = R"ptx(
.version 9.3
.target sm_100
.address_size 64
.entry fma_forms() {
  .reg .f32 %f<4>;
  .reg .f64 %d<4>;
  .reg .f16 %h<4>;
  .reg .b16 %b<4>;
  .reg .b32 %p<4>;
  .reg .b64 %q<4>;
  fma.rn.ftz.sat.f32 %f0, 0d3ff0000000000000, 1e300, %f3;
  fma.rz.f32 %f0, %f1, %f2, %f3;
  fma.rn.f64 %d0, 0f3f800000, %d2, %d3;
  fma.rp.f64 %d0, %d1, %d2, %d3;
  fma.rm.ftz.f32x2 %q0, %q1, %q2, %q3;
  fma.rn.sat.f16 %h0, %b1, %h2, %h3;
  fma.rn.ftz.f16x2 %p0, %p1, %p2, %p3;
  fma.rn.ftz.relu.f16 %h0, %h1, %h2, %h3;
  fma.rn.oob.sat.f16 %h0, %h1, %h2, %h3;
  fma.rn.oob.relu.f16x2 %p0, %p1, %p2, %p3;
  fma.rn.relu.bf16 %b0, %b1, %b2, %b3;
  fma.rn.bf16x2 %p0, %p1, %p2, %p3;
  fma.rn.oob.bf16 %b0, %b1, %b2, %b3;
  fma.rn.oob.relu.bf16x2 %p0, %p1, %p2, %p3;
  fma.rp.sat.f32.f16 %f0, %h1, %b2, 1.0;
  fma.rm.f32.bf16 %f0, %b1, %b2, %f3;
}
)ptx";
  ptx_frontend::PtxSyntaxParser parser(fixture);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return 40;
  const auto module = resolveModule(*ast);
  if (!module || module->functions.size() != 1 ||
      module->functions.front().body.size() != 16 ||
      Fma::get_syntax_descriptor().variants.size() != 16)
    return 41;

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  for (const auto& instruction : module->functions.front().body) {
    const auto* fma = std::get_if<Fma>(&instruction);
    if (!fma || !checker::check(*fma, context))
      return 42;
  }
  const auto& mixed_instruction =
      std::get<Fma>(module->functions.front().body[14]);
  const auto* mixed = std::get_if<Fma::MixedF32F16>(&mixed_instruction.variant);
  if (!mixed || mixed->result_type != ScalarType::F32 ||
      mixed->input_type != ScalarType::F16 ||
      mixed->rounding.value != RoundingMode::Rp || !mixed->saturate.value ||
      mixed->dst.value.declared_type != ScalarType::F32 ||
      mixed->src1.value.declared_type != ScalarType::F16 ||
      mixed->src2.value.declared_type != ScalarType::B16)
    return 43;
  const auto* addend = std::get_if<ResolvedImmediate>(&mixed->src3.value);
  if (!addend || addend->type != ScalarType::F32 || addend->bits != 0x3f800000)
    return 44;
  const checker::Context old_target{
      .target = {.ptx_version = {8, 6}, .sm_version = 90}};
  if (checker::check(mixed_instruction, old_target))
    return 45;
  return 0;
}

int main() {
  constexpr std::string_view source = "add.u32 %r0, %r1, 1;";
  ptx_frontend::PtxCstParser cst_parser(source);
  const auto cst = cst_parser.parseInstruction();
  if (!cst || cst->sourceText() != source)
    return 1;

  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseInstruction();
  if (!ast)
    return 2;

  const auto resolved =
      ptx_frontend::resolved_ir::resolve<ptx_frontend::resolved_ir::Add>(*ast);
  if (!resolved)
    return 3;

  constexpr std::string_view module_source =
      ".version 8.0\n.target sm_80\n.address_size 64\n"
      ".visible .global .u32 counter;\n"
      ".entry kernel(.param .u32 n) { .reg .u32 %r<2>; start: "
      "add.u32 %r0, %r1, 1; }";
  ptx_frontend::PtxSyntaxParser module_parser(module_source);
  const auto module = module_parser.parseModule();
  if (!module || module->items.size() != 5 ||
      !std::holds_alternative<ptx_frontend::syntax_ast::AstVariableDeclaration>(
          module->items[3]))
    return 4;
  const auto& function =
      std::get<ptx_frontend::syntax_ast::AstFunction>(module->items[4]);
  if (function.parameters.size() != 1 || function.body.size() != 3)
    return 5;

  const auto symbols = ptx_frontend::binding::bindSymbols(*module);
  if (!symbols.diagnostics.empty() || symbols.table.scopes().size() != 2)
    return 6;
  if (!ptx_frontend::declaration_semantics::checkDeclarations(*module,
                                                              symbols.table)
           .empty())
    return 12;
  const auto kernel =
      symbols.table.lookup(symbols.table.moduleScope(), "kernel");
  if (!kernel)
    return 7;
  const auto counter =
      symbols.table.lookup(symbols.table.moduleScope(), "counter");
  if (!counter ||
      symbols.table.symbol(counter->symbol).linkage !=
          ptx_frontend::binding::SymbolLinkage::Visible ||
      !ptx_frontend::binding::isSpecialRegister("%laneid") ||
      ptx_frontend::binding::isSpecialRegister("%envreg32"))
    return 8;
  const auto function_scope = symbols.table.symbol(kernel->symbol).owned_scope;
  if (!function_scope || !symbols.table.lookup(*function_scope, "%r1"))
    return 9;

  const auto resolved_module =
      ptx_frontend::resolved_ir::resolveModule(*module);
  if (!resolved_module || resolved_module->functions.size() != 1 ||
      resolved_module->functions.front().body.size() != 1)
    return 10;
  const auto& resolved_add = std::get<ptx_frontend::resolved_ir::Add>(
      resolved_module->functions.front().body.front());
  const auto& integer_add =
      std::get<ptx_frontend::resolved_ir::Add::IntegerNoSat>(
          resolved_add.variant);
  if (!integer_add.dst.value.symbol_id)
    return 11;

  bool add_has_legacy_mixed_precision_order = false;
  for (const auto& variant :
       ptx_frontend::resolved_ir::Add::get_syntax_descriptor().variants) {
    if (variant.variant_name != "MixedF32" ||
        variant.modifier_order_aliases.size() != 1 ||
        variant.modifier_order_aliases.front().modifiers.size() != 5) {
      continue;
    }
    const auto& legacy_order = variant.modifier_order_aliases.front().modifiers;
    add_has_legacy_mixed_precision_order =
        legacy_order[0].kind_id == "rounding" &&
        legacy_order[1].kind_id == "result_type" &&
        legacy_order[2].kind_id == "input_type" &&
        legacy_order[4].kind_id == "sat";
  }
  if (!add_has_legacy_mixed_precision_order)
    return 30;

  if (const int metadata_result = check_entry_parameter_metadata();
      metadata_result != 0) {
    return metadata_result;
  }
  if (const int storage_result = check_storage_declaration_metadata();
      storage_result != 0) {
    return storage_result;
  }
  if (const int fma_result = check_fma_contract(); fma_result != 0)
    return fma_result;

  ptx_frontend::PtxSyntaxParser call_parser(
      "call (%result), callee, (%argument, 1);");
  const auto call = call_parser.parseInstruction();
  if (!call || call->operands.size() != 3 ||
      !std::holds_alternative<ptx_frontend::syntax_ast::AstCallParameterList>(
          call->operands[0]) ||
      !std::holds_alternative<ptx_frontend::syntax_ast::AstCallTarget>(
          call->operands[1]))
    return 13;

  constexpr std::string_view valid_call_module = R"ptx(
.func callee(.reg .u32 register_input, .param .u32 parameter_input,
             .param .s16 literal_input);
.entry caller() {
  .reg .u32 %r;
  .param .u32 parameter_argument;
  call callee, (%r, parameter_argument, -4);
}
)ptx";
  ptx_frontend::PtxSyntaxParser valid_call_parser(valid_call_module);
  const auto valid_call_ast = valid_call_parser.parseModule();
  if (!valid_call_ast)
    return 14;
  const auto valid_call_resolved =
      ptx_frontend::resolved_ir::resolveModule(*valid_call_ast);
  if (!valid_call_resolved || valid_call_resolved->functions.size() != 2 ||
      valid_call_resolved->functions[1].body.size() != 1 ||
      !std::holds_alternative<ptx_frontend::resolved_ir::Call>(
          valid_call_resolved->functions[1].body.front())) {
    return 15;
  }

  constexpr std::string_view invalid_call_module = R"ptx(
.func callee(.reg .u32 input);
.entry caller() {
  .reg .u64 %wide;
  call callee, (%wide);
}
)ptx";
  ptx_frontend::PtxSyntaxParser invalid_call_parser(invalid_call_module);
  const auto invalid_call_ast = invalid_call_parser.parseModule();
  if (!invalid_call_ast)
    return 16;
  const auto invalid_call_resolved =
      ptx_frontend::resolved_ir::resolveModule(*invalid_call_ast);
  if (invalid_call_resolved || invalid_call_resolved.error().size() != 1 ||
      invalid_call_resolved.error().front().message !=
          "Direct call input argument 1 for 'callee' has type or vector "
          "shape mismatch.") {
    return 17;
  }

  constexpr std::string_view control_flow_corpus = R"ptx(
.version 9.3
.target sm_30
.address_size 64
.func direct_callee(.reg .u32 input);
.func listed_callee(.reg .u32 input);
.entry caller() {
  .reg .u64 %fptr;
  .reg .u32 %direct_input, %indirect_input, %index;
empty: .callprototype _;
targets: .calltargets listed_callee;
branches: .branchtargets done;
  call direct_callee, (%direct_input);
  call %fptr, empty;
  call %fptr, (%indirect_input), targets;
  brx.idx %index, branches;
done:
}
)ptx";
  ptx_frontend::PtxSyntaxParser control_flow_parser(control_flow_corpus);
  const auto control_flow_ast = control_flow_parser.parseModule();
  if (!control_flow_ast)
    return 18;
  const auto control_flow_resolved =
      ptx_frontend::resolved_ir::resolveModule(*control_flow_ast);
  if (!control_flow_resolved || control_flow_resolved->functions.size() != 3 ||
      control_flow_resolved->functions[2].body.size() != 4)
    return 19;
  const ptx_frontend::resolved_ir::checker::Context control_flow_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 30},
      .instruction_range = {},
  };
  for (const auto& instruction : control_flow_resolved->functions[2].body) {
    const auto checked = std::visit(
        [&](const auto& value) {
          return ptx_frontend::resolved_ir::checker::check(
              value, control_flow_context);
        },
        instruction);
    if (!checked)
      return 20;
  }

  constexpr std::string_view directive_corpus = R"ptx(
.version 9.3
.target sm_30
.address_size 64
.file 0x1U "directive-corpus.ptx"
.section .debug_str { debug_name: .b8 0; };
.pragma "module";
.entry directives() .pragma "entry"; .maxnreg 32 .maxntid 32, 1, 1
    .minnctapersm 1 {
  .reg .u32 %r0, %r1;
  .loc 0x1U 1 0
  {
    .pragma "nested";
    .loc 1 2 0, function_name debug_name, inlined_at 1 1 0;
    add.u32 %r0, %r1, 1;
  }
}
)ptx";
  ptx_frontend::PtxCstParser directive_cst_parser(directive_corpus);
  const auto directive_cst = directive_cst_parser.parseModule();
  if (!directive_cst || !directive_cst.diagnostics.empty() ||
      directive_cst->sourceText() != directive_corpus) {
    return 21;
  }
  ptx_frontend::PtxSyntaxParser directive_parser(directive_corpus);
  const auto directive_ast = directive_parser.parseModule();
  if (!directive_ast || !directive_ast.diagnostics.empty())
    return 22;
  const auto directive_symbols =
      ptx_frontend::binding::bindSymbols(*directive_ast);
  if (!directive_symbols.diagnostics.empty() ||
      !ptx_frontend::declaration_semantics::checkDeclarations(
          *directive_ast, directive_symbols.table)
           .empty()) {
    return 23;
  }
  const auto directive_resolved =
      ptx_frontend::resolved_ir::resolveModule(*directive_ast);
  if (!directive_resolved || directive_resolved->functions.size() != 1 ||
      directive_resolved->functions.front().body.size() != 1 ||
      !std::holds_alternative<ptx_frontend::resolved_ir::Add>(
          directive_resolved->functions.front().body.front())) {
    return 24;
  }
  const ptx_frontend::resolved_ir::checker::Context directive_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 30},
      .instruction_range = {},
  };
  if (!ptx_frontend::resolved_ir::checker::check(
          std::get<ptx_frontend::resolved_ir::Add>(
              directive_resolved->functions.front().body.front()),
          directive_context)) {
    return 25;
  }

  ptx_frontend::PtxSyntaxParser overflow_parser(
      ".file 18446744073709551616U \"overflow.ptx\"\n"
      ".entry overflow() { }");
  const auto overflow_ast = overflow_parser.parseModule();
  if (!overflow_ast || !overflow_ast.diagnostics.empty())
    return 26;
  const auto overflow_resolved =
      ptx_frontend::resolved_ir::resolveModule(*overflow_ast);
  if (overflow_resolved || overflow_resolved.error().size() != 1 ||
      overflow_resolved.error().front().message !=
          "Debug file index must be an unsigned 64-bit integer.") {
    return 27;
  }

  constexpr std::string_view unknown_directive_corpus = R"ptx(
.version 9.3
.language "C++";
.entry after_unknown() { .reg .u32 %r0, %r1; add.u32 %r0, %r1, 1; }
)ptx";
  ptx_frontend::PtxCstParser unknown_cst_parser(unknown_directive_corpus);
  const auto unknown_cst = unknown_cst_parser.parseModule();
  if (!unknown_cst || unknown_cst.diagnostics.size() != 1 ||
      unknown_cst->sourceText() != unknown_directive_corpus ||
      unknown_cst.diagnostics.front().message !=
          "expected module directive, variable declaration, or function") {
    return 28;
  }
  ptx_frontend::PtxSyntaxParser unknown_parser(unknown_directive_corpus);
  const auto unknown_ast = unknown_parser.parseModule();
  if (!unknown_ast || unknown_ast.diagnostics.size() != 1 ||
      unknown_ast.diagnostics.front().message !=
          unknown_cst.diagnostics.front().message ||
      unknown_ast->items.size() != 2 ||
      !std::holds_alternative<ptx_frontend::syntax_ast::AstFunction>(
          unknown_ast->items.back())) {
    return 29;
  }
  return 0;
}

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Build a real target-context call with a u32 register formal and owned storage. */
std::string table_module(
    std::string_view declaration = ".global .u64 T[4] = {f,g,f};",
    std::string_view body = "call %fp, (1+2), T;", std::string_view locals = "",
    std::string_view functions =
        ".func f(.reg .u32 x); .func g(.reg .u32 y);") {
  return ".version 9.3\n.target sm_80\n.address_size 64\n" +
         std::string(functions) + "\n" + std::string(declaration) +
         "\n.entry kernel() { .reg .u64 %fp; .reg .u32 %r;\n" +
         std::string(locals) + "\n" + std::string(body) + "\n}";
}

/** Borrow the actual flist branch of the final caller's first instruction. */
ResolvedCallTableRef& table_ref(ResolvedModule& module) {
  auto& call = dynamic_cast<CallDirect&>(*module.functions.back().body.front());
  return std::get<ResolvedCallTableRef>(call.metadata->value);
}

/** Domain variants preserve sparse tails, repeated slots and actual storage IDs. */
TEST(CallTables, ResolvesOwnedStorageDomainsAndSparseSlots) {
  for (const auto declaration :
       {".global .u64 T[4] = {f,g,f};", ".const .u32 T[] = {f,g,f};",
        ".global .u32 T[8] = {f};", ".const .u64 T[3] = {(f),g,f};"}) {
    SCOPED_TRACE(declaration);
    auto ast = test_helpers::parseModule(table_module(declaration));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    ASSERT_EQ(module->call_tables.size(), 1u);
    const auto& table = module->call_tables.front();
    const auto& storage = module->storage_declarations.back();
    EXPECT_EQ(table.symbol_id, storage.symbol_id);
    EXPECT_EQ(table_ref(*module).symbol_id, storage.symbol_id);
    EXPECT_EQ(module->symbols.symbol(table.symbol_id).kind,
              binding::SymbolKind::Variable);
    ASSERT_EQ(table.slots.size(), storage.initializer.size());
    for (size_t index = 0; index < table.slots.size(); ++index) {
      EXPECT_EQ(table.slots[index].index, index);
      EXPECT_EQ(table.slots[index].byte_offset,
                storage.initializer[index].byte_offset);
      EXPECT_EQ(table.slots[index].range, storage.initializer[index].range);
      EXPECT_NE(table.slots[index].target_range, SourceRange{});
    }
    if (table.slots.size() == 3)
      EXPECT_EQ(table.slots[0].symbol_id, table.slots[2].symbol_id);
    EXPECT_TRUE(validateModule(*module));
  }
}

/** The reported zero-argument call owns its table after source destruction. */
TEST(CallTables, ResolvesOriginalZeroArgumentFunctionArrayCall) {
  const auto module = [] {
    const auto ast = test_helpers::parseModule(R"(
.version 9.3
.target sm_80
.address_size 64
.func callee() { ret; }
.global .align 8 .u64 targets[1] = {callee};
.entry kernel() {
  .reg .u64 %fp;
  mov.u64 %fp, callee;
  call %fp, targets;
  ret;
}
)");
    EXPECT_TRUE(ast);
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(module) << module.error().front().message;
  ASSERT_EQ(module->call_tables.size(), 1u);
  EXPECT_TRUE(module->call_tables.front().signature.parameters.empty());
  ASSERT_EQ(module->call_tables.front().slots.size(), 1u);
  EXPECT_EQ(module->call_tables.front().symbol_id,
            module->storage_declarations.front().symbol_id);
  EXPECT_TRUE(validateModule(*module));
  const auto copy = *module;
  EXPECT_TRUE(validateModule(copy));
}

/** Local visibility and aliases preserve their own identities and canonical ABI. */
TEST(CallTables, ResolvesLocalShadowingAliasesAndDefinitions) {
  const auto ast = test_helpers::parseModule(
      table_module(".global .u64 T[1] = {f};",
                   "{ .const .u32 T[] = {alias_fn,f}; call %fp, (7), T; } call "
                   "%fp, (%r), T;",
                   "",
                   ".func f(.reg .u32 x); .func alias_fn(.reg .u32 x); "
                   ".func f(.reg .u32 x) { ret; } .alias alias_fn, f;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  ASSERT_EQ(module->call_tables.size(), 2u);
  EXPECT_NE(module->call_tables[0].symbol_id, module->call_tables[1].symbol_id);
  const auto& local = module->call_tables.front();
  EXPECT_EQ(local.owner_function, module->functions.back().symbol_id);
  ASSERT_EQ(local.slots.size(), 2u);
  EXPECT_NE(local.slots[0].symbol_id, local.slots[1].symbol_id);
  EXPECT_EQ(local.slots[0].canonical_function,
            local.slots[1].canonical_function);
  EXPECT_TRUE(validateModule(*module));
}

/** Return formals and integer source expressions share the established call ABI. */
TEST(CallTables, ConvertsInputsAgainstFullFormalSignature) {
  const auto ast = test_helpers::parseModule(
      table_module(".global .u32 T[] = {f};", "call (%r), %fp, ((1+2)*3), T;",
                   "", ".func (.reg .u32 out) f(.reg .u32 in);"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  const auto& call =
      dynamic_cast<const CallDirect&>(*module->functions.back().body.front());
  ASSERT_TRUE(call.arguments);
  const auto& literal =
      std::get<ResolvedCallLiteral>(call.arguments->value.values.front().value);
  ASSERT_TRUE(literal.value);
  EXPECT_EQ(literal.value->bits, 9u);
  EXPECT_EQ(module->call_tables.front().signature.return_parameters.size(), 1u);
  EXPECT_TRUE(validateModule(*module));
}

/** Table element width is independent of the containing address-size domain. */
TEST(CallTables, AcceptsWideTableWithThirtyTwoBitFunctionPointer) {
  auto source = table_module();
  source.replace(source.find(".address_size 64"), 16, ".address_size 32");
  source.replace(source.find(".reg .u64 %fp"), 13, ".reg .u32 %fp");
  const auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  EXPECT_EQ(module->call_tables.front().element_type, base::ScalarType::U64);
  EXPECT_TRUE(validateModule(*module));
}

/** Parameter-space return/input staging uses the same indirect-call ABI path. */
TEST(CallTables, PreservesParameterSpaceCallStaging) {
  const auto ast = test_helpers::parseModule(table_module(
      ".const .u64 T[]={f};",
      "st.param.b32 [arg], %r; call (result), %fp, (arg), T; ld.param.b32 "
      "%r,[result];",
      ".param .b32 arg,result;", ".func (.param .b32 out) f(.param .b32 in);"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  EXPECT_TRUE(validateModule(*module));
}

/** Unsupported table forms remain cleanly rejected only at flist consumption. */
TEST(CallTables, RejectsExcludedStorageAndInitializerDomains) {
  for (const auto declaration :
       {".global .u64 T = f;", ".global .u64 T[2][1] = {{f},{g}};",
        ".global .v2 .u64 T[1] = {f,g};", ".global .u64 T[2];",
        ".extern .global .u64 T[2];", ".global .u64 T[2] = {1,2};",
        ".global .u64 T[2] = {f,0};", ".global .u64 T[1] = {f+1};",
        ".global .u64 T[1] = {0xff(f)};",
        ".global .u32 data; .global .u64 T[1] = {data};",
        ".global .u64 T[1] = {generic(data)}; .global .u32 data;"}) {
    SCOPED_TRACE(declaration);
    const auto ast = test_helpers::parseModule(table_module(declaration));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  for (const auto local :
       {".local .u64 T[2];", ".shared .u64 T[2];", ".param .u64 T[2];"}) {
    SCOPED_TRACE(local);
    const auto ast =
        test_helpers::parseModule(table_module("", "call %fp, (1), T;", local));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Ordinary data is not globally constrained to a callable target set. */
TEST(CallTables, LeavesUnconsumedDataUnclassified) {
  const auto ast = test_helpers::parseModule(
      table_module(".global .u64 T[3] = {f,g,0};", "", "",
                   ".func f(.reg .u32 x); .func g(.reg .f32 y);"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  EXPECT_TRUE(module->call_tables.empty());
}

/** Exact lexical occurrence ordering applies to tables and initializer functions. */
TEST(CallTables, RejectsForwardTablesFunctionsAndAliases) {
  for (const auto source :
       {table_module("", "call %fp, (1), T; .global .u64 T[1] = {f};"),
        table_module("", "call %fp, (1), T;") + ".global .u64 T[1] = {f};",
        table_module(".global .u64 T[1] = {f}; .func f(.reg .u32 x);",
                     "call %fp,(1),T;", "", ""),
        table_module(".global .u64 T[1] = {alias_fn}; .func alias_fn(.reg .u32 "
                     "x); .alias alias_fn,f;",
                     "call %fp,(1),T;", "", ".func f(.reg .u32 x) { ret; }")}) {
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Complete signatures, including ABI attributes, must agree before call typing. */
TEST(CallTables, RejectsSignatureAndCallAbiMismatches) {
  for (const auto functions :
       {".func f(.reg .u32 x); .func g(.reg .f32 x);",
        ".func f(.reg .u32 x); .func g(.param .u32 x);",
        ".func f(.reg .u32 x); .func (.reg .u32 out) g(.reg .u32 x);",
        ".func f(.param .align 4 .b8 x[4]); .func g(.param .align 8 .b8 x[4]);",
        ".func f(.param .align 4 .b8 x[4]); .func g(.param .align 4 .b8 x[8]);",
        ".func f(.reg .u32 x); .func g(.reg .u32 x) .noreturn;"}) {
    SCOPED_TRACE(functions);
    const auto ast = test_helpers::parseModule(table_module(
        ".global .u64 T[2]={f,g};", "call %fp,(1),T;", "", functions));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto resolved = resolveAndValidateModule(*ast);
    ASSERT_FALSE(resolved);
    ASSERT_FALSE(resolved.error().empty());
    EXPECT_EQ(resolved.error().front().message,
              "Call table targets require identical full function signatures.");
  }
  const auto entry_target = test_helpers::parseModule(
      table_module(".global .u64 T[2]={f,g};", "call %fp,(1),T;", "",
                   ".func f(.reg .u32 x); .entry g() { ret; }"));
  ASSERT_MODULE_PARSE_SUCCEEDS(entry_target);
  EXPECT_FALSE(resolveAndValidateModule(*entry_target));
  for (const auto call :
       {"call %fp,T;", "call %fp,(1,2),T;", "call %fp,(1.0),T;",
        "call %fp,(0x100000000),T;", "call (%r),%fp,(1),T;"}) {
    SCOPED_TRACE(call);
    const auto ast =
        test_helpers::parseModule(table_module(".global .u64 T[]={f};", call));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Device-function pointer grammar remains excluded independently of tables. */
TEST(CallTables, PreservesDeviceFunctionPointerExclusion) {
  const auto ast = test_helpers::parseModule(
      table_module(".global .u64 T[]={f};", "call %fp,(1),T;", "",
                   ".func f(.param .u64 .ptr .global .align 8 x);"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto resolved = resolveAndValidateModule(*ast);
  ASSERT_FALSE(resolved);
  ASSERT_FALSE(resolved.error().empty());
  EXPECT_EQ(resolved.error().front().message,
            ".ptr is supported only on scalar .entry .param .u32/.u64 inputs.");
}

/** Full owned signature comparison includes pointer fields, not only scalar ABI. */
TEST(CallTables, RejoinsOwnedPointerSignatureFields) {
  for (unsigned mutation = 0; mutation < 3; ++mutation) {
    const auto ast = test_helpers::parseModule(table_module());
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    ASSERT_TRUE(validateModule(*module));
    auto& formal = module->functions[1].contract.signature.parameters.front();
    if (mutation == 0)
      formal.is_pointer = true;
    else if (mutation == 1)
      formal.pointed_state_space =
          call_argument_compatibility::PointedStateSpace::Global;
    else
      formal.pointer_alignment = uint64_t{16};
    const auto checked = validateModule(*module);
    ASSERT_FALSE(checked);
    EXPECT_TRUE(std::ranges::any_of(checked.error(), [](const auto&
                                                            diagnostic) {
      return diagnostic.message ==
             "Call table targets require identical full function signatures.";
    }));
  }
}

/** A genuine table payload cannot be transplanted into the register callee slot. */
TEST(CallTables, RejectsCoherentCalleeMetadataSwap) {
  const auto ast = test_helpers::parseModule(table_module());
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  auto& call =
      dynamic_cast<CallDirect&>(*module->functions.back().body.front());
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.back().instruction_ranges.front()};
  ASSERT_TRUE(call.check(context));
  ASSERT_TRUE(validateModule(*module));
  ASSERT_TRUE(call.target_indirect_callee);
  ASSERT_TRUE(call.metadata);
  std::swap(*call.target_indirect_callee, *call.metadata);
  const auto checked = call.check(context);
  ASSERT_FALSE(checked);
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
    return diagnostic.message ==
           "Call table is allowed only in the final flist operand.";
  }));
  EXPECT_FALSE(validateModule(*module));
}

/** Owned tables and their visited real storage identity survive syntax destruction. */
TEST(CallTables, OwnsContractsAndVisitorAfterSourceRelease) {
  auto module = [&] {
    const auto ast = test_helpers::parseModule(table_module());
    EXPECT_TRUE(ast);
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(module) << module.error().front().message;
  auto copied = *module;
  auto moved = std::move(*module);
  EXPECT_EQ(copied.call_tables, moved.call_tables);
  EXPECT_TRUE(validateModule(copied));
  EXPECT_TRUE(validateModule(moved));
  /** Observe only actual table branches and never retain borrowed references. */
  struct Observer final : detail::IReferenceObserver {
    /** Storage identity copied without retaining instruction-owned data. */
    std::optional<binding::SymbolId> table;
    /** Preserve the real storage identity from the indirect union branch. */
    void indirect_callee(const ResolvedIndirectCallee& callee,
                         std::span<const SourceRange>,
                         checker::AddressSymbolResolutionPolicy) override {
      if (const auto* ref = std::get_if<ResolvedCallTableRef>(&callee))
        table = ref->symbol_id;
    }
  } observer;
  auto cloned = moved.functions.back().body.front()->clone();
  cloned->visit_references(observer);
  EXPECT_EQ(observer.table, moved.call_tables.front().symbol_id);
}

/** Each mutation starts from an independently validated complete-context baseline. */
TEST(CallTables, RejectsOwnedContractAndStorageTampering) {
  for (unsigned mutation = 0; mutation < 36; ++mutation) {
    SCOPED_TRACE(mutation);
    const auto ast = test_helpers::parseModule(table_module());
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    ASSERT_TRUE(validateModule(*module));
    auto& table = module->call_tables.front();
    auto& storage = module->storage_declarations.front();
    auto& ref = table_ref(*module);
    switch (mutation) {
      case 0:
        module->call_tables.clear();
        break;
      case 1:
        module->call_tables.push_back(table);
        break;
      case 2:
        table.symbol_id = module->functions.front().symbol_id;
        break;
      case 3:
        table.scope_id = module->functions.back().declaration_scope;
        break;
      case 4:
        table.owner_function = module->functions.back().symbol_id;
        break;
      case 5:
        table.name = "other";
        break;
      case 6:
        table.range = {};
        break;
      case 7:
        table.slots.pop_back();
        break;
      case 8:
        std::swap(table.slots[0], table.slots[1]);
        break;
      case 9:
        ++table.slots[0].byte_offset;
        break;
      case 10:
        table.slots[0].target_range = {};
        break;
      case 11:
        table.slots[0].canonical_function = module->functions.back().symbol_id;
        break;
      case 12:
        table.signature.parameters.clear();
        break;
      case 13:
        storage.array_extents = {2, 2};
        break;
      case 14:
        storage.element_type = base::ScalarType::U32;
        break;
      case 15:
        storage.space = StorageSpace::Shared;
        break;
      case 16:
        storage.initialization = StorageInitializationKind::Zero;
        break;
      case 17:
        storage.owner_function = module->functions.back().symbol_id;
        break;
      case 18:
        std::get<StorageRelocation>(storage.initializer[0].value).addend_bits =
            1;
        break;
      case 19:
        std::get<StorageRelocation>(storage.initializer[0].value).byte_mask =
            255;
        break;
      case 20:
        ref.spelling = "wrong";
        break;
      case 21:
        ref.range = {};
        break;
      case 22:
        ref.symbol_id = module->functions.front().symbol_id;
        break;
      case 23:
        std::get<StorageRelocation>(storage.initializer[0].value).symbol_id =
            table.slots[1].symbol_id;
        table.slots[0].symbol_id = table.slots[1].symbol_id;
        table.slots[0].canonical_function = table.slots[1].canonical_function;
        break;
      case 24:
        storage.initializer.pop_back();
        table.slots.pop_back();
        break;
      case 25:
        storage.range = module->range;
        table.range = module->range;
        break;
      case 26:
        storage.array_extents = {8};
        storage.byte_extent = 64;
        break;
      case 27:
        std::get<StorageRelocation>(storage.initializer[0].value).address_kind =
            StorageAddressKind::Generic;
        break;
      case 28:
        std::get<StorageRelocation>(storage.initializer[0].value)
            .target_range = {};
        break;
      case 29:
        storage.vector_width = 2;
        break;
      case 30:
        storage.declaration_kind = StorageDeclarationKind::External;
        break;
      case 31:
        module->storage_declarations.clear();
        break;
      case 32: {
        auto& call =
            dynamic_cast<CallDirect&>(*module->functions.back().body.front());
        auto& literal = std::get<ResolvedCallLiteral>(
            call.arguments->value.values.front().value);
        literal.value->bits ^= 1;
        break;
      }
      case 33:
        ++table.slots[0].index;
        break;
      case 34:
        ++storage.initializer[0].byte_offset;
        ++table.slots[0].byte_offset;
        break;
      case 35:
        storage.initializer[0].value = StorageConstant{};
        break;
    }
    EXPECT_FALSE(validateModule(*module));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

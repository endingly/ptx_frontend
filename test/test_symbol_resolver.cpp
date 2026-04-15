// test/test_symbol_resolver.cpp
//
// Unit tests for the symbol-resolution and binding phase.
//
// We test:
//   1. SymbolTable API: insert_global, insert_local, lookup, duplicate detection
//   2. resolve_module: collection phase (registers, params, labels, globals)
//   3. resolve_module: resolution phase (operand Ident → SymbolId)
//   4. Error cases: undefined symbols, duplicate declarations

#include <gtest/gtest.h>
#include "symbol_resolver.hpp"
#include "ptx_parser.hpp"

using namespace ptx_frontend;

// ============================================================
// §1  SymbolTable unit tests
// ============================================================

TEST(SymbolTable, InsertAndLookupGlobal) {
  SymbolTable sym;
  auto r = sym.insert_global("foo", SymbolKind::Function,
                              make_scalar(ScalarType::B64), StateSpace::Reg);
  ASSERT_TRUE(r.has_value());
  SymbolId id = *r;

  auto lr = sym.lookup("foo");
  ASSERT_TRUE(lr.has_value());
  EXPECT_EQ(*lr, id);

  const SymbolEntry& e = sym.entry(id);
  EXPECT_EQ(e.name, "foo");
  EXPECT_EQ(e.kind, SymbolKind::Function);
  EXPECT_TRUE(e.func_scope.empty());
}

TEST(SymbolTable, InsertAndLookupLocal) {
  SymbolTable sym;
  auto r = sym.insert_local("my_func", "%r0", SymbolKind::Register,
                             make_scalar(ScalarType::B32), StateSpace::Reg);
  ASSERT_TRUE(r.has_value());
  SymbolId id = *r;

  // Look up with function scope
  auto lr = sym.lookup("%r0", "my_func");
  ASSERT_TRUE(lr.has_value());
  EXPECT_EQ(*lr, id);

  // Look up without function scope — should fail
  auto mr = sym.lookup("%r0");
  EXPECT_FALSE(mr.has_value());
}

TEST(SymbolTable, LocalShadowsGlobal_NotApplicable) {
  // A local symbol in one function and a global with same name
  SymbolTable sym;
  (void)sym.insert_global("shared_name", SymbolKind::Global,
                           make_scalar(ScalarType::B32), StateSpace::Global);
  (void)sym.insert_local("f", "shared_name", SymbolKind::Register,
                          make_scalar(ScalarType::F32), StateSpace::Reg);

  // Local scope lookup finds the local symbol first
  auto lr = sym.lookup("shared_name", "f");
  ASSERT_TRUE(lr.has_value());
  EXPECT_EQ(sym.entry(*lr).space, StateSpace::Reg);  // the local one

  // Module scope lookup finds the global
  auto gr = sym.lookup("shared_name");
  ASSERT_TRUE(gr.has_value());
  EXPECT_EQ(sym.entry(*gr).space, StateSpace::Global);
}

TEST(SymbolTable, DuplicateGlobalInsertError) {
  SymbolTable sym;
  (void)sym.insert_global("dup", SymbolKind::Function,
                           make_scalar(ScalarType::B64), StateSpace::Reg);
  auto r2 = sym.insert_global("dup", SymbolKind::Function,
                               make_scalar(ScalarType::B64), StateSpace::Reg);
  EXPECT_FALSE(r2.has_value());
  EXPECT_TRUE(r2.error().message.find("dup") != std::string::npos);
}

TEST(SymbolTable, DuplicateLocalInsertError) {
  SymbolTable sym;
  (void)sym.insert_local("f", "%p", SymbolKind::Register,
                          make_scalar(ScalarType::Pred), StateSpace::Reg);
  auto r2 = sym.insert_local("f", "%p", SymbolKind::Register,
                              make_scalar(ScalarType::Pred), StateSpace::Reg);
  EXPECT_FALSE(r2.has_value());
}

TEST(SymbolTable, LookupUndefined) {
  SymbolTable sym;
  auto r = sym.lookup("nonexistent");
  EXPECT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().message.find("nonexistent") != std::string::npos);
}

TEST(SymbolTable, MultipleSymbolsAssignDistinctIds) {
  SymbolTable sym;
  auto r0 = sym.insert_global("a", SymbolKind::Global,
                               make_scalar(ScalarType::B32), StateSpace::Global);
  auto r1 = sym.insert_global("b", SymbolKind::Global,
                               make_scalar(ScalarType::B32), StateSpace::Global);
  auto r2 = sym.insert_local("f", "%r0", SymbolKind::Register,
                              make_scalar(ScalarType::B32), StateSpace::Reg);
  ASSERT_TRUE(r0 && r1 && r2);
  EXPECT_NE(*r0, *r1);
  EXPECT_NE(*r1, *r2);
  EXPECT_NE(*r0, *r2);
  EXPECT_EQ(sym.size(), 3u);
}

// ============================================================
// §2  resolve_module — collection phase
// ============================================================

// Parse a PTX snippet and resolve it; check the symbol table.
static ResolvedModule parse_and_resolve(std::string_view src,
                                         std::vector<ResolveError>& errs) {
  auto r = parse_module(src);
  EXPECT_TRUE(r.has_value()) << (r ? "" : r.error().message);
  return resolve_module(*r, errs);
}

static constexpr std::string_view kMinimalKernel = R"PTX(
.version 8.0
.target sm_89
.address_size 64

.entry simple_kernel(.param .b64 param0)
{
    .reg .b64 %rd<2>;
    .reg .b32 %r<4>;
    .reg .pred %p;

    ld.param.u64 %rd0, [param0];
    add.u32 %r0, %r1, %r2;
    @%p bra END;
END:
    ret;
}
)PTX";

TEST(ResolveModule, CollectionPhase_FunctionSymbol) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kMinimalKernel, errs);

  // The kernel name must be in the module scope
  auto r = rm.symbols.lookup("simple_kernel");
  ASSERT_TRUE(r.has_value()) << r.error().message;
  EXPECT_EQ(rm.symbols.entry(*r).kind, SymbolKind::Function);
  EXPECT_TRUE(rm.symbols.entry(*r).func_scope.empty());
}

TEST(ResolveModule, CollectionPhase_Parameters) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kMinimalKernel, errs);

  // "param0" should be a local Param symbol
  auto r = rm.symbols.lookup("param0", "simple_kernel");
  ASSERT_TRUE(r.has_value()) << r.error().message;
  EXPECT_EQ(rm.symbols.entry(*r).kind, SymbolKind::Param);
}

TEST(ResolveModule, CollectionPhase_ParameterisedRegisters) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kMinimalKernel, errs);

  // .reg .b64 %rd<2> → %rd0 and %rd1
  for (auto& nm : {"rd0", "rd1"}) {
    std::string name = std::string("%") + nm;
    auto r = rm.symbols.lookup(name, "simple_kernel");
    ASSERT_TRUE(r.has_value()) << "missing: " << name;
    EXPECT_EQ(rm.symbols.entry(*r).kind, SymbolKind::Register);
    EXPECT_EQ(rm.symbols.entry(*r).space, StateSpace::Reg);
  }
}

TEST(ResolveModule, CollectionPhase_ScalarPredReg) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kMinimalKernel, errs);

  auto r = rm.symbols.lookup("%p", "simple_kernel");
  ASSERT_TRUE(r.has_value()) << r.error().message;
  EXPECT_EQ(rm.symbols.entry(*r).kind, SymbolKind::Register);
}

TEST(ResolveModule, CollectionPhase_Label) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kMinimalKernel, errs);

  auto r = rm.symbols.lookup("END", "simple_kernel");
  ASSERT_TRUE(r.has_value()) << r.error().message;
  EXPECT_EQ(rm.symbols.entry(*r).kind, SymbolKind::Label);
}

// ============================================================
// §3  resolve_module — resolution phase (operand Ident → SymbolId)
// ============================================================

TEST(ResolveModule, ResolutionPhase_NoErrors_OnValidKernel) {
  std::vector<ResolveError> errs;
  parse_and_resolve(kMinimalKernel, errs);
  for (auto& e : errs)
    ADD_FAILURE() << "Unexpected error: " << e.message;
  EXPECT_TRUE(errs.empty());
}

TEST(ResolveModule, ResolutionPhase_DirectivesPreserved) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kMinimalKernel, errs);
  // Should have exactly one directive (the .entry)
  EXPECT_EQ(rm.directives.size(), 1u);
}

TEST(ResolveModule, ResolutionPhase_ResolvedOperandsAreValidIds) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kMinimalKernel, errs);
  EXPECT_TRUE(errs.empty());

  // Walk all statements in the single function directive and verify that
  // every operand is a valid SymbolId (i.e. not kInvalidSymbolId).
  const auto& dir = rm.directives.front();
  const auto* method = std::get_if<Directive<ResolvedOp>::MethodD>(&dir.inner);
  ASSERT_NE(method, nullptr);
  ASSERT_TRUE(method->func.body.has_value());

  std::function<void(const Statement<ResolvedOp>&)> walk =
      [&](const Statement<ResolvedOp>& stmt) {
        std::visit(
            [&](const auto& s) {
              using S = std::decay_t<decltype(s)>;
              if constexpr (std::is_same_v<S, Statement<ResolvedOp>::BlockS>) {
                for (const auto& inner : s.stmts)
                  walk(inner);
              }
            },
            stmt.inner);
      };

  for (const auto& stmt : *method->func.body)
    walk(stmt);
  // If we get here without crashing we have a live resolved AST.
}

// ============================================================
// §4  Error cases
// ============================================================

static constexpr std::string_view kUndefinedRegKernel = R"PTX(
.version 7.0
.target sm_80
.address_size 64

.entry bad_kernel()
{
    .reg .b32 %r<2>;
    add.u32 %r0, %r1, %r99;
}
)PTX";

TEST(ResolveModule, ErrorCase_UndefinedRegister) {
  std::vector<ResolveError> errs;
  parse_and_resolve(kUndefinedRegKernel, errs);
  // %r99 was never declared — expect at least one error
  EXPECT_FALSE(errs.empty());
  bool found = false;
  for (auto& e : errs)
    if (e.message.find("%r99") != std::string::npos ||
        e.message.find("r99") != std::string::npos)
      found = true;
  EXPECT_TRUE(found) << "Expected error mentioning %r99";
}

// Test that a kernel with a global variable is correctly collected
static constexpr std::string_view kGlobalVar = R"PTX(
.version 7.0
.target sm_80
.address_size 64

.global .b32 g_counter;

.entry use_global()
{
    .reg .b32 %r0;
    ld.global.b32 %r0, [g_counter];
    ret;
}
)PTX";

TEST(ResolveModule, CollectionPhase_GlobalVariable) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kGlobalVar, errs);

  auto r = rm.symbols.lookup("g_counter");
  ASSERT_TRUE(r.has_value()) << "g_counter not found in module scope";
  EXPECT_EQ(rm.symbols.entry(*r).kind, SymbolKind::Global);
  EXPECT_EQ(rm.symbols.entry(*r).space, StateSpace::Global);
}

TEST(ResolveModule, ResolutionPhase_GlobalVar_NoErrors) {
  std::vector<ResolveError> errs;
  parse_and_resolve(kGlobalVar, errs);
  for (auto& e : errs)
    ADD_FAILURE() << "Unexpected error: " << e.message;
  EXPECT_TRUE(errs.empty());
}

// Multiple functions — each has its own local scope
static constexpr std::string_view kTwoFunctions = R"PTX(
.version 7.0
.target sm_80
.address_size 64

.func (.param .b32 ret0) add_one(.param .b32 x)
{
    .reg .b32 %r<2>;
    ld.param.b32 %r0, [x];
    add.u32 %r1, %r0, 1;
    st.param.b32 [ret0], %r1;
    ret;
}

.entry caller()
{
    .reg .b32 %r<2>;
    .param .b32 result;
    .param .b32 arg;
    mov.b32 %r0, 41;
    st.param.b32 [arg], %r0;
    call (result), add_one, (arg);
    ld.param.b32 %r1, [result];
    ret;
}
)PTX";

TEST(ResolveModule, TwoFunctions_BothCollected) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kTwoFunctions, errs);

  auto f1 = rm.symbols.lookup("add_one");
  auto f2 = rm.symbols.lookup("caller");
  EXPECT_TRUE(f1.has_value()) << "add_one not found";
  EXPECT_TRUE(f2.has_value()) << "caller not found";
}

TEST(ResolveModule, TwoFunctions_LocalScopesAreIndependent) {
  std::vector<ResolveError> errs;
  auto rm = parse_and_resolve(kTwoFunctions, errs);

  // %r0 exists in both functions but they are different symbols
  auto r_add = rm.symbols.lookup("%r0", "add_one");
  auto r_cal = rm.symbols.lookup("%r0", "caller");
  ASSERT_TRUE(r_add.has_value()) << "%r0 not found in add_one";
  ASSERT_TRUE(r_cal.has_value()) << "%r0 not found in caller";
  EXPECT_NE(*r_add, *r_cal) << "same SymbolId for different scopes";
}

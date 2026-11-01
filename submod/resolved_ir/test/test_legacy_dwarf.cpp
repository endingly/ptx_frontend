#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/lexer/ptx_lexer.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <string>
#include <string_view>
#include <tuple>

namespace ptx_frontend::resolved_ir {
namespace {
/** Construct initialized source without legacy-illegal address-size or debug options. */
std::string dwarfSource(std::string record = "@@DWARF .byte 0x1",
                        std::string version = "1.3",
                        std::string target = "sm_13", std::string body = "") {
  return (version.empty() ? "" : ".version " + version + "\n") +
         (target.empty() ? "" : ".target " + target + "\n") + record +
         "\n.entry k(){.reg .u32 %r;mov.u32 %r,1;" + body + "ret;}";
}
/** Own records after the parser, AST and source have all been destroyed. */
auto dwarfOwned(std::string text) {
  PtxSyntaxParser p(text);
  auto a = p.parseModule();
  if (!a || !a.diagnostics.empty())
    return std::expected<ResolvedModule, ModuleResolveDiagnostics>{
        std::unexpected(ModuleResolveDiagnostics{})};
  return resolveModuleOnly(*a);
}
/** Return one numeric production for direct current/provided mutation. */
syntax_ast::AstLegacyDwarfData& dwarfData(ResolvedModule& module,
                                          size_t index = 0) {
  return std::get<syntax_ast::AstLegacyDwarfData>(
      module.legacy_dwarf_records[index].value.payload);
}
/** Check both public routes under the explicitly chosen context policy. */
void dwarfBoth(const syntax_ast::AstModule& ast, const ResolvedModule& module,
               bool expected,
               ModuleValidationPolicy policy =
                   ModuleValidationPolicy::RequireCompleteContext) {
  for (auto checked :
       {validateModule(module, policy), validateModule(ast, module, policy)}) {
    EXPECT_EQ(bool(checked), expected);
    if (expected && !checked)
      for (auto& d : checked.error())
        ADD_FAILURE() << d.message;
  }
}
/** Archived complete example; data is retained structurally without claiming DWARF decoding. */
constexpr std::string_view dwarfExample =
    R"ptx(@@DWARF .section .debug_pubnames, "", @progbits
@@DWARF .byte   0x2b, 0x00, 0x00, 0x00, 0x02, 0x00
@@DWARF .4byte  .debug_info
@@DWARF .4byte  0x000006b5, 0x00000364, 0x61395a5f, 0x5f736f63
@@DWARF .4byte  0x6e69616d, 0x63613031, 0x6150736f, 0x736d6172
@@DWARF .byte   0x00, 0x00, 0x00, 0x00, 0x00)ptx";

/** Every window control uses a lawful source profile and otherwise identical payload. */
TEST(LegacyDwarf, SourceWindowAndIndependentPartialVersion) {
  for (auto [version, target] : {std::pair{"1.1", "sm_10"},
                                 {"1.2", "sm_10"},
                                 {"1.2", "sm_12"},
                                 {"1.3", "sm_13"},
                                 {"1.5", "sm_13"},
                                 {"2.0", "sm_13"},
                                 {"2.0", "sm_20"}}) {
    auto control = dwarfOwned(dwarfSource("", version, target));
    ASSERT_TRUE(control);
    ASSERT_TRUE(validateModule(*control));
    auto text = dwarfSource("@@DWARF .byte 0x1", version, target);
    PtxSyntaxParser p(text);
    auto a = p.parseModule();
    ASSERT_TRUE(a);
    ASSERT_TRUE(a.diagnostics.empty());
    auto m = resolveModuleOnly(*a);
    const bool good = std::string_view(version) != "1.1" &&
                      std::string_view(version) != "2.0";
    EXPECT_EQ(bool(m), good) << version << target;
    if (good) {
      ASSERT_TRUE(m);
      dwarfBoth(*a, *m, true);
    } else {
      ASSERT_FALSE(m);
      EXPECT_TRUE(std::ranges::any_of(m.error(), [](const auto& d) {
        return d.checker_kind ==
                   checker::CheckDiagnosticKind::UnsupportedAvailability &&
               d.message.find("@@DWARF") != std::string::npos;
      }));
    }
  }
  auto base = dwarfOwned(dwarfSource("@@DWARF .byte 0x1", "1.3", ""));
  ASSERT_TRUE(base);
  for (auto version : {checker::PtxVersion{1, 1}, checker::PtxVersion{1, 2},
                       checker::PtxVersion{2, 0}}) {
    auto current = *base;
    for (auto& r : current.header.regions)
      r.version = version;
    for (auto& f : current.functions)
      f.source_version = version;
    auto checked =
        validateModule(current, ModuleValidationPolicy::AvailableContext);
    EXPECT_EQ(bool(checked), version == checker::PtxVersion(1, 2));
    if (version != checker::PtxVersion(1, 2)) {
      ASSERT_FALSE(checked);
      EXPECT_TRUE(std::ranges::any_of(checked.error(), [](auto& d) {
        return d.kind ==
                   checker::CheckDiagnosticKind::UnsupportedAvailability &&
               d.message.find("@@DWARF") != std::string::npos;
      }));
    }
    auto text = dwarfSource("@@DWARF .byte 0x1",
                            version == checker::PtxVersion(1, 1)   ? "1.1"
                            : version == checker::PtxVersion(1, 2) ? "1.2"
                                                                   : "2.0",
                            "");
    PtxSyntaxParser p(text);
    auto a = p.parseModule();
    ASSERT_TRUE(a);
    auto supplied =
        validateModule(*a, *base, ModuleValidationPolicy::AvailableContext);
    EXPECT_EQ(bool(supplied), version == checker::PtxVersion(1, 2));
  }
  auto text = dwarfSource("@@DWARF .byte 0x1", "", "");
  PtxSyntaxParser p(text);
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  auto m = resolveModuleOnly(*a);
  ASSERT_TRUE(m);
  dwarfBoth(*a, *m, true, ModuleValidationPolicy::AvailableContext);
  for (auto checked :
       {validateModule(*m, ModuleValidationPolicy::RequireCompleteContext),
        validateModule(*a, *m,
                       ModuleValidationPolicy::RequireCompleteContext)}) {
    ASSERT_FALSE(checked);
    EXPECT_TRUE(std::ranges::any_of(checked.error(), [](auto& d) {
      return d.kind == checker::CheckDiagnosticKind::MissingValidationContext;
    }));
  }
  dwarfData(*m).value = std::vector<syntax_ast::AstSyntax>{};
  dwarfBoth(*a, *m, false, ModuleValidationPolicy::AvailableContext);
}

/** Unsigned original hex words keep full capacity and reject signs, other radices and narrowing. */
TEST(LegacyDwarf, OriginalUnsignedDomainsAndSymbolAlternatives) {
  for (auto [directive, maximum, overflow] :
       {std::tuple{".byte", "0xff", "0x100"},
        std::tuple{".4byte", "0xffffffff", "0x100000000"},
        std::tuple{".quad", "0xffffffffffffffff", "0x10000000000000000"}}) {
    for (auto word : {std::string("0x0"), std::string(maximum),
                      std::string("0x1U"), std::string("0X1u")}) {
      auto m = dwarfOwned(
          dwarfSource(std::string("@@DWARF ") + directive + " " + word));
      ASSERT_TRUE(m) << directive << word;
      ASSERT_EQ(m->legacy_dwarf_records.size(), 1u);
      auto& words =
          std::get<std::vector<syntax_ast::AstSyntax>>(dwarfData(*m).value);
      ASSERT_EQ(words.size(), 1u);
      EXPECT_EQ(words[0].text, word);
      EXPECT_TRUE(validateModule(*m));
    }
    for (auto word : {overflow, "-0x1", "+0x1", "-0x0", "1", "0b1", "01",
                      "0f00000001", "0d0000000000000001", "0h0001"})
      EXPECT_FALSE(dwarfOwned(
          dwarfSource(std::string("@@DWARF ") + directive + " " + word)))
          << directive << word;
  }
  for (auto record :
       {"@@DWARF .4byte .debug_info", "@@DWARF .quad external_label"}) {
    auto m = dwarfOwned(dwarfSource(record));
    ASSERT_TRUE(m);
    EXPECT_TRUE(
        std::holds_alternative<syntax_ast::AstSyntax>(dwarfData(*m).value));
    EXPECT_TRUE(validateModule(*m));
  }
  for (auto record :
       {"@@DWARF .byte label", "@@DWARF .4byte 0x1,label",
        "@@DWARF .quad label,0x1", "@@DWARF .quad label+4",
        "@@DWARF .quad label1-label2", "@@DWARF label:", "@@DWARF .ascii \"x\"",
        "@@DWARF .b8 0x1", "@@dwarf .byte 0x1", "@ @DWARF .byte 0x1",
        "@@DWARFx .byte 0x1", "@@DWARF .byte"})
    EXPECT_FALSE(dwarfOwned(dwarfSource(record))) << record;
  auto m = dwarfOwned(dwarfSource("@@DWARF .quad 0XFFFFFFFFFFFFFFFFu"));
  ASSERT_TRUE(m);
  auto& word =
      std::get<std::vector<syntax_ast::AstSyntax>>(dwarfData(*m).value)[0];
  EXPECT_EQ(base::parseIntegerMagnitude(word.text), UINT64_MAX);
}

/** One shared parser covers the example, finite header and token-whitespace completion rules. */
TEST(LegacyDwarf, ExactExampleHeadersAndWhitespace) {
  auto m = dwarfOwned(dwarfSource(std::string(dwarfExample)));
  ASSERT_TRUE(m);
  ASSERT_EQ(m->legacy_dwarf_records.size(), 6u);
  EXPECT_TRUE(validateModule(*m));
  auto& header = std::get<syntax_ast::AstLegacyDwarfSectionHeader>(
      m->legacy_dwarf_records[0].value.payload);
  EXPECT_EQ(header.name.text, ".debug_pubnames");
  EXPECT_EQ(header.type_prefix.text, "@");
  EXPECT_EQ(header.type_name.text, "progbits");
  for (auto name : {"another_section", ".other_section"})
    EXPECT_TRUE(dwarfOwned(dwarfSource(std::string("@@DWARF .section ") + name +
                                       ",\"\",@progbits;")));
  for (auto record : {"@@DWARF .section .x,\"a\",@progbits",
                      "@@DWARF .section .x,\"\",@nobits",
                      "@@DWARF .section .x,\"\",@progbits,4",
                      "@@DWARF .section .x", "@@DWARF .section .x { .b8 1 }"})
    EXPECT_FALSE(dwarfOwned(dwarfSource(record))) << record;
  const std::string spaced =
      "@@DWARF /*a*/\n.byte\n0x1 "
      "/*b*/,\n0Xffu;\n@@DWARF\n.section\n.debug_pubnames,\n\"\",\n@ /*c*/ "
      "progbits;";
  auto text = dwarfSource(spaced);
  PtxCstParser cp(text);
  auto c = cp.parseModule();
  ASSERT_TRUE(c);
  ASSERT_TRUE(c.diagnostics.empty());
  EXPECT_EQ(c->sourceText(), text);
  PtxSyntaxParser p(text);
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  auto owned = resolveModuleOnly(*a);
  ASSERT_TRUE(owned);
  ASSERT_EQ(owned->legacy_dwarf_records.size(), 2u);
  EXPECT_LT(owned->legacy_dwarf_records[0].value.range.start.line,
            owned->legacy_dwarf_records[0].value.range.end.line);
  dwarfBoth(*a, *owned, true);
  EXPECT_FALSE(dwarfOwned(dwarfSource("@@DWARF .byte 0x1\n +4")));
}

/** Source-wide records use definition occurrences and blocks without consuming instruction or label slots. */
TEST(LegacyDwarf, ActualScopesLexicalOrderAndInstructionPositions) {
  const std::string text = R"ptx(.version 1.3
.target sm_13
@@DWARF .byte 0x1
.func f();
.func f(){@@DWARF .byte 0x2 ret;}
.entry k(){.reg .u32 %r;mov.u32 %r,1;
@@DWARF .byte 0x3
L0:{@@DWARF .byte 0x4 .reg .u32 %s;mov.u32 %s,2;}
@@DWARF .quad external_label
bra L0;ret;}
@@DWARF .section .debug_pubnames,"",@progbits
)ptx";
  PtxSyntaxParser p(text);
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  ASSERT_TRUE(a.diagnostics.empty());
  auto m = dwarfOwned(text);
  ASSERT_TRUE(m);
  ASSERT_EQ(m->functions.size(), 3u);
  ASSERT_EQ(m->legacy_dwarf_records.size(), 6u);
  EXPECT_EQ(m->legacy_dwarf_records[0].scope_id, m->symbols.moduleScope());
  EXPECT_EQ(m->legacy_dwarf_records[1].scope_id,
            m->symbols.functionScope(m->functions[1].range));
  EXPECT_NE(m->functions[0].declaration_scope,
            m->functions[1].declaration_scope);
  EXPECT_EQ(m->legacy_dwarf_records[2].scope_id,
            m->functions[2].declaration_scope);
  EXPECT_EQ(m->symbols.scope(m->legacy_dwarf_records[3].scope_id).parent,
            m->functions[2].declaration_scope);
  EXPECT_EQ(m->legacy_dwarf_records[4].scope_id,
            m->functions[2].declaration_scope);
  EXPECT_EQ(m->legacy_dwarf_records[5].scope_id, m->symbols.moduleScope());
  EXPECT_EQ(m->functions[2].body.size(), 4u);
  ASSERT_EQ(m->functions[2].label_positions.size(), 1u);
  EXPECT_EQ(m->functions[2].label_positions[0].instruction_offset, 1u);
  EXPECT_EQ(m->functions[2].instruction_ranges.size(), 4u);
  dwarfBoth(*a, *m, true);
  auto copied = *m;
  ResolvedModule assigned;
  assigned = copied;
  auto moved = std::move(assigned);
  auto fn = moved.functions[2];
  moved.functions[2] = fn;
  for (auto& i : moved.functions[2].body)
    i = i->clone();
  EXPECT_EQ(moved.legacy_dwarf_records[3].value.range,
            m->legacy_dwarf_records[3].value.range);
  dwarfBoth(*a, moved, true);
  std::ranges::reverse(moved.legacy_dwarf_records);
  dwarfBoth(*a, moved, true);
  for (auto scope :
       {binding::ScopeId{UINT32_MAX}, m->functions[0].declaration_scope,
        m->functions[2].declaration_scope}) {
    auto bad = *m;
    bad.legacy_dwarf_records[1].scope_id = scope;
    dwarfBoth(*a, bad, false);
  }
  const auto shifted = dwarfSource("@@DWARF .byte 0x5\n\n\n\n\n\n", "1.3",
                                   "sm_13", "@@DWARF .quad external_label;");
  PtxSyntaxParser q(shifted);
  auto b = q.parseModule();
  ASSERT_TRUE(b);
  auto ordinary = dwarfOwned(
      dwarfSource("@@DWARF .byte 0x1", "1.3", "sm_13", "@@DWARF .byte 0x1;"));
  ASSERT_TRUE(ordinary);
  EXPECT_TRUE(validateModule(*b, *ordinary));
  auto invalid_header = dwarfSource("", "1.3", "sm_13");
  invalid_header.replace(invalid_header.find(".entry k()"), 10,
                         ".entry k() @@DWARF .byte 0x1 ");
  PtxSyntaxParser hp(invalid_header);
  auto rejected = hp.parseModule();
  EXPECT_TRUE(!rejected || !rejected.diagnostics.empty());
  ResolvedModule empty;
  EXPECT_TRUE(empty.legacy_dwarf_records.empty());
}

/** Metadata words are current authoritative format data; neither dataset can hide malformed values. */
TEST(LegacyDwarf, CurrentAndProvidedIntrinsicsAreIndependent) {
  auto text = dwarfSource();
  PtxSyntaxParser p(text);
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  auto m = dwarfOwned(text);
  ASSERT_TRUE(m);
  for (int edit = 0; edit < 10; ++edit) {
    auto bad = *m;
    auto& r = bad.legacy_dwarf_records[0].value;
    auto& data = dwarfData(bad);
    if (edit == 0)
      data.kind = static_cast<syntax_ast::LegacyDwarfDataKind>(255);
    if (edit == 1)
      data.directive.text = ".quad";
    if (edit == 2)
      data.value = std::vector<syntax_ast::AstSyntax>{};
    if (edit == 3)
      std::get<std::vector<syntax_ast::AstSyntax>>(data.value)[0].text =
          "0x100";
    if (edit == 4)
      std::get<std::vector<syntax_ast::AstSyntax>>(data.value)[0].text = "-0x1";
    if (edit == 5)
      r.marker.text = "@@dwarf";
    if (edit == 6)
      r.marker.range = {{0, 1}, {1, 2}};
    if (edit == 7)
      r.range = {{200, 1}, {200, 2}};
    if (edit == 8)
      data.value = syntax_ast::AstSyntax{"label", data.range};
    if (edit == 9)
      bad.legacy_dwarf_records[0].scope_id = {UINT32_MAX};
    dwarfBoth(*a, bad, false);
  }
  auto current = *m;
  std::get<std::vector<syntax_ast::AstSyntax>>(dwarfData(current).value)[0]
      .text = "0x2";
  dwarfBoth(*a, current, true);
  EXPECT_EQ(
      std::get<std::vector<syntax_ast::AstSyntax>>(dwarfData(*m).value)[0].text,
      "0x1");
  for (int edit = 0; edit < 3; ++edit) {
    PtxSyntaxParser q(text);
    auto b = q.parseModule();
    ASSERT_TRUE(b);
    auto& record = std::get<syntax_ast::AstLegacyDwarfRecord>(b->items[2]);
    auto& data = std::get<syntax_ast::AstLegacyDwarfData>(record.payload);
    if (edit == 0)
      record.marker.text = "@@dwarf";
    if (edit == 1)
      std::get<std::vector<syntax_ast::AstSyntax>>(data.value)[0].text =
          "0x100";
    if (edit == 2)
      data.range = {{100, 1}, {100, 2}};
    EXPECT_FALSE(validateModule(*b, *m));
  }
  auto different = dwarfSource("@@DWARF .quad external_label");
  PtxSyntaxParser q(different);
  auto b = q.parseModule();
  ASSERT_TRUE(b);
  EXPECT_TRUE(validateModule(*b, *m));
  auto changed_instruction = different;
  changed_instruction.replace(changed_instruction.find("mov.u32 %r,1"), 12,
                              "mov.u32 %r,2");
  PtxSyntaxParser x(changed_instruction);
  auto changed = x.parseModule();
  ASSERT_TRUE(changed);
  EXPECT_FALSE(validateModule(*changed, *m));
  dwarfBoth(*a, *m, true);
}

/** Format windows apply independently to retained and supplied serialization without producer-history vetoes. */
TEST(LegacyDwarf, CrossFormatRemovalRestoreAndPresenceBoundary) {
  auto old_text = dwarfSource();
  auto old = dwarfOwned(old_text);
  ASSERT_TRUE(old);
  auto modern_text =
      dwarfSource(".section .debug_info {.b8 1}", "2.0", "sm_20");
  PtxSyntaxParser p(modern_text);
  auto modern_ast = p.parseModule();
  ASSERT_TRUE(modern_ast);
  auto modern = dwarfOwned(modern_text);
  ASSERT_TRUE(modern);
  PtxSyntaxParser q(old_text);
  auto old_ast = q.parseModule();
  ASSERT_TRUE(old_ast);
  auto upper = validateModule(*modern_ast, *old);
  ASSERT_FALSE(upper);
  EXPECT_TRUE(std::ranges::any_of(upper.error(), [](auto& d) {
    return d.kind == checker::CheckDiagnosticKind::UnsupportedAvailability &&
           d.message.find("@@DWARF") != std::string::npos;
  }));
  auto retained = old->legacy_dwarf_records;
  old->legacy_dwarf_records.clear();
  EXPECT_TRUE(validateModule(*modern_ast, *old));
  old->legacy_dwarf_records = retained;
  EXPECT_FALSE(validateModule(*modern_ast, *old));
  EXPECT_FALSE(validateModule(*old_ast, *modern));
  auto sections = modern->sections;
  modern->sections.clear();
  EXPECT_TRUE(validateModule(*old_ast, *modern));
  modern->sections = sections;
  EXPECT_FALSE(validateModule(*old_ast, *modern));
  auto invalid = *old;
  std::get<std::vector<syntax_ast::AstSyntax>>(dwarfData(invalid).value)[0]
      .text = "0x100";
  auto rejected = validateModule(*modern_ast, invalid);
  ASSERT_FALSE(rejected);
  EXPECT_TRUE(std::ranges::any_of(rejected.error(), [](auto& d) {
    return d.kind == checker::CheckDiagnosticKind::RuleViolation &&
           d.message.find("hexadecimal") != std::string::npos;
  }));
  const auto partial_text =
      dwarfSource("@@DWARF .quad external_label", "", "sm_13, debug");
  PtxSyntaxParser z(partial_text);
  auto partial_ast = z.parseModule();
  ASSERT_TRUE(partial_ast);
  auto partial = dwarfOwned(partial_text);
  ASSERT_TRUE(partial);
  dwarfBoth(*partial_ast, *partial, true,
            ModuleValidationPolicy::AvailableContext);
  auto strict =
      validateModule(*partial, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(strict);
  EXPECT_TRUE(std::ranges::any_of(strict.error(), [](auto& d) {
    return d.kind == checker::CheckDiagnosticKind::MissingValidationContext;
  }));
  auto absent = *partial;
  absent.legacy_dwarf_records.clear();
  auto zero = validateModule(*partial_ast, absent,
                             ModuleValidationPolicy::AvailableContext);
  ASSERT_FALSE(zero);
  EXPECT_TRUE(std::ranges::any_of(zero.error(), [](auto& d) {
    return d.message.find("this dataset contains no sections") !=
           std::string::npos;
  }));
  auto empty_text = dwarfSource("", "", "sm_13, debug");
  PtxSyntaxParser zz(empty_text);
  auto empty_ast = zz.parseModule();
  ASSERT_TRUE(empty_ast);
  EXPECT_FALSE(validateModule(*empty_ast, *partial,
                              ModuleValidationPolicy::AvailableContext));
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

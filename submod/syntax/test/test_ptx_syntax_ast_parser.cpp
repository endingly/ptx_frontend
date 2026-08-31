#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/syntax/ptx_syntax_lower.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend {
namespace {

using syntax_ast::AstAddress;
using syntax_ast::AstAddressOffset;
using syntax_ast::AstBranchTarget;
using syntax_ast::AstBranchTargetSet;
using syntax_ast::AstBlock;
using syntax_ast::AstCallParameterList;
using syntax_ast::AstCallTarget;
using syntax_ast::AstCallTargetSet;
using syntax_ast::AstCallPrototype;
using syntax_ast::AstCallTargets;
using syntax_ast::AstBranchTargets;
using syntax_ast::AstIdentifierRef;
using syntax_ast::AstImmediate;
using syntax_ast::AstInstruction;
using syntax_ast::AstPredicateOperand;
using syntax_ast::AstRegisterPredicatePair;
using syntax_ast::AstVectorMember;
using syntax_ast::AstVectorPack;

std::string_view sourceSlice(std::string_view source, SourceRange range) {
  const auto offset = [source](SourcePos position) {
    size_t start = 0;
    for (int32_t line = 1; line < position.line; ++line) {
      const size_t newline = source.find('\n', start);
      if (newline == std::string_view::npos)
        return source.size();
      start = newline + 1;
    }
    return start + static_cast<size_t>(position.column - 1);
  };
  return source.substr(offset(range.start),
                       offset(range.end) - offset(range.start));
}

TEST(PtxSyntaxParser, ParsesInstructionWithoutCstTrivia) {
  PtxSyntaxParser parser("  // leading\nadd.sat.s32 %r1, %r2, -1;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_TRUE(result.diagnostics.empty());

  const auto& instruction = *result;
  EXPECT_EQ(instruction.opcode.syntax.text, "add");

  ASSERT_EQ(instruction.modifiers.size(), 2u);
  EXPECT_EQ(instruction.modifiers[0].syntax.text, ".sat");
  EXPECT_EQ(instruction.modifiers[1].syntax.text, ".s32");

  ASSERT_EQ(instruction.operands.size(), 3u);
  EXPECT_TRUE(
      std::holds_alternative<AstIdentifierRef>(instruction.operands[0]));
  EXPECT_TRUE(
      std::holds_alternative<AstIdentifierRef>(instruction.operands[1]));
  ASSERT_TRUE(std::holds_alternative<AstImmediate>(instruction.operands[2]));
  EXPECT_EQ(std::get<AstImmediate>(instruction.operands[2]).syntax.text, "-1");
}

TEST(PtxSyntaxParser, ParsesPredicateAddressAndVectorMember) {
  PtxSyntaxParser parser("@!%p add.u32 [%rd1+16], %r2.x, %r3;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;

  const auto& instruction = *result;
  ASSERT_TRUE(instruction.predicate.has_value());
  EXPECT_TRUE(instruction.predicate->negated);
  EXPECT_EQ(instruction.predicate->name.syntax.text, "%p");

  ASSERT_EQ(instruction.operands.size(), 3u);
  ASSERT_TRUE(std::holds_alternative<AstAddress>(instruction.operands[0]));
  const auto& address = std::get<AstAddress>(instruction.operands[0]);
  EXPECT_TRUE(address.bracketed);
  ASSERT_TRUE(address.offset.has_value());
  EXPECT_EQ(address.offset->operation, AstAddressOffset::Operator::Add);
  EXPECT_EQ(address.offset->magnitude.syntax.text, "16");

  ASSERT_TRUE(std::holds_alternative<AstVectorMember>(instruction.operands[1]));
  const auto& member = std::get<AstVectorMember>(instruction.operands[1]);
  EXPECT_EQ(member.base.syntax.text, "%r2");
  EXPECT_EQ(member.selector.text, ".x");
}

TEST(PtxSyntaxParser, ParsesVectorPack) {
  PtxSyntaxParser parser("mov.b32 {%r1, -2, 0x10}, %r2;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;

  ASSERT_EQ(result->operands.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<AstVectorPack>(result->operands[0]));
  const auto& pack = std::get<AstVectorPack>(result->operands[0]);
  ASSERT_EQ(pack.elements.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<AstIdentifierRef>(pack.elements[0]));
  EXPECT_TRUE(std::holds_alternative<AstImmediate>(pack.elements[1]));
  EXPECT_TRUE(std::holds_alternative<AstImmediate>(pack.elements[2]));
}

TEST(PtxSyntaxParser, LowersRegisterPredicatePairAsOneOperand) {
  PtxSyntaxParser parser("setp.eq.u32 %p0|%p1, %r0, %r1;");
  const auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  ASSERT_EQ(result->operands.size(), 3u);
  const auto& pair =
      std::get<AstRegisterPredicatePair>(result->operands.front());
  EXPECT_EQ(pair.dst.syntax.text, "%p0");
  EXPECT_EQ(pair.predicate.syntax.text, "%p1");
  EXPECT_NE(pair.range.start, pair.range.end);
}

TEST(PtxSyntaxParser, LowersDedicatedCallAndBranchOperands) {
  PtxSyntaxParser call_parser(
      "call.uni (%result), %callee, (%arg, 4), targets;");
  auto call = call_parser.parseInstruction();
  ASSERT_TRUE(call.has_value()) << call.diagnostics.front().message;
  ASSERT_EQ(call->operands.size(), 4u);

  const auto& returns = std::get<AstCallParameterList>(call->operands[0]);
  EXPECT_EQ(returns.kind, syntax_ast::AstCallParameterListKind::Return);
  ASSERT_EQ(returns.parameters.size(), 1u);
  EXPECT_EQ(std::get<AstIdentifierRef>(returns.parameters[0]).syntax.text,
            "%result");
  EXPECT_EQ(std::get<AstCallTarget>(call->operands[1]).name.syntax.text,
            "%callee");

  const auto& inputs = std::get<AstCallParameterList>(call->operands[2]);
  EXPECT_EQ(inputs.kind, syntax_ast::AstCallParameterListKind::Input);
  ASSERT_EQ(inputs.parameters.size(), 2u);
  EXPECT_EQ(std::get<AstIdentifierRef>(inputs.parameters[0]).syntax.text,
            "%arg");
  EXPECT_EQ(std::get<AstImmediate>(inputs.parameters[1]).syntax.text, "4");
  EXPECT_EQ(std::get<AstCallTargetSet>(call->operands[3]).name.syntax.text,
            "targets");

  PtxSyntaxParser branch_parser("bra done;");
  auto branch = branch_parser.parseInstruction();
  ASSERT_TRUE(branch.has_value()) << branch.diagnostics.front().message;
  ASSERT_EQ(branch->operands.size(), 1u);
  EXPECT_EQ(std::get<AstBranchTarget>(branch->operands[0]).name.syntax.text,
            "done");

  PtxSyntaxParser indexed_branch_parser("brx.idx %r0, targets;");
  auto indexed_branch = indexed_branch_parser.parseInstruction();
  ASSERT_TRUE(indexed_branch.has_value())
      << indexed_branch.diagnostics.front().message;
  ASSERT_EQ(indexed_branch->operands.size(), 2u);
  EXPECT_EQ(std::get<AstIdentifierRef>(indexed_branch->operands[0]).syntax.text,
            "%r0");
  const auto& target_set =
      std::get<AstBranchTargetSet>(indexed_branch->operands[1]);
  EXPECT_EQ(target_set.name.syntax.text, "targets");
  EXPECT_EQ(target_set.range, target_set.name.syntax.range);
}

TEST(PtxSyntaxParser, LowersFunctionLocalCallPrototypePayload) {
  constexpr std::string_view source = R"ptx(
.func dispatch() {
  prototype: .callprototype (.param .u32 result) _ (.reg .u32 arg, .param .b8 bytes[12]) .noreturn .abi_preserve 10 .abi_preserve_control 2;
}
)ptx";
  PtxSyntaxParser parser(source);

  const auto module = parser.parseModule();

  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto& function = std::get<syntax_ast::AstFunction>(module->items.front());
  ASSERT_EQ(function.body.size(), 1u);
  const auto& prototype = std::get<AstCallPrototype>(function.body.front());
  EXPECT_EQ(prototype.label.syntax.text, "prototype");
  EXPECT_EQ(prototype.sink.syntax.text, "_");
  ASSERT_EQ(prototype.return_parameters.size(), 1u);
  EXPECT_EQ(prototype.return_parameters.front().name.syntax.text, "result");
  ASSERT_EQ(prototype.parameters.size(), 2u);
  EXPECT_EQ(prototype.parameters[0].name.syntax.text, "arg");
  EXPECT_EQ(prototype.parameters[1].name.syntax.text, "bytes");
  EXPECT_TRUE(prototype.parameters[1].is_array);
  ASSERT_TRUE(prototype.noreturn_directive.has_value());
  EXPECT_EQ(prototype.noreturn_directive->text, ".noreturn");
  ASSERT_TRUE(prototype.abi_preserve.has_value());
  EXPECT_EQ(prototype.abi_preserve->directive.text, ".abi_preserve");
  EXPECT_EQ(prototype.abi_preserve->count.text, "10");
  ASSERT_TRUE(prototype.abi_preserve_control.has_value());
  EXPECT_EQ(prototype.abi_preserve_control->directive.text,
            ".abi_preserve_control");
  EXPECT_EQ(prototype.abi_preserve_control->count.text, "2");
  EXPECT_EQ(prototype.range.start.line, 3u);
  EXPECT_EQ(prototype.label.syntax.range.start.column, 3u);
}

TEST(PtxSyntaxParser, LowersNestedFunctionBlocks) {
  constexpr std::string_view source = R"ptx(.entry kernel() {
  {
    outer:
    { add.u32 %r0, %r1, %r2; }
  }
})ptx";
  PtxSyntaxParser parser(source);

  const auto module = parser.parseModule();

  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto& function =
      std::get<syntax_ast::AstFunction>(module->items.front());
  ASSERT_EQ(function.body.size(), 1u);
  const auto& outer = *std::get<std::unique_ptr<AstBlock>>(function.body[0]);
  EXPECT_EQ(outer.range.start.line, 2u);
  ASSERT_EQ(outer.body.size(), 2u);
  const auto& inner = *std::get<std::unique_ptr<AstBlock>>(outer.body[1]);
  EXPECT_EQ(inner.range.start.line, 4u);
  EXPECT_EQ(inner.range.end.line, 4u);
  ASSERT_EQ(inner.body.size(), 1u);
  EXPECT_TRUE(
      std::holds_alternative<syntax_ast::AstInstruction>(inner.body[0]));
}

TEST(PtxSyntaxParser, LowersFunctionLocalCallTargetsPayload) {
  constexpr std::string_view source = R"ptx(
.func caller() {
  list: .calltargets first, second, second;
}
)ptx";
  PtxSyntaxParser parser(source);

  const auto module = parser.parseModule();

  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto& function =
      std::get<syntax_ast::AstFunction>(module->items.front());
  ASSERT_EQ(function.body.size(), 1u);
  const auto& targets = std::get<AstCallTargets>(function.body.front());
  EXPECT_EQ(targets.label.syntax.text, "list");
  ASSERT_EQ(targets.targets.size(), 3u);
  EXPECT_EQ(targets.targets[0].syntax.text, "first");
  EXPECT_EQ(targets.targets[1].syntax.text, "second");
  EXPECT_EQ(targets.targets[2].syntax.text, "second");
  EXPECT_EQ(targets.range.start.line, 3u);
  EXPECT_EQ(targets.targets[1].syntax.range.start.column, 29u);
}

TEST(PtxSyntaxParser, LowersFunctionLocalBranchTargetsPayload) {
  constexpr std::string_view source = R"ptx(
.func dispatch() {
  table: .branchtargets L1, N<5>, L1;
}
)ptx";
  PtxSyntaxParser parser(source);

  const auto module = parser.parseModule();

  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto& function =
      std::get<syntax_ast::AstFunction>(module->items.front());
  ASSERT_EQ(function.body.size(), 1u);
  const auto& targets = std::get<AstBranchTargets>(function.body.front());
  EXPECT_EQ(targets.label.syntax.text, "table");
  ASSERT_EQ(targets.targets.size(), 3u);
  EXPECT_EQ(targets.targets[0].name.syntax.text, "L1");
  EXPECT_FALSE(targets.targets[0].count.has_value());
  EXPECT_EQ(targets.targets[1].name.syntax.text, "N");
  ASSERT_TRUE(targets.targets[1].count.has_value());
  EXPECT_EQ(targets.targets[1].count->text, "5");
  EXPECT_EQ(targets.targets[2].name.syntax.text, "L1");
  EXPECT_EQ(targets.targets[1].range.start.line, 3u);
  EXPECT_EQ(targets.targets[1].count->range.start.line, 3u);
}

TEST(PtxSyntaxParser, LowersUnbracketedAddressOffsetOperation) {
  PtxSyntaxParser parser("ld.u32 %r1, %rd1-4;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;

  ASSERT_EQ(result->operands.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<AstAddress>(result->operands[1]));
  const auto& address = std::get<AstAddress>(result->operands[1]);
  EXPECT_FALSE(address.bracketed);
  ASSERT_TRUE(address.offset.has_value());
  EXPECT_EQ(address.offset->operation, AstAddressOffset::Operator::Subtract);
  EXPECT_EQ(address.offset->magnitude.syntax.text, "4");
}

TEST(PtxSyntaxParser, ParsesNegatedPredicateOperand) {
  PtxSyntaxParser parser("bar.red.popc.u32 %r0, 1, !%p1;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;

  ASSERT_EQ(result->operands.size(), 3u);
  ASSERT_TRUE(std::holds_alternative<AstPredicateOperand>(result->operands[2]));
  const auto& predicate = std::get<AstPredicateOperand>(result->operands[2]);
  EXPECT_TRUE(predicate.negated);
  EXPECT_EQ(predicate.name.syntax.text, "%p1");
}

TEST(PtxSyntaxParser, RejectsMissingInstructionTerminator) {
  PtxSyntaxParser parser("add.u32 %r1, %r2, %r3");

  auto result = parser.parseInstruction();
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1u);
  EXPECT_EQ(result.diagnostics.front().message, "expected ';'");
  EXPECT_EQ(result.diagnostics.front().range,
            (SourceRange{SourcePos{1, 22}, SourcePos{1, 22}}));
}

TEST(PtxSyntaxParser, MapsCstDiagnosticsToSyntaxDiagnostics) {
  constexpr std::string_view source = "add.u32 %r1, %r2, %r3";
  PtxCstParser cst_parser(source);
  PtxSyntaxParser syntax_parser(source);

  const auto cst = cst_parser.parseInstruction();
  const auto syntax = syntax_parser.parseInstruction();

  ASSERT_FALSE(cst.has_value());
  ASSERT_FALSE(syntax.has_value());
  ASSERT_EQ(cst.diagnostics.size(), 1u);
  ASSERT_EQ(syntax.diagnostics.size(), 1u);
  EXPECT_EQ(syntax.diagnostics.front().message,
            cst.diagnostics.front().message);
  EXPECT_EQ(syntax.diagnostics.front().range, cst.diagnostics.front().range);
}

TEST(PtxSyntaxParser, LowersOnlyValidNeighborsOfRecoveredModuleCst) {
  constexpr std::string_view source = R"ptx(.version nope;
.target;
.entry kernel() { { add.u32 %r0, %r1, %r2 sub.u32 %r3, %r4, %r5; } }
)ptx";
  PtxCstParser cst_parser(source);
  PtxSyntaxParser syntax_parser(source);

  const auto cst = cst_parser.parseModule();
  ASSERT_TRUE(cst.has_value());
  ASSERT_EQ(cst.diagnostics.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<syntax_cst::CstRecoveryNode>(
      cst->module()->items[0]));
  const auto lowered = lowerSyntaxModule(*cst);
  const auto syntax = syntax_parser.parseModule();

  ASSERT_TRUE(lowered.has_value());
  EXPECT_TRUE(lowered.diagnostics.empty());
  ASSERT_EQ(lowered->items.size(), 1u);
  const auto& function = std::get<syntax_ast::AstFunction>(lowered->items[0]);
  ASSERT_EQ(function.body.size(), 1u);
  const auto& block =
      *std::get<std::unique_ptr<AstBlock>>(function.body[0]);
  ASSERT_EQ(block.body.size(), 1u);
  EXPECT_EQ(std::get<AstInstruction>(block.body[0]).opcode.syntax.text, "sub");

  ASSERT_TRUE(syntax.has_value());
  ASSERT_EQ(syntax->items.size(), 1u);
  ASSERT_EQ(syntax.diagnostics.size(), cst.diagnostics.size());
  for (std::size_t index = 0; index < cst.diagnostics.size(); ++index) {
    EXPECT_EQ(syntax.diagnostics[index].message,
              cst.diagnostics[index].message);
    EXPECT_EQ(syntax.diagnostics[index].range, cst.diagnostics[index].range);
  }
}

TEST(PtxSyntaxParser, RejectsEmptyVectorPack) {
  PtxSyntaxParser parser("mov.b32 {}, %r1;");

  auto result = parser.parseInstruction();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.diagnostics.front().message, "vector operand cannot be empty");
}

TEST(AstFile, DistinguishesInstructionFragmentAndModuleRoots) {
  syntax_ast::AstFile module_file{syntax_ast::AstModule{}};

  EXPECT_EQ(module_file.instruction(), nullptr);
  ASSERT_NE(module_file.module(), nullptr);
  EXPECT_TRUE(module_file.module()->items.empty());
}

TEST(SyntaxLower, RejectsModuleRootAsInstructionFragment) {
  syntax_cst::CstFile module_file{
      .tokens = {},
      .root = syntax_cst::CstModule{.items = {}, .token_range = {}},
  };

  const auto result = lowerSyntaxInstruction(module_file);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.diagnostics.front().message,
            "expected an instruction-fragment CST root");
}

TEST(PtxSyntaxParser, LowersMinimalModuleToTypedAst) {
  constexpr std::string_view source = R"ptx(.version 8.0
.target sm_80, debug
.address_size 64
.visible .entry kernel() {
  add.u32 %r0, %r1, %r2;
}
)ptx";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  ASSERT_EQ(result->items.size(), 4u);
  const auto& version =
      std::get<syntax_ast::AstVersionDirective>(result->items[0]);
  EXPECT_EQ(version.version.text, "8.0");

  const auto& target =
      std::get<syntax_ast::AstTargetDirective>(result->items[1]);
  ASSERT_EQ(target.targets.size(), 2u);
  EXPECT_EQ(target.targets[0].text, "sm_80");
  EXPECT_EQ(target.targets[1].text, "debug");

  const auto& address_size =
      std::get<syntax_ast::AstAddressSizeDirective>(result->items[2]);
  EXPECT_EQ(address_size.bit_width.text, "64");

  const auto& function = std::get<syntax_ast::AstFunction>(result->items[3]);
  EXPECT_TRUE(function.is_entry);
  EXPECT_EQ(function.name.syntax.text, "kernel");
  ASSERT_EQ(function.qualifiers.size(), 1u);
  EXPECT_EQ(function.qualifiers[0].text, ".visible");
  ASSERT_EQ(function.body.size(), 1u);
  EXPECT_EQ(
      std::get<syntax_ast::AstInstruction>(function.body[0]).opcode.syntax.text,
      "add");
}

TEST(PtxSyntaxParser, LowersTypedFileDirectives) {
  constexpr std::string_view source = R"ptx(.file 0x1U "source.ptx"
.file 1 "large.ptx", 0, 18446744073709551615U;
)ptx";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  ASSERT_EQ(result->items.size(), 2u);
  const auto& short_form =
      std::get<syntax_ast::AstFileDirective>(result->items[0]);
  EXPECT_EQ(short_form.file_index.text, "0x1U");
  EXPECT_EQ(short_form.filename.text, "\"source.ptx\"");
  EXPECT_FALSE(short_form.timestamp.has_value());
  EXPECT_FALSE(short_form.file_size.has_value());
  EXPECT_EQ(short_form.range.start.line, 1u);

  const auto& full_form =
      std::get<syntax_ast::AstFileDirective>(result->items[1]);
  ASSERT_TRUE(full_form.timestamp.has_value());
  ASSERT_TRUE(full_form.file_size.has_value());
  EXPECT_EQ(full_form.timestamp->text, "0");
  EXPECT_EQ(full_form.file_size->text, "18446744073709551615U");
  EXPECT_EQ(full_form.timestamp->range.start.line, 2u);
  EXPECT_EQ(full_form.file_size->range.start.line, 2u);
  EXPECT_EQ(full_form.range.start.line, 2u);
}

TEST(PtxSyntaxParser, LowersRawSectionPayloadSyntax) {
  constexpr std::string_view source = R"ptx(.section .debug_info {
Lbegin:
  .b8 -128, 0, 255
  .b16 -32768, 65535
  .b32 -2147483648, 4294967295U
  .b64 -9223372036854775808, 18446744073709551615U
  .b32 .debug_abbrev
  .b64 Lbegin
  .b32 .debug_str+0x4
  .b64 Lbegin+12
  .b32 Lend-Lbegin
  .b64 Lbegin-Lend
Lend:
};
)ptx";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& section =
      std::get<syntax_ast::AstSectionDirective>(result->items.front());
  EXPECT_EQ(section.name.text, ".debug_info");
  ASSERT_EQ(section.payload.size(), 46u);
  EXPECT_EQ(section.payload.front().text, "Lbegin");
  EXPECT_EQ(section.payload.at(29).text, ".debug_str");
  EXPECT_EQ(section.payload.at(30).text, "+");
  EXPECT_EQ(section.payload.at(31).text, "0x4");
  EXPECT_EQ(section.payload.at(36).text, ".b32");
  EXPECT_EQ(section.payload.at(37).text, "Lend");
  EXPECT_EQ(section.payload.at(38).text, "-");
  EXPECT_EQ(section.range.start.line, 1u);
  EXPECT_EQ(section.payload.at(29).range.start.line, 9u);
}

TEST(PtxSyntaxParser, LowersPragmasAtAllSupportedScopes) {
  constexpr std::string_view source = R"ptx(.pragma "module", "opaque";
.entry kernel() .pragma "nounroll"; {
  .pragma "frequency 32";
  {
    .pragma "nested", "opaque";
  }
}
)ptx";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& module_pragma =
      std::get<syntax_ast::AstPragma>(result->items.front());
  ASSERT_EQ(module_pragma.strings.size(), 2u);
  EXPECT_EQ(module_pragma.strings[1].text, "\"opaque\"");
  EXPECT_EQ(module_pragma.range.start.line, 1u);

  const auto& function =
      std::get<syntax_ast::AstFunction>(result->items[1]);
  ASSERT_EQ(function.pragmas.size(), 1u);
  EXPECT_EQ(function.pragmas[0].strings[0].text, "\"nounroll\"");
  EXPECT_EQ(function.pragmas[0].range.start.line, 2u);
  ASSERT_EQ(function.body.size(), 2u);
  const auto& body_pragma =
      std::get<syntax_ast::AstPragma>(function.body.front());
  EXPECT_EQ(body_pragma.strings[0].text, "\"frequency 32\"");
  const auto& block =
      *std::get<std::unique_ptr<syntax_ast::AstBlock>>(function.body[1]);
  const auto& nested_pragma =
      std::get<syntax_ast::AstPragma>(block.body.front());
  ASSERT_EQ(nested_pragma.strings.size(), 2u);
  EXPECT_EQ(nested_pragma.range.start.line, 5u);
}

TEST(PtxSyntaxParser, LowersEntryKernelResourceDirectives) {
  constexpr std::string_view source = R"ptx(.entry kernel() .pragma "before";
    .maxnreg 32 .maxntid 16, 8, 4 .pragma "after";
    .minnctapersm 2 { }
)ptx";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function =
      std::get<syntax_ast::AstFunction>(result->items.front());
  ASSERT_EQ(function.pragmas.size(), 2u);
  ASSERT_EQ(function.resources.size(), 3u);
  const auto& maxnreg = function.resources[0];
  EXPECT_EQ(maxnreg.kind, syntax_ast::AstKernelResourceKind::MaxNreg);
  ASSERT_EQ(maxnreg.values.size(), 1u);
  EXPECT_EQ(maxnreg.values[0].text, "32");
  EXPECT_EQ(maxnreg.range.start.line, 2u);
  const auto& maxntid = function.resources[1];
  EXPECT_EQ(maxntid.kind, syntax_ast::AstKernelResourceKind::MaxNtid);
  ASSERT_EQ(maxntid.values.size(), 3u);
  EXPECT_EQ(maxntid.values[1].text, "8");
  EXPECT_EQ(maxntid.values[2].range.start.line, 2u);
  EXPECT_EQ(function.resources[2].kind,
            syntax_ast::AstKernelResourceKind::MinNctaPerSm);
}

TEST(PtxSyntaxParser, LowersRequiredThreadCountDirective) {
  PtxSyntaxParser parser(".entry kernel() .reqntid 64, 2 { }");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function =
      std::get<syntax_ast::AstFunction>(result->items.front());
  ASSERT_EQ(function.resources.size(), 1u);
  EXPECT_EQ(function.resources[0].kind,
            syntax_ast::AstKernelResourceKind::ReqNtid);
  EXPECT_EQ(function.resources[0].values.size(), 2u);
}

TEST(PtxSyntaxParser, LowersClusterDimensionDirectives) {
  PtxSyntaxParser parser(R"ptx(
.entry kernel() .reqnctapercluster 2, 3 .explicitcluster .maxclusterrank 8 { }
)ptx");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function =
      std::get<syntax_ast::AstFunction>(result->items.front());
  ASSERT_EQ(function.resources.size(), 3u);
  const auto& req = function.resources[0];
  EXPECT_EQ(req.kind, syntax_ast::AstKernelResourceKind::ReqNctaPerCluster);
  ASSERT_EQ(req.values.size(), 2u);
  EXPECT_EQ(req.values[1].text, "3");
  const auto& explicit_cluster = function.resources[1];
  EXPECT_EQ(explicit_cluster.kind,
            syntax_ast::AstKernelResourceKind::ExplicitCluster);
  EXPECT_TRUE(explicit_cluster.values.empty());
  EXPECT_EQ(function.resources[2].kind,
            syntax_ast::AstKernelResourceKind::MaxClusterRank);
}

TEST(PtxSyntaxParser, LowersNestedLocDirectives) {
  constexpr std::string_view source = R"ptx(.entry kernel() {
  .loc 0x2U 4237 0
  {
    .loc 1 15 3, function_name .debug_str+16, inlined_at 0x1U 10 5;
  }
}
)ptx";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function =
      std::get<syntax_ast::AstFunction>(result->items.front());
  const auto& basic =
      std::get<syntax_ast::AstLocDirective>(function.body.front());
  EXPECT_EQ(basic.file_index.text, "0x2U");
  EXPECT_EQ(basic.line_number.text, "4237");
  EXPECT_EQ(basic.column_position.text, "0");
  EXPECT_FALSE(basic.inline_context.has_value());
  EXPECT_EQ(basic.range.start.line, 2u);

  const auto& block =
      *std::get<std::unique_ptr<syntax_ast::AstBlock>>(function.body[1]);
  const auto& full = std::get<syntax_ast::AstLocDirective>(block.body.front());
  ASSERT_TRUE(full.inline_context.has_value());
  const auto& context = *full.inline_context;
  EXPECT_EQ(context.function_name_label.syntax.text, ".debug_str");
  ASSERT_TRUE(context.function_name_offset.has_value());
  EXPECT_EQ(context.function_name_offset->text, "16");
  EXPECT_EQ(context.file_index.text, "0x1U");
  EXPECT_EQ(context.line_number.text, "10");
  EXPECT_EQ(context.column_position.text, "5");
  EXPECT_EQ(context.function_name_label.syntax.range.start.line, 4u);
  EXPECT_EQ(context.range.start.line, 4u);
  EXPECT_EQ(full.range.start.line, 4u);
}

TEST(PtxSyntaxParser, LowersFuncDefinition) {
  PtxSyntaxParser parser(
      ".func (.param .b32 result) helper(.param .b32 input) { ret; }");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  ASSERT_EQ(result->items.size(), 1u);
  const auto& function = std::get<syntax_ast::AstFunction>(result->items[0]);
  EXPECT_FALSE(function.is_entry);
  EXPECT_FALSE(function.is_prototype);
  EXPECT_EQ(function.name.syntax.text, "helper");
  ASSERT_EQ(function.return_parameters.size(), 1u);
  EXPECT_EQ(function.return_parameters[0].state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(function.return_parameters[0].name.syntax.text, "result");
  ASSERT_EQ(function.parameters.size(), 1u);
  EXPECT_EQ(function.parameters[0].name.syntax.text, "input");
  ASSERT_EQ(function.body.size(), 1u);
  EXPECT_EQ(
      std::get<syntax_ast::AstInstruction>(function.body[0]).opcode.syntax.text,
      "ret");
}

TEST(PtxSyntaxParser, LowersParameterAttributesArraysAndPrototype) {
  PtxSyntaxParser parser(
      ".extern .func sink(.param .align 8 .b8 blob[2 * WARP_SZ], "
      ".param .u64 .ptr .global .align 16 ptr) .noreturn;");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function = std::get<syntax_ast::AstFunction>(result->items[0]);
  EXPECT_TRUE(function.is_prototype);
  EXPECT_TRUE(function.is_noreturn);
  ASSERT_EQ(function.qualifiers.size(), 1u);
  EXPECT_EQ(function.qualifiers[0].text, ".extern");
  ASSERT_EQ(function.parameters.size(), 2u);

  const auto& blob = function.parameters[0];
  EXPECT_EQ(blob.state_space, syntax_ast::AstStateSpace::Parameter);
  ASSERT_TRUE(blob.alignment.has_value());
  EXPECT_EQ(blob.alignment->text, "8");
  EXPECT_TRUE(blob.is_array);
  ASSERT_TRUE(blob.array_size.has_value());
  const auto& size =
      std::get<syntax_ast::AstConstantBinary>(blob.array_size->node);
  EXPECT_EQ(size.operation, syntax_ast::AstConstantBinaryOperator::Multiply);
  const auto& warp_size =
      std::get<syntax_ast::AstConstantLiteral>(size.right->node).value;
  EXPECT_EQ(warp_size.kind, syntax_ast::AstImmediateKind::WarpSize);
  EXPECT_EQ(warp_size.syntax.text, "WARP_SZ");

  const auto& pointer = function.parameters[1];
  EXPECT_TRUE(pointer.is_pointer);
  ASSERT_TRUE(pointer.pointer_space.has_value());
  EXPECT_EQ(pointer.pointer_space->text, ".global");
  ASSERT_TRUE(pointer.pointer_alignment.has_value());
  EXPECT_EQ(pointer.pointer_alignment->text, "16");
  EXPECT_TRUE(function.body.empty());
}

TEST(PtxSyntaxParser, LowersRegisterDeclarationsAndLabels) {
  PtxSyntaxParser parser(
      ".entry kernel() { .reg .align 16 .u32 %r<3>, %tmp; loop: ret; }");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function = std::get<syntax_ast::AstFunction>(result->items[0]);
  ASSERT_EQ(function.body.size(), 3u);

  const auto& declaration =
      std::get<syntax_ast::AstVariableDeclaration>(function.body[0]);
  ASSERT_TRUE(declaration.alignment.has_value());
  EXPECT_EQ(declaration.alignment->text, "16");
  EXPECT_EQ(declaration.type.text, ".u32");
  ASSERT_EQ(declaration.declarators.size(), 2u);
  EXPECT_EQ(declaration.declarators[0].name.syntax.text, "%r");
  ASSERT_TRUE(declaration.declarators[0].parameterized_count.has_value());
  EXPECT_EQ(declaration.declarators[0].parameterized_count->text, "3");
  EXPECT_EQ(declaration.declarators[1].name.syntax.text, "%tmp");

  const auto& label = std::get<syntax_ast::AstLabel>(function.body[1]);
  EXPECT_EQ(label.name.syntax.text, "loop");
  EXPECT_TRUE(
      std::holds_alternative<syntax_ast::AstInstruction>(function.body[2]));
}

TEST(PtxSyntaxParser, LowersModuleAndFunctionVariableDeclarations) {
  constexpr std::string_view source =
      ".visible .global .align 16 .v4 .f32 values[2][3];\n"
      ".entry kernel() {\n"
      "  .shared .v2 .u16 tile[8];\n"
      "  .local .u32 scratch[19][19];\n"
      "  .param .align 8 .b8 argument[12];\n"
      "  ret;\n"
      "}";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  ASSERT_EQ(result->items.size(), 2u);
  const auto& global =
      std::get<syntax_ast::AstVariableDeclaration>(result->items[0]);
  EXPECT_EQ(global.state_space, syntax_ast::AstStateSpace::Global);
  ASSERT_EQ(global.qualifiers.size(), 1u);
  EXPECT_EQ(global.qualifiers[0].text, ".visible");
  ASSERT_TRUE(global.alignment.has_value());
  EXPECT_EQ(global.alignment->text, "16");
  ASSERT_TRUE(global.vector_type.has_value());
  EXPECT_EQ(global.vector_type->text, ".v4");
  ASSERT_EQ(global.declarators[0].array_dimensions.size(), 2u);
  ASSERT_TRUE(global.declarators[0].array_dimensions[1].size.has_value());
  EXPECT_EQ(std::get<syntax_ast::AstConstantLiteral>(
                global.declarators[0].array_dimensions[1].size->node)
                .value.syntax.text,
            "3");

  const auto& function = std::get<syntax_ast::AstFunction>(result->items[1]);
  ASSERT_EQ(function.body.size(), 4u);
  EXPECT_EQ(std::get<syntax_ast::AstVariableDeclaration>(function.body[0])
                .state_space,
            syntax_ast::AstStateSpace::Shared);
  EXPECT_EQ(std::get<syntax_ast::AstVariableDeclaration>(function.body[1])
                .state_space,
            syntax_ast::AstStateSpace::Local);
  EXPECT_EQ(std::get<syntax_ast::AstVariableDeclaration>(function.body[2])
                .state_space,
            syntax_ast::AstStateSpace::Parameter);
}

TEST(PtxSyntaxParser, LowersConstantExpressionPrecedenceAndConditional) {
  PtxSyntaxParser parser(
      ".global .s64 value = "
      "(.s64)(1 + 2 * 3) > 0 && flag ? ~0 : -1;");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& declaration =
      std::get<syntax_ast::AstVariableDeclaration>(result->items[0]);
  const auto& initializer = *declaration.declarators[0].initializer;
  const auto& expression =
      std::get<syntax_ast::AstConstantExpression>(initializer.value);
  const auto& conditional =
      std::get<syntax_ast::AstConstantConditional>(expression.node);

  const auto& logical_and =
      std::get<syntax_ast::AstConstantBinary>(conditional.condition->node);
  EXPECT_EQ(logical_and.operation,
            syntax_ast::AstConstantBinaryOperator::LogicalAnd);
  const auto& comparison =
      std::get<syntax_ast::AstConstantBinary>(logical_and.left->node);
  EXPECT_EQ(comparison.operation,
            syntax_ast::AstConstantBinaryOperator::Greater);
  const auto& cast =
      std::get<syntax_ast::AstConstantCast>(comparison.left->node);
  EXPECT_EQ(cast.type.text, ".s64");
  const auto& parenthesized =
      std::get<syntax_ast::AstConstantParenthesized>(cast.operand->node);
  const auto& addition =
      std::get<syntax_ast::AstConstantBinary>(parenthesized.expression->node);
  EXPECT_EQ(addition.operation, syntax_ast::AstConstantBinaryOperator::Add);
  const auto& product =
      std::get<syntax_ast::AstConstantBinary>(addition.right->node);
  EXPECT_EQ(product.operation, syntax_ast::AstConstantBinaryOperator::Multiply);
  EXPECT_TRUE(std::holds_alternative<syntax_ast::AstConstantUnary>(
      conditional.true_expression->node));
  EXPECT_TRUE(std::holds_alternative<syntax_ast::AstConstantUnary>(
      conditional.false_expression->node));
}

TEST(PtxSyntaxParser, LowersEveryConstantBinaryOperator) {
  using Operator = syntax_ast::AstConstantBinaryOperator;
  struct Case {
    std::string_view expression;
    Operator operation;
  };
  constexpr Case cases[] = {
      {"2 * 3", Operator::Multiply},      {"6 / 2", Operator::Divide},
      {"7 % 4", Operator::Remainder},     {"1 + 2", Operator::Add},
      {"3 - 1", Operator::Subtract},      {"1 << 2", Operator::ShiftLeft},
      {"4 >> 1", Operator::ShiftRight},   {"1 < 2", Operator::Less},
      {"1 <= 2", Operator::LessEqual},    {"2 > 1", Operator::Greater},
      {"2 >= 1", Operator::GreaterEqual}, {"1 == 1", Operator::Equal},
      {"1 != 2", Operator::NotEqual},     {"1 & 3", Operator::BitwiseAnd},
      {"1 ^ 3", Operator::BitwiseXor},    {"1 | 2", Operator::BitwiseOr},
      {"1 && 2", Operator::LogicalAnd},   {"1 || 2", Operator::LogicalOr},
  };

  for (const auto& test_case : cases) {
    PtxSyntaxParser parser(std::string{".global .u32 value = "} +
                           std::string{test_case.expression} + ";");
    const auto result = parser.parseModule();
    ASSERT_TRUE(result.has_value())
        << test_case.expression << ": " << result.diagnostics.front().message;
    const auto& declaration =
        std::get<syntax_ast::AstVariableDeclaration>(result->items[0]);
    const auto& initializer = *declaration.declarators[0].initializer;
    const auto& expression =
        std::get<syntax_ast::AstConstantExpression>(initializer.value);
    ASSERT_TRUE(
        std::holds_alternative<syntax_ast::AstConstantBinary>(expression.node))
        << test_case.expression;
    EXPECT_EQ(
        std::get<syntax_ast::AstConstantBinary>(expression.node).operation,
        test_case.operation)
        << test_case.expression;
  }
}

TEST(PtxSyntaxParser, LowersRemainingConstantUnaryOperators) {
  PtxSyntaxParser parser(".global .u32 value = +1 + !flag;");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& declaration =
      std::get<syntax_ast::AstVariableDeclaration>(result->items[0]);
  const auto& initializer = *declaration.declarators[0].initializer;
  const auto& expression =
      std::get<syntax_ast::AstConstantExpression>(initializer.value);
  const auto& addition =
      std::get<syntax_ast::AstConstantBinary>(expression.node);
  EXPECT_EQ(
      std::get<syntax_ast::AstConstantUnary>(addition.left->node).operation,
      syntax_ast::AstConstantUnaryOperator::Plus);
  EXPECT_EQ(
      std::get<syntax_ast::AstConstantUnary>(addition.right->node).operation,
      syntax_ast::AstConstantUnaryOperator::LogicalNot);
}

TEST(PtxSyntaxParser, LowersNestedInitializerAndSymbolAddressOperator) {
  PtxSyntaxParser parser(
      ".global .u64 pointers[][2] = "
      "{{generic(base), generic(base) + 8}, {base, 0}};");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& declaration =
      std::get<syntax_ast::AstVariableDeclaration>(result->items[0]);
  const auto& declarator = declaration.declarators[0];
  ASSERT_EQ(declarator.array_dimensions.size(), 2u);
  EXPECT_FALSE(declarator.array_dimensions[0].size.has_value());
  const auto& outer =
      std::get<syntax_ast::AstInitializerList>(declarator.initializer->value);
  ASSERT_EQ(outer.elements.size(), 2u);
  const auto& first_row =
      std::get<syntax_ast::AstInitializerList>(outer.elements[0].value);
  ASSERT_EQ(first_row.elements.size(), 2u);
  const auto& generic_expression =
      std::get<syntax_ast::AstConstantExpression>(first_row.elements[0].value);
  const auto& generic =
      std::get<syntax_ast::AstConstantCall>(generic_expression.node);
  EXPECT_EQ(std::get<syntax_ast::AstConstantSymbol>(generic.callee->node)
                .name.syntax.text,
            "generic");
  const auto& address_expression =
      std::get<syntax_ast::AstConstantExpression>(first_row.elements[1].value);
  EXPECT_EQ(std::get<syntax_ast::AstConstantBinary>(address_expression.node)
                .operation,
            syntax_ast::AstConstantBinaryOperator::Add);
}

TEST(PtxSyntaxParser, PreservesM11ComplexModifierCorpusLosslessly) {
  const auto root = std::filesystem::path(__FILE__)
                        .parent_path()
                        .parent_path()
                        .parent_path()
                        .parent_path();
  std::ifstream input(root / "corpus" / "m11" / "complex_modifiers.ptx");
  ASSERT_TRUE(input);
  const std::string source{std::istreambuf_iterator<char>{input}, {}};

  PtxCstParser cst_parser(source);
  const auto cst = cst_parser.parseModule();
  ASSERT_TRUE(cst.has_value()) << cst.diagnostics.front().message;
  EXPECT_TRUE(cst.diagnostics.empty());
  EXPECT_EQ(cst->sourceText(), source);
  PtxCstParser reparsing(cst->sourceText());
  const auto reparsed = reparsing.parseModule();
  ASSERT_TRUE(reparsed.has_value()) << reparsed.diagnostics.front().message;
  EXPECT_TRUE(reparsed.diagnostics.empty());
  EXPECT_EQ(reparsed->sourceText(), source);

  const auto& cst_function = std::get<syntax_cst::CstFunction>(
      cst->module()->items.back());
  ASSERT_EQ(cst_function.body.size(), 5u);
  const auto& cst_packed_load =
      std::get<syntax_cst::CstInstruction>(cst_function.body[1]);
  const auto& cst_pack = std::get<syntax_cst::CstVectorPack>(
      cst_packed_load.operands.front().operand);
  ASSERT_EQ(cst_pack.elements.size(), 2u);
  ASSERT_EQ(cst_pack.commas.size(), 1u);
  EXPECT_EQ(cst->token(cst_pack.commas.front()).text, ",");
  ASSERT_TRUE(cst_packed_load.operands.front().trailing_comma.has_value());
  EXPECT_EQ(cst->token(*cst_packed_load.operands.front().trailing_comma).text,
            ",");
  for (const std::string_view spelling : {
           ".16x64b",          ".16x128b",      ".4x256b",
           ".layout::v0",      ".kind::mxf8f6f4", ".block_scale",
           ".scale_vec::1X",   ".collector::a::fill",
       }) {
    const auto token = std::ranges::find_if(
        cst->tokens, [spelling](const auto& candidate) {
          return candidate.text == spelling;
        });
    ASSERT_NE(token, cst->tokens.end()) << spelling;
    EXPECT_EQ(token->kind, TokenKind::DotIdent);
  }

  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  EXPECT_TRUE(ast.diagnostics.empty());
  const auto& ast_function =
      std::get<syntax_ast::AstFunction>(ast->items.back());
  ASSERT_EQ(ast_function.body.size(), 5u);
  const auto expect_modifiers = [&](size_t item,
                                    std::initializer_list<std::string_view> expected) {
    const auto& instruction = std::get<AstInstruction>(ast_function.body[item]);
    ASSERT_EQ(instruction.modifiers.size(), expected.size());
    for (size_t index = 0; index < expected.size(); ++index) {
      const auto expected_spelling = *(expected.begin() + index);
      EXPECT_EQ(instruction.modifiers[index].syntax.text, expected_spelling);
      EXPECT_EQ(sourceSlice(source, instruction.modifiers[index].syntax.range),
                expected_spelling);
    }
  };
  expect_modifiers(0, {".ld", ".sync", ".aligned", ".16x64b", ".x1", ".b32"});
  expect_modifiers(1, {".ld", ".sync", ".aligned", ".16x128b", ".x1", ".b32"});
  expect_modifiers(2, {".cp", ".cta_group::1", ".4x256b"});
  expect_modifiers(3, {".check_layout", ".layout::v0", ".shared::cta", ".b64"});
  expect_modifiers(4, {".mma", ".cta_group::1", ".kind::mxf8f6f4",
                       ".block_scale", ".scale_vec::1X",
                       ".collector::a::fill"});
  const auto& ast_pack = std::get<AstVectorPack>(
      std::get<AstInstruction>(ast_function.body[1]).operands.front());
  EXPECT_EQ(ast_pack.elements.size(), 2u);
}

TEST(PtxSyntaxParser, LowersM11DeclarationHeaderDirectivesLosslessly) {
  PtxSyntaxParser parser(R"ptx(
.version 9.3
.global .attribute(.managed, .unified(1, 2)) .u32 managed;
.alias alias_fn, target;
.func .attribute(.unified(3, 4)) target() .noreturn .abi_preserve 2
    .abi_preserve_control 1 .language "PTX", 10;
.entry kernel() .reqntid 1 .reqnctapercluster 1 .blocksareclusters
    .language "cuda c++" {}
)ptx");
  const auto result = parser.parseModule();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& variable =
      std::get<syntax_ast::AstVariableDeclaration>(result->items[1]);
  ASSERT_EQ(variable.attributes.size(), 2u);
  EXPECT_EQ(variable.attributes[1].values.size(), 2u);
  const auto& alias = std::get<syntax_ast::AstAliasDirective>(result->items[2]);
  EXPECT_EQ(alias.alias.syntax.text, "alias_fn");
  const auto& function = std::get<syntax_ast::AstFunction>(result->items[3]);
  EXPECT_TRUE(function.noreturn_directive.has_value());
  EXPECT_TRUE(function.abi_preserve.has_value());
  ASSERT_TRUE(function.language.has_value());
  EXPECT_EQ(function.language->values.size(), 2u);
  EXPECT_EQ(function.language->range.end.line, function.range.end.line);
  EXPECT_LT(function.language->range.end.column, function.range.end.column);
  const auto& entry = std::get<syntax_ast::AstFunction>(result->items[4]);
  EXPECT_TRUE(entry.blocks_are_clusters.has_value());
}

TEST(PtxSyntaxParser, RejectsMalformedM11DeclarationSyntax) {
  constexpr std::array<std::string_view, 4> sources = {
      ".global .attribute(.managed(1, 2)) .u32 x;",
      ".global .attribute(.unified(1)) .u32 x;",
      ".entry kernel() .noreturn {}",
      ".func f() .abi_preserve;",
  };
  for (const auto source : sources) {
    PtxSyntaxParser parser(source);
    const auto result = parser.parseModule();
    EXPECT_FALSE(result.diagnostics.empty()) << source;
  }
}

TEST(PtxSyntaxParser, RetainsMmaThroughputAsGenericPragma) {
  PtxSyntaxParser parser(".pragma \"mma_throughput\";");
  const auto result = parser.parseModule();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& pragma = std::get<syntax_ast::AstPragma>(result->items.front());
  ASSERT_EQ(pragma.strings.size(), 1u);
  EXPECT_EQ(pragma.strings.front().text, "\"mma_throughput\"");
}

}  // namespace
}  // namespace ptx_frontend

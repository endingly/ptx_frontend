#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Repeat a small grammar fragment without committing oversized fixtures. */
std::string repeated(std::string_view fragment, std::size_t count) {
  std::string result;
  result.reserve(fragment.size() * count);
  for (std::size_t index = 0; index < count; ++index)
    result += fragment;
  return result;
}

/** Build a declaration whose combined expression/list tree has the given depth. */
std::string declarationAtDepth(std::string_view shape, std::size_t depth) {
  const auto wrappers = depth - 1;
  std::string dimensions;
  std::string value;
  if (shape == "unary")
    value = repeated("+", wrappers) + "1";
  else if (shape == "parentheses")
    value = repeated("(", wrappers) + "1" + repeated(")", wrappers);
  else if (shape == "cast")
    value = repeated("(.u64)", wrappers) + "1";
  else if (shape == "call")
    value = repeated("0xff(", wrappers) + "1" + repeated(")", wrappers);
  else if (shape == "postfix")
    value = "0xff" + repeated("(1)", wrappers);
  else if (shape == "conditional")
    value = repeated("1 ? 1 : ", wrappers) + "1";
  else if (shape == "binary")
    value = "1" + repeated(" + 1", wrappers);
  else {
    const auto lists = shape == "mixed" ? wrappers / 2 : wrappers;
    dimensions = repeated("[1]", lists);
    value = repeated("{", lists) + repeated("+", wrappers - lists) +
            "1" + repeated("}", lists);
  }
  return ".global .u32 value" + dimensions + " = " + value + ";\n";
}

/** Recursive grammar and iterative tree-building forms share one boundary. */
constexpr std::array shapes{"unary", "parentheses", "cast", "call", "postfix",
                            "conditional", "binary", "initializer", "mixed"};

/** Exercise lowering, checking, resolution, and ordinary recursive destruction. */
TEST(ConstantTreeDepth, AcceptsBelowAndAtLimitThroughModuleResolution) {
  for (const auto shape : shapes) {
    for (const auto depth : {PtxCstParser::maxConstantTreeDepth - 1,
                             PtxCstParser::maxConstantTreeDepth}) {
      SCOPED_TRACE(std::string{shape} + " depth=" + std::to_string(depth));
      const std::string source = ".version 9.3\n.target sm_80\n.address_size 64\n" +
                                 declarationAtDepth(shape, depth);
      PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast.has_value());
      ASSERT_TRUE(ast.diagnostics.empty()) << ast.diagnostics.front().message;
      const auto resolved = resolveModule(*ast);
      // Nested mask calls and chained callees are syntactically supported, but
      // are not materializable storage initializers. Still exercise their checks.
      if (std::string_view{shape} == "call" || std::string_view{shape} == "postfix")
        EXPECT_FALSE(resolved.has_value());
      else
        ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    }
  }
}

/** Rejection retains source fidelity and recovers a later independent declaration. */
TEST(ConstantTreeDepth, RejectsOverLimitAndRecoversThroughLowering) {
  for (const auto shape : shapes) {
    for (const auto depth : {PtxCstParser::maxConstantTreeDepth + 1,
                             PtxCstParser::maxConstantTreeDepth * 8}) {
      SCOPED_TRACE(std::string{shape} + " depth=" + std::to_string(depth));
      const std::string source = declarationAtDepth(shape, depth) +
                                 ".global .u32 survivor = 7;\n";
      PtxCstParser cst_parser(source);
      const auto cst = cst_parser.parseModule();
      ASSERT_TRUE(cst.has_value());
      ASSERT_FALSE(cst.diagnostics.empty());
      EXPECT_NE(cst.diagnostics.front().message.find("depth limit"), std::string::npos);
      EXPECT_EQ(cst.diagnostics.front().range.start.line, 1);
      EXPECT_EQ(cst->sourceText(), source);
      PtxSyntaxParser ast_parser(source);
      const auto ast = ast_parser.parseModule();
      ASSERT_TRUE(ast.has_value());
      ASSERT_EQ(ast.diagnostics.size(), cst.diagnostics.size());
      EXPECT_EQ(ast.diagnostics.front().range, cst.diagnostics.front().range);
      const auto resolved = resolveModule(*ast);
      ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
      ASSERT_EQ(resolved->storage_declarations.size(), 1u);
      EXPECT_EQ(resolved->symbols.symbol(resolved->storage_declarations[0].symbol_id).name,
                "survivor");
    }
  }
}

/** Partial children unwind safely, including a deep completed left operand. */
TEST(ConstantTreeDepth, HandlesTruncatedTreesAndPartialOwnership) {
  for (const auto shape : shapes) {
    for (const auto depth : {PtxCstParser::maxConstantTreeDepth,
                             PtxCstParser::maxConstantTreeDepth + 1}) {
      std::string source = declarationAtDepth(shape, depth);
      source.resize(source.size() - 2);  // Remove the semicolon and newline.
      source += " + (";
      SCOPED_TRACE(std::string{shape} + " depth=" + std::to_string(depth));
      PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast.has_value());
      EXPECT_FALSE(ast.diagnostics.empty());
    }
  }
}

/** A depth policy must not become an accidental total-element budget. */
TEST(ConstantTreeDepth, KeepsWideShallowInitializersAndSiblingBudgetsIndependent) {
  const std::string source = ".global .u32 wide[] = {" + repeated("1,", 1024) +
      "1};\n" + declarationAtDepth("unary", PtxCstParser::maxConstantTreeDepth) +
      ".global .u32 last = 2;\n";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value());
  ASSERT_TRUE(ast.diagnostics.empty()) << ast.diagnostics.front().message;
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->storage_declarations.size(), 3u);
  EXPECT_EQ(resolved->storage_declarations[0].initializer.size(), 1025u);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

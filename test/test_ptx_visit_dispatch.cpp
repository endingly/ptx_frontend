#include <gtest/gtest.h>
#include <utility>
#include <variant>
#include "ptx_ir.hpp"
#include "ptx_visit_dispatch.hpp"

TEST(PtxVisitDispatch, VisitInstrAdd) {
  using namespace ptx_frontend;

  // Create a simple InstrAdd with dummy operands
  InstrAdd<ParsedOperand<Ident>> instr{
      .data = ArithInteger{ScalarType::U32},
      .dst =
          ParsedOperand<Ident>::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src1 =
          ParsedOperand<Ident>::from_value(RegOrImmediate<Ident>::Reg("src1")),
      .src2 =
          ParsedOperand<Ident>::from_value(RegOrImmediate<Ident>::Reg("src2")),
  };

  // Create a Visitor that checks the visited operands
  struct TestVisitor : Visitor<ParsedOperand<Ident>, std::string> {
    expected<void, std::string> visit(const ParsedOperand<Ident>& op,
                                      std::optional<VisitTypeSpace> ts,
                                      bool is_dst, bool relaxed) override {
      // Check that the operand is one of the expected ones
      auto flag = std::visit(
          [](auto&& item) -> std::pair<bool, std::string> {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, RegOrImmediate<Ident>>) {
              if (std::holds_alternative<Ident>(item.value)) {
                auto id = std::get<Ident>(item.value);
                auto flag_r = id == "dst" || id == "src1" || id == "src2";
                return {flag_r, std::string(id)};
              } else {
                return {false,
                        ""};  // Immediate value should not appear in this test
              }
            } else {
              return {false, ""};
            }
          },
          op.value);

      if (flag.first) {
        return {};
      } else {
        return tl::unexpected("Unexpected operand: " + flag.second);
      }
    }

    expected<void, std::string> visit_ident(const std::string_view& id,
                                            std::optional<VisitTypeSpace> ts,
                                            bool is_dst,
                                            bool relaxed) override {
      // This should not be called for this test
      return tl::unexpected("visit_ident should not be called");
    }
  } visitor;

  // Call the visit_instr function and check it succeeds
  auto result = visit_instr(instr, visitor);
  EXPECT_TRUE(result.has_value());
}
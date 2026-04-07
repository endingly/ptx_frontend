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

TEST(PtxVisitDispatch, VisitInstrCreatePolicyFractional) {
  using namespace ptx_frontend;

  InstrCreatePolicyFractional<ParsedOp> instr{
      .data = CreatePolicyFractionalDetails{EvictionPriority::Normal, 0.5f},
      .dst_policy = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("policy")),
  };

  std::vector<std::pair<Ident, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool is_dst,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), is_dst});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0].first, "policy");
  EXPECT_TRUE(visited[0].second);  // is_dst
}

TEST(PtxVisitDispatch, MapInstrCreatePolicyFractional) {
  using namespace ptx_frontend;

  InstrCreatePolicyFractional<ParsedOp> instr{
      .data = CreatePolicyFractionalDetails{EvictionPriority::Normal, 0.5f},
      .dst_policy = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("policy")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        // 模拟重命名
        if (id == "policy")
          return Ident{"policy_renamed"};
        return tl::unexpected<std::string>("unknown id");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto& roi = std::get<RegOrImmediate<Ident>>(r->dst_policy.value);
  EXPECT_EQ(std::get<Ident>(roi.value), "policy_renamed");
}

TEST(PtxVisitDispatch, VisitInstrCvt_NoSrc2) {
  using namespace ptx_frontend;

  InstrCvt<ParsedOp> instr{
      .data = CvtDetails{ScalarType::F32, ScalarType::S32,
                         CvtModeFPFromSigned{RoundingMode::NearestEven, false}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
      .src2 = std::nullopt,
  };

  // collect (ident, is_dst) pairs in visit order
  std::vector<std::pair<Ident, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool is_dst,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), is_dst});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_TRUE(visited[0].second);
  EXPECT_EQ(visited[1].first, "src");
  EXPECT_FALSE(visited[1].second);
}

TEST(PtxVisitDispatch, VisitInstrCvt_WithSrc2) {
  using namespace ptx_frontend;

  InstrCvt<ParsedOp> instr{
      .data = CvtDetails{ScalarType::F32, ScalarType::BF16x2,
                         CvtModeFPTruncate{RoundingMode::NearestEven, false,
                                           std::nullopt, false, false}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src1")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src2")),
  };

  std::vector<std::pair<Ident, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool is_dst,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), is_dst});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 3u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_TRUE(visited[0].second);
  EXPECT_EQ(visited[1].first, "src1");
  EXPECT_FALSE(visited[1].second);
  EXPECT_EQ(visited[2].first, "src2");
  EXPECT_FALSE(visited[2].second);
}

TEST(PtxVisitDispatch, MapInstrCvt) {
  using namespace ptx_frontend;

  InstrCvt<ParsedOp> instr{
      .data = CvtDetails{ScalarType::F32, ScalarType::S32,
                         CvtModeFPFromSigned{RoundingMode::NearestEven, false}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
      .src2 = std::nullopt,
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "dst")
          return Ident{"dst2"};
        if (id == "src")
          return Ident{"src2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto& dst_roi = std::get<RegOrImmediate<Ident>>(r->dst.value);
  EXPECT_EQ(std::get<Ident>(dst_roi.value), "dst2");
  auto& src_roi = std::get<RegOrImmediate<Ident>>(r->src.value);
  EXPECT_EQ(std::get<Ident>(src_roi.value), "src2");
  EXPECT_FALSE(r->src2.has_value());
}

TEST(PtxVisitDispatch, VisitInstrCvta) {
  using namespace ptx_frontend;

  InstrCvta<ParsedOp> instr{
      .data = CvtaDetails{StateSpace::Global, CvtaDirection::GenericToExplicit},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };

  std::vector<std::pair<Ident, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool is_dst,
          bool relaxed) -> expected<void, std::string> {
        EXPECT_TRUE(relaxed);  // cvta 总是 relaxed
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), is_dst});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_TRUE(visited[0].second);
  EXPECT_EQ(visited[1].first, "src");
  EXPECT_FALSE(visited[1].second);
}

TEST(PtxVisitDispatch, MapInstrCvta) {
  using namespace ptx_frontend;

  InstrCvta<ParsedOp> instr{
      .data = CvtaDetails{StateSpace::Shared, CvtaDirection::ExplicitToGeneric},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "dst")
          return Ident{"dst_r"};
        if (id == "src")
          return Ident{"src_r"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->data.state_space, StateSpace::Shared);
  auto& dst_roi = std::get<RegOrImmediate<Ident>>(r->dst.value);
  EXPECT_EQ(std::get<Ident>(dst_roi.value), "dst_r");
  auto& src_roi = std::get<RegOrImmediate<Ident>>(r->src.value);
  EXPECT_EQ(std::get<Ident>(src_roi.value), "src_r");
}

TEST(PtxVisitDispatch, VisitInstrDiv_Int) {
  using namespace ptx_frontend;

  InstrDiv<ParsedOp> instr{
      .data = DivInt{DivSign::Signed, ScalarType::S32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src1")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src2")),
  };

  std::vector<Ident> ids;
  std::vector<ScalarType> types;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        types.push_back(std::get<ScalarTypeT>(*ts->type).type);
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "dst");
  EXPECT_EQ(ids[1], "src1");
  EXPECT_EQ(ids[2], "src2");

  EXPECT_EQ(types[0], ScalarType::S32);
  EXPECT_EQ(types[1], ScalarType::S32);
  EXPECT_EQ(types[2], ScalarType::S32);
}

TEST(PtxVisitDispatch, MapInstrDiv) {
  using namespace ptx_frontend;

  InstrDiv<ParsedOp> instr{
      .data = DivInt{DivSign::Unsigned, ScalarType::U32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return Ident{id == "d" ? "d2" : id == "a" ? "a2" : "b2"};
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

TEST(PtxVisitDispatch, VisitInstrDp4a) {
  using namespace ptx_frontend;

  // atype=S32, btype=U32 → ctype()=S32
  InstrDp4a<ParsedOp> instr{
      .data = Dp4aDetails{ScalarType::S32, ScalarType::U32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src1")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src2")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src3")),
  };

  std::vector<std::pair<Ident, ScalarType>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value),
                           std::get<ScalarTypeT>(*ts->type).type});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 4u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_EQ(visited[0].second, ScalarType::S32);  // ctype
  EXPECT_EQ(visited[1].first, "src1");
  EXPECT_EQ(visited[1].second, ScalarType::S32);  // atype
  EXPECT_EQ(visited[2].first, "src2");
  EXPECT_EQ(visited[2].second, ScalarType::U32);  // btype
  EXPECT_EQ(visited[3].first, "src3");
  EXPECT_EQ(visited[3].second, ScalarType::S32);  // ctype (acc)
}

TEST(PtxVisitDispatch, MapInstrDp4a) {
  using namespace ptx_frontend;

  InstrDp4a<ParsedOp> instr{
      .data = Dp4aDetails{ScalarType::U32, ScalarType::U32},  // ctype=U32
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "c")
          return Ident{"c2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "c2");
}

// ── InstrEx2 ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrEx2) {
  using namespace ptx_frontend;

  InstrEx2<ParsedOp> instr{
      .data = TypeFtz{std::nullopt, ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };

  std::vector<std::pair<Ident, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool is_dst,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::F32);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), is_dst});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_TRUE(visited[0].second);
  EXPECT_EQ(visited[1].first, "src");
  EXPECT_FALSE(visited[1].second);
}

TEST(PtxVisitDispatch, MapInstrEx2) {
  using namespace ptx_frontend;

  InstrEx2<ParsedOp> instr{
      .data = TypeFtz{std::nullopt, ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return id == "d" ? Ident{"d2"} : Ident{"s2"};
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "s2");
}

// ── InstrFma ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrFma) {
  using namespace ptx_frontend;

  InstrFma<ParsedOp> instr{
      .data = ArithFloat{ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::F32);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "c");
}

TEST(PtxVisitDispatch, MapInstrFma) {
  using namespace ptx_frontend;

  InstrFma<ParsedOp> instr{
      .data = ArithFloat{ScalarType::F64},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        std::string s(id);
        return Ident{s + "2"};  // 注意：Ident = string_view，测试中用字面量
      });

  // 字面量版本（避免悬空 string_view）
  auto vm2 = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "c")
          return Ident{"c2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm2);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "c2");
}

// ── InstrLd ────────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrLd) {
  using namespace ptx_frontend;

  InstrLd<ParsedOp> instr{
      .data =
          LdDetails{
              .qualifier = LdStQualifierData{LdStQualifier::Weak},
              .state_space = StateSpace::Global,
              .caching = LdCacheOperator::Cached,
              .typ = make_scalar(ScalarType::F32),
          },
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };

  std::vector<std::tuple<Ident, StateSpace, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool is_dst,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space, is_dst});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  // dst: Reg 空间
  EXPECT_EQ(std::get<0>(visited[0]), "dst");
  EXPECT_EQ(std::get<1>(visited[0]), StateSpace::Reg);
  EXPECT_TRUE(std::get<2>(visited[0]));
  // src: Global 地址
  EXPECT_EQ(std::get<0>(visited[1]), "addr");
  EXPECT_EQ(std::get<1>(visited[1]), StateSpace::Global);
  EXPECT_FALSE(std::get<2>(visited[1]));
}

TEST(PtxVisitDispatch, MapInstrLd) {
  using namespace ptx_frontend;

  InstrLd<ParsedOp> instr{
      .data =
          LdDetails{
              .qualifier = LdStQualifierData{LdStQualifier::Weak},
              .state_space = StateSpace::Shared,
              .typ = make_scalar(ScalarType::U32),
          },
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("ptr")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "dst")
          return Ident{"dst2"};
        if (id == "ptr")
          return Ident{"ptr2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "dst2");
  EXPECT_EQ(get_id(r->src), "ptr2");
  EXPECT_EQ(r->data.state_space, StateSpace::Shared);
}

// ── InstrLg2 ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrLg2) {
  using namespace ptx_frontend;
  InstrLg2<ParsedOp> instr{
      .data = FlushToZero{false},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };
  std::vector<std::pair<Ident, ScalarType>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value),
                           std::get<ScalarTypeT>(*ts->type).type});
        return {};
      });
  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_EQ(visited[0].second, ScalarType::F32);
  EXPECT_EQ(visited[1].first, "src");
  EXPECT_EQ(visited[1].second, ScalarType::F32);
}

// ── InstrMad ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMad_Int) {
  using namespace ptx_frontend;
  InstrMad<ParsedOp> instr{
      .data = MadInt{MulIntControl::Low, false, ScalarType::S32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::S32);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });
  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "c");
}

TEST(PtxVisitDispatch, MapInstrMad) {
  using namespace ptx_frontend;
  InstrMad<ParsedOp> instr{
      .data = MadInt{MulIntControl::Low, false, ScalarType::U32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "c")
          return Ident{"c2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "c2");
}

// ── InstrMax / InstrMin ─────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMax) {
  using namespace ptx_frontend;
  InstrMax<ParsedOp> instr{
      .data = MinMaxInt{MinMaxSign::Signed, ScalarType::S32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::S32);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });
  EXPECT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

TEST(PtxVisitDispatch, VisitInstrMin) {
  using namespace ptx_frontend;
  InstrMin<ParsedOp> instr{
      .data = MinMaxFloat{std::nullopt, false, ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });
  EXPECT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

// ── InstrMembar ────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMembar) {
  using namespace ptx_frontend;
  InstrMembar instr{MemScope::Gpu};
  int call_count = 0;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        ++call_count;
        return {};
      });
  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  EXPECT_EQ(call_count, 0);  // no operands
}

TEST(PtxVisitDispatch, MapInstrMembar) {
  using namespace ptx_frontend;
  InstrMembar instr{MemScope::Sys};
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->data, MemScope::Sys);
}

// ── InstrMov ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMov) {
  using namespace ptx_frontend;
  InstrMov<ParsedOp> instr{
      .data = MovDetails{make_scalar(ScalarType::B32)},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };
  std::vector<std::pair<Ident, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool is_dst,
          bool relaxed) -> expected<void, std::string> {
        EXPECT_TRUE(relaxed);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), is_dst});
        return {};
      });
  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_TRUE(visited[0].second);
  EXPECT_EQ(visited[1].first, "src");
  EXPECT_FALSE(visited[1].second);
}

TEST(PtxVisitDispatch, MapInstrMov) {
  using namespace ptx_frontend;
  InstrMov<ParsedOp> instr{
      .data = MovDetails{make_scalar(ScalarType::U64)},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return id == "d" ? Ident{"d2"} : Ident{"s2"};
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "s2");
}

// ── InstrMul ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMul_Wide) {
  using namespace ptx_frontend;
  // mul.wide.s32: src=S32, dst=S64
  InstrMul<ParsedOp> instr{
      .data = MulInt{ScalarType::S32, MulIntControl::Wide},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<std::pair<Ident, ScalarType>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value),
                           std::get<ScalarTypeT>(*ts->type).type});
        return {};
      });
  EXPECT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(visited.size(), 3u);
  EXPECT_EQ(visited[0].first, "d");
  EXPECT_EQ(visited[0].second, ScalarType::S64);  // widened
  EXPECT_EQ(visited[1].first, "a");
  EXPECT_EQ(visited[1].second, ScalarType::S32);
  EXPECT_EQ(visited[2].first, "b");
  EXPECT_EQ(visited[2].second, ScalarType::S32);
}

TEST(PtxVisitDispatch, MapInstrMul) {
  using namespace ptx_frontend;
  InstrMul<ParsedOp> instr{
      .data = MulInt{ScalarType::U32, MulIntControl::Low},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

// ── InstrMul24 ─────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMul24) {
  using namespace ptx_frontend;
  InstrMul24<ParsedOp> instr{
      .data = Mul24Details{ScalarType::S32, Mul24Control::Lo},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::S32);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });
  EXPECT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

// ── InstrNanosleep ─────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrNanosleep) {
  using namespace ptx_frontend;
  InstrNanosleep<ParsedOp> instr{
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("delay")),
  };
  std::vector<std::pair<Ident, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool is_dst,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::U32);
        EXPECT_FALSE(is_dst);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), is_dst});
        return {};
      });
  EXPECT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0].first, "delay");
}

TEST(PtxVisitDispatch, MapInstrNanosleep) {
  using namespace ptx_frontend;
  InstrNanosleep<ParsedOp> instr{
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto& roi = std::get<RegOrImmediate<Ident>>(r->src.value);
  EXPECT_EQ(std::get<Ident>(roi.value), "d2");
}

// ── InstrNeg ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrNeg) {
  using namespace ptx_frontend;
  InstrNeg<ParsedOp> instr{
      .data = TypeFtz{true, ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };
  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::F32);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });
  EXPECT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "dst");
  EXPECT_EQ(ids[1], "src");
}

// ── InstrNot ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrNot) {
  using namespace ptx_frontend;
  InstrNot<ParsedOp> instr{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };
  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::B32);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });
  EXPECT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "dst");
  EXPECT_EQ(ids[1], "src");
}

// ── InstrOr ────────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrOr) {
  using namespace ptx_frontend;
  InstrOr<ParsedOp> instr{
      .data = ScalarType::B64,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(std::get<ScalarTypeT>(*ts->type).type, ScalarType::B64);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });
  EXPECT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

TEST(PtxVisitDispatch, MapInstrOr) {
  using namespace ptx_frontend;
  InstrOr<ParsedOp> instr{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

// ── InstrPopc ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrPopc) {
  using namespace ptx_frontend;
  InstrPopc<ParsedOp> instr{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  auto r = visit_instr(instr, v);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
}

TEST(PtxVisitDispatch, MapInstrPopc) {
  using namespace ptx_frontend;
  InstrPopc<ParsedOp> instr{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "a2");
}

// ── InstrPrmt ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrPrmt) {
  using namespace ptx_frontend;
  InstrPrmt<ParsedOp> instr{
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  auto r = visit_instr(instr, v);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "c");
}

TEST(PtxVisitDispatch, MapInstrPrmt) {
  using namespace ptx_frontend;
  InstrPrmt<ParsedOp> instr{
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "c")
          return Ident{"c2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "c2");
}

// ── InstrRcp ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrRcp) {
  using namespace ptx_frontend;
  InstrRcp<ParsedOp> instr{
      .data = RcpData{.kind = RcpKind{std::monostate{}},
                      .flush_to_zero = std::nullopt,
                      .type_ = ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  auto r = visit_instr(instr, v);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
}

TEST(PtxVisitDispatch, MapInstrRcp) {
  using namespace ptx_frontend;
  InstrRcp<ParsedOp> instr{
      .data = RcpData{.kind = RcpKind{std::monostate{}},
                      .flush_to_zero = std::nullopt,
                      .type_ = ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "a2");
}

// ── InstrRem ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrRem) {
  using namespace ptx_frontend;
  InstrRem<ParsedOp> instr{
      .data = ScalarType::S32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  auto r = visit_instr(instr, v);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

TEST(PtxVisitDispatch, MapInstrRem) {
  using namespace ptx_frontend;
  InstrRem<ParsedOp> instr{
      .data = ScalarType::S32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

// ── InstrRet ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrRet) {
  using namespace ptx_frontend;
  InstrRet instr{.data = RetData{.uniform = false}};
  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> { return {}; });
  auto r = visit_instr(instr, v);
  ASSERT_TRUE(r.has_value());
}

TEST(PtxVisitDispatch, MapInstrRet) {
  using namespace ptx_frontend;
  InstrRet instr{.data = RetData{.uniform = true}};
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> { return id; });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->data.uniform, true);
}

// ── InstrRsqrt ─────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrRsqrt) {
  using namespace ptx_frontend;
  InstrRsqrt<ParsedOp> instr{
      .data = TypeFtz{.flush_to_zero = false, .type_ = ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  auto r = visit_instr(instr, v);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
}

TEST(PtxVisitDispatch, MapInstrRsqrt) {
  using namespace ptx_frontend;
  InstrRsqrt<ParsedOp> instr{
      .data = TypeFtz{.flush_to_zero = false, .type_ = ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "a2");
}

// ── InstrSelp ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSelp) {
  using namespace ptx_frontend;
  InstrSelp<ParsedOp> instr{
      .data = ScalarType::F32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "p");
}

TEST(PtxVisitDispatch, MapInstrSelp) {
  using namespace ptx_frontend;
  InstrSelp<ParsedOp> instr{
      .data = ScalarType::F32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "p")
          return Ident{"p2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "p2");
}

// ── InstrSet ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSet) {
  using namespace ptx_frontend;
  InstrSet<ParsedOp> instr{
      .data = SetData{.dtype = ScalarType::U32,
                      .base = SetpData{.type_ = ScalarType::F32,
                                       .flush_to_zero = std::nullopt,
                                       .cmp_op = SetpCompareFloat::Less}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

TEST(PtxVisitDispatch, MapInstrSet) {
  using namespace ptx_frontend;
  InstrSet<ParsedOp> instr{
      .data = SetData{.dtype = ScalarType::U32,
                      .base = SetpData{.type_ = ScalarType::F32,
                                       .flush_to_zero = std::nullopt,
                                       .cmp_op = SetpCompareFloat::Less}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

// ── InstrSetBool ───────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSetBool) {
  using namespace ptx_frontend;
  InstrSetBool<ParsedOp> instr{
      .data =
          SetBoolData{
              .dtype = ScalarType::U32,
              .base =
                  SetpBoolData{
                      .base = SetpData{.type_ = ScalarType::S32,
                                       .flush_to_zero = std::nullopt,
                                       .cmp_op = SetpCompareInt::SignedLess},
                      .bool_op = SetpBoolPostOp::And,
                      .negate_src3 = false}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "p");
}

TEST(PtxVisitDispatch, MapInstrSetBool) {
  using namespace ptx_frontend;
  InstrSetBool<ParsedOp> instr{
      .data =
          SetBoolData{
              .dtype = ScalarType::U32,
              .base =
                  SetpBoolData{
                      .base = SetpData{.type_ = ScalarType::S32,
                                       .flush_to_zero = std::nullopt,
                                       .cmp_op = SetpCompareInt::SignedLess},
                      .bool_op = SetpBoolPostOp::And,
                      .negate_src3 = false}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "p")
          return Ident{"p2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "p2");
}

// ── InstrSetp ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSetp) {
  using namespace ptx_frontend;
  InstrSetp<ParsedOp> instr{
      .data = SetpData{.type_ = ScalarType::S32,
                       .flush_to_zero = std::nullopt,
                       .cmp_op = SetpCompareInt::SignedLess},
      .dst1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p1")),
      .dst2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p2")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "p1");
  EXPECT_EQ(ids[1], "p2");
  EXPECT_EQ(ids[2], "a");
  EXPECT_EQ(ids[3], "b");
}

TEST(PtxVisitDispatch, MapInstrSetp) {
  using namespace ptx_frontend;
  InstrSetp<ParsedOp> instr{
      .data = SetpData{.type_ = ScalarType::S32,
                       .flush_to_zero = std::nullopt,
                       .cmp_op = SetpCompareInt::SignedLess},
      .dst1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p1")),
      .dst2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p2")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "p1")
          return Ident{"p1x"};
        if (id == "p2")
          return Ident{"p2x"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst1), "p1x");
  ASSERT_TRUE(r->dst2.has_value());
  EXPECT_EQ(get_id(*r->dst2), "p2x");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

// ── InstrSetpBool ──────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSetpBool) {
  using namespace ptx_frontend;
  InstrSetpBool<ParsedOp> instr{
      .data =
          SetpBoolData{.base = SetpData{.type_ = ScalarType::S32,
                                        .flush_to_zero = std::nullopt,
                                        .cmp_op = SetpCompareInt::SignedLess},
                       .bool_op = SetpBoolPostOp::And,
                       .negate_src3 = false},
      .dst1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p1")),
      .dst2 = std::nullopt,
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("q")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 4u);  // dst2 absent
  EXPECT_EQ(ids[0], "p1");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "q");
}

TEST(PtxVisitDispatch, MapInstrSetpBool) {
  using namespace ptx_frontend;
  InstrSetpBool<ParsedOp> instr{
      .data =
          SetpBoolData{.base = SetpData{.type_ = ScalarType::S32,
                                        .flush_to_zero = std::nullopt,
                                        .cmp_op = SetpCompareInt::SignedLess},
                       .bool_op = SetpBoolPostOp::And,
                       .negate_src3 = false},
      .dst1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p1")),
      .dst2 = std::nullopt,
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("q")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "p1")
          return Ident{"p1x"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "q")
          return Ident{"q2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst1), "p1x");
  EXPECT_FALSE(r->dst2.has_value());
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "q2");
}

// ── InstrShflSync ──────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrShflSync) {
  using namespace ptx_frontend;
  InstrShflSync<ParsedOp> instr{
      .data = ShflSyncDetails{.mode = ShuffleMode::Idx},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .dst_pred = std::nullopt,
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src_lane = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("lane")),
      .src_opts = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("opts")),
      .src_membermask =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mask")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 5u);  // dst_pred absent
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "lane");
  EXPECT_EQ(ids[3], "opts");
  EXPECT_EQ(ids[4], "mask");
}

TEST(PtxVisitDispatch, MapInstrShflSync) {
  using namespace ptx_frontend;
  InstrShflSync<ParsedOp> instr{
      .data = ShflSyncDetails{.mode = ShuffleMode::Idx},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .dst_pred = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src_lane = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("lane")),
      .src_opts = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("opts")),
      .src_membermask =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mask")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        std::string s(id);
        return Ident{
            (s + "2").c_str()};  // NOTE: dangling — use arena in real code
      });
  // Use simpler identity rename for test correctness
  auto vm2 = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "p")
          return Ident{"p2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "lane")
          return Ident{"lane2"};
        if (id == "opts")
          return Ident{"opts2"};
        if (id == "mask")
          return Ident{"mask2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm2);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  ASSERT_TRUE(r->dst_pred.has_value());
  EXPECT_EQ(get_id(*r->dst_pred), "p2");
  EXPECT_EQ(get_id(r->src), "a2");
  EXPECT_EQ(get_id(r->src_lane), "lane2");
  EXPECT_EQ(get_id(r->src_opts), "opts2");
  EXPECT_EQ(get_id(r->src_membermask), "mask2");
}

// ── InstrShf ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrShf) {
  using namespace ptx_frontend;
  InstrShf<ParsedOp> instr{
      .data = ShfDetails{.direction = ShiftDirection::Left,
                         .mode = FunnelShiftMode::Clamp},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src_a = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src_b = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src_c = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "c");
}

TEST(PtxVisitDispatch, MapInstrShf) {
  using namespace ptx_frontend;
  InstrShf<ParsedOp> instr{
      .data = ShfDetails{.direction = ShiftDirection::Left,
                         .mode = FunnelShiftMode::Clamp},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src_a = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src_b = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src_c = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "c")
          return Ident{"c2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src_a), "a2");
  EXPECT_EQ(get_id(r->src_b), "b2");
  EXPECT_EQ(get_id(r->src_c), "c2");
}

// ── InstrShl ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrShl) {
  using namespace ptx_frontend;
  InstrShl<ParsedOp> instr{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("n")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "n");
}

TEST(PtxVisitDispatch, MapInstrShl) {
  using namespace ptx_frontend;
  InstrShl<ParsedOp> instr{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("n")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "n")
          return Ident{"n2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "n2");
}

// ── InstrShr ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrShr) {
  using namespace ptx_frontend;
  InstrShr<ParsedOp> instr{
      .data =
          ShrData{.type_ = ScalarType::S32, .kind = RightShiftKind::Arithmetic},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("n")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "n");
}

TEST(PtxVisitDispatch, MapInstrShr) {
  using namespace ptx_frontend;
  InstrShr<ParsedOp> instr{
      .data =
          ShrData{.type_ = ScalarType::S32, .kind = RightShiftKind::Arithmetic},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("n")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "n")
          return Ident{"n2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "n2");
}

// ── InstrSin ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSin) {
  using namespace ptx_frontend;
  InstrSin<ParsedOp> instr{
      .data = FlushToZero{.flush_to_zero = false},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
}

TEST(PtxVisitDispatch, MapInstrSin) {
  using namespace ptx_frontend;
  InstrSin<ParsedOp> instr{
      .data = FlushToZero{.flush_to_zero = false},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "a2");
}

// ── InstrSqrt ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSqrt) {
  using namespace ptx_frontend;
  InstrSqrt<ParsedOp> instr{
      .data = RcpData{.kind = RcpKind{std::monostate{}},
                      .flush_to_zero = std::nullopt,
                      .type_ = ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
}

TEST(PtxVisitDispatch, MapInstrSqrt) {
  using namespace ptx_frontend;
  InstrSqrt<ParsedOp> instr{
      .data = RcpData{.kind = RcpKind{std::monostate{}},
                      .flush_to_zero = std::nullopt,
                      .type_ = ScalarType::F32},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "a2");
}

// ── InstrSt ────────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSt) {
  using namespace ptx_frontend;
  InstrSt<ParsedOp> instr{
      .data = StData{.qualifier = LdStQualifierData{},
                     .state_space = StateSpace::Global,
                     .caching = StCacheOperator::Writeback,
                     .typ = make_scalar(ScalarType::F32)},
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("val")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "addr");
  EXPECT_EQ(ids[1], "val");
}

TEST(PtxVisitDispatch, MapInstrSt) {
  using namespace ptx_frontend;
  InstrSt<ParsedOp> instr{
      .data = StData{.qualifier = LdStQualifierData{},
                     .state_space = StateSpace::Global,
                     .caching = StCacheOperator::Writeback,
                     .typ = make_scalar(ScalarType::F32)},
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("val")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "addr")
          return Ident{"addr2"};
        if (id == "val")
          return Ident{"val2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->src1), "addr2");
  EXPECT_EQ(get_id(r->src2), "val2");
}

// ── InstrSub ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSub) {
  using namespace ptx_frontend;
  InstrSub<ParsedOp> instr{
      .data = ArithDetails{ArithInteger{.type_ = ScalarType::S32}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

TEST(PtxVisitDispatch, MapInstrSub) {
  using namespace ptx_frontend;
  InstrSub<ParsedOp> instr{
      .data = ArithDetails{ArithInteger{.type_ = ScalarType::S32}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

// ── InstrTrap ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTrap) {
  using namespace ptx_frontend;
  InstrTrap instr{};
  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> { return {}; });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
}

TEST(PtxVisitDispatch, MapInstrTrap) {
  using namespace ptx_frontend;
  InstrTrap instr{};
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> { return id; });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
}

// ── InstrXor ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrXor) {
  using namespace ptx_frontend;
  InstrXor<ParsedOp> instr{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

TEST(PtxVisitDispatch, MapInstrXor) {
  using namespace ptx_frontend;
  InstrXor<ParsedOp> instr{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

// ── InstrTanh ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTanh) {
  using namespace ptx_frontend;
  InstrTanh<ParsedOp> instr{
      .data = ScalarType::F32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
}

TEST(PtxVisitDispatch, MapInstrTanh) {
  using namespace ptx_frontend;
  InstrTanh<ParsedOp> instr{
      .data = ScalarType::F32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "a2");
}

// ── InstrVote ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrVote) {
  using namespace ptx_frontend;
  // Ballot mode: dst is b32
  InstrVote<ParsedOp> instr{
      .data = VoteDetails{.mode = VoteMode::Ballot, .negate = false},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mask")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "p");
  EXPECT_EQ(ids[2], "mask");
}

TEST(PtxVisitDispatch, MapInstrVote) {
  using namespace ptx_frontend;
  InstrVote<ParsedOp> instr{
      .data = VoteDetails{.mode = VoteMode::Ballot, .negate = false},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("p")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mask")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "p")
          return Ident{"p2"};
        if (id == "mask")
          return Ident{"mask2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "p2");
  EXPECT_EQ(get_id(r->src2), "mask2");
}

// ── InstrReduxSync ─────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrReduxSync) {
  using namespace ptx_frontend;
  InstrReduxSync<ParsedOp> instr{
      .data =
          ReduxSyncData{.type_ = ScalarType::U32, .reduction = Reduction::And},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src_membermask =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mask")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "mask");
}

TEST(PtxVisitDispatch, MapInstrReduxSync) {
  using namespace ptx_frontend;
  InstrReduxSync<ParsedOp> instr{
      .data =
          ReduxSyncData{.type_ = ScalarType::U32, .reduction = Reduction::And},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src_membermask =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mask")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "mask")
          return Ident{"mask2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "a2");
  EXPECT_EQ(get_id(r->src_membermask), "mask2");
}

// ── InstrLdMatrix ──────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrLdMatrix) {
  using namespace ptx_frontend;
  InstrLdMatrix<ParsedOp> instr{
      .data = LdMatrixDetails{.shape = MatrixShape::M8N8,
                              .number = MatrixNumber::X1,
                              .transpose = false,
                              .state_space = StateSpace::Shared,
                              .type_ = ScalarType::B16},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "addr");
}

TEST(PtxVisitDispatch, MapInstrLdMatrix) {
  using namespace ptx_frontend;
  InstrLdMatrix<ParsedOp> instr{
      .data = LdMatrixDetails{.shape = MatrixShape::M8N8,
                              .number = MatrixNumber::X1,
                              .transpose = false,
                              .state_space = StateSpace::Shared,
                              .type_ = ScalarType::B16},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "addr")
          return Ident{"addr2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "addr2");
}

// ── InstrGridDepControl ────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrGridDepControl) {
  using namespace ptx_frontend;
  InstrGridDepControl instr{.data = GridDepControlAction::LaunchDependents};
  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> { return {}; });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
}

TEST(PtxVisitDispatch, MapInstrGridDepControl) {
  using namespace ptx_frontend;
  InstrGridDepControl instr{.data = GridDepControlAction::WaitOnDependent};
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> { return id; });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->data, GridDepControlAction::WaitOnDependent);
}

// ── InstrMma ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMma) {
  using namespace ptx_frontend;
  InstrMma<ParsedOp> instr{
      .data = MmaDetails{.alayout = MatrixLayout::RowMajor,
                         .blayout = MatrixLayout::ColMajor,
                         .cd_type_scalar = ScalarType::F32,
                         .ab_type_scalar = ScalarType::F16},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "c");
}

TEST(PtxVisitDispatch, MapInstrMma) {
  using namespace ptx_frontend;
  InstrMma<ParsedOp> instr{
      .data = MmaDetails{.alayout = MatrixLayout::RowMajor,
                         .blayout = MatrixLayout::ColMajor,
                         .cd_type_scalar = ScalarType::F32,
                         .ab_type_scalar = ScalarType::F16},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "c")
          return Ident{"c2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "c2");
}

// ── InstrCopysign ──────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrCopysign) {
  using namespace ptx_frontend;
  InstrCopysign<ParsedOp> instr{
      .data = ScalarType::F32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

TEST(PtxVisitDispatch, MapInstrCopysign) {
  using namespace ptx_frontend;
  InstrCopysign<ParsedOp> instr{
      .data = ScalarType::F32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
}

// ── InstrPrefetch ──────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrPrefetch) {
  using namespace ptx_frontend;
  InstrPrefetch<ParsedOp> instr{
      .data =
          PrefetchData{.space = StateSpace::Global, .level = CacheLevel::L1},
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], "addr");
}

TEST(PtxVisitDispatch, MapInstrPrefetch) {
  using namespace ptx_frontend;
  InstrPrefetch<ParsedOp> instr{
      .data =
          PrefetchData{.space = StateSpace::Global, .level = CacheLevel::L1},
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "addr")
          return Ident{"addr2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->src), "addr2");
}

// ── InstrSad ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSad) {
  using namespace ptx_frontend;
  InstrSad<ParsedOp> instr{
      .data = ScalarType::U32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "c");
}

TEST(PtxVisitDispatch, MapInstrSad) {
  using namespace ptx_frontend;
  InstrSad<ParsedOp> instr{
      .data = ScalarType::U32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "c")
          return Ident{"c2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "c2");
}

// ── InstrDp2a ──────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrDp2a) {
  using namespace ptx_frontend;
  InstrDp2a<ParsedOp> instr{
      .data = Dp2aData{.atype = ScalarType::U32,
                       .btype = ScalarType::U32,
                       .control = Dp2aControl::Low},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  ASSERT_TRUE(visit_instr(instr, v).has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
  EXPECT_EQ(ids[3], "c");
}

TEST(PtxVisitDispatch, MapInstrDp2a) {
  using namespace ptx_frontend;
  InstrDp2a<ParsedOp> instr{
      .data = Dp2aData{.atype = ScalarType::U32,
                       .btype = ScalarType::U32,
                       .control = Dp2aControl::Low},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
      .src3 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("c")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        if (id == "c")
          return Ident{"c2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src1), "a2");
  EXPECT_EQ(get_id(r->src2), "b2");
  EXPECT_EQ(get_id(r->src3), "c2");
}

// ── visit_instruction / map_instruction (unified variant entry points) ─────
TEST(PtxVisitDispatch, VisitInstruction) {
  using namespace ptx_frontend;
  // Use InstrAdd wrapped in Instruction<ParsedOp> to exercise the unified
  // std::visit dispatch.
  Instruction<ParsedOp> instr = InstrAdd<ParsedOp>{
      .data = ArithDetails{ArithInteger{.type_ = ScalarType::S32}},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  std::vector<std::string> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&ids](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
             bool) -> expected<void, std::string> {
        ids.push_back(std::string(
            std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value)));
        return {};
      });
  auto r = visit_instruction(instr, v);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "d");
  EXPECT_EQ(ids[1], "a");
  EXPECT_EQ(ids[2], "b");
}

TEST(PtxVisitDispatch, MapInstruction) {
  using namespace ptx_frontend;
  // Use InstrOr wrapped in Instruction<ParsedOp> to exercise the unified
  // map dispatch, verifying the result is held as InstrOr<ParsedOp>.
  Instruction<ParsedOp> instr = InstrOr<ParsedOp>{
      .data = ScalarType::B32,
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src1 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .src2 = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "a")
          return Ident{"a2"};
        if (id == "b")
          return Ident{"b2"};
        return tl::unexpected<std::string>("unknown");
      });
  auto r = map_instruction(instr, vm);
  ASSERT_TRUE(r.has_value());
  // The result variant must hold InstrOr<ParsedOp>
  ASSERT_TRUE(std::holds_alternative<InstrOr<ParsedOp>>(*r));
  const auto& result = std::get<InstrOr<ParsedOp>>(*r);
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(result.dst), "d2");
  EXPECT_EQ(get_id(result.src1), "a2");
  EXPECT_EQ(get_id(result.src2), "b2");
}
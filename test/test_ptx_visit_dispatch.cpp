#include <gtest/gtest.h>
#include <utility>
#include <variant>
#include "ptx_ir/instr.hpp"
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
      .data = LdMatrixDetails{.shape = LdStMatrixShape::M8N8,
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
      .data = LdMatrixDetails{.shape = LdStMatrixShape::M8N8,
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
      .data = MmaDetails{.shape = MmaShape::M16N8K16,
                         .alayout = MatrixLayout::RowMajor,
                         .blayout = MatrixLayout::ColMajor,
                         .dtype = ScalarType::F32,
                         .atype = ScalarType::F16,
                         .btype = ScalarType::F16},
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
      .data = MmaDetails{.shape = MmaShape::M16N8K16,
                         .alayout = MatrixLayout::RowMajor,
                         .blayout = MatrixLayout::ColMajor,
                         .dtype = ScalarType::F32,
                         .atype = ScalarType::F16,
                         .btype = ScalarType::F16},
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

// ── InstrMovMatrix ─────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMovMatrix) {
  using namespace ptx_frontend;

  InstrMovMatrix<ParsedOp> instr{
      .data = MovMatrixDetails{MatrixNumber::X1, /*transpose=*/true},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };

  std::vector<std::pair<Ident, bool>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool is_dst,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(ts->space, StateSpace::Shared);
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

TEST(PtxVisitDispatch, MapInstrMovMatrix) {
  using namespace ptx_frontend;

  InstrMovMatrix<ParsedOp> instr{
      .data = MovMatrixDetails{MatrixNumber::X1, false},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d")
          return Ident{"d2"};
        if (id == "s")
          return Ident{"s2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "d2");
  EXPECT_EQ(get_id(r->src), "s2");
  EXPECT_EQ(r->data.transpose, false);
}

// ── InstrCpAsyncBulk ───────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrCpAsyncBulk) {
  using namespace ptx_frontend;

  InstrCpAsyncBulk<ParsedOp> instr{
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
      .size = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("sz")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 3u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_EQ(visited[0].second, StateSpace::SharedCluster);
  EXPECT_EQ(visited[1].first, "src");
  EXPECT_EQ(visited[1].second, StateSpace::Global);
  EXPECT_EQ(visited[2].first, "sz");
  EXPECT_EQ(visited[2].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrCpAsyncBulk) {
  using namespace ptx_frontend;

  InstrCpAsyncBulk<ParsedOp> instr{
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
      .size = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("sz")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "dst")
          return Ident{"dst2"};
        if (id == "src")
          return Ident{"src2"};
        if (id == "sz")
          return Ident{"sz2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "dst2");
  EXPECT_EQ(get_id(r->src), "src2");
  EXPECT_EQ(get_id(r->size), "sz2");
}

// ── InstrCpAsyncBulkTensor ─────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrCpAsyncBulkTensor_ToShared) {
  using namespace ptx_frontend;

  InstrCpAsyncBulkTensor<ParsedOp> instr{
      .data =
          CpAsyncBulkTensorDetails{TensorDim::D2,
                                   CpAsyncBulkTensorDir::GlobalToSharedCluster},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .tensormap = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tmap")),
      .coords = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cx")),
                 ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cy"))},
      .offsets = std::nullopt,
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  // dst, tmap, cx, cy
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "dst");
  EXPECT_EQ(ids[1], "tmap");
  EXPECT_EQ(ids[2], "cx");
  EXPECT_EQ(ids[3], "cy");
}

TEST(PtxVisitDispatch, VisitInstrCpAsyncBulkTensor_WithOffsets) {
  using namespace ptx_frontend;

  InstrCpAsyncBulkTensor<ParsedOp> instr{
      .data =
          CpAsyncBulkTensorDetails{TensorDim::D2,
                                   CpAsyncBulkTensorDir::GlobalToSharedCluster},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .tensormap = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tmap")),
      .coords = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cx")),
                 ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cy"))},
      .offsets =
          std::vector<ParsedOp>{
              ParsedOp::from_value(RegOrImmediate<Ident>::Reg("ox")),
              ParsedOp::from_value(RegOrImmediate<Ident>::Reg("oy")),
          },
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  // dst, tmap, cx, cy, ox, oy
  ASSERT_EQ(ids.size(), 6u);
  EXPECT_EQ(ids[4], "ox");
  EXPECT_EQ(ids[5], "oy");
}

TEST(PtxVisitDispatch, MapInstrCpAsyncBulkTensor) {
  using namespace ptx_frontend;

  InstrCpAsyncBulkTensor<ParsedOp> instr{
      .data =
          CpAsyncBulkTensorDetails{TensorDim::D1,
                                   CpAsyncBulkTensorDir::GlobalToSharedCluster},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .tensormap = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tmap")),
      .coords = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cx"))},
      .offsets = std::nullopt,
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "dst")
          return Ident{"r_dst"};
        if (id == "tmap")
          return Ident{"r_tmap"};
        if (id == "cx")
          return Ident{"r_cx"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::string(
        std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value));
  };
  EXPECT_EQ(get_id(r->dst), "r_dst");
  EXPECT_EQ(get_id(r->tensormap), "r_tmap");
  ASSERT_EQ(r->coords.size(), 1u);
  EXPECT_EQ(get_id(r->coords[0]), "r_cx");
  EXPECT_FALSE(r->offsets.has_value());
}

// ── InstrCpAsyncBulkCommitGroup ────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrCpAsyncBulkCommitGroup) {
  using namespace ptx_frontend;

  InstrCpAsyncBulkCommitGroup instr{};
  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = visit_instr<ParsedOp, std::string>(instr, v);
  EXPECT_TRUE(r.has_value());
}

TEST(PtxVisitDispatch, MapInstrCpAsyncBulkCommitGroup) {
  using namespace ptx_frontend;

  InstrCpAsyncBulkCommitGroup instr{};
  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
}

// ── InstrCpAsyncBulkWaitGroup ──────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrCpAsyncBulkWaitGroup) {
  using namespace ptx_frontend;

  InstrCpAsyncBulkWaitGroup<ParsedOp> instr{
      .n = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("n")),
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        EXPECT_EQ(ts->space, StateSpace::Reg);
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], "n");
}

TEST(PtxVisitDispatch, MapInstrCpAsyncBulkWaitGroup) {
  using namespace ptx_frontend;

  InstrCpAsyncBulkWaitGroup<ParsedOp> instr{
      .n = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("n")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "n")
          return Ident{"n2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->n), "n2");
}

// ── InstrTensormapReplace ──────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTensormapReplace) {
  using namespace ptx_frontend;

  InstrTensormapReplace<ParsedOp> instr{
      .data = TensormapReplaceDetails{TensormapReplaceField::GlobalAddress},
      .tmap_ptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tmap")),
      .new_val = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("val")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "tmap");
  EXPECT_EQ(visited[0].second, StateSpace::Global);
  EXPECT_EQ(visited[1].first, "val");
  EXPECT_EQ(visited[1].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrTensormapReplace) {
  using namespace ptx_frontend;

  InstrTensormapReplace<ParsedOp> instr{
      .data = TensormapReplaceDetails{TensormapReplaceField::SwizzlingMode},
      .tmap_ptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tmap")),
      .new_val = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("val")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "tmap")
          return Ident{"tmap2"};
        if (id == "val")
          return Ident{"val2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->tmap_ptr), "tmap2");
  EXPECT_EQ(get_id(r->new_val), "val2");
  EXPECT_EQ(r->data.field, TensormapReplaceField::SwizzlingMode);
}

// ── InstrTensormapCpFenceProxy ─────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTensormapCpFenceProxy) {
  using namespace ptx_frontend;

  InstrTensormapCpFenceProxy<ParsedOp> instr{
      .data =
          TensormapCpFenceProxyDetails{AtomSemantics::Acquire, MemScope::Sys},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0].first, "addr");
  EXPECT_EQ(visited[0].second, StateSpace::Shared);
}

TEST(PtxVisitDispatch, MapInstrTensormapCpFenceProxy) {
  using namespace ptx_frontend;

  InstrTensormapCpFenceProxy<ParsedOp> instr{
      .data =
          TensormapCpFenceProxyDetails{AtomSemantics::Acquire, MemScope::Gpu},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
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
  EXPECT_EQ(get_id(r->addr), "addr2");
  EXPECT_EQ(r->data.scope, MemScope::Gpu);
}

// ── InstrPrefetchu ─────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrPrefetchu) {
  using namespace ptx_frontend;

  InstrPrefetchu<ParsedOp> instr{
      .level = CacheLevel::L1,
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0].first, "addr");
  EXPECT_EQ(visited[0].second, StateSpace::Generic);
}

TEST(PtxVisitDispatch, MapInstrPrefetchu) {
  using namespace ptx_frontend;

  InstrPrefetchu<ParsedOp> instr{
      .level = CacheLevel::L2,
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
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
  EXPECT_EQ(get_id(r->addr), "addr2");
  EXPECT_EQ(r->level, CacheLevel::L2);
}

// ── InstrClusterLaunchControl ──────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrClusterLaunchControl_TryCancel) {
  using namespace ptx_frontend;

  // TryCancel — no dst
  InstrClusterLaunchControl<ParsedOp> instr{
      .data = ClusterLaunchControlDetails{CLCOp::TryCancel},
      .dst = std::nullopt,
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], "addr");
}

TEST(PtxVisitDispatch, VisitInstrClusterLaunchControl_QueryCancel) {
  using namespace ptx_frontend;

  // QueryCancel — has pred dst
  InstrClusterLaunchControl<ParsedOp> instr{
      .data = ClusterLaunchControlDetails{CLCOp::QueryCancel},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("pred")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };

  std::vector<std::pair<Ident, bool>> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool is_dst,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back({std::get<Ident>(roi.value), is_dst});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0].first, "pred");
  EXPECT_TRUE(ids[0].second);  // is_dst
  EXPECT_EQ(ids[1].first, "addr");
  EXPECT_FALSE(ids[1].second);
}

TEST(PtxVisitDispatch, MapInstrClusterLaunchControl_NoDst) {
  using namespace ptx_frontend;

  InstrClusterLaunchControl<ParsedOp> instr{
      .data = ClusterLaunchControlDetails{CLCOp::TryCancel},
      .dst = std::nullopt,
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
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
  EXPECT_FALSE(r->dst.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->addr), "addr2");
}

TEST(PtxVisitDispatch, MapInstrClusterLaunchControl_WithDst) {
  using namespace ptx_frontend;

  InstrClusterLaunchControl<ParsedOp> instr{
      .data = ClusterLaunchControlDetails{CLCOp::QueryCancel},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("pred")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "pred")
          return Ident{"pred2"};
        if (id == "addr")
          return Ident{"addr2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  ASSERT_TRUE(r->dst.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(*r->dst), "pred2");
  EXPECT_EQ(get_id(r->addr), "addr2");
}

// ── InstrRed ───────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrRed) {
  using namespace ptx_frontend;

  InstrRed<ParsedOp> instr{
      .data =
          RedDetails{AtomSemantics::Relaxed, MemScope::Gpu, StateSpace::Global,
                     AtomicOp::Add, make_scalar(ScalarType::U32)},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "addr");
  EXPECT_EQ(visited[0].second, StateSpace::Global);
  EXPECT_EQ(visited[1].first, "src");
  EXPECT_EQ(visited[1].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrRed) {
  using namespace ptx_frontend;

  InstrRed<ParsedOp> instr{
      .data =
          RedDetails{AtomSemantics::Relaxed, MemScope::Sys, StateSpace::Global,
                     AtomicOp::Add, make_scalar(ScalarType::U32)},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "addr")
          return Ident{"addr2"};
        if (id == "src")
          return Ident{"src2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->addr), "addr2");
  EXPECT_EQ(get_id(r->src), "src2");
}

// ── InstrMbarrierInit ──────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMbarrierInit) {
  using namespace ptx_frontend;

  InstrMbarrierInit<ParsedOp> instr{
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "bar");
  EXPECT_EQ(visited[0].second, StateSpace::Shared);
  EXPECT_EQ(visited[1].first, "cnt");
  EXPECT_EQ(visited[1].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrMbarrierInit) {
  using namespace ptx_frontend;

  InstrMbarrierInit<ParsedOp> instr{
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "bar")
          return Ident{"bar2"};
        if (id == "cnt")
          return Ident{"cnt2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->addr), "bar2");
  EXPECT_EQ(get_id(r->count), "cnt2");
}

// ── InstrMbarrierInval ─────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMbarrierInval) {
  using namespace ptx_frontend;

  InstrMbarrierInval<ParsedOp> instr{
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0].first, "bar");
  EXPECT_EQ(visited[0].second, StateSpace::Shared);
}

TEST(PtxVisitDispatch, MapInstrMbarrierInval) {
  using namespace ptx_frontend;

  InstrMbarrierInval<ParsedOp> instr{
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "bar")
          return Ident{"bar2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->addr), "bar2");
}

// ── InstrMbarrierArrive ────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMbarrierArrive_Simple) {
  using namespace ptx_frontend;

  // arrive_drop: no token, no count
  InstrMbarrierArrive<ParsedOp> instr{
      .data = MbarrierArriveDetails{true, false, std::nullopt},
      .token = std::nullopt,
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .count = std::nullopt,
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], "bar");
}

TEST(PtxVisitDispatch, VisitInstrMbarrierArrive_WithTokenAndCount) {
  using namespace ptx_frontend;

  InstrMbarrierArrive<ParsedOp> instr{
      .data = MbarrierArriveDetails{false, false, std::nullopt},
      .token = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tok")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  // token, addr, count
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "tok");
  EXPECT_EQ(ids[1], "bar");
  EXPECT_EQ(ids[2], "cnt");
}

TEST(PtxVisitDispatch, MapInstrMbarrierArrive) {
  using namespace ptx_frontend;

  InstrMbarrierArrive<ParsedOp> instr{
      .data = MbarrierArriveDetails{false, false, std::nullopt},
      .token = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tok")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .count = std::nullopt,
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "tok")
          return Ident{"tok2"};
        if (id == "bar")
          return Ident{"bar2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  ASSERT_TRUE(r->token.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(*r->token), "tok2");
  EXPECT_EQ(get_id(r->addr), "bar2");
  EXPECT_FALSE(r->count.has_value());
}

// ── InstrMbarrierTestWait ──────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMbarrierTestWait) {
  using namespace ptx_frontend;

  InstrMbarrierTestWait<ParsedOp> instr{
      .data = MbarrierTestWaitDetails{false},
      .done = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("done")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .token_or_parity =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tok")),
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
  EXPECT_EQ(visited[0].first, "done");
  EXPECT_TRUE(visited[0].second);  // is_dst
  EXPECT_EQ(visited[1].first, "bar");
  EXPECT_FALSE(visited[1].second);
  EXPECT_EQ(visited[2].first, "tok");
  EXPECT_FALSE(visited[2].second);
}

TEST(PtxVisitDispatch, MapInstrMbarrierTestWait) {
  using namespace ptx_frontend;

  InstrMbarrierTestWait<ParsedOp> instr{
      .data = MbarrierTestWaitDetails{true},
      .done = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("done")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .token_or_parity =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("par")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "done")
          return Ident{"done2"};
        if (id == "bar")
          return Ident{"bar2"};
        if (id == "par")
          return Ident{"par2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->done), "done2");
  EXPECT_EQ(get_id(r->addr), "bar2");
  EXPECT_EQ(get_id(r->token_or_parity), "par2");
  EXPECT_TRUE(r->data.parity);
}

// ── InstrMbarrierTryWait ───────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMbarrierTryWait_NoTimeout) {
  using namespace ptx_frontend;

  InstrMbarrierTryWait<ParsedOp> instr{
      .data = MbarrierTryWaitDetails{false, false},
      .done = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("done")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .token_or_parity =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tok")),
      .timeout_ns = std::nullopt,
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "done");
  EXPECT_EQ(ids[1], "bar");
  EXPECT_EQ(ids[2], "tok");
}

TEST(PtxVisitDispatch, VisitInstrMbarrierTryWait_WithTimeout) {
  using namespace ptx_frontend;

  InstrMbarrierTryWait<ParsedOp> instr{
      .data = MbarrierTryWaitDetails{false, true},
      .done = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("done")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .token_or_parity =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tok")),
      .timeout_ns = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("ns")),
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[3], "ns");
}

TEST(PtxVisitDispatch, MapInstrMbarrierTryWait) {
  using namespace ptx_frontend;

  InstrMbarrierTryWait<ParsedOp> instr{
      .data = MbarrierTryWaitDetails{false, true},
      .done = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("done")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .token_or_parity =
          ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tok")),
      .timeout_ns = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("ns")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "done")
          return Ident{"done2"};
        if (id == "bar")
          return Ident{"bar2"};
        if (id == "tok")
          return Ident{"tok2"};
        if (id == "ns")
          return Ident{"ns2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->done), "done2");
  EXPECT_EQ(get_id(r->addr), "bar2");
  EXPECT_EQ(get_id(r->token_or_parity), "tok2");
  ASSERT_TRUE(r->timeout_ns.has_value());
  EXPECT_EQ(get_id(*r->timeout_ns), "ns2");
}

// ── InstrMbarrierExpectTx ──────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMbarrierExpectTx) {
  using namespace ptx_frontend;

  InstrMbarrierExpectTx<ParsedOp> instr{
      .data = MbarrierExpectTxDetails{MemScope::Cta},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .tx_count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "bar");
  EXPECT_EQ(visited[0].second, StateSpace::Shared);
  EXPECT_EQ(visited[1].first, "cnt");
  EXPECT_EQ(visited[1].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrMbarrierExpectTx) {
  using namespace ptx_frontend;

  InstrMbarrierExpectTx<ParsedOp> instr{
      .data = MbarrierExpectTxDetails{MemScope::Cluster},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .tx_count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "bar")
          return Ident{"bar2"};
        if (id == "cnt")
          return Ident{"cnt2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->addr), "bar2");
  EXPECT_EQ(get_id(r->tx_count), "cnt2");
  EXPECT_EQ(r->data.scope, MemScope::Cluster);
}

// ── InstrMbarrierCompleteTx ────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrMbarrierCompleteTx) {
  using namespace ptx_frontend;

  InstrMbarrierCompleteTx<ParsedOp> instr{
      .data = MbarrierCompleteTxDetails{MemScope::Cta},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .tx_count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].second, StateSpace::Shared);
  EXPECT_EQ(visited[1].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrMbarrierCompleteTx) {
  using namespace ptx_frontend;

  InstrMbarrierCompleteTx<ParsedOp> instr{
      .data = MbarrierCompleteTxDetails{MemScope::Cta},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("bar")),
      .tx_count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "bar")
          return Ident{"bar2"};
        if (id == "cnt")
          return Ident{"cnt2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->addr), "bar2");
  EXPECT_EQ(get_id(r->tx_count), "cnt2");
}

// ── InstrStMatrix ──────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrStMatrix) {
  using namespace ptx_frontend;

  InstrStMatrix<ParsedOp> instr{
      .data = StMatrixDetails{LdStMatrixShape::M16N8, MatrixNumber::X2, false},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
      .srcs = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s0")),
               ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s1"))},
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  // addr (Shared), s0 (Reg), s1 (Reg)
  ASSERT_EQ(visited.size(), 3u);
  EXPECT_EQ(visited[0].first, "addr");
  EXPECT_EQ(visited[0].second, StateSpace::Shared);
  EXPECT_EQ(visited[1].first, "s0");
  EXPECT_EQ(visited[1].second, StateSpace::Reg);
  EXPECT_EQ(visited[2].first, "s1");
  EXPECT_EQ(visited[2].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrStMatrix) {
  using namespace ptx_frontend;

  InstrStMatrix<ParsedOp> instr{
      .data = StMatrixDetails{LdStMatrixShape::M8N8, MatrixNumber::X1, false},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
      .srcs = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s0"))},
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "addr")
          return Ident{"addr2"};
        if (id == "s0")
          return Ident{"s0_2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->addr), "addr2");
  ASSERT_EQ(r->srcs.size(), 1u);
  EXPECT_EQ(get_id(r->srcs[0]), "s0_2");
}

// ── InstrWgmmaMmaAsync ─────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrWgmmaMmaAsync) {
  using namespace ptx_frontend;

  InstrWgmmaMmaAsync<ParsedOp> instr{
      .data =
          WgmmaMmaAsyncDetails{
              .shape = WgmmaShape::M64N8K16,
              .blayout = MatrixLayout::ColMajor,
              .dtype = ScalarType::F32,
              .atype = ScalarType::F16,
              .btype = ScalarType::F16,
              .a_source = WgmmaInput::Register,
              .scale_d = true,
              .flush_to_zero = std::nullopt,
              .saturate = false,
          },
      .d_srcs = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d0")),
                 ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d1"))},
      .a = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .b = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  // d0, d1, a, b
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], "d0");
  EXPECT_EQ(ids[1], "d1");
  EXPECT_EQ(ids[2], "a");
  EXPECT_EQ(ids[3], "b");
}

TEST(PtxVisitDispatch, MapInstrWgmmaMmaAsync) {
  using namespace ptx_frontend;

  InstrWgmmaMmaAsync<ParsedOp> instr{
      .data =
          WgmmaMmaAsyncDetails{
              .shape = WgmmaShape::M64N8K16,
              .blayout = MatrixLayout::ColMajor,
              .dtype = ScalarType::F32,
              .atype = ScalarType::F16,
              .btype = ScalarType::F16,
              .a_source = WgmmaInput::Register,
              .scale_d = false,
              .flush_to_zero = std::nullopt,
              .saturate = false,
          },
      .d_srcs = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d0"))},
      .a = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("a")),
      .b = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("b")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d0")
          return Ident{"d0_2"};
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
  ASSERT_EQ(r->d_srcs.size(), 1u);
  EXPECT_EQ(get_id(r->d_srcs[0]), "d0_2");
  EXPECT_EQ(get_id(r->a), "a2");
  EXPECT_EQ(get_id(r->b), "b2");
}

// ── InstrTcgen05Alloc ──────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05Alloc) {
  using namespace ptx_frontend;

  InstrTcgen05Alloc<ParsedOp> instr{
      .data = Tcgen05AllocDetails{Tcgen05CtaGroup::Cta1},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .ncols = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("ncols")),
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
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "tptr");
  EXPECT_TRUE(visited[0].second);  // is_dst
  EXPECT_EQ(visited[1].first, "ncols");
  EXPECT_FALSE(visited[1].second);
}

TEST(PtxVisitDispatch, MapInstrTcgen05Alloc) {
  using namespace ptx_frontend;

  InstrTcgen05Alloc<ParsedOp> instr{
      .data = Tcgen05AllocDetails{Tcgen05CtaGroup::Cta2},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .ncols = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("ncols")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "tptr")
          return Ident{"tptr2"};
        if (id == "ncols")
          return Ident{"ncols2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->tptr), "tptr2");
  EXPECT_EQ(get_id(r->ncols), "ncols2");
  EXPECT_EQ(r->data.cta_group, Tcgen05CtaGroup::Cta2);
}

// ── InstrTcgen05Dealloc ────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05Dealloc) {
  using namespace ptx_frontend;

  InstrTcgen05Dealloc<ParsedOp> instr{
      .data = Tcgen05DeallocDetails{},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .ncols = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("ncols")),
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "tptr");
  EXPECT_EQ(ids[1], "ncols");
}

TEST(PtxVisitDispatch, MapInstrTcgen05Dealloc) {
  using namespace ptx_frontend;

  InstrTcgen05Dealloc<ParsedOp> instr{
      .data = Tcgen05DeallocDetails{},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .ncols = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("ncols")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "tptr")
          return Ident{"tptr2"};
        if (id == "ncols")
          return Ident{"ncols2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->tptr), "tptr2");
  EXPECT_EQ(get_id(r->ncols), "ncols2");
}

// ── InstrTcgen05Ld ─────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05Ld_NoOffset) {
  using namespace ptx_frontend;

  InstrTcgen05Ld<ParsedOp> instr{
      .data = Tcgen05LdStDetails{Tcgen05Shape::Shape16x64b, false, false, false,
                                 ScalarType::B32},
      .dsts = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d0")),
               ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d1"))},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .offset = std::nullopt,
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  // d0 (Reg), d1 (Reg), tptr (Tmem)
  ASSERT_EQ(visited.size(), 3u);
  EXPECT_EQ(visited[0].second, StateSpace::Reg);
  EXPECT_EQ(visited[2].first, "tptr");
  EXPECT_EQ(visited[2].second, StateSpace::Tmem);
}

TEST(PtxVisitDispatch, VisitInstrTcgen05Ld_WithOffset) {
  using namespace ptx_frontend;

  InstrTcgen05Ld<ParsedOp> instr{
      .data = Tcgen05LdStDetails{Tcgen05Shape::Shape32x32b, false, false, true,
                                 ScalarType::B32},
      .dsts = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d0"))},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .offset = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("off")),
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  // d0, tptr, off
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[2], "off");
}

TEST(PtxVisitDispatch, MapInstrTcgen05Ld) {
  using namespace ptx_frontend;

  InstrTcgen05Ld<ParsedOp> instr{
      .data = Tcgen05LdStDetails{Tcgen05Shape::Shape16x64b, false, false, false,
                                 ScalarType::B16},
      .dsts = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("d0"))},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .offset = std::nullopt,
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "d0")
          return Ident{"d0_2"};
        if (id == "tptr")
          return Ident{"tptr2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  ASSERT_EQ(r->dsts.size(), 1u);
  EXPECT_EQ(get_id(r->dsts[0]), "d0_2");
  EXPECT_EQ(get_id(r->tptr), "tptr2");
  EXPECT_FALSE(r->offset.has_value());
}

// ── InstrTcgen05St ─────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05St) {
  using namespace ptx_frontend;

  InstrTcgen05St<ParsedOp> instr{
      .data = Tcgen05LdStDetails{Tcgen05Shape::Shape16x64b, false, false, false,
                                 ScalarType::B32},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .offset = std::nullopt,
      .srcs = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s0")),
               ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s1"))},
  };

  std::vector<Ident> ids;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace>, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        ids.push_back(std::get<Ident>(roi.value));
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  // tptr, s0, s1
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], "tptr");
  EXPECT_EQ(ids[1], "s0");
  EXPECT_EQ(ids[2], "s1");
}

TEST(PtxVisitDispatch, MapInstrTcgen05St) {
  using namespace ptx_frontend;

  InstrTcgen05St<ParsedOp> instr{
      .data = Tcgen05LdStDetails{Tcgen05Shape::Shape16x64b, false, false, false,
                                 ScalarType::B32},
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .offset = std::nullopt,
      .srcs = {ParsedOp::from_value(RegOrImmediate<Ident>::Reg("s0"))},
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "tptr")
          return Ident{"tptr2"};
        if (id == "s0")
          return Ident{"s0_2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->tptr), "tptr2");
  EXPECT_FALSE(r->offset.has_value());
  ASSERT_EQ(r->srcs.size(), 1u);
  EXPECT_EQ(get_id(r->srcs[0]), "s0_2");
}

// ── InstrTcgen05Wait ───────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05Wait) {
  using namespace ptx_frontend;

  InstrTcgen05Wait<ParsedOp> instr{.data =
                                       Tcgen05WaitDetails{Tcgen05WaitKind::Ld}};

  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
}

TEST(PtxVisitDispatch, MapInstrTcgen05Wait) {
  using namespace ptx_frontend;

  InstrTcgen05Wait<ParsedOp> instr{.data =
                                       Tcgen05WaitDetails{Tcgen05WaitKind::St}};

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->data.kind, Tcgen05WaitKind::St);
}

// ── InstrTcgen05Shift ──────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05Shift) {
  using namespace ptx_frontend;

  InstrTcgen05Shift<ParsedOp> instr{
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0].first, "tptr");
  EXPECT_EQ(visited[0].second, StateSpace::Tmem);
}

TEST(PtxVisitDispatch, MapInstrTcgen05Shift) {
  using namespace ptx_frontend;

  InstrTcgen05Shift<ParsedOp> instr{
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "tptr")
          return Ident{"tptr2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->tptr), "tptr2");
}

// ── InstrTcgen05CommitArrival ──────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05CommitArrival) {
  using namespace ptx_frontend;

  InstrTcgen05CommitArrival<ParsedOp> instr{
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .mbar = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mbar")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "tptr");
  EXPECT_EQ(visited[0].second, StateSpace::Tmem);
  EXPECT_EQ(visited[1].first, "mbar");
  EXPECT_EQ(visited[1].second, StateSpace::Shared);
}

TEST(PtxVisitDispatch, MapInstrTcgen05CommitArrival) {
  using namespace ptx_frontend;

  InstrTcgen05CommitArrival<ParsedOp> instr{
      .tptr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("tptr")),
      .mbar = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mbar")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "tptr")
          return Ident{"tptr2"};
        if (id == "mbar")
          return Ident{"mbar2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->tptr), "tptr2");
  EXPECT_EQ(get_id(r->mbar), "mbar2");
}

// ── InstrTcgen05Relinquish ─────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05Relinquish) {
  using namespace ptx_frontend;

  InstrTcgen05Relinquish<ParsedOp> instr{};

  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
}

TEST(PtxVisitDispatch, MapInstrTcgen05Relinquish) {
  using namespace ptx_frontend;

  InstrTcgen05Relinquish<ParsedOp> instr{};

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
}

// ── InstrTcgen05Fence ──────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05Fence) {
  using namespace ptx_frontend;

  InstrTcgen05Fence<ParsedOp> instr{
      .data = Tcgen05FenceDetails{Tcgen05FenceKind::BeforeThreadSync},
  };

  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
}

TEST(PtxVisitDispatch, MapInstrTcgen05Fence) {
  using namespace ptx_frontend;

  InstrTcgen05Fence<ParsedOp> instr{
      .data = Tcgen05FenceDetails{Tcgen05FenceKind::AfterThreadSync},
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->data.kind, Tcgen05FenceKind::AfterThreadSync);
}

// ── InstrTcgen05Cp ─────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05Cp) {
  using namespace ptx_frontend;

  InstrTcgen05Cp<ParsedOp> instr{
      .data =
          Tcgen05CpDetails{Tcgen05CpShape::Shape128x, Tcgen05CpDir::SmemToTmem},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
      .count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 3u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_EQ(visited[0].second, StateSpace::Tmem);
  EXPECT_EQ(visited[1].first, "src");
  EXPECT_EQ(visited[1].second, StateSpace::Shared);
  EXPECT_EQ(visited[2].first, "cnt");
  EXPECT_EQ(visited[2].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrTcgen05Cp) {
  using namespace ptx_frontend;

  InstrTcgen05Cp<ParsedOp> instr{
      .data =
          Tcgen05CpDetails{Tcgen05CpShape::Shape128x, Tcgen05CpDir::SmemToTmem},
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .src = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("src")),
      .count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "dst")
          return Ident{"dst2"};
        if (id == "src")
          return Ident{"src2"};
        if (id == "cnt")
          return Ident{"cnt2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "dst2");
  EXPECT_EQ(get_id(r->src), "src2");
  EXPECT_EQ(get_id(r->count), "cnt2");
}

// ── InstrTcgen05MbarrierExpectTx ───────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrTcgen05MbarrierExpectTx) {
  using namespace ptx_frontend;

  InstrTcgen05MbarrierExpectTx<ParsedOp> instr{
      .mbar = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mbar")),
      .tx_count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "mbar");
  EXPECT_EQ(visited[0].second, StateSpace::Shared);
  EXPECT_EQ(visited[1].first, "cnt");
  EXPECT_EQ(visited[1].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrTcgen05MbarrierExpectTx) {
  using namespace ptx_frontend;

  InstrTcgen05MbarrierExpectTx<ParsedOp> instr{
      .mbar = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mbar")),
      .tx_count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "mbar")
          return Ident{"mbar2"};
        if (id == "cnt")
          return Ident{"cnt2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->mbar), "mbar2");
  EXPECT_EQ(get_id(r->tx_count), "cnt2");
}

// ── InstrClusterBarrier ────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrClusterBarrier) {
  using namespace ptx_frontend;

  InstrClusterBarrier<ParsedOp> instr{
      .data = ClusterBarrierDetails{ClusterBarrierOp::Arrive, true},
  };

  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
}

TEST(PtxVisitDispatch, MapInstrClusterBarrier) {
  using namespace ptx_frontend;

  InstrClusterBarrier<ParsedOp> instr{
      .data = ClusterBarrierDetails{ClusterBarrierOp::Wait, false},
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->data.op, ClusterBarrierOp::Wait);
  EXPECT_FALSE(r->data.aligned);
}

// ── InstrSetMaxNReg ────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrSetMaxNReg) {
  using namespace ptx_frontend;

  InstrSetMaxNReg<ParsedOp> instr{
      .data = SetMaxNRegDetails{SetMaxNRegOp::Inc},
      .count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0].first, "cnt");
  EXPECT_EQ(visited[0].second, StateSpace::Reg);
}

TEST(PtxVisitDispatch, MapInstrSetMaxNReg) {
  using namespace ptx_frontend;

  InstrSetMaxNReg<ParsedOp> instr{
      .data = SetMaxNRegDetails{SetMaxNRegOp::Dec},
      .count = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("cnt")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "cnt")
          return Ident{"cnt2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->count), "cnt2");
  EXPECT_EQ(r->data.op, SetMaxNRegOp::Dec);
}

// ── InstrGetCtaRank ────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrGetCtaRank) {
  using namespace ptx_frontend;

  InstrGetCtaRank<ParsedOp> instr{
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
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
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "dst");
  EXPECT_TRUE(visited[0].second);
  EXPECT_EQ(visited[1].first, "addr");
  EXPECT_FALSE(visited[1].second);
}

TEST(PtxVisitDispatch, MapInstrGetCtaRank) {
  using namespace ptx_frontend;

  InstrGetCtaRank<ParsedOp> instr{
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("dst")),
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "dst")
          return Ident{"dst2"};
        if (id == "addr")
          return Ident{"addr2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "dst2");
  EXPECT_EQ(get_id(r->addr), "addr2");
}

// ── InstrElectSync ─────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrElectSync) {
  using namespace ptx_frontend;

  InstrElectSync<ParsedOp> instr{
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("pred")),
      .membermask = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mask")),
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
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].first, "pred");
  EXPECT_TRUE(visited[0].second);
  EXPECT_EQ(visited[1].first, "mask");
  EXPECT_FALSE(visited[1].second);
}

TEST(PtxVisitDispatch, MapInstrElectSync) {
  using namespace ptx_frontend;

  InstrElectSync<ParsedOp> instr{
      .dst = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("pred")),
      .membermask = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("mask")),
  };

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident id, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        if (id == "pred")
          return Ident{"pred2"};
        if (id == "mask")
          return Ident{"mask2"};
        return tl::unexpected<std::string>("unknown");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
  auto get_id = [](const ParsedOp& op) {
    return std::get<Ident>(std::get<RegOrImmediate<Ident>>(op.value).value);
  };
  EXPECT_EQ(get_id(r->dst), "pred2");
  EXPECT_EQ(get_id(r->membermask), "mask2");
}

// ── InstrDiscard ───────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrDiscard) {
  using namespace ptx_frontend;

  InstrDiscard<ParsedOp> instr{
      .data = DiscardDetails{StateSpace::Global},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
  };

  std::vector<std::pair<Ident, StateSpace>> visited;
  auto v = make_visitor<ParsedOp, std::string>(
      [&](const ParsedOp& op, std::optional<VisitTypeSpace> ts, bool,
          bool) -> expected<void, std::string> {
        auto& roi = std::get<RegOrImmediate<Ident>>(op.value);
        visited.push_back({std::get<Ident>(roi.value), ts->space});
        return {};
      });

  auto r = visit_instr(instr, v);
  EXPECT_TRUE(r.has_value());
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0].first, "addr");
  EXPECT_EQ(visited[0].second, StateSpace::Global);
}

TEST(PtxVisitDispatch, MapInstrDiscard) {
  using namespace ptx_frontend;

  InstrDiscard<ParsedOp> instr{
      .data = DiscardDetails{StateSpace::Local},
      .addr = ParsedOp::from_value(RegOrImmediate<Ident>::Reg("addr")),
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
  EXPECT_EQ(get_id(r->addr), "addr2");
  EXPECT_EQ(r->data.space, StateSpace::Local);
}

// ── InstrBrkpt ─────────────────────────────────────────────────────────────
TEST(PtxVisitDispatch, VisitInstrBrkpt) {
  using namespace ptx_frontend;

  InstrBrkpt instr{};

  auto v = make_visitor<ParsedOp, std::string>(
      [](const ParsedOp&, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<void, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = visit_instr<ParsedOp, std::string>(instr, v);
  EXPECT_TRUE(r.has_value());
}

TEST(PtxVisitDispatch, MapInstrBrkpt) {
  using namespace ptx_frontend;

  InstrBrkpt instr{};

  auto vm = make_visitor_map<ParsedOp, ParsedOp, std::string>(
      [](Ident, std::optional<VisitTypeSpace>, bool,
         bool) -> expected<Ident, std::string> {
        return tl::unexpected<std::string>("should not be called");
      });

  auto r = map_instr(instr, vm);
  ASSERT_TRUE(r.has_value());
}
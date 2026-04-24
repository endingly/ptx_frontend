#include <gtest/gtest.h>
#include <deque>
#include <string>
#include <string_view>
#include <vector>
#include "ptx_lexer.hpp"

using namespace ptx_frontend;

// Intern into a stable pool so string_views outlive the PtxLexer's flex buffer.
std::string_view ptx_intern(const char* s, std::size_t len, void*) {
  static std::deque<std::string> pool;
  pool.emplace_back(s, len);
  return pool.back();
}

// ── helpers ──────────────────────────────────────────────────────────────────

// Lex the full input and return a flat list of (kind, text) pairs, excluding Eof.
static std::vector<std::pair<TokenKind, std::string_view>> lex_all(
    std::string_view src) {
  PtxLexer lexer(src);
  std::vector<std::pair<TokenKind, std::string_view>> out;
  while (true) {
    auto tok = lexer.next();
    if (tok.kind == TokenKind::Eof)
      break;
    out.emplace_back(tok.kind, tok.text);
  }
  return out;
}

// Lex a single token (first non-whitespace) and return it.
static PtxLexer::Token lex1(std::string_view src) {
  PtxLexer lexer(src);
  return lexer.next();
}

// ── existing test ─────────────────────────────────────────────────────────────

TEST(PtxLexer, SimpleStatementMultiline) {
  std::string_view src = R"(
.version 8.0 .target sm_80

mov.b32 %r0, %r1;
  )";
  PtxLexer lexer(src);

  struct Expected {
    TokenKind kind;
    std::string_view text;
    int line;
  };
  Expected expected[] = {
      {TokenKind::DotVersion, ".version", 2},
      {TokenKind::F64, "8.0", 2},
      {TokenKind::DotTarget, ".target", 2},
      {TokenKind::Ident, "sm_80", 2},
      {TokenKind::Mov, "mov", 4},
      {TokenKind::DotB32, ".b32", 4},
      {TokenKind::Ident, "%r0", 4},
      {TokenKind::Comma, ",", 4},
      {TokenKind::Ident, "%r1", 4},
      {TokenKind::Semicolon, ";", 4},
      {TokenKind::Eof, "", 5},
  };

  for (auto& e : expected) {
    auto tok = lexer.next();
    EXPECT_EQ(tok.kind, e.kind) << "text=" << tok.text;
    EXPECT_EQ(tok.text, e.text);
  }
}

// ── PTX 9.2: new opcode tokens ────────────────────────────────────────────────

TEST(PtxLexer92Opcodes, MatrixOpcodes) {
  // stmatrix, wgmma, movmatrix
  auto toks = lex_all("stmatrix wgmma movmatrix");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].first, TokenKind::Stmatrix);
  EXPECT_EQ(toks[0].second, "stmatrix");
  EXPECT_EQ(toks[1].first, TokenKind::Wgmma);
  EXPECT_EQ(toks[1].second, "wgmma");
  EXPECT_EQ(toks[2].first, TokenKind::Movmatrix);
  EXPECT_EQ(toks[2].second, "movmatrix");
}

TEST(PtxLexer92Opcodes, Tcgen05Opcode) {
  auto tok = lex1("tcgen05");
  EXPECT_EQ(tok.kind, TokenKind::Tcgen05);
  EXPECT_EQ(tok.text, "tcgen05");
}

TEST(PtxLexer92Opcodes, FenceRedMbarrierOpcodes) {
  auto toks = lex_all("fence red mbarrier");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].first, TokenKind::Fence);
  EXPECT_EQ(toks[1].first, TokenKind::Red);
  EXPECT_EQ(toks[2].first, TokenKind::Mbarrier);
}

TEST(PtxLexer92Opcodes, MiscNewOpcodes) {
  auto toks = lex_all(
      "setmaxnreg getctarank elect discard brkpt tensormap prefetchu "
      "clusterlaunchcontrol");
  ASSERT_EQ(toks.size(), 8u);
  EXPECT_EQ(toks[0].first, TokenKind::Setmaxnreg);
  EXPECT_EQ(toks[1].first, TokenKind::Getctarank);
  EXPECT_EQ(toks[2].first, TokenKind::Elect);
  EXPECT_EQ(toks[3].first, TokenKind::Discard);
  EXPECT_EQ(toks[4].first, TokenKind::Brkpt);
  EXPECT_EQ(toks[5].first, TokenKind::Tensormap);
  EXPECT_EQ(toks[6].first, TokenKind::Prefetchu);
  EXPECT_EQ(toks[7].first, TokenKind::Clusterlaunchcontrol);
}

// ── PTX 9.2: mbarrier dot-modifiers ──────────────────────────────────────────

TEST(PtxLexer92Mbarrier, SubOpTokens) {
  auto toks = lex_all(
      ".arrive .arrive_drop .drop .init .inval "
      ".expect_tx .complete_tx .test_wait .try_wait "
      ".parity .no_complete");
  ASSERT_EQ(toks.size(), 11u);
  EXPECT_EQ(toks[0].first, TokenKind::DotArrive);
  EXPECT_EQ(toks[1].first, TokenKind::DotArriveDrop);
  EXPECT_EQ(toks[2].first, TokenKind::DotDrop);
  EXPECT_EQ(toks[3].first, TokenKind::DotInit);
  EXPECT_EQ(toks[4].first, TokenKind::DotInval);
  EXPECT_EQ(toks[5].first, TokenKind::DotExpectTx);
  EXPECT_EQ(toks[6].first, TokenKind::DotCompleteTx);
  EXPECT_EQ(toks[7].first, TokenKind::DotTestWait);
  EXPECT_EQ(toks[8].first, TokenKind::DotTryWait);
  EXPECT_EQ(toks[9].first, TokenKind::DotParity);
  EXPECT_EQ(toks[10].first, TokenKind::DotNoComplete);
}

// arrive_drop must not be matched as arrive + drop separately
TEST(PtxLexer92Mbarrier, ArriveDrop_LongestMatch) {
  auto tok = lex1(".arrive_drop");
  EXPECT_EQ(tok.kind, TokenKind::DotArriveDrop);
  EXPECT_EQ(tok.text, ".arrive_drop");
}

// ── PTX 9.2: fence dot-modifiers ─────────────────────────────────────────────

TEST(PtxLexer92Fence, FenceModifiers) {
  auto toks = lex_all(".proxy .sc .alias");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].first, TokenKind::DotProxy);
  EXPECT_EQ(toks[0].second, ".proxy");
  EXPECT_EQ(toks[1].first, TokenKind::DotSc);
  EXPECT_EQ(toks[1].second, ".sc");
  EXPECT_EQ(toks[2].first, TokenKind::DotAlias);
  EXPECT_EQ(toks[2].second, ".alias");
}

// ── PTX 9.2: tcgen05 sub-op modifiers ────────────────────────────────────────

TEST(PtxLexer92Tcgen05, SubOpModifiers) {
  auto toks =
      lex_all(".alloc .dealloc .commit_arrival .relinquish .pack .offset");
  ASSERT_EQ(toks.size(), 6u);
  EXPECT_EQ(toks[0].first, TokenKind::DotAlloc);
  EXPECT_EQ(toks[1].first, TokenKind::DotDealloc);
  EXPECT_EQ(toks[2].first, TokenKind::DotCommitArrival);
  EXPECT_EQ(toks[3].first, TokenKind::DotRelinquish);
  EXPECT_EQ(toks[4].first, TokenKind::DotPack);
  EXPECT_EQ(toks[5].first, TokenKind::DotOffset);
}

TEST(PtxLexer92Tcgen05, CtaGroupModifiers) {
  auto toks = lex_all(".cta1 .cta2");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::DotCta1);
  EXPECT_EQ(toks[0].second, ".cta1");
  EXPECT_EQ(toks[1].first, TokenKind::DotCta2);
  EXPECT_EQ(toks[1].second, ".cta2");
}

// ── PTX 9.2: tcgen05 shapes (numeric dot-tokens) ─────────────────────────────

TEST(PtxLexer92Tcgen05, ShapeTokens) {
  auto toks = lex_all(
      ".16x64b .16x128b .16x256b .32x32b .32x64b .32x128b .16x32bx2 .128x "
      ".4x256b");
  ASSERT_EQ(toks.size(), 9u);
  EXPECT_EQ(toks[0].first, TokenKind::DotShape16x64b);
  EXPECT_EQ(toks[0].second, ".16x64b");
  EXPECT_EQ(toks[1].first, TokenKind::DotShape16x128b);
  EXPECT_EQ(toks[1].second, ".16x128b");
  EXPECT_EQ(toks[2].first, TokenKind::DotShape16x256b);
  EXPECT_EQ(toks[2].second, ".16x256b");
  EXPECT_EQ(toks[3].first, TokenKind::DotShape32x32b);
  EXPECT_EQ(toks[3].second, ".32x32b");
  EXPECT_EQ(toks[4].first, TokenKind::DotShape32x64b);
  EXPECT_EQ(toks[4].second, ".32x64b");
  EXPECT_EQ(toks[5].first, TokenKind::DotShape32x128b);
  EXPECT_EQ(toks[5].second, ".32x128b");
  EXPECT_EQ(toks[6].first, TokenKind::DotShape16x32bx2);
  EXPECT_EQ(toks[6].second, ".16x32bx2");
  EXPECT_EQ(toks[7].first, TokenKind::DotShape128x);
  EXPECT_EQ(toks[7].second, ".128x");
  EXPECT_EQ(toks[8].first, TokenKind::DotShape4x256b);
  EXPECT_EQ(toks[8].second, ".4x256b");
}

// ── PTX 9.2: cp.async.bulk modifiers ─────────────────────────────────────────

TEST(PtxLexer92CpAsyncBulk, BulkModifiers) {
  auto toks = lex_all(".bulk_group .bulk .async .commit_group .wait_group");
  ASSERT_EQ(toks.size(), 5u);
  EXPECT_EQ(toks[0].first, TokenKind::DotBulkGroup);
  EXPECT_EQ(toks[1].first, TokenKind::DotBulk);
  EXPECT_EQ(toks[2].first, TokenKind::DotAsync);
  EXPECT_EQ(toks[3].first, TokenKind::DotCommitGroup);
  EXPECT_EQ(toks[4].first, TokenKind::DotWaitGroup);
}

// bulk_group must be matched before bulk (longest match)
TEST(PtxLexer92CpAsyncBulk, BulkGroup_LongestMatch) {
  auto tok = lex1(".bulk_group");
  EXPECT_EQ(tok.kind, TokenKind::DotBulkGroup);
  EXPECT_EQ(tok.text, ".bulk_group");
}

// ── PTX 9.2: tensormap dot-modifiers ─────────────────────────────────────────

TEST(PtxLexer92Tensormap, SubOpModifiers) {
  auto toks = lex_all(".replace .cp_fenceproxy");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::DotReplace);
  EXPECT_EQ(toks[0].second, ".replace");
  EXPECT_EQ(toks[1].first, TokenKind::DotCpFenceproxy);
  EXPECT_EQ(toks[1].second, ".cp_fenceproxy");
}

// ── PTX 9.2: cluster launch control ──────────────────────────────────────────

TEST(PtxLexer92ClusterLaunchControl, SubOpModifiers) {
  auto toks = lex_all(".try_cancel .query_cancel");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::DotTryCancel);
  EXPECT_EQ(toks[0].second, ".try_cancel");
  EXPECT_EQ(toks[1].first, TokenKind::DotQueryCancel);
  EXPECT_EQ(toks[1].second, ".query_cancel");
}

// ── PTX 9.2: wgmma modifiers ─────────────────────────────────────────────────

TEST(PtxLexer92Wgmma, ModifierTokens) {
  auto toks = lex_all(".mma_async .scale_d");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::DotMmaAsync);
  EXPECT_EQ(toks[0].second, ".mma_async");
  EXPECT_EQ(toks[1].first, TokenKind::DotScaleD);
  EXPECT_EQ(toks[1].second, ".scale_d");
}

// ── PTX 9.2: compound :: tokens → DotIdent ───────────────────────────────────

TEST(PtxLexer92CompoundTokens, KindTokens) {
  // All .kind::* tokens must lex as DotIdent with the full text preserved
  const char* kinds[] = {
      ".kind::mxf8f6f4", ".kind::mxf4nvf4", ".kind::mxf4", ".kind::f16bf16",
      ".kind::tf32",     ".kind::i8",       ".kind::b1",
  };
  for (auto* k : kinds) {
    auto tok = lex1(k);
    EXPECT_EQ(tok.kind, TokenKind::DotIdent) << "failed for: " << k;
    EXPECT_EQ(tok.text, std::string_view(k)) << "text mismatch for: " << k;
  }
}

TEST(PtxLexer92CompoundTokens, MbarrierCompleteTxBytes) {
  auto tok = lex1(".mbarrier::complete_tx::bytes");
  EXPECT_EQ(tok.kind, TokenKind::DotIdent);
  EXPECT_EQ(tok.text, ".mbarrier::complete_tx::bytes");
}

TEST(PtxLexer92CompoundTokens, MulticastClusterMbarrierCompleteTxBytes) {
  auto tok = lex1(".multicast::cluster::mbarrier::complete_tx::bytes");
  EXPECT_EQ(tok.kind, TokenKind::DotIdent);
  EXPECT_EQ(tok.text, ".multicast::cluster::mbarrier::complete_tx::bytes");
}

TEST(PtxLexer92CompoundTokens, PackModeTokens) {
  auto toks =
      lex_all(".b8x16.b6x16_p32 .b8x16.b4x16_p64 .b6x16_p32 .b4x16_p64");
  ASSERT_EQ(toks.size(), 4u);
  for (auto& [k, _] : toks)
    EXPECT_EQ(k, TokenKind::DotIdent);
  EXPECT_EQ(toks[0].second, ".b8x16.b6x16_p32");
  EXPECT_EQ(toks[1].second, ".b8x16.b4x16_p64");
  EXPECT_EQ(toks[2].second, ".b6x16_p32");
  EXPECT_EQ(toks[3].second, ".b4x16_p64");
}

TEST(PtxLexer92CompoundTokens, CollectorSmemToken) {
  auto tok = lex1(".collector::b0::smem");
  EXPECT_EQ(tok.kind, TokenKind::DotIdent);
  EXPECT_EQ(tok.text, ".collector::b0::smem");
}

TEST(PtxLexer92CompoundTokens, ScaleNegTokens) {
  auto toks = lex_all(".scale::2,1 .scale::1,2 .neg::a .neg::b");
  ASSERT_EQ(toks.size(), 4u);
  for (auto& [k, _] : toks)
    EXPECT_EQ(k, TokenKind::DotIdent);
}

// ── PTX 9.2: realistic instruction-like sequences ────────────────────────────

// tcgen05.ld.sync.aligned.16x64b.x1 {dst}, [tptr];
TEST(PtxLexer92Integration, Tcgen05Ld) {
  auto toks = lex_all("tcgen05 .ld .sync .aligned .16x64b .x1");
  ASSERT_EQ(toks.size(), 6u);
  EXPECT_EQ(toks[0].first, TokenKind::Tcgen05);
  EXPECT_EQ(toks[1].first, TokenKind::DotIdent);
  EXPECT_EQ(toks[1].second, ".ld");
  EXPECT_EQ(toks[2].first, TokenKind::DotSync);
  EXPECT_EQ(toks[3].first, TokenKind::DotAligned);
  EXPECT_EQ(toks[4].first, TokenKind::DotShape16x64b);
  EXPECT_EQ(toks[5].first, TokenKind::DotX1);
}

// tcgen05.alloc.cta1 [tptr], ncols;
TEST(PtxLexer92Integration, Tcgen05Alloc) {
  auto toks = lex_all("tcgen05 .alloc .cta1");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].first, TokenKind::Tcgen05);
  EXPECT_EQ(toks[1].first, TokenKind::DotAlloc);
  EXPECT_EQ(toks[2].first, TokenKind::DotCta1);
}

// mbarrier.arrive.shared::cta.b64 [addr];
TEST(PtxLexer92Integration, MbarrierArrive) {
  auto toks = lex_all("mbarrier .arrive .b64");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].first, TokenKind::Mbarrier);
  EXPECT_EQ(toks[1].first, TokenKind::DotArrive);
  EXPECT_EQ(toks[2].first, TokenKind::DotB64);
}

// mbarrier.expect_tx.shared::cta.b64 [addr], txcount;
TEST(PtxLexer92Integration, MbarrierExpectTx) {
  auto toks = lex_all("mbarrier .expect_tx .b64");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].first, TokenKind::Mbarrier);
  EXPECT_EQ(toks[1].first, TokenKind::DotExpectTx);
  EXPECT_EQ(toks[2].first, TokenKind::DotB64);
}

// fence.sc.cta;
TEST(PtxLexer92Integration, FenceScCta) {
  auto toks = lex_all("fence .sc .cta ;");
  ASSERT_EQ(toks.size(), 4u);
  EXPECT_EQ(toks[0].first, TokenKind::Fence);
  EXPECT_EQ(toks[1].first, TokenKind::DotSc);
  EXPECT_EQ(toks[2].first, TokenKind::DotCta);
  EXPECT_EQ(toks[3].first, TokenKind::Semicolon);
}

// fence.proxy.tensormap::generic.cta  (proxy + compound DotIdent)
TEST(PtxLexer92Integration, FenceProxyTensormap) {
  auto toks = lex_all("fence .proxy .tensormap::generic .cta");
  ASSERT_EQ(toks.size(), 4u);
  EXPECT_EQ(toks[0].first, TokenKind::Fence);
  EXPECT_EQ(toks[1].first, TokenKind::DotProxy);
  EXPECT_EQ(toks[2].first, TokenKind::DotIdent);
  EXPECT_EQ(toks[2].second, ".tensormap::generic");
  EXPECT_EQ(toks[3].first, TokenKind::DotCta);
}

// setmaxnreg.inc.sync.aligned.u32 128;
TEST(PtxLexer92Integration, SetMaxNReg) {
  auto toks = lex_all("setmaxnreg .inc .sync .aligned .u32");
  ASSERT_EQ(toks.size(), 5u);
  EXPECT_EQ(toks[0].first, TokenKind::Setmaxnreg);
  EXPECT_EQ(toks[1].first, TokenKind::DotInc);
  EXPECT_EQ(toks[2].first, TokenKind::DotSync);
  EXPECT_EQ(toks[3].first, TokenKind::DotAligned);
  EXPECT_EQ(toks[4].first, TokenKind::DotU32);
}

// elect.sync %p, membermask;
TEST(PtxLexer92Integration, ElectSync) {
  auto toks = lex_all("elect .sync");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::Elect);
  EXPECT_EQ(toks[1].first, TokenKind::DotSync);
}

// stmatrix.sync.aligned.x1.b16 [addr], {src};
TEST(PtxLexer92Integration, StMatrix) {
  auto toks = lex_all("stmatrix .sync .aligned .x1 .b16");
  ASSERT_EQ(toks.size(), 5u);
  EXPECT_EQ(toks[0].first, TokenKind::Stmatrix);
  EXPECT_EQ(toks[1].first, TokenKind::DotSync);
  EXPECT_EQ(toks[2].first, TokenKind::DotAligned);
  EXPECT_EQ(toks[3].first, TokenKind::DotX1);
  EXPECT_EQ(toks[4].first, TokenKind::DotB16);
}

// wgmma.mma_async.sync.aligned.m64n128k16.f32.f16.f16
TEST(PtxLexer92Integration, WgmmaMmaAsync) {
  auto toks = lex_all("wgmma .mma_async .sync .aligned");
  ASSERT_EQ(toks.size(), 4u);
  EXPECT_EQ(toks[0].first, TokenKind::Wgmma);
  EXPECT_EQ(toks[1].first, TokenKind::DotMmaAsync);
  EXPECT_EQ(toks[2].first, TokenKind::DotSync);
  EXPECT_EQ(toks[3].first, TokenKind::DotAligned);
}

// cp.async.bulk.commit_group;
TEST(PtxLexer92Integration, CpAsyncBulkCommitGroup) {
  auto toks = lex_all("cp .async .bulk .commit_group ;");
  ASSERT_EQ(toks.size(), 5u);
  EXPECT_EQ(toks[0].first, TokenKind::Cp);
  EXPECT_EQ(toks[1].first, TokenKind::DotAsync);
  EXPECT_EQ(toks[2].first, TokenKind::DotBulk);
  EXPECT_EQ(toks[3].first, TokenKind::DotCommitGroup);
  EXPECT_EQ(toks[4].first, TokenKind::Semicolon);
}

// cp.async.bulk.wait_group 0;
TEST(PtxLexer92Integration, CpAsyncBulkWaitGroup) {
  auto toks = lex_all("cp .async .bulk .wait_group");
  ASSERT_EQ(toks.size(), 4u);
  EXPECT_EQ(toks[0].first, TokenKind::Cp);
  EXPECT_EQ(toks[1].first, TokenKind::DotAsync);
  EXPECT_EQ(toks[2].first, TokenKind::DotBulk);
  EXPECT_EQ(toks[3].first, TokenKind::DotWaitGroup);
}

// tensormap.replace.tile.global_address.b1024 [tmap], addr;
TEST(PtxLexer92Integration, TensormapReplace) {
  auto toks = lex_all("tensormap .replace");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::Tensormap);
  EXPECT_EQ(toks[1].first, TokenKind::DotReplace);
}

// clusterlaunchcontrol.try_cancel [addr], pred;
TEST(PtxLexer92Integration, ClusterLaunchControl) {
  auto toks = lex_all("clusterlaunchcontrol .try_cancel");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::Clusterlaunchcontrol);
  EXPECT_EQ(toks[1].first, TokenKind::DotTryCancel);
}

// discard.global.b64 [addr];
TEST(PtxLexer92Integration, Discard) {
  auto toks = lex_all("discard .global .b64");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].first, TokenKind::Discard);
  EXPECT_EQ(toks[1].first, TokenKind::DotGlobal);
  EXPECT_EQ(toks[2].first, TokenKind::DotB64);
}

// prefetchu.L1 [addr];
TEST(PtxLexer92Integration, Prefetchu) {
  auto toks = lex_all("prefetchu .L1");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::Prefetchu);
  EXPECT_EQ(toks[1].first, TokenKind::DotL1);
}

// getctarank dst, addr;
TEST(PtxLexer92Integration, GetCtaRank) {
  auto tok = lex1("getctarank");
  EXPECT_EQ(tok.kind, TokenKind::Getctarank);
  EXPECT_EQ(tok.text, "getctarank");
}

// brkpt;
TEST(PtxLexer92Integration, Brkpt) {
  auto toks = lex_all("brkpt ;");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, TokenKind::Brkpt);
  EXPECT_EQ(toks[1].first, TokenKind::Semicolon);
}

// movmatrix.sync.aligned.m8n8.trans.b16 dst, src;
TEST(PtxLexer92Integration, MovMatrix) {
  auto toks = lex_all("movmatrix .sync .aligned .m8n8 .trans .b16");
  ASSERT_EQ(toks.size(), 6u);
  EXPECT_EQ(toks[0].first, TokenKind::Movmatrix);
  EXPECT_EQ(toks[1].first, TokenKind::DotSync);
  EXPECT_EQ(toks[2].first, TokenKind::DotAligned);
  EXPECT_EQ(toks[3].first, TokenKind::DotM8n8);
  EXPECT_EQ(toks[4].first, TokenKind::DotTrans);
  EXPECT_EQ(toks[5].first, TokenKind::DotB16);
}

// tcgen05.ld with offset: tcgen05 .ld .sync .aligned .32x32b .x4 .offset
TEST(PtxLexer92Integration, Tcgen05LdWithOffset) {
  auto toks = lex_all("tcgen05 .ld .sync .aligned .32x32b .x4 .offset");
  ASSERT_EQ(toks.size(), 7u);
  EXPECT_EQ(toks[0].first, TokenKind::Tcgen05);
  EXPECT_EQ(toks[4].first, TokenKind::DotShape32x32b);
  EXPECT_EQ(toks[6].first, TokenKind::DotOffset);
}
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

syntax_ast::AstModule parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value()) << module.diagnostics.front().message;
  return std::move(*module);
}

TEST(ResolvedModule, ResolvesAndChecksEveryM10CorpusModule) {
  const auto file = std::filesystem::path{__FILE__}
                        .parent_path()
                        .parent_path()
                        .parent_path()
                        .parent_path() /
                    "corpus" / "m10" / "advanced_kernel.ptx";
  std::ifstream input(file);
  ASSERT_TRUE(input) << file;
  const std::string source{std::istreambuf_iterator<char>{input}, {}};

  PtxSyntaxParser parser(source);
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value()) << file;
  EXPECT_TRUE(parsed.diagnostics.empty()) << file;
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << file;
  ASSERT_EQ(resolved->functions.size(), 1u);
  const auto& function = resolved->functions.front();
  ASSERT_FALSE(function.body.empty());

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
  };
  for (const auto& instruction : function.body) {
    const auto checked = std::visit(
        [&](const auto& value) { return checker::check(value, context); },
        instruction);
    EXPECT_TRUE(checked.has_value()) << file;
  }
}

TEST(ResolvedModule, RejectsM10CorpusNegativeBoundaries) {
  const checker::Context current{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
  };
  const auto invalid_copy = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { cp.async.ca.shared.global [shared_value], [global_value], 3; }
)ptx"));
  ASSERT_TRUE(invalid_copy.has_value()) << invalid_copy.error().front().message;
  const auto copy_check = checker::check(
      std::get<Cp>(invalid_copy->functions.front().body.front()), current);
  ASSERT_FALSE(copy_check.has_value());
  EXPECT_EQ(copy_check.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);

  const auto invalid_atom = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  .reg .u32 %r<2>;
  atom.global.relaxed.cta.add.u32 %r0, [local_value], %r1;
}
)ptx"));
  ASSERT_TRUE(invalid_atom.has_value()) << invalid_atom.error().front().message;
  const auto atom_check = checker::check(
      std::get<Atom>(invalid_atom->functions.front().body.front()), current);
  ASSERT_FALSE(atom_check.has_value());
  EXPECT_EQ(atom_check.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto invalid_vote = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  .reg .pred %p0;
  .reg .b64 %mask;
  vote.sync.ballot.b32 %b0, %p0, %mask;
}
)ptx"));
  ASSERT_TRUE(invalid_vote.has_value()) << invalid_vote.error().front().message;
  const auto vote_check = checker::check(
      std::get<Vote>(invalid_vote->functions.front().body.front()), current);
  ASSERT_FALSE(vote_check.has_value());
  EXPECT_EQ(vote_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto invalid_shfl = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  .reg .u32 %u0;
  shfl.sync.idx.b32 %b0|%u0, %b0, 0, 31, 0xffffffff;
}
)ptx"));
  ASSERT_FALSE(invalid_shfl.has_value());

  const auto invalid_ldmatrix = resolveModule(parseModule(R"ptx(
.global .b16 global_matrix;
.entry kernel() {
  .reg .b32 %m<2>;
  ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%m0, %m1}, [global_matrix];
}
)ptx"));
  ASSERT_TRUE(invalid_ldmatrix.has_value())
      << invalid_ldmatrix.error().front().message;
  const auto ldmatrix_check = checker::check(
      std::get<Ldmatrix>(invalid_ldmatrix->functions.front().body.front()),
      current);
  ASSERT_FALSE(ldmatrix_check.has_value());
  EXPECT_EQ(ldmatrix_check.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto invalid_mma = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .f32 %d<3>;
  .reg .f32 %c<4>;
  .reg .f16x2 %a<2>;
  .reg .f16x2 %b<1>;
  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 {%d0, %d1, %d2}, {%a0, %a1}, {%b0}, {%c0, %c1, %c2, %c3};
}
)ptx"));
  ASSERT_FALSE(invalid_mma.has_value());

  const auto valid_mma = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .f32 %d<4>;
  .reg .f32 %c<4>;
  .reg .f16x2 %a<2>;
  .reg .f16x2 %b<1>;
  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 {%d0, %d1, %d2, %d3}, {%a0, %a1}, {%b0}, {%c0, %c1, %c2, %c3};
}
)ptx"));
  ASSERT_TRUE(valid_mma.has_value()) << valid_mma.error().front().message;
  const auto old_target = checker::check(
      std::get<Mma>(valid_mma->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {6, 4}, .sm_version = 80}});
  ASSERT_FALSE(old_target.has_value());
  EXPECT_EQ(old_target.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

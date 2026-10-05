#include <gtest/gtest.h>

#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for resolver support tests. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

TEST(SelectVariantLoadStore, SelectsLegalCacheOperatorsAndRejectsWrongOnes) {
  const auto expect_load = [](std::string_view source,
                              std::string_view expected) {
    const auto ast = parse_instruction(source);
    const auto selected = select_variant_name(ast, ld_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };
  expect_load("ld.ca.u32 %r0, [%rd0];", "GenericScalar");
  expect_load("ld.global.cv.u32 %r0, [%rd0];", "ExplicitScalar");
  expect_load("ld.cg.v2.u32 {%r0, %r1}, [%rd0];",
              "GenericVector");
  expect_load("ld.shared.ca.v4.u16 {%h0, %h1, %h2, %h3}, [%rd0];",
              "ExplicitVector");
  expect_load("ld.relaxed.cta.global.u32 %r0, [%rd0];",
              "ExplicitScalar");
  expect_load("ld.global.relaxed.cta.u32 %r0, [%rd0];",
              "ExplicitScalar");
  expect_load("ld.relaxed.cta.global.v2.u32 {%r0, %r1}, [%rd0];",
              "ExplicitVector");
  expect_load("ld.global.relaxed.cta.v2.u32 {%r0, %r1}, [%rd0];",
              "ExplicitVector");
  expect_load("ld.mmio.relaxed.sys.global.u32 %r0, [%rd0];",
              "ExplicitScalar");
  expect_load("ld.global.relaxed.sys.mmio.u32 %r0, [%rd0];",
              "ExplicitScalar");

  const auto invalid_load = parse_instruction("ld.wb.u32 %r0, [%rd0];");
  const auto invalid_load_selected = select_variant_name(invalid_load, ld_syntax_descriptor());
  ASSERT_FALSE(invalid_load_selected.has_value());
  EXPECT_EQ(invalid_load_selected.error().range,
            invalid_load.modifiers.front().syntax.range);
  EXPECT_EQ(invalid_load_selected.error().message, "Unknown modifier '.wb'.");

  const auto expect_store = [](std::string_view source,
                               std::string_view expected) {
    const auto ast = parse_instruction(source);
    const auto selected = select_variant_name(ast, st_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };
  expect_store("st.wt.u32 [%rd0], %r0;", "GenericScalar");
  expect_store("st.global.cg.u32 [%rd0], %r0;",
               "ExplicitScalar");
  expect_store("st.wt.v2.u32 [%rd0], {%r0, %r1};",
               "GenericVector");
  expect_store("st.shared.cg.v4.u16 [%rd0], {%h0, %h1, %h2, %h3};",
               "ExplicitVector");
  expect_store("st.release.sys.global.u32 [%rd0], %r0;",
               "ExplicitScalar");
  expect_store("st.global.release.sys.u32 [%rd0], %r0;",
               "ExplicitScalar");
  expect_store("st.release.sys.global.v2.u32 [%rd0], {%r0, %r1};",
               "ExplicitVector");
  expect_store("st.global.release.sys.v2.u32 [%rd0], {%r0, %r1};",
               "ExplicitVector");
  expect_store("st.mmio.relaxed.sys.global.u32 [%rd0], %r0;",
               "ExplicitScalar");
  expect_store("st.global.relaxed.sys.mmio.u32 [%rd0], %r0;",
               "ExplicitScalar");

  const auto invalid_store = parse_instruction("st.ca.u32 [%rd0], %r0;");
  const auto invalid_store_selected = select_variant_name(invalid_store, st_syntax_descriptor());
  ASSERT_FALSE(invalid_store_selected.has_value());
  EXPECT_EQ(invalid_store_selected.error().range,
            invalid_store.modifiers.front().syntax.range);
  EXPECT_EQ(invalid_store_selected.error().message, "Unknown modifier '.ca'.");

  const auto modern_vector = parse_instruction(
      "ld.v8.u32 {%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];");
  const auto modern_vector_selected = select_variant_name(modern_vector, ld_syntax_descriptor());
  ASSERT_TRUE(modern_vector_selected.has_value())
      << modern_vector_selected.error().message;
  EXPECT_EQ(*modern_vector_selected, "GenericVector");

  const auto vector_mmio =
      parse_instruction("ld.mmio.relaxed.sys.v2.u32 {%r0, %r1}, [%rd0];");
  const auto vector_mmio_selected = select_variant_name(vector_mmio, ld_syntax_descriptor());
  ASSERT_FALSE(vector_mmio_selected.has_value());
  EXPECT_EQ(
      vector_mmio_selected.error().message,
      "No variant of instruction 'ld' accepts this modifier combination.");
}

TEST(ResolveLoadStore, PreservesCacheValuesAndOmittedSentinel) {
  const auto cached_load_ast = parse_instruction("ld.cg.u32 %r0, [%rd0];");
  const auto cached_load = resolveLd(cached_load_ast);
  ASSERT_TRUE(cached_load.has_value()) << cached_load.error().message;
  const auto* load_variant =
      dynamic_cast<LdGenericScalar*>(cached_load->get());
  ASSERT_NE(load_variant, nullptr);
  EXPECT_EQ(load_variant->cache.value, CacheOperator::Cg);
  ASSERT_EQ(load_variant->cache.locs.size(), 1u);
  EXPECT_EQ(load_variant->cache.locs.front(),
            cached_load_ast.modifiers.front().syntax.range);

  const auto omitted_load_ast = parse_instruction("ld.u32 %r0, [%rd0];");
  const auto omitted_load = resolveLd(omitted_load_ast);
  ASSERT_TRUE(omitted_load.has_value()) << omitted_load.error().message;
  const auto* omitted_load_variant =
      dynamic_cast<LdGenericScalar*>(omitted_load->get());
  ASSERT_NE(omitted_load_variant, nullptr);
  EXPECT_EQ(omitted_load_variant->cache.value, CacheOperator::Unspecified);
  EXPECT_TRUE(omitted_load_variant->cache.locs.empty());

  const auto cached_store_ast =
      parse_instruction("st.global.wb.u32 [%rd0], %r0;");
  const auto cached_store = resolveSt(cached_store_ast);
  ASSERT_TRUE(cached_store.has_value()) << cached_store.error().message;
  const auto* store_variant =
      dynamic_cast<StExplicitScalar*>(cached_store->get());
  ASSERT_NE(store_variant, nullptr);
  EXPECT_EQ(store_variant->cache.value, CacheOperator::Wb);
  ASSERT_EQ(store_variant->cache.locs.size(), 1u);
  EXPECT_EQ(store_variant->cache.locs.front(),
            cached_store_ast.modifiers[1].syntax.range);

  const auto omitted_store_ast =
      parse_instruction("st.global.u32 [%rd0], %r0;");
  const auto omitted_store = resolveSt(omitted_store_ast);
  ASSERT_TRUE(omitted_store.has_value()) << omitted_store.error().message;
  const auto* omitted_store_variant =
      dynamic_cast<StExplicitScalar*>(omitted_store->get());
  ASSERT_NE(omitted_store_variant, nullptr);
  EXPECT_EQ(omitted_store_variant->cache.value, CacheOperator::Unspecified);
  EXPECT_TRUE(omitted_store_variant->cache.locs.empty());
}

TEST(ResolveLoadStore, PreservesMemoryConsistencyDefaultsAndExplicitWeak) {
  const auto omitted_ast = parse_instruction("ld.u32 %r0, [%rd0];");
  const auto omitted = resolveLd(omitted_ast);
  ASSERT_TRUE(omitted.has_value()) << omitted.error().message;
  const auto* omitted_variant =
      dynamic_cast<LdGenericScalar*>(omitted->get());
  ASSERT_NE(omitted_variant, nullptr);
  EXPECT_EQ(omitted_variant->semantics.value, MemoryConsistency::Omitted);
  EXPECT_TRUE(omitted_variant->semantics.locs.empty());
  EXPECT_EQ(omitted_variant->scope.value, MemoryScope::None);
  EXPECT_TRUE(omitted_variant->scope.locs.empty());

  const auto weak_ast = parse_instruction("ld.weak.u32 %r0, [%rd0];");
  const auto weak = resolveLd(weak_ast);
  ASSERT_TRUE(weak.has_value()) << weak.error().message;
  const auto* weak_variant =
      dynamic_cast<LdGenericScalar*>(weak->get());
  ASSERT_NE(weak_variant, nullptr);
  EXPECT_EQ(weak_variant->semantics.value, MemoryConsistency::Weak);
  ASSERT_EQ(weak_variant->semantics.locs.size(), 1U);
  EXPECT_EQ(weak_variant->semantics.locs.front(),
            weak_ast.modifiers.front().syntax.range);

  const auto acquire =
      select_variant_name(parse_instruction("ld.acquire.gpu.u32 %r0, [%rd0];"), ld_syntax_descriptor());
  ASSERT_TRUE(acquire.has_value()) << acquire.error().message;
  EXPECT_EQ(*acquire, "GenericScalar");
  const auto release =
      select_variant_name(parse_instruction("st.release.sys.u32 [%rd0], %r0;"), st_syntax_descriptor());
  ASSERT_TRUE(release.has_value()) << release.error().message;
  EXPECT_EQ(*release, "GenericScalar");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

#include <iostream>
#include <optional>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/stack_manipulation/alloca.gen.hpp>
#include <ptx_frontend/resolved_ir/model/stack_manipulation/stacksave.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Check owned public stack contracts after parser and AST destruction. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  constexpr std::string_view source = R"ptx(
.version 7.3
.target sm_52
.address_size 32
.entry k() {
  .reg .b64 %rd<3>;
  .reg .u32 %r<3>;
  .reg .pred %p;
  @%p stacksave.u32 %r0;
  stacksave.u64 %rd0;
  alloca.u32 %r1, %r2;
  alloca.u32 %r1, 0, 8;
  alloca.u64 %rd1, %rd2;
  alloca.u64 %rd1, 16, 4294967297;
  stackrestore.u32 %r0;
  stackrestore.u64 %rd0;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return 1;
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!resolved) {
      for (const auto& diagnostic : resolved.error())
        std::cerr << diagnostic.message << '\n';
      return 2;
    }
    owned = std::move(*resolved);
  }
  if (!ir::validateModule(*owned,
                          ir::ModuleValidationPolicy::RequireCompleteContext))
    return 3;
  auto& body = owned->functions.front().body;
  const auto& token = dynamic_cast<const ir::StacksaveU32&>(*body[0]);
  const auto& implicit = dynamic_cast<const ir::AllocaU32&>(*body[2]);
  const auto& explicit_eight = dynamic_cast<const ir::AllocaU32&>(*body[3]);
  const auto& converted = dynamic_cast<const ir::AllocaU64&>(*body[5]);
  if (body.size() != 8 ||
      token.stack_descriptor()->operation != ir::StackOperation::Save ||
      token.dst.value.enclosing_function_kind !=
          ir::EnclosingFunctionKind::Entry ||
      !token.dst.value.function_scope || implicit.alignment ||
      !explicit_eight.alignment ||
      implicit.dst.value.address_state_space !=
          ptx_frontend::base::DeclarationStateSpace::Local ||
      implicit.stack_descriptor()->width != 32 ||
      implicit.stack_descriptor()->default_alignment != 8 ||
      !converted.alignment || converted.alignment->value.bits != 1 ||
      converted.alignment->value.integer_source_bits != 4294967297ull)
    return 4;
  auto& mutable_token = dynamic_cast<ir::StacksaveU32&>(*body[0]);
  mutable_token.dst.value.enclosing_function_kind =
      ir::EnclosingFunctionKind::Unknown;
  if (ir::validateModule(*owned))
    return 5;
  std::cout << "installed stack contracts passed\n";
  return 0;
}

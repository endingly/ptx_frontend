#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

namespace {
using ptx_frontend::resolved_ir::AddIntegerNoSat;
using ptx_frontend::resolved_ir::InstructionKind;
using ptx_frontend::resolved_ir::Opcode;

static_assert(
    std::is_same_v<decltype(AddIntegerNoSat::kind), const InstructionKind>);
static_assert(std::is_same_v<decltype(AddIntegerNoSat::opcode), const Opcode>);
static_assert((static_cast<std::uint32_t>(AddIntegerNoSat::kind) &
               0xffff0000u) ==
              static_cast<std::uint32_t>(AddIntegerNoSat::opcode));
}  // namespace

/** A single form header retains typed IDs without the named catalogue. */
TEST(ResolvedIrPublicHeaders, NarrowFormIdentityCompilesWithoutCatalogue) {
  EXPECT_NE(static_cast<std::uint32_t>(AddIntegerNoSat::kind), 0u);
}

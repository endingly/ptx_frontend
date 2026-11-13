#include "frontend_fuzz.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
/** Replay one canary without the libFuzzer runtime. */
ptx_frontend::fuzz::FrontendStages replay(std::string_view source) {
  return ptx_frontend::fuzz::exerciseFrontend(
      reinterpret_cast<const uint8_t*>(source.data()), source.size());
}
/** Fail even in optimized builds when a canary misses its intended stage. */
void require(bool condition) {
  if (!condition)
    std::abort();
}
}  // namespace

/** Prove valid and rejected seeds exercise the intended pipeline boundaries. */
int main() {
  for (const auto source :
       {".version 9.3\n.target sm_100\n.address_size 64\n.entry c1() { .reg "
        ".v4 .u16 V; mov.v4.u16 V, %tid; ret; }",
        ".version 9.3\n.target sm_100\n.address_size 64\n.entry store() { "
        ".shared .b32 slot; st.shared.b32 [slot], 17; ret; }"}) {
    const auto stages = replay(source);
    require(stages.clean_module && stages.strict_success &&
            stages.ownership_checked && !stages.oversized &&
            stages.functions_checked == 1 && stages.cloned_instructions == 2);
  }
  const auto missing_context = replay(".entry k() { ret; }");
  require(missing_context.clean_module && !missing_context.strict_success);
  for (const auto source :
       {std::string("\0\xff", 2), std::string("/*"),
        std::string(".entry k() { {"),
        ".global .u32 x = " + std::string(1024, '+') + "1;",
        ".entry k() {" + std::string(300, '{') + std::string(300, '}') + "}"})
    (void)replay(source);
  require(replay(std::string(65537, 'x')).oversized);
  std::cout << "frontend canary: 2 strict + ownership successes; "
               "rejection/depth/cap replay passed\n";
}

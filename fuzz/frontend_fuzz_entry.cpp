#include "frontend_fuzz.hpp"

/** Feed arbitrary source bytes to the bounded frontend pipeline. */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  (void)ptx_frontend::fuzz::exerciseFrontend(data, size);
  return 0;
}

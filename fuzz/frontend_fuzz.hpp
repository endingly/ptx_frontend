#pragma once

#include <cstddef>
#include <cstdint>

namespace ptx_frontend::fuzz {
/** Internal replay evidence; never part of the installed SDK. */
struct FrontendStages {
  /** Input exceeded the bounded frontend budget. */
  bool oversized{};
  /** Module AST was complete and carried no recovery diagnostics. */
  bool clean_module{};
  /** Strict resolution accepted the source after parser/source destruction. */
  bool strict_success{};
  /** Owned validation and independent copy/move/clone checks completed without AST. */
  bool ownership_checked{};
  /** Number of owned function values copied, assigned and moved. */
  std::size_t functions_checked{};
  /** Number of independent instruction clones validated in their module context. */
  std::size_t cloned_instructions{};
};
/** Exercise source parsing and clean-module strict resolution with bounded input. */
FrontendStages exerciseFrontend(const uint8_t* data, std::size_t size);
}  // namespace ptx_frontend::fuzz

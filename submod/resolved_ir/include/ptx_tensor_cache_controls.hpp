#pragma once

#include <optional>
#include <string>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_instruction_base.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>

namespace ptx_frontend::resolved_ir {

/** Copied selected cache controls; no syntax or resolved-module owner is kept. */
struct TensorCacheControlsReport {
  /** Whether the selected instruction is a canonical tensor access. */
  bool applicable = false;
  /** Authored hint value and source ranges; absent on the parent form. */
  std::optional<WithLocs<bool>> hint;
  /** Final owned register/immediate source and ranges, when supplied. */
  std::optional<WithLocs<RegOrImm>> policy;
  /** Malformed selected metadata or carrier claims, independent of target. */
  std::vector<std::string> diagnostics;
};

/** Copy cache state from one selected exact Cp form after AST death. */
[[nodiscard]] TensorCacheControlsReport query_tensor_cache_controls(
    const Instruction& instruction);

}  // namespace ptx_frontend::resolved_ir

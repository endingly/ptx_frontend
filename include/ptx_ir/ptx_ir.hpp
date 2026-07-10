#pragma once

#include "ptx_ir/base.hpp"
#include "ptx_ir/source_loc.hpp"

// Stable installed path. The file is generated during the build, but users
// never need to know that.
#include "ptx_ir_registry.gen.hpp"

namespace ptx_frontend {

using generated::PtxInstruction;

};
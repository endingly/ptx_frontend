#pragma once

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>
#include <ptx_frontend/resolved_ir/tcgen_descriptor_domains.gen.hpp>

namespace ptx_frontend::resolved_ir {

/** Borrowed future TCGEN instruction-descriptor source role.
 *  A selected owning MMA form must establish this role; this header supplies no
 *  source factory or carrier policy. It does not authenticate known bits.
 */
struct TcgenInstructionDescriptorView {
  /** Register reference borrowed from the owning resolved instruction. */
  const ResolvedRegisterRef* source;
};

/** Borrowed future TCGEN zero-column descriptor source role.
 *  A selected owning MMA form must establish this role; this header supplies no
 *  source factory or carrier policy. It does not authenticate known bits.
 */
struct TcgenZeroColumnDescriptorView {
  /** Register reference borrowed from the owning resolved instruction. */
  const ResolvedRegisterRef* source;
};

}  // namespace ptx_frontend::resolved_ir

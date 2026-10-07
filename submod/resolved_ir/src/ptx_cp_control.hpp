#pragma once

#include <ptx_frontend/resolved_ir/ptx_instruction_base.hpp>

namespace ptx_frontend::resolved_ir::detail {

/** Borrow only Cp control/cache-policy registers for AST-free cached-type checks.
 * This internal traversal validates the selected direct-field layout before
 * calling the observer and never stores borrowed payloads or spans.
 */
void visit_cp_control_registers(const Instruction&, IReferenceObserver&);

}  // namespace ptx_frontend::resolved_ir::detail

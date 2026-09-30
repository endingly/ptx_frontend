#pragma once

#include <cstdint>
#include <span>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>

namespace ptx_frontend::resolved_ir {

struct Ldmatrix;
struct Mma;
struct Movmatrix;
struct Stmatrix;
struct Wmma;
struct Wgmma;

namespace detail {

/** Closed payload identities emitted by private matrix reference shards. */
enum class MatrixReferenceKind : uint8_t {
  Predicate,
  RegisterVector,
  Register,
  ScaleSelector,
  SharedMatrixDescriptor,
  PredicateSource,
  Address,
  RegisterOrImmediate,
};

/** Borrowed reference payload valid only during its callback invocation. */
struct MatrixReferenceView {
  /** Exact payload identity for the type-erased pointer. */
  MatrixReferenceKind kind;
  /** Borrowed pointer to the selected resolved operand value. */
  const void* value;
  /** Source locations retained by the owned operand field. */
  std::span<const SourceRange> locations;
  /** Generated address-symbol policy of this operand use. */
  checker::AddressSymbolResolutionPolicy address_resolution_policy;
};

/** Callback invoked synchronously for each selected matrix reference field. */
using MatrixReferenceCallback = void (*)(MatrixReferenceView, void*);

/** Visit one selected matrix instruction through bounded private shards. */
void visit_matrix_references(const Ldmatrix& instruction,
                             MatrixReferenceCallback callback, void* context);
/** Visit one selected matrix instruction through bounded private shards. */
void visit_matrix_references(const Mma& instruction,
                             MatrixReferenceCallback callback, void* context);
/** Visit one selected matrix instruction through bounded private shards. */
void visit_matrix_references(const Movmatrix& instruction,
                             MatrixReferenceCallback callback, void* context);
/** Visit one selected matrix instruction through bounded private shards. */
void visit_matrix_references(const Stmatrix& instruction,
                             MatrixReferenceCallback callback, void* context);
/** Visit one selected matrix instruction through bounded private shards. */
void visit_matrix_references(const Wmma& instruction,
                             MatrixReferenceCallback callback, void* context);
/** Visit the selected WGMMA form through bounded private shards. */
void visit_matrix_references(const Wgmma& instruction,
                             MatrixReferenceCallback callback, void* context);

}  // namespace detail
}  // namespace ptx_frontend::resolved_ir

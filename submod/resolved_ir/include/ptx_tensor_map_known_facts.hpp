#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <ptx_frontend/resolved_ir/model/data_movement/cp/model.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>

namespace ptx_frontend::resolved_ir {

namespace checker {
struct Context;
}

/** Selected tensor direction used only by caller-known descriptor checks. */
enum class TensorKnownFactDirection : uint8_t { Load, Store, Prefetch, Reduce };

/** Selected shared destination topology, if the instruction has one. */
enum class TensorKnownFactDestination : uint8_t { Cta, Cluster };

/** Independently claimed meaning of the single direction-dependent code 15. */
enum class TensorKnownCode15Interpretation : uint8_t { Load, Store };

/** Only the five accepted Table 33 field domains may be claimed here. */
template <typename Value>
concept TensorKnownEncodedField =
    std::same_as<Value, TensorMapElementType> ||
    std::same_as<Value, TensorMapInterleaveLayout> ||
    std::same_as<Value, TensorMapSwizzleMode> ||
    std::same_as<Value, TensorMapSwizzleAtomicity> ||
    std::same_as<Value, TensorMapFillMode>;

/** Owned result of a field-specific projection and its raw source code.
 *
 * The query rechecks code/value agreement through the accepted Table 33
 * helper. A false validity claim is diagnosed; absence remains unresolved.
 * This record is a caller claim and never authenticates descriptor bytes.
 */
template <TensorKnownEncodedField Value>
struct TensorKnownProjectedField {
  /** Closed field enum used by the accepted projection helper. */
  using ValueType = Value;
  /** Existing field-specific closed enum, if projected. */
  std::optional<Value> value;
  /** Unsigned source code before the B32 field use. */
  std::optional<uint64_t> code;
  /** Canonical projection result claimed by the caller. */
  std::optional<bool> valid;
};

/** Element projection with optional independent code-15 meaning. */
struct TensorKnownProjectedElement
    : TensorKnownProjectedField<TensorMapElementType> {
  /** Present only when the caller knows the code-15 semantic interpretation. */
  std::optional<TensorKnownCode15Interpretation> code15_interpretation;
};

/** A converted instruction-use integer with optional original 64-bit bits.
 *
 * Value has S32 units for a tensor coordinate and U16 units for im2col info.
 * No source bits means a separately known runtime register value; it never
 * means the authored literal was zero.
 */
struct TensorKnownConvertedInteger {
  /** Converted signed-coordinate or unsigned-info value, if independently known. */
  std::optional<int64_t> value;
  /** Original integer literal bits, before narrowing at instruction use. */
  std::optional<uint64_t> source_bits;
};

/** Bounded values whose arity and individual entries may be unknown.
 *
 * Values represent a single named unit in the containing fact record. An
 * absent entry is not zero. The array owns all values and has no AST lifetime.
 */
struct TensorKnownUnsignedArray {
  /** Number of meaningful slots, at most five. */
  size_t arity = 0;
  /** Supplied element counts, steps, or byte values as named by the member. */
  std::array<std::optional<uint64_t>, 5> values{};
};

/** Bounded signed opposite-edge offset values, not absolute endpoints. */
struct TensorKnownSignedArray {
  /** Number of meaningful spatial slots, at most three. */
  size_t arity = 0;
  /** Signed lower- or upper-edge element offsets. */
  std::array<std::optional<int32_t>, 3> values{};
};

/** Selected access copied from a canonical owned-form projection.
 *
 * This pure record does not inspect an instruction, infer descriptor state,
 * or prove map bytes. Availability claims must come from the accepted selected
 * variant and modifier-value queries, not a numeric SM comparison.
 */
struct TensorKnownAccessContext {
  /** Instruction direction. */
  TensorKnownFactDirection direction = TensorKnownFactDirection::Load;
  /** Existing closed tensor access mode. */
  TensorAccessMode mode = TensorAccessMode::Tiled;
  /** Selected instruction rank, independent of descriptor rank claim. */
  TensorRank rank = TensorRank::One;
  /** Selected destination topology, absent for stores and prefetch. */
  std::optional<TensorKnownFactDestination> destination;
  /** Exact catalog identity; no inferred architecture-family broadening. */
  std::optional<base::TargetIdentity> target_identity;
  /** Existing selected reduction operation, if any. */
  std::optional<TensorReductionOp> reduction_op;
  /** Converted S32 coordinate facts in source-role order. */
  std::array<TensorKnownConvertedInteger, 5> coordinates{};
  /** One to five for tiled, five for gather/scatter, zero when unknown. */
  size_t coordinate_arity = 0;
  /** Converted U16 im2col information facts in role order. */
  std::array<TensorKnownConvertedInteger, 3> info{};
  /** Zero for explicitly omitted info or unknown when info_known is false. */
  size_t info_arity = 0;
  /** Whether information presence/absence was actually selected. */
  bool info_known = false;
  /** Canonical selected variant availability result, if queried. */
  std::optional<bool> selected_variant_available;
  /** Canonical modifier/value availability result, if queried. */
  std::optional<bool> selected_value_available;
  /** Applied atomicity-use provenance, distinct from a raw Table 33 code. */
  std::optional<bool> active_atomicity_use;
  /** Optional written/effective TMA CTA group, not a TCGEN body condition. */
  std::optional<TensorCtaGroup> group;
};

/** Independent caller claims about an opaque tensor-map and this access.
 *
 * Table 33 fields reuse the accepted closed enums. A present enum is a typed
 * claim and still requires the canonical encoded-field projection before the
 * query may call its source code valid. MAP-object/data/box/shared addresses
 * and element/byte units deliberately remain separate.
 */
struct TensorMapKnownFacts {
  /** Claimed descriptor rank, one through five when present. */
  std::optional<TensorRank> rank;
  /** Projected Table 33 element identity; code 15 keeps directional meaning. */
  std::optional<TensorKnownProjectedElement> element;
  /** Projected Table 33 interleave identity. */
  std::optional<TensorKnownProjectedField<TensorMapInterleaveLayout>>
      interleave;
  /** Projected Table 33 swizzle identity. */
  std::optional<TensorKnownProjectedField<TensorMapSwizzleMode>> swizzle;
  /** Valid raw code does not imply selected active atomicity use. */
  std::optional<TensorKnownProjectedField<TensorMapSwizzleAtomicity>> atomicity;
  /** Projected Table 33 fill identity. */
  std::optional<TensorKnownProjectedField<TensorMapFillMode>> fill;
  /** Full tensor dimensions as element counts. */
  std::optional<TensorKnownUnsignedArray> full_dimensions_elements;
  /** Tiled traversal box extents as element counts. */
  std::optional<TensorKnownUnsignedArray> tiled_box_elements;
  /** Per-dimension traversal steps as element counts, not byte strides. */
  std::optional<TensorKnownUnsignedArray> traversal_steps_elements;
  /** Independently supplied global byte strides; no replacement-field inference. */
  std::optional<TensorKnownUnsignedArray> global_strides_bytes;
  /** Byte-valued tensor strides for explicitly sourced packed checks. */
  std::optional<TensorKnownUnsignedArray> tensor_strides_bytes;
  /** Rank-minus-two im2col spatial box extents as element counts. */
  std::optional<TensorKnownUnsignedArray> im2col_spatial_box_elements;
  /** Signed offsets measured from each spatial lower edge. */
  std::optional<TensorKnownSignedArray> im2col_lower_edge_offsets;
  /** Signed offsets measured from each opposite upper edge. */
  std::optional<TensorKnownSignedArray> im2col_upper_edge_offsets;
  /** W-only box width, measured in elements. */
  std::optional<uint64_t> w_box_elements;
  /** W-only lower-edge signed element offset. */
  std::optional<int32_t> w_lower_edge_offset;
  /** W-only opposite upper-edge signed element offset. */
  std::optional<int32_t> w_upper_edge_offset;
  /** Requested NDHW pixels, not bytes or channels. */
  std::optional<uint64_t> pixels_per_column_elements;
  /** Requested channel elements per pixel, not byte footprint. */
  std::optional<uint64_t> channels_per_pixel_elements;
  /** Independently known available NDHW pixels. */
  std::optional<uint64_t> available_ndhw_pixels;
  /** Packed Box-Size[0] supplied directly in bytes. */
  std::optional<uint64_t> inner_box_bytes;
  /** Packed Tensor-Size[0] supplied directly in bytes. */
  std::optional<uint64_t> inner_tensor_bytes;
  /** Total accessed box size supplied directly in bytes. */
  std::optional<uint64_t> accessed_box_bytes;
  /** Address of the opaque 128-byte MAP object; not tensor data. */
  std::optional<uint64_t> map_object_address_bytes;
  /** Global tensor data base address, distinct from MAP object. */
  std::optional<uint64_t> data_base_address_bytes;
  /** Address of the accessed data box. */
  std::optional<uint64_t> accessed_box_address_bytes;
  /** Shared destination address for swizzle alignment and pattern offset. */
  std::optional<uint64_t> shared_destination_address_bytes;
  /** Independent reduction scalar interpretation; no guessed Table 33 bridge. */
  std::optional<base::ScalarType> reduction_interpretation;
};

/** Rule outcome distinguishes a checked relation from missing provenance. */
enum class TensorKnownFactStatus : uint8_t {
  Checked,
  Violated,
  Unresolved,
  NotApplicable,
};

/** One generated-rule result; its ID is supplied by the focused renderer. */
struct TensorKnownFactOutcome {
  /** Catalog ID 1..22; never an aggregate descriptor-validity bit. */
  uint8_t rule_id = 0;
  /** Result for this one relation only. */
  TensorKnownFactStatus status = TensorKnownFactStatus::Unresolved;
  /** Named missing fact, contradiction, or sourced predicate. */
  std::string detail;
  /** Optional repeating-pattern offset, not a new pointer-alignment gate. */
  std::optional<uint64_t> derived_value;
};

/** Pure owned result; it does not certify the referenced descriptor's bytes. */
struct TensorKnownFactsReport {
  /** One result per generated rule, in stable catalog order. */
  std::vector<TensorKnownFactOutcome> outcomes;
  /** Invalid caller enum, arity, or source/bits consistency diagnostics. */
  std::vector<std::string> diagnostics;
};

/** Owned projection from one selected tensor Cp form.
 *
 * Borrowed checker descriptors, target features and operand views stay inside
 * the projection call. An absent access with diagnostics denotes malformed
 * selected metadata; a non-tensor Cp form has neither access nor diagnostics.
 */
struct TensorKnownSelectedProjection {
  /** Copied selected-use facts, independent of the source instruction lifetime. */
  std::optional<TensorKnownAccessContext> access;
  /** Malformed selected tag, storage or converted-value provenance. */
  std::vector<std::string> diagnostics;
};

/** Copy selected tensor-use facts after validating typed storage and tags. */
[[nodiscard]] TensorKnownSelectedProjection project_tensor_known_access_context(
    const Cp& instruction, const checker::Context& context);

/** Query declaration whose body is emitted from the focused rule catalog.
 *
 * The standard generation plan supplies its private out-of-line definition.
 */
[[nodiscard]] TensorKnownFactsReport validate_tensor_access_facts(
    const TensorKnownAccessContext& context, const TensorMapKnownFacts& facts);

}  // namespace ptx_frontend::resolved_ir

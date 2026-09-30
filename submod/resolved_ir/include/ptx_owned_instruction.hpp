#pragma once

#include <concepts>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>

namespace ptx_frontend::resolved_ir {

/** Exact generated record shape accepted by owner borrowing and boxing. */
template <typename T>
concept OwnedOpcodeRecord = requires {
  typename T::VariantType;
  requires std::is_scoped_enum_v<typename T::VariantType>;
  {
    T::get_resolved_descriptor()
  } -> std::same_as<const check_end::ResolvedInstructionDescriptor&>;
};

namespace detail {

/** Internal foundation borrow used only during synchronous module validation. */
struct OwnedReferenceView {
  /** Exact C++ type of the borrowed foundation payload. */
  const std::type_info& type;
  /** Borrowed typed payload; valid until payload replacement or destruction, including across owner moves. */
  const void* payload;
  /** Borrowed source locations; callers must use them within the callback. */
  std::span<const SourceRange> locations;
  /** Generated policy for an address symbol reference. */
  checker::AddressSymbolResolutionPolicy address_policy;
};

/** Internal synchronous sink for reference-bearing foundation values. */
struct OwnedReferenceSink {
  /** Caller state, valid for the duration of one visit. */
  void* state;
  /** Called synchronously for each reference in descriptor order; null skips visiting. */
  void (*accept)(void*, OwnedReferenceView);
};

/** Foundation payloads whose reference structure the module validator understands. */
template <typename T>
concept OwnedReferencePayload =
    std::same_as<T, ResolvedRegisterRef> ||
    std::same_as<T, ResolvedMbarrierStateToken> ||
    std::same_as<T, ResolvedRegisterOrSink> || std::same_as<T, RegOrImm> ||
    std::same_as<T, ResolvedShflSyncDestination> ||
    std::same_as<T, ResolvedPredicatePair> ||
    std::same_as<T, ResolvedPredicatePairOrSink> ||
    std::same_as<T, ResolvedPredicateOrSink> ||
    std::same_as<T, ResolvedMovSource> ||
    std::same_as<T, ResolvedCpAsyncSourceControl> ||
    std::same_as<T, ResolvedPredicate> ||
    std::same_as<T, ResolvedPredicateSource> ||
    std::same_as<T, ResolvedBranchTarget> ||
    std::same_as<T, ResolvedBranchTargetSet> ||
    std::same_as<T, ResolvedVectorRegisterRef> ||
    std::same_as<T, ResolvedSymbolRef> || std::same_as<T, ResolvedAddress> ||
    std::same_as<T, ResolvedRegisterVector> ||
    std::same_as<T, ResolvedTensorCoordinate> ||
    std::same_as<T, ResolvedFunctionRef> ||
    std::same_as<T, ResolvedIndirectCallee> ||
    std::same_as<T, ResolvedCallParameterRef> ||
    std::same_as<T, ResolvedCallArguments>;

/** Immutable operations shared by every owner of one exact opcode record type. */
struct OwnedInstructionOps {
  /** Exact payload type identity for checked borrowing. */
  const std::type_info& type;
  /** Destroy a uniquely owned typed record. */
  void (*destroy)(void*) noexcept;
  /** Deep-copy a typed record; allocation or payload copy may throw. */
  void* (*clone)(const void*);
  /** Canonical opcode spelling independent of the AST. */
  std::string_view opcode;
  /** Run the original generated typed checker. */
  checker::CheckResult (*check)(const void*, const checker::Context&);
  /** Visit borrowed foundation reference values in generated order. */
  void (*references)(const void*, OwnedReferenceSink);
};
struct OwnerAccess;
}  // namespace detail

/** Deep-copyable two-pointer owner of one generated typed instruction record.
 *
 * A default-constructed or moved-from owner is empty. Moving an owner or
 * growing its vector preserves pointers borrowed from its payload. Replacing
 * or destroying that payload ends the borrow. The owning module rejects empty
 * entries during validation.
 */
class OwnedInstruction {
 public:
  /** Construct an empty owner. */
  OwnedInstruction() noexcept = default;
  /** Deep-copy the typed payload. */
  OwnedInstruction(const OwnedInstruction& other)
      : payload_(other.payload_ ? other.ops_->clone(other.payload_) : nullptr),
        ops_(other.ops_) {}
  /** Transfer ownership without moving the typed payload. */
  OwnedInstruction(OwnedInstruction&& other) noexcept
      : payload_(std::exchange(other.payload_, nullptr)),
        ops_(std::exchange(other.ops_, nullptr)) {}
  /** Box a generated opcode record through its per-op bridge. */
  template <typename T>
    requires(OwnedOpcodeRecord<std::remove_cvref_t<T>> &&
             requires(T&& value) { box_instruction(std::forward<T>(value)); })
  OwnedInstruction(T&& value)
      : OwnedInstruction(box_instruction(std::forward<T>(value))) {}
  /** Release the unique typed payload. */
  ~OwnedInstruction() { reset(); }
  /** Replace by an independent deep copy, preserving the old value on failure. */
  OwnedInstruction& operator=(const OwnedInstruction& other) {
    if (this != &other) {
      OwnedInstruction copy(other);
      swap(copy);
    }
    return *this;
  }
  /** Release the old payload and transfer ownership from the source. */
  OwnedInstruction& operator=(OwnedInstruction&& other) noexcept {
    if (this != &other) {
      reset();
      payload_ = std::exchange(other.payload_, nullptr);
      ops_ = std::exchange(other.ops_, nullptr);
    }
    return *this;
  }
  /** Report whether this owner holds a generated record. */
  [[nodiscard]] explicit operator bool() const noexcept {
    return payload_ != nullptr;
  }
  /** Return the canonical opcode, or an empty spelling for an empty owner. */
  [[nodiscard]] std::string_view opcode_name() const noexcept {
    return ops_ ? ops_->opcode : std::string_view{};
  }
  /** Check the typed payload; an empty owner produces a module mismatch. */
  [[nodiscard]] checker::CheckResult check(
      const checker::Context& context) const {
    if (payload_)
      return ops_->check(payload_, context);
    return std::unexpected(checker::CheckDiagnostics{{
        .kind = checker::CheckDiagnosticKind::ModuleSourceMismatch,
        .range = context.instruction_range,
        .message = "Resolved module contains an empty instruction owner.",
    }});
  }
  /** Visit foundation references synchronously; do not mutate the payload reentrantly. */
  void visit_references(detail::OwnedReferenceSink sink) const {
    if (payload_ && sink.accept)
      ops_->references(payload_, sink);
  }
  /** Borrow the exact generated opcode record, or return null on mismatch. */
  template <typename T>
    requires OwnedOpcodeRecord<T>
  [[nodiscard]] T* get_if() noexcept {
    return ops_ && ops_->type == typeid(T) ? static_cast<T*>(payload_)
                                           : nullptr;
  }
  /** Borrow the exact generated opcode record, or return null on mismatch. */
  template <typename T>
    requires OwnedOpcodeRecord<T>
  [[nodiscard]] const T* get_if() const noexcept {
    return ops_ && ops_->type == typeid(T) ? static_cast<const T*>(payload_)
                                           : nullptr;
  }
  /** Exchange fixed-size ownership handles. */
  void swap(OwnedInstruction& other) noexcept {
    std::swap(payload_, other.payload_);
    std::swap(ops_, other.ops_);
  }

 private:
  friend struct detail::OwnerAccess;
  /** Unique allocation containing one exact generated record. */
  void* payload_{};
  /** Static immutable table owned by that opcode's generated translation unit. */
  const detail::OwnedInstructionOps* ops_{};
  /** Adopt a typed allocation and its matching immutable table. */
  OwnedInstruction(void* payload,
                   const detail::OwnedInstructionOps* ops) noexcept
      : payload_(payload), ops_(ops) {}
  /** Release the current typed allocation and restore the empty invariant. */
  void reset() noexcept {
    if (payload_)
      ops_->destroy(payload_);
    payload_ = nullptr;
    ops_ = nullptr;
  }
};

namespace detail {
/** Narrow construction bridge used by generated opcode translation units. */
struct OwnerAccess {
  /** Adopt an allocation whose type exactly matches the supplied static table. */
  static OwnedInstruction adopt(void* payload,
                                const OwnedInstructionOps* ops) noexcept {
    return OwnedInstruction(payload, ops);
  }
};
}  // namespace detail

}  // namespace ptx_frontend::resolved_ir

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/common/source_loc.hpp>

namespace ptx_frontend::resolved_ir {

/** Addressable declaration spaces; parameters and registers have separate APIs. */
enum class StorageSpace : uint8_t { Global, Constant, Shared, Local };

/** Opaque object identities whose physical size and alignment are not specified. */
enum class StorageOpaqueType : uint8_t { Texture, Sampler, Surface };

/** A fundamental declaration type or opaque identity; instruction-only formats are rejected. */
using StorageElementType = std::variant<base::ScalarType, StorageOpaqueType>;

/** Whether this source declaration defines storage or refers to external storage. */
enum class StorageDeclarationKind : uint8_t { Definition, External };

/** Initial contents promised by this declaration, without allocating a byte image. */
enum class StorageInitializationKind : uint8_t {
  /** No initial contents promised: shared/local storage or an opaque object. */
  Uninitialized,
  /** Implicit zero initialization of a global/constant scalar object or aggregate. */
  Zero,
  /** Explicit source initializer: zero-fill the object, then apply sparse entries. */
  Explicit,
  /** External declaration: this module supplies no initial contents. */
  External,
};

/** Address interpretation for a relocation; no value is a simulated address. */
enum class StorageAddressKind : uint8_t { StateSpace, Generic, Function };

/**
 * Owned constant bits for one initializer element, up to 128 bits.
 * Word significance is independent of host byte order; the declaration gives
 * the element width. Integer expressions are evaluated at 64 bits before widening.
 */
struct StorageConstant {
  /** Low-order bits; unused high bits are zero for types narrower than 64 bits. */
  uint64_t bits{};
  /** Bits 64..127; zero for elements of at most 64 bits. For .b128, signed
   * negative expression results are sign-extended; all other results zero-extend.
   */
  uint64_t high_bits{};
};

/** A link/runtime-resolved symbol address with an optional byte extraction. */
struct StorageRelocation {
  /** Identity in the owning ResolvedModule's symbol table. */
  binding::SymbolId symbol_id;
  /** Member of a parameterized declaration; absent for ordinary symbols. */
  std::optional<uint32_t> parameterized_index;
  StorageAddressKind address_kind{};
  /** Signed byte addend represented as 64-bit two's-complement bits. */
  uint64_t addend_bits{};
  /** Raw mask() immediate: 0xff shifted by 0, 8, ..., 56; applied after the addend. */
  std::optional<uint64_t> byte_mask;
};

/** One explicitly supplied scalar initializer, flattened in declaration order. */
struct StorageInitializerElement {
  /** Byte offset within one declared object, not an address or allocation offset. */
  uint64_t byte_offset{};
  std::variant<StorageConstant, StorageRelocation> value;
  SourceRange range;
};

/** Owned allocation inputs for one source declarator; contains no AST pointers. */
struct ResolvedStorageDeclaration {
  /** Stable within the module; repeated compatible extern declarations share it. */
  binding::SymbolId symbol_id;
  /** Lexical declaration scope, including nested blocks. */
  binding::ScopeId scope_id;
  /** Enclosing function identity, or no value for a module-level declaration. */
  std::optional<binding::SymbolId> owner_function;
  StorageSpace space{};
  StorageElementType element_type;
  /** Scalar lanes per array element: 1, 2, or 4. */
  uint8_t vector_width = 1;
  /** Outer-to-inner element counts; an external unsized first dimension is null. */
  std::vector<std::optional<uint64_t>> array_extents;
  /** Bytes per object, including vectors/arrays; absent for opaque or unsized data. */
  std::optional<uint64_t> byte_extent;
  /** Effective alignment in bytes; absent only when an opaque type has no default. */
  std::optional<uint64_t> alignment;
  /** Source's explicit byte alignment, distinct from an inferred natural alignment. */
  std::optional<uint64_t> explicit_alignment;
  binding::SymbolLinkage linkage{};
  StorageDeclarationKind declaration_kind{};
  /** True only for an external shared declaration with an unsized first dimension. */
  bool is_dynamic_shared{};
  /** Number of separately named objects; byte_extent is per object, not their sum. */
  std::optional<uint32_t> parameterized_count;
  /** Source attribute only; downstream decides allocation policy. */
  bool is_managed{};
  /** Upper/lower UUID halves for .unified; no host/device address is fabricated. */
  std::optional<std::array<uint64_t, 2>> unified_id;
  StorageInitializationKind initialization{};
  /** Explicit scalar entries; omitted positions in Explicit mode are zero-filled. */
  std::vector<StorageInitializerElement> initializer;
  /** Range of this declaration occurrence, including compatible redeclarations. */
  SourceRange range;
};

}  // namespace ptx_frontend::resolved_ir

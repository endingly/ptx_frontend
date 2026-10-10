#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/base/ptx_ast_types.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/common/source_loc.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_unified_id.hpp>

namespace ptx_frontend::resolved_ir {

/** Addressable declaration spaces; parameters and registers have separate APIs. */
enum class StorageSpace : uint8_t { Global, Constant, Shared, Local };

/** Source-compatible name for the shared opaque identity domain. */
using StorageOpaqueType = base::OpaqueResourceKind;

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
  /** Named opaque-resource fields, with no implied byte image or defaults. */
  OpaqueStatic,
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

/** Static opaque member names from the PTX resource declaration contract. */
enum class OpaqueStaticField : uint8_t {
  Width,
  Height,
  Depth,
  ChannelDataType,
  ChannelOrder,
  NormalizedCoords,
  FilterMode,
  AddressMode0,
  AddressMode1,
  AddressMode2,
  ArraySize,
  NumMipmapLevels,
  NumSamples,
  ForceUnnormalizedCoords,
  MemoryLayout
};

/** Named PTX and OpenCL enumerants admitted for static opaque members. */
enum class OpaqueStaticEnum : uint16_t {
  Nearest,
  Linear,
  Wrap,
  Mirror,
  ClampOgl,
  ClampToEdge,
  ClampToBorder,
  ClSnormInt8 = 0x10d0,
  ClSnormInt16,
  ClUnormInt8,
  ClUnormInt16,
  ClUnormShort565,
  ClUnormShort555,
  ClUnormInt101010,
  ClSignedInt8,
  ClSignedInt16,
  ClSignedInt32,
  ClUnsignedInt8,
  ClUnsignedInt16,
  ClUnsignedInt32,
  ClHalfFloat,
  ClFloat,
  ClR = 0x10b0,
  ClA,
  ClRg,
  ClRa,
  ClRgb,
  ClRgba,
  ClBgra,
  ClArgb,
  ClIntensity,
  ClLuminance
};

/** One source-specified static resource member, with no byte offset. */
struct OpaqueStaticMember {
  /** Typed member identity independent of its source spelling. */
  OpaqueStaticField field{};
  /** Numeric constant or recognized named enumerant. */
  std::variant<uint64_t, OpaqueStaticEnum> value;
  /** Assignment range retained for source checks and diagnostics. */
  SourceRange range;
};

/** One statically initialized opaque object within an optional array shape. */
struct OpaqueStaticObject {
  /** Zero-based aggregate position on each declared array axis. */
  std::vector<uint64_t> indices;
  /** Source-ordered named fields; omitted runtime defaults remain unspecified. */
  std::vector<OpaqueStaticMember> members;
  /** Range of this object's named-member initializer. */
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
  /** Source used the deprecated `.tex .u32` spelling for a texture identity. */
  bool legacy_texture = false;
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
  /** Typed `.unified` UUID; no host/device address is fabricated. */
  std::optional<ResolvedUnifiedId> unified_id;
  StorageInitializationKind initialization{};
  /** Explicit scalar entries; omitted positions in Explicit mode are zero-filled. */
  std::vector<StorageInitializerElement> initializer;
  /** Ordered per-object named resource data; never flattened to byte offsets. */
  std::vector<OpaqueStaticObject> opaque_static_objects;
  /** Range of this declaration occurrence, including compatible redeclarations. */
  SourceRange range;
};

}  // namespace ptx_frontend::resolved_ir

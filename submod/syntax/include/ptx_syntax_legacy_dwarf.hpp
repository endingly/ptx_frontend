#pragma once

#include <cstdint>
#include <ptx_frontend/syntax/ptx_syntax_sections.hpp>
#include <variant>
#include <vector>

namespace ptx_frontend::syntax_ast {
/** Unsigned serialization widths; these are not executable PTX scalar types. */
enum class LegacyDwarfDataKind : uint8_t { Byte, FourByte, Quad };
/** One closed numeric-list or unlinked single-symbol serialization production. */
struct AstLegacyDwarfData {
  /** Width selected by the exact written directive. */
  LegacyDwarfDataKind kind{};
  /** Owned .byte, .4byte or .quad spelling and physical range. */
  AstSyntax directive;
  /** Nonempty original hex words, or one symbolic label for the wider forms. */
  std::variant<std::vector<AstSyntax>, AstSyntax> value;
  /** Whole data payload, containing its directive and all value leaves. */
  SourceRange range;
};
/** Closed legacy section header; no arbitrary host-assembler flags or expressions. */
struct AstLegacyDwarfSectionHeader {
  /** Exact .section keyword, symbolic name, empty quoted flags and @progbits leaves. */
  AstSyntax directive, name, flags, type_prefix, type_name;
  /** Whole header payload, containing every separate token leaf. */
  SourceRange range;
};
/** Owning typed serialization record independent of module/instruction AST definitions. */
struct AstLegacyDwarfRecord {
  /** Original canonical @@DWARF marker and physical range. */
  AstSyntax marker;
  /** One structurally complete legacy production. */
  std::variant<AstLegacyDwarfData, AstLegacyDwarfSectionHeader> payload;
  /** Whole marker/payload extent, including an optional source semicolon. */
  SourceRange range;
};
}  // namespace ptx_frontend::syntax_ast

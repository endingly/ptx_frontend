#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <ptx_frontend/common/source_loc.hpp>

namespace ptx_frontend::syntax_ast {

/** Source spelling retained by a semantic syntax leaf. */
struct AstSyntax {
  std::string text;
  SourceRange range;
};

/** Nominal bit width of a section data production, independent of linked addresses. */
enum class AstSectionWidth : uint8_t { B8 = 8, B16 = 16, B32 = 32, B64 = 64 };
/** An owning signed integer leaf without a truncated numeric cache. */
struct AstSectionInteger {
  /** Original sign spelling and range, absent for an unsigned written leaf. */
  std::optional<AstSyntax> sign;
  /** Original magnitude spelling; full UINT64_MAX remains representable. */
  AstSyntax magnitude;
  SourceRange range;
};
/** An owning label definition; labels are not executable instruction symbols. */
struct AstSectionLabelDefinition {
  AstSyntax name;
  SourceRange range;
};
/** A single symbolic address whose linked value may remain unknown. */
struct AstSectionSymbol {
  AstSyntax name;
};
/** A symbolic byte addend retaining both written operators independently. */
struct AstSectionAddend {
  AstSyntax name;
  /** Binary plus/minus before the optionally signed integer leaf. */
  AstSyntax operation;
  AstSectionInteger value;
};
/** Two labels whose unique logical-section locality can be checked statically. */
struct AstSectionDifference {
  AstSyntax left;
  AstSyntax operation;
  AstSyntax right;
};
/** One owning typed data entry with a selected grammar production. */
struct AstSectionData {
  /** Logical storage width; must agree with the retained .bN type spelling. */
  AstSectionWidth width;
  AstSyntax type;
  std::variant<std::vector<AstSectionInteger>, AstSectionSymbol,
               AstSectionAddend, AstSectionDifference>
      value;
  SourceRange range;
};
/** Owning value entries contain no CST IDs, expression trees or source borrows. */
using AstSectionEntry = std::variant<AstSectionLabelDefinition, AstSectionData>;
/** An owning section fragment; equal names identify the same logical section. */
struct AstSectionDirective {
  AstSyntax name;
  /** Typed entries in written order; raw spelling/trivia authority remains the CST. */
  std::vector<AstSectionEntry> payload;
  SourceRange range;
};

}  // namespace ptx_frontend::syntax_ast

#include <concepts>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#ifdef PTX_SNAPSHOT_GENERATED
#include "ptx_parser_driver.hpp"
#endif

namespace {

/** JSON output context; the borrowed CST resolves concrete token spans. */
struct Writer {
  /** Output stream borrowed for the complete snapshot lifetime. */
  std::ostream& out;
  /** Non-owning parsed token buffer, absent while serializing lexer output. */
  const ptx_frontend::syntax_cst::CstFile* file = nullptr;
};

/** Encode source bytes without requiring UTF-8 or dropping embedded NULs. */
void encode(Writer& writer, std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  writer.out << '"';
  for (const unsigned char byte : value) {
    if (byte == '"' || byte == '\\')
      writer.out << '\\' << static_cast<char>(byte);
    else if (byte < 32 || byte >= 127)
      writer.out << "\\u00" << hex[byte >> 4] << hex[byte & 15];
    else
      writer.out << static_cast<char>(byte);
  }
  writer.out << '"';
}

/** Encode an owned source spelling with the same byte-preserving convention. */
void encode(Writer& writer, const std::string& value) {
  encode(writer, std::string_view(value));
}

/** Encode a semantic boolean distinctly from an integer or enumeration. */
void encode(Writer& writer, bool value) {
  writer.out << (value ? "true" : "false");
}

/** Encode integral public IDs and coordinates without character formatting. */
template <std::integral T>
  requires(!std::same_as<T, bool>)
void encode(Writer& writer, T value) {
  writer.out << +value;
}

/** Encode public enum identity by its stable underlying value. */
template <typename T>
  requires std::is_enum_v<T>
void encode(Writer& writer, T value) {
  encode(writer, static_cast<std::underlying_type_t<T>>(value));
}

// Declare every concrete overload before recursive container instantiation.
#define SNAPSHOT_BEGIN(Type) /** Encode all declared public fields. */ \
  void encode(Writer& writer, const Type& value);                      \
  /** Identify an explicitly enumerated public syntax record. */       \
  void snapshotRecord(const Type*);
#define SNAPSHOT_FIELD(Name)
#define SNAPSHOT_END()
#include "snapshot_fields.inc"
#undef SNAPSHOT_BEGIN
#undef SNAPSHOT_FIELD
#undef SNAPSHOT_END

/** Recognize explicitly enumerated public records and scalar identity types. */
template <typename T>
        struct IsSnapshotValue : std::bool_constant < std::integral<T> ||
    std::is_enum_v<T> || std::same_as<T, std::string> ||
    requires(const T* value) {
  snapshotRecord(value);
}>{};
/** Require optional children themselves to have complete snapshot support. */
template <typename T>
struct IsSnapshotValue<std::optional<T>> : IsSnapshotValue<T> {};
/** Require owned children themselves to have complete snapshot support. */
template <typename T>
struct IsSnapshotValue<std::unique_ptr<T>> : IsSnapshotValue<T> {};
/** Require sequence elements themselves to have complete snapshot support. */
template <typename T>
struct IsSnapshotValue<std::vector<T>> : IsSnapshotValue<T> {};
/** Require every public variant alternative to have complete snapshot support. */
template <typename... T>
struct IsSnapshotValue<std::variant<T...>>
    : std::bool_constant<(IsSnapshotValue<T>::value && ...)> {};
/** Require result values and diagnostics to have complete snapshot support. */
template <typename T, typename D>
struct IsSnapshotValue<ptx_frontend::ResultWithDiagnostics<T, D>>
    : std::bool_constant<IsSnapshotValue<T>::value &&
                         IsSnapshotValue<D>::value> {};
/** Supported values are explicit public records, scalars, or their containers. */
template <typename T>
concept SnapshotValue = IsSnapshotValue<T>::value;

/** Encode optional ownership and explicitly distinguish an absent value. */
template <SnapshotValue T>
void encode(Writer& writer, const std::optional<T>& value);
/** Encode exclusively owned recursive syntax without exposing pointer addresses. */
template <SnapshotValue T>
void encode(Writer& writer, const std::unique_ptr<T>& value);
/** Encode source-ordered homogeneous public fields. */
template <SnapshotValue T>
void encode(Writer& writer, const std::vector<T>& values);
/** Encode variant identity as well as the complete selected payload. */
template <SnapshotValue... T>
void encode(Writer& writer, const std::variant<T...>& value);
/** Encode optional result value and ordered diagnostics together. */
template <SnapshotValue T, SnapshotValue D>
void encode(Writer& writer,
            const ptx_frontend::ResultWithDiagnostics<T, D>& value);

/** Encode optional ownership and explicitly distinguish an absent value. */
template <SnapshotValue T>
void encode(Writer& writer, const std::optional<T>& value) {
  if (value)
    encode(writer, *value);
  else
    writer.out << "null";
}

/** Encode exclusively owned recursive syntax without exposing pointer addresses. */
template <SnapshotValue T>
void encode(Writer& writer, const std::unique_ptr<T>& value) {
  if (value)
    encode(writer, *value);
  else
    writer.out << "null";
}

/** Encode source-ordered homogeneous public fields. */
template <SnapshotValue T>
void encode(Writer& writer, const std::vector<T>& values) {
  writer.out << '[';
  bool first = true;
  for (const auto& value : values) {
    if (!first)
      writer.out << ',';
    first = false;
    encode(writer, value);
  }
  writer.out << ']';
}

/** Encode variant identity as well as the complete selected payload. */
template <SnapshotValue... T>
void encode(Writer& writer, const std::variant<T...>& value) {
  writer.out << "{\"alternative\":" << value.index() << ",\"value\":";
  std::visit([&](const auto& child) { encode(writer, child); }, value);
  writer.out << '}';
}

/** Encode optional result value and ordered diagnostics together. */
template <SnapshotValue T, SnapshotValue D>
void encode(Writer& writer,
            const ptx_frontend::ResultWithDiagnostics<T, D>& value) {
  writer.out << "{\"has_value\":";
  encode(writer, value.has_value());
#define SNAPSHOT_RESULT_FIELD(Name) \
  writer.out << ",\"" #Name "\":";  \
  encode(writer, value.Name)
  SNAPSHOT_RESULT_FIELD(value);
  SNAPSHOT_RESULT_FIELD(diagnostics);
#undef SNAPSHOT_RESULT_FIELD
  writer.out << '}';
}

/** Append a resolved source range for every concrete span, beyond token IDs. */
void resolvedRange(Writer& writer,
                   const ptx_frontend::syntax_cst::CstTokenRange& range) {
  if (writer.file) {
    writer.out << ",\"source_range\":";
    encode(writer, writer.file->sourceRange(range));
  }
}

/** Non-span public nodes require no computed fields. */
template <typename T>
  requires SnapshotValue<T> &&
           (!std::same_as<T, ptx_frontend::syntax_cst::CstTokenRange>)
void resolvedRange(Writer&, const T&) {}

#define SNAPSHOT_BEGIN(Type) /** Encode all declared public fields. */ \
  void encode(Writer& writer, const Type& value) {                     \
    writer.out << "{\"_type\":\"" #Type "\"";
#define SNAPSHOT_FIELD(Name)       \
  writer.out << ",\"" #Name "\":"; \
  encode(writer, value.Name);
#define SNAPSHOT_END()          \
  resolvedRange(writer, value); \
  writer.out << '}';            \
  }
#include "snapshot_fields.inc"
#undef SNAPSHOT_BEGIN
#undef SNAPSHOT_FIELD
#undef SNAPSHOT_END

}  // namespace

/** Snapshot exactly one instruction/module from raw stdin in a fresh process. */
int main(int argc, char** argv) {
  if (argc != 2 || (std::string_view(argv[1]) != "instruction" &&
                    std::string_view(argv[1]) != "module")) {
    std::cerr << "usage: parser_snapshot instruction|module < source.ptx\n";
    return 2;
  }
  const bool module = std::string_view(argv[1]) == "module";
  const std::string source{std::istreambuf_iterator<char>(std::cin), {}};
  ptx_frontend::PtxLexer lexer(source);
  std::vector<ptx_frontend::PtxToken> tokens;
  do {
    tokens.push_back(lexer.next());
  } while (tokens.back().kind != ptx_frontend::TokenKind::Eof);

  ptx_frontend::PtxCstParser parser(source);
#ifdef PTX_SNAPSHOT_GENERATED
  auto cst = ptx_frontend::detail::parseGenerated(
      source, module ? ptx_frontend::detail::EntryMode::Module
                     : ptx_frontend::detail::EntryMode::Instruction);
#else
  auto cst = module ? parser.parseModule() : parser.parseInstruction();
#endif
  Writer writer{std::cout, cst ? &*cst : nullptr};
  writer.out << "{\"schema\":1,\"source\":";
  encode(writer, source);
  writer.out << ",\"lexer_tokens\":";
  encode(writer, tokens);
  writer.out << ",\"cst\":";
  encode(writer, cst);
  writer.out << ",\"source_text\":";
  if (cst)
    encode(writer, cst->sourceText());
  else
    writer.out << "null";
  writer.out << ",\"lowered\":";
  if (!cst)
    writer.out << "null";
  else if (module)
    encode(writer, ptx_frontend::lowerSyntaxModule(*cst));
  else
    encode(writer, ptx_frontend::lowerSyntaxInstruction(*cst));
  writer.out << ",\"facade\":";
  ptx_frontend::PtxSyntaxParser facade(source);
  if (module)
    encode(writer, facade.parseModule());
  else
    encode(writer, facade.parseInstruction());
  writer.out << "}\n";
}

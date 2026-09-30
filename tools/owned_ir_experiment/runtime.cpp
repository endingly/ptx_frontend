#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

namespace ir = ptx_frontend::resolved_ir;

/** Parse one corpus without accepting a partial AST or parser diagnostics. */
ptx_frontend::syntax_ast::AstModule parse_clean(std::string_view source) {
  ptx_frontend::PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed.value || !parsed.diagnostics.empty())
    throw std::runtime_error("Corpus parsing failed.");
  return std::move(*parsed.value);
}

/** Count the outer typed record object retained by one owner allocation. */
template <std::size_t Index = 0>
std::size_t payload_bytes(const ir::OwnedInstruction& instruction) {
  if constexpr (Index == std::variant_size_v<ir::InstructionUnion>) {
    throw std::runtime_error("Unknown or empty opcode in resolved corpus.");
  } else {
    using Record = std::variant_alternative_t<Index, ir::InstructionUnion>;
    if (instruction.get_if<Record>())
      return sizeof(Record);
    return payload_bytes<Index + 1>(instruction);
  }
}

/** Read one canonical opcode without constructing a replacement record. */
std::string_view opcode_name(const ir::OwnedInstruction& instruction) {
  return instruction.opcode_name();
}

/** Prevent the compiler from eliding or moving a benchmark result across calls. */
template <typename T>
void keep_result(const T& value) {
#if defined(__clang__) || defined(__GNUC__)
  asm volatile("" : : "g"(&value) : "memory");
#else
  (void)value;
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

/** Warm one operation, then retain five independent batch means in nanoseconds. */
template <typename Operation>
  requires std::invocable<Operation&>
std::array<double, 5> sample_nanoseconds(std::size_t iterations,
                                         Operation&& operation) {
  for (std::size_t index = 0; index < std::max<std::size_t>(1, iterations / 10);
       ++index)
    operation();
  std::array<double, 5> samples{};
  for (double& sample : samples) {
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < iterations; ++index)
      operation();
    const auto end = std::chrono::steady_clock::now();
    sample = std::chrono::duration<double, std::nano>(end - begin).count() /
             static_cast<double>(iterations);
  }
  return samples;
}

/** Print raw batch means and their middle value after sorting a copy. */
void print_samples(std::string_view key, std::array<double, 5> samples) {
  std::cout << ",\"" << key << "_samples_ns\":[";
  for (std::size_t index = 0; index < samples.size(); ++index) {
    if (index)
      std::cout << ',';
    std::cout << samples[index];
  }
  std::sort(samples.begin(), samples.end());
  std::cout << "],\"" << key << "_median_ns\":" << samples[2];
}

/** Write a JSON string, escaping quotes, backslashes, and control bytes. */
void print_json_string(std::string_view value) {
  constexpr char hex_digits[] = "0123456789abcdef";
  std::cout << '"';
  for (const char raw_byte : value) {
    const auto byte = static_cast<unsigned char>(raw_byte);
    switch (byte) {
      case '"':
        std::cout << "\\\"";
        break;
      case '\\':
        std::cout << "\\\\";
        break;
      case '\b':
        std::cout << "\\b";
        break;
      case '\f':
        std::cout << "\\f";
        break;
      case '\n':
        std::cout << "\\n";
        break;
      case '\r':
        std::cout << "\\r";
        break;
      case '\t':
        std::cout << "\\t";
        break;
      default:
        if (byte < 0x20) {
          std::cout << "\\u00" << hex_digits[byte >> 4]
                    << hex_digits[byte & 0x0f];
        } else {
          std::cout << static_cast<char>(byte);
        }
    }
  }
  std::cout << '"';
}

/** Read a corpus, compare its owned operations, and print machine-readable data. */
int run(int argc, char** argv) {
  if (argc != 3)
    throw std::runtime_error(
        "Usage: ptx_owned_ir_runtime CORPUS.ptx ITERATIONS");
  const std::size_t iterations = std::stoull(argv[2]);
  if (iterations == 0)
    throw std::runtime_error("Iteration count must be positive.");
  std::ifstream input(argv[1]);
  if (!input)
    throw std::runtime_error("Corpus file cannot be read.");
  const std::string source(std::istreambuf_iterator<char>{input}, {});
  auto ast = parse_clean(source);
  auto resolved = ir::resolveAndValidateModule(ast);
  if (!resolved)
    throw std::runtime_error("Corpus resolution failed: " +
                             resolved.error().front().message);
  std::size_t instructions = 0;
  std::size_t body_capacity_bytes = 0;
  std::size_t record_payload_bytes = 0;
  for (const auto& function : resolved->functions) {
    instructions += function.body.size();
    body_capacity_bytes +=
        function.body.capacity() * sizeof(ir::OwnedInstruction);
    for (const auto& instruction : function.body)
      record_payload_bytes += payload_bytes(instruction);
  }
  std::uint64_t checksum = 0;
  const auto resolve_samples = sample_nanoseconds(iterations, [&] {
    auto result = ir::resolveAndValidateModule(ast);
    if (!result)
      throw std::runtime_error("Repeated corpus resolution failed.");
    keep_result(result);
    checksum += result->functions.size();
  });
  const auto validate_samples = sample_nanoseconds(iterations, [&] {
    auto result = ir::validateModule(*resolved);
    if (!result)
      throw std::runtime_error("Owned corpus validation failed.");
    keep_result(result);
    checksum += resolved->functions.size();
  });
  const auto copy_samples = sample_nanoseconds(iterations, [&] {
    ir::ResolvedModule copy = *resolved;
    keep_result(copy);
    checksum += copy.functions.size();
  });
  const auto traverse_samples = sample_nanoseconds(iterations, [&] {
    for (const auto& function : resolved->functions)
      for (const auto& instruction : function.body) {
        const auto opcode = opcode_name(instruction);
        keep_result(opcode);
        checksum += opcode.size();
      }
  });
  std::cout << "{\"configuration\":\"owned\",\"corpus\":";
  print_json_string(argv[1]);
  std::cout << ",\"iterations\":" << iterations
            << ",\"functions\":" << resolved->functions.size()
            << ",\"instructions\":" << instructions
            << ",\"instruction_slot_bytes\":" << sizeof(ir::OwnedInstruction)
            << ",\"body_capacity_bytes\":" << body_capacity_bytes
            << ",\"outer_record_object_bytes\":" << record_payload_bytes;
  print_samples("resolve_and_validate", resolve_samples);
  print_samples("validate_owned", validate_samples);
  print_samples("copy_module", copy_samples);
  print_samples("traverse_opcodes", traverse_samples);
  std::cout << ",\"checksum\":" << checksum << "}\n";
  return 0;
}

}  // namespace

/** Report benchmark errors to the caller instead of producing partial numbers. */
int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

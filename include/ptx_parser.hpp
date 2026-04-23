#pragma once
#include <expected.hpp>
#include <string>
#include "ptx_ir/instr.hpp"
#include "ptx_lexer.hpp"

/* 
Module
 ├── .version  .target  .address_size    (module header)
 └── Directive*
      ├── .extern / .visible / .weak  +  (Variable | Function)
      └── Function
           ├── MethodDeclaration         (.entry / .func + params)
           ├── TuningDirective*          (.maxnreg / .maxntid / ...)
           └── Statement*
                ├── Label                (ident:)
                ├── Variable             (.reg .b32 %r<4>;)
                └── Instruction          (@pred  opcode  operands;)
*/

namespace ptx_frontend {

struct ParseError {
  int line;
  int column;
  std::string message;
};

/**
 * Parse a full PTX module from source text.
 *
 *   auto result = parse_module(src);
 *   if (!result) { /* handle result.error() *\/ }
 *   Module& mod = *result;
 */
tl::expected<Module, ParseError> parse_module(std::string_view src);

}  // namespace ptx_frontend
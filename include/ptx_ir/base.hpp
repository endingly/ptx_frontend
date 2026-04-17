#pragma once
#include <cstdint>
#include <cstring>
#include <expected.hpp>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>
#include "utils.hpp"

namespace ptx_frontend {

using tl::expected;
using tl::unexpected;

enum class ScalarType : uint8_t {
  U8,
  U8x4,
  U16,
  U16x2,
  U32,
  U64,
  S8,
  S8x4,
  S16,
  S16x2,
  S32,
  S64,
  B8,
  B16,
  B32,
  B64,
  B128,
  F16,
  F16x2,
  F32,
  F32x2,
  F64,
  BF16,
  BF16x2,
  E4m3x2,
  E5m2x2,
  Pred,
  TF32,  // .tf32  — 19-bit mantissa, sm_80+ tensor core
  E4m3,  // .e4m3  — FP8 single element (non packed)
  E5m2,  // .e5m2  — FP8 single element (non packed)
};

enum class ScalarKind { Bit, Unsigned, Signed, Float, Pred };

std::string to_string(ScalarType t);
ScalarKind scalar_kind(ScalarType t);
uint8_t scalar_size_of(ScalarType t);

struct ScalarTypeT {
  ScalarType type;
};

struct VectorTypeT {
  uint8_t count;
  ScalarType type;
};

struct ArrayTypeT {
  std::optional<uint8_t> vector_size;  // non-zero if present
  ScalarType type;
  std::vector<uint32_t> dims;
};

using Type = std::variant<ScalarTypeT, VectorTypeT, ArrayTypeT>;

inline Type make_scalar(ScalarType t) {
  return ScalarTypeT{t};
}

inline Type make_vector(uint8_t n, ScalarType t) {
  return VectorTypeT{n, t};
}

enum class StateSpace : uint8_t {
  Reg,
  Const,
  Global,
  Local,
  Param,
  Shared,
  SharedCluster,
  Tex,
  Generic,
  Tmem  // for blackwell(sm_100+)
};

enum class MemScope : uint8_t { Cta, Cluster, Gpu, Sys };

struct ImmediateValue {
  std::variant<uint64_t, int64_t, float, double> value;

  template <typename T>
    requires std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t> ||
             std::is_same_v<T, float> || std::is_same_v<T, double>
  static ImmediateValue from_value(T v) {
    ImmediateValue i;
    i.value = v;
    return i;
  }

  uint64_t get_bits() const {
    return std::visit(
        [](auto v) -> uint64_t {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, uint64_t>) {
            return v;
          } else if constexpr (std::is_same_v<T, int64_t>) {
            return static_cast<uint64_t>(v);
          } else if constexpr (std::is_same_v<T, float>) {
            uint32_t b;
            std::memcpy(&b, &v, sizeof(b));
            return b;
          } else {  // double
            uint64_t b;
            std::memcpy(&b, &v, sizeof(b));
            return b;
          }
        },
        value);
  }

  bool operator==(const ImmediateValue& other) const = default;
};

using Ident = std::string_view;

template <typename Id>
concept IdentLike = std::copyable<Id> and std::equality_comparable<Id>;

template <IdentLike Id>
struct RegOrImmediate {
  std::variant<ImmediateValue, Id> value;

  static RegOrImmediate Reg(Id id) {
    RegOrImmediate r;
    r.value = id;
    return r;
  }

  static RegOrImmediate Imm(ImmediateValue v) {
    RegOrImmediate r;
    r.value = v;
    return r;
  }

  bool operator==(const RegOrImmediate& other) const = default;
};

template <IdentLike Id>
struct ParsedOperand {

  //     Reg,        // %r0
  //     RegOffset,  // %r0+4
  //     Imm,        // 0x1234
  //     VecMember,  // %r0.x  (.x=0 .y=1 .z=2 .w=3)
  //     VecPack,    // {%r0, %r1, %r2, %r3}

  struct RegOffset {
    Id base;
    int32_t offset;

    bool operator==(const RegOffset& other) const = default;
  };

  struct VecMemberIdx {
    Id base;
    uint8_t member;  // .x=0 .y=1 .z=2 .w=3

    bool operator==(const VecMemberIdx& other) const = default;
  };

  using VecPack = std::vector<RegOrImmediate<Id>>;

  // start for operand type traits
  static constexpr bool is_operand = true;
  using id_type = Id;
  // end for operand type traits

  std::variant<RegOffset, VecMemberIdx, RegOrImmediate<Id>, ImmediateValue,
               VecPack>
      value;

  template <typename T>
    requires utils::is_one_of<T, RegOffset, VecMemberIdx, RegOrImmediate<Id>,
                              ImmediateValue, VecPack>
  static ParsedOperand from_value(T v) {
    ParsedOperand p;
    if constexpr (std::is_same_v<T, VecPack>) {
      p.value = std::move(v);
    } else {
      p.value = v;
    }
    return p;
  }

  template <IdentLike NewId, typename Err, typename Fn>
    requires std::invocable<Fn, Id>
  expected<ParsedOperand<NewId>, Err> map_id(Fn&& fn) const {
    /*  doc:
        variant member              map_id operantor
        ─────────────────────────────────────────────────────
        RegOffset{base, offset}     fn(base) → new base，offset 不变
        VecMemberIdx{base,member}   fn(base) → new base，member 不变
        RegOrImmediate<Id>          is_reg ? fn(reg) : copy imm directly
        ImmediateValue              not have Id, copy directly
        VecPack                     process each element, is_reg ? fn : copy directly
     */
    return std::visit(
        [&](const auto& v) -> expected<ParsedOperand<NewId>, Err> {
          using T = std::decay_t<decltype(v)>;

          // %r0+4
          if constexpr (std::is_same_v<T, RegOffset>) {
            auto r = fn(v.base);
            if (!r)
              return tl::unexpected(r.error());
            return ParsedOperand<NewId>::from_value(
                typename ParsedOperand<NewId>::RegOffset{*r, v.offset});
          }

          // %r0.x
          else if constexpr (std::is_same_v<T, VecMemberIdx>) {
            auto r = fn(v.base);
            if (!r)
              return tl::unexpected(r.error());
            return ParsedOperand<NewId>::from_value(
                typename ParsedOperand<NewId>::VecMemberIdx{*r, v.member});
          }

          // %r0(Reg) or imm like 0xffff
          else if constexpr (std::is_same_v<T, RegOrImmediate<Id>>) {
            if (std::holds_alternative<Id>(v.value)) {
              auto r = fn(std::get<Id>(v.value));
              if (!r)
                return unexpected(r.error());
              return ParsedOperand<NewId>::from_value(
                  RegOrImmediate<NewId>::Reg(*r));
            } else {
              return ParsedOperand<NewId>::from_value(
                  RegOrImmediate<NewId>::Imm(
                      std::get<ImmediateValue>(v.value)));
            }
          }

          // 0x1234, not have id
          else if constexpr (std::is_same_v<T, ImmediateValue>) {
            return ParsedOperand<NewId>::from_value(v);
          }

          // {%r0, %r1, ...}
          else if constexpr (std::is_same_v<T, VecPack>) {
            typename ParsedOperand<NewId>::VecPack new_vec;
            new_vec.reserve(v.size());
            for (const auto& item : v) {
              if (std::holds_alternative<Id>(item.value)) {
                auto r = fn(std::get<Id>(item.value));
                if (!r)
                  return tl::unexpected(r.error());
                new_vec.push_back(RegOrImmediate<NewId>::Reg(*r));
              } else {
                new_vec.push_back(RegOrImmediate<NewId>::Imm(
                    std::get<ImmediateValue>(item.value)));
              }
            }
            return ParsedOperand<NewId>::from_value(std::move(new_vec));
          }
        },
        value);
  }

  bool operator==(const ParsedOperand&) const = default;
};

using ParsedOp = ParsedOperand<Ident>;

// ============================================================
// § 7  Predicate guard  ←  ast.rs::PredAt
// ============================================================

struct PredAt {
  bool negate;  // @!p vs @p
  Ident label;
};

// ============================================================
// § 10  Variable / MultiVariable  ←  ast.rs
// ============================================================

struct VariableInfo {
  std::optional<uint32_t> align;
  Type v_type;
  StateSpace state_space;
  std::vector<RegOrImmediate<Ident>> array_init;
};

struct Variable {
  VariableInfo info;
  Ident name;
};

// MultiVariable: either a parameterized range (%r<N>) or an explicit list
struct MultiVariableParameterized {
  VariableInfo info;
  Ident name;
  uint32_t count;
};

struct MultiVariableNames {
  VariableInfo info;
  std::vector<Ident> names;
};

using MultiVariable =
    std::variant<MultiVariableParameterized, MultiVariableNames>;

/**
 * @brief OperandLike concept to constrain instruction operand types. An operand type must be copyable and equality comparable, tagged with a static boolean is_operand, and have an associated id_type for the IdentLike constraint on operands.
 * 
 * @tparam Op The operand type to be constrained.
 * @note Generally, only two types satisfy this constraint: `ParsedOperand`/`ResolveOperand`
 */
template <typename Op>
concept OperandLike = requires {
  requires Op::is_operand == true;  // taged with is_operand
  typename Op::
      id_type;  // must have an associated id_type for the IdentLike constraint on operands
} && std::copyable<Op> && std::equality_comparable<Op>;

// ============================================================
// § 12  TuningDirective / MethodDeclaration / Function / Directive / Module
//        ←  ast.rs
// ============================================================

// .pragma maxnreg etc.

/*
code example:
```ptx
.func my_kernel() 
  .maxnreg 64
  .maxntid 32,4,1
  .reqntid 16,16,1
  .minnctapersm 2
  .noreturn
{
  // function body
}
```
*/
struct TuningMaxNReg {
  uint32_t n;
};
struct TuningMaxNtid {
  uint32_t x, y, z;
};
struct TuningReqNtid {
  uint32_t x, y, z;
};
struct TuningMinNCtaPerSm {
  uint32_t n;
};
struct TuningNoReturn {};
using TuningDirective =
    std::variant<TuningMaxNReg, TuningMaxNtid, TuningReqNtid,
                 TuningMinNCtaPerSm, TuningNoReturn>;

// .entry vs .func
enum class MethodKind { Kernel, Func };

struct MethodDeclaration {
  std::vector<Variable> return_arguments;
  MethodKind kind;        // Kernel=.entry, Func=.func
  std::string_view name;  // raw source slice
  std::vector<Variable> input_arguments;
  std::optional<Ident> shared_mem;
};

// Linking visibility
enum class LinkingDirective : uint8_t {
  None = 0,
  Extern = 1 << 0,
  Visible = 1 << 1,
  Weak = 1 << 2,
};

inline LinkingDirective operator|(LinkingDirective a, LinkingDirective b) {
  return static_cast<LinkingDirective>(static_cast<uint8_t>(a) |
                                       static_cast<uint8_t>(b));
}

inline bool has_flag(LinkingDirective d, LinkingDirective f) {
  return (static_cast<uint8_t>(d) & static_cast<uint8_t>(f)) != 0;
}

// convert to string functions for error messages
std::string to_string(bool v);

};  // namespace ptx_frontend
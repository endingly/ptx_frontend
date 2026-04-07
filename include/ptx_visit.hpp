#pragma once
#include "ptx_ir.hpp"

namespace ptx_frontend {

// ============================================================
// § 14  Visitor / VisitorMut / VisitorMap  ←  ast.rs
// ============================================================

// info of visit: ptx type + address space, and whether it's dst operand
struct VisitTypeSpace {
  const Type* type;  // ptx type wanted by the instruction
  StateSpace space;
};

// ---- 14.1  Visitor (read only) --------------------------------------------

template <OperandLike Op, typename Err>
struct Visitor {
  virtual ~Visitor() = default;

  // take full operand (e.g. %r1, 0x42, {%r1,%r2}, ...)
  virtual expected<void, Err> visit(const Op& args,
                                    std::optional<VisitTypeSpace> type_space,
                                    bool is_dst, bool relaxed_type_check) = 0;

  // english: Handle pure symbolic references (label names / function names)
  virtual expected<void, Err> visit_ident(
      const typename Op::id_type& args,
      std::optional<VisitTypeSpace> type_space, bool is_dst,
      bool relaxed_type_check) = 0;
};

// A convenient wrapper that allows directly passing a lambda / FnMut equivalent.
// The lambda is expected to handle both full operands and pure symbolic references (labels/function names) by accepting an optional VisitTypeSpace and flags for destination operand and relaxed type checking.
template <OperandLike Op, typename Err, typename Fn>
  requires std::invocable<Fn, const Op&, std::optional<VisitTypeSpace>, bool,
                          bool>
struct LambdaVisitor : Visitor<Op, Err> {
  Fn fn_;
  explicit LambdaVisitor(Fn fn) : fn_(std::move(fn)) {}

  expected<void, Err> visit(const Op& args, std::optional<VisitTypeSpace> ts,
                            bool is_dst, bool relaxed) override {
    return fn_(args, ts, is_dst, relaxed);
  }

  // ident by default delegates to visit
  expected<void, Err> visit_ident(const typename Op::id_type& id,
                                  std::optional<VisitTypeSpace> ts, bool is_dst,
                                  bool relaxed) override {
    // english: By default, we wrap the raw Id into an Op and delegate to visit—this only works if Op can be constructed from Id.
    // For special handling, subclasses can override this method.
    return fn_(Op::from_value(RegOrImmediate<typename Op::id_type>::Reg(id)),
               ts, is_dst, relaxed);
  }
};

template <OperandLike Op, typename Err, typename Fn>
auto make_visitor(Fn&& fn) {
  return LambdaVisitor<Op, Err, std::decay_t<Fn>>(std::forward<Fn>(fn));
}

// ---- 14.3  VisitorMap (consume old Op, produce new Op) -------------------------

template <OperandLike From, OperandLike To, typename Err>
struct VisitorMap {
  virtual ~VisitorMap() = default;

  virtual expected<To, Err> visit(From args,  // by value：消费
                                  std::optional<VisitTypeSpace> type_space,
                                  bool is_dst, bool relaxed_type_check) = 0;

  virtual expected<typename To::id_type, Err> visit_ident(
      typename From::id_type args,  // by value：consume
      std::optional<VisitTypeSpace> type_space, bool is_dst,
      bool relaxed_type_check) = 0;
};

// 便利包装：lambda 版 VisitorMap
// 对应 Rust 的两个 blanket impl：
//   impl<T,U,Fn> VisitorMap<ParsedOperand<T>, ParsedOperand<U>, Err> for Fn
//   impl<T,U,Fn> VisitorMap<T, U, Err> for Fn where T::Ident = T
template <OperandLike From, OperandLike To, typename Err, typename Fn>
  requires std::invocable<Fn, typename From::id_type,
                          std::optional<VisitTypeSpace>, bool, bool>
struct LambdaVisitorMap : VisitorMap<From, To, Err> {
  Fn fn_;
  explicit LambdaVisitorMap(Fn fn) : fn_(std::move(fn)) {}

  // visit：将 From 里的每个 id 映射为 To 里的 id，保留其他字段
  expected<To, Err> visit(From args, std::optional<VisitTypeSpace> ts,
                          bool is_dst, bool relaxed) override {
    // From 需要提供 map_id() 方法（见 ParsedOperand 的实现）
    return args.template map_id<typename To::id_type, Err>(
        [&](typename From::id_type id) -> expected<typename To::id_type, Err> {
          return fn_(id, ts, is_dst, relaxed);
        });
  }

  expected<typename To::id_type, Err> visit_ident(
      typename From::id_type id, std::optional<VisitTypeSpace> ts, bool is_dst,
      bool relaxed) override {
    return fn_(id, ts, is_dst, relaxed);
  }
};

template <OperandLike From, OperandLike To, typename Err, typename Fn>
auto make_visitor_map(Fn&& fn) {
  return LambdaVisitorMap<From, To, Err, std::decay_t<Fn>>(
      std::forward<Fn>(fn));
}

};  // namespace ptx_frontend
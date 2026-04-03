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

// 便利包装：允许直接传 lambda / FnMut 等价物
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

  // ident 默认委托给 visit（与 Rust 的 FnMut blanket impl 一致）
  expected<void, Err> visit_ident(const typename Op::id_type& id,
                                  std::optional<VisitTypeSpace> ts, bool is_dst,
                                  bool relaxed) override {
    // 将裸 Id 包装成 Op 再转发——仅当 Op 支持从 Id 构造时有效
    // 如需特殊处理，子类覆盖此方法即可
    return fn_(Op::from_value(id), ts, is_dst, relaxed);
  }
};

template <OperandLike Op, typename Err, typename Fn>
auto make_visitor(Fn&& fn) {
  return LambdaVisitor<Op, Err, std::decay_t<Fn>>(std::forward<Fn>(fn));
}

// ---- 14.2  VisitorMut (mutable) -----------------------------------------

template <OperandLike Op, typename Err>
struct VisitorMut {
  virtual ~VisitorMut() = default;

  virtual expected<void, Err> visit(Op& args,
                                    std::optional<VisitTypeSpace> type_space,
                                    bool is_dst, bool relaxed_type_check) = 0;

  virtual expected<void, Err> visit_ident(
      typename Op::id_type& args, std::optional<VisitTypeSpace> type_space,
      bool is_dst, bool relaxed_type_check) = 0;
};

// ---- 14.3  VisitorMap (consume old Op, produce new Op) -------------------------

template <OperandLike From, OperandLike To, typename Err>
struct VisitorMap {
  virtual ~VisitorMap() = default;

  virtual expected<To, Err> visit(From args,  // by value：消费
                                  std::optional<VisitTypeSpace> type_space,
                                  bool is_dst, bool relaxed_type_check) = 0;

  virtual expected<typename To::id_type, Err> visit_ident(
      typename From::id_type args,  // by value：消费
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
    return args.map_id(
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
#pragma once
#include "ptx_ir.hpp"
#include "ptx_visit.hpp"

namespace ptx_frontend {

using tl::unexpected;

template <typename T>
ScalarType get_scalar_type(const T& data);

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrAbs<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data.type_;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src, ts, /*is_dst*/ false, false); !r)
    return r;
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrAbs<To>, Err> map_instr(InstrAbs<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data.type_;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrAbs<To>{instr.data, *dst, *src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrActivemask<Op>& instr,
                                Visitor<Op, Err>& v) {
  // activemask always produces b32, no data field
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  return v.visit(instr.dst, ts, /*is_dst*/ true, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrActivemask<To>, Err> map_instr(InstrActivemask<From> instr,
                                             VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  return InstrActivemask<To>{*dst};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrAdd<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = get_scalar_type(instr.data);

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src2, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrAdd<To>, Err> map_instr(InstrAdd<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = get_scalar_type(instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrAdd<To>{instr.data, *dst, *src1, *src2};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrAddExtended<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data.type_;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src2, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrAddExtended<To>, Err> map_instr(InstrAddExtended<From> instr,
                                              VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data.type_;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrAddExtended<To>{instr.data, *dst, *src1, *src2};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSubExtended<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data.type_;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src2, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSubExtended<To>, Err> map_instr(InstrSubExtended<From> instr,
                                              VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data.type_;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrSubExtended<To>{instr.data, *dst, *src1, *src2};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMadExtended<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data.type_;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src3, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMadExtended<To>, Err> map_instr(InstrMadExtended<From> instr,
                                              VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data.type_;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrMadExtended<To>{instr.data, *dst, *src1, *src2, *src3};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrAnd<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src2, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrAnd<To>, Err> map_instr(InstrAnd<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrAnd<To>{instr.data, *dst, *src1, *src2};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrAtom<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = instr.data.typ;
  VisitTypeSpace ts{&t, instr.data.space};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src2, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrAtom<To>, Err> map_instr(InstrAtom<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t = instr.data.typ;
  VisitTypeSpace ts{&t, instr.data.space};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrAtom<To>{instr.data, *dst, *src1, *src2};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrAtomCas<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data.type_;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, instr.data.space};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src3, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrAtomCas<To>, Err> map_instr(InstrAtomCas<From> instr,
                                          VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data.type_;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, instr.data.space};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrAtomCas<To>{instr.data, *dst, *src1, *src2, *src3};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBarWarp<Op>& instr,
                                Visitor<Op, Err>& v) {
  // bar.warp.sync membermask: src is b32 warp member mask, no dst
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBarWarp<To>, Err> map_instr(InstrBarWarp<From> instr,
                                          VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrBarWarp<To>{*src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBar<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  if (instr.src2) {
    if (auto r = v.visit(*instr.src2, ts, false, false); !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBar<To>, Err> map_instr(InstrBar<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  std::optional<To> src2;
  if (instr.src2) {
    auto r = v.visit(std::move(*instr.src2), ts, false, false);
    if (!r)
      return unexpected(r.error());
    src2 = std::move(*r);
  }
  return InstrBar<To>{instr.data, *src1, src2};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBarRed<Op>& instr,
                                Visitor<Op, Err>& v) {
  // dst type depends on reduction: popc → u32, and/or → pred
  ScalarType dst_st = (instr.data.pred_reduction == Reduction::Popc)
                          ? ScalarType::U32
                          : ScalarType::Pred;
  Type t_dst = make_scalar(dst_st);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};

  Type t_b32 = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_b32{&t_b32, StateSpace::Reg};

  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};

  if (auto r = v.visit(instr.dst1, ts_dst, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src_barrier, ts_b32, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src_predicate, ts_pred, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src_negate_predicate, ts_pred, false, false); !r)
    return r;
  if (instr.src_threadcount) {
    if (auto r = v.visit(*instr.src_threadcount, ts_b32, false, false); !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBarRed<To>, Err> map_instr(InstrBarRed<From> instr,
                                         VisitorMap<From, To, Err>& v) {
  ScalarType dst_st = (instr.data.pred_reduction == Reduction::Popc)
                          ? ScalarType::U32
                          : ScalarType::Pred;
  Type t_dst = make_scalar(dst_st);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_b32 = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_b32{&t_b32, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};

  auto dst1 = v.visit(std::move(instr.dst1), ts_dst, true, false);
  if (!dst1)
    return unexpected(dst1.error());
  auto src_barrier =
      v.visit(std::move(instr.src_barrier), ts_b32, false, false);
  if (!src_barrier)
    return unexpected(src_barrier.error());
  auto src_predicate =
      v.visit(std::move(instr.src_predicate), ts_pred, false, false);
  if (!src_predicate)
    return unexpected(src_predicate.error());
  auto src_negate_predicate =
      v.visit(std::move(instr.src_negate_predicate), ts_pred, false, false);
  if (!src_negate_predicate)
    return unexpected(src_negate_predicate.error());

  std::optional<To> src_threadcount;
  if (instr.src_threadcount) {
    auto r = v.visit(std::move(*instr.src_threadcount), ts_b32, false, false);
    if (!r)
      return unexpected(r.error());
    src_threadcount = std::move(*r);
  }
  return InstrBarRed<To>{instr.data,
                         *dst1,
                         *src_barrier,
                         *src_predicate,
                         *src_negate_predicate,
                         src_threadcount};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBfe<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src3, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBfe<To>, Err> map_instr(InstrBfe<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrBfe<To>{instr.data, *dst, *src1, *src2, *src3};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBfi<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, /*is_dst*/ false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, /*is_dst*/ false, false); !r)
    return r;
  if (auto r = v.visit(instr.src3, ts, /*is_dst*/ false, false); !r)
    return r;
  return v.visit(instr.src4, ts, /*is_dst*/ false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBfi<To>, Err> map_instr(InstrBfi<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return unexpected(src3.error());
  auto src4 = v.visit(std::move(instr.src4), ts, false, false);
  if (!src4)
    return unexpected(src4.error());
  return InstrBfi<To>{instr.data, *dst, *src1, *src2, *src3, *src4};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBmsk<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src_a, ts, false, false); !r)
    return r;
  return v.visit(instr.src_b, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBmsk<To>, Err> map_instr(InstrBmsk<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src_a = v.visit(std::move(instr.src_a), ts, false, false);
  if (!src_a)
    return unexpected(src_a.error());
  auto src_b = v.visit(std::move(instr.src_b), ts, false, false);
  if (!src_b)
    return unexpected(src_b.error());
  return InstrBmsk<To>{instr.data, *dst, *src_a, *src_b};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBra<typename Op::id_type>& instr,
                                Visitor<Op, Err>& v) {
  return v.visit_ident(instr.src, std::nullopt, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBra<typename To::id_type>, Err> map_instr(
    InstrBra<typename From::id_type> instr, VisitorMap<From, To, Err>& v) {
  auto src = v.visit_ident(std::move(instr.src), std::nullopt, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrBra<typename To::id_type>{*src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBrev<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src, ts, /*is_dst*/ false, false); !r)
    return r;
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBrev<To>, Err> map_instr(InstrBrev<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrBrev<To>{instr.data, *dst, *src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCall<Op>& instr,
                                Visitor<Op, Err>& v) {
  // return arguments: Id → visit_ident as dst
  for (size_t i = 0; i < instr.arguments.return_arguments.size(); ++i) {
    Type t = instr.data.return_arguments[i].first;
    VisitTypeSpace ts{&t, instr.data.return_arguments[i].second};
    if (auto r =
            v.visit_ident(instr.arguments.return_arguments[i], ts, true, false);
        !r)
      return r;
  }
  // func name: pure ident, no type
  if (auto r = v.visit_ident(instr.arguments.func, std::nullopt, false, false);
      !r)
    return r;
  // input arguments: full Op → visit as src
  for (size_t i = 0; i < instr.arguments.input_arguments.size(); ++i) {
    Type t = instr.data.input_arguments[i].first;
    VisitTypeSpace ts{&t, instr.data.input_arguments[i].second};
    if (auto r = v.visit(instr.arguments.input_arguments[i], ts, false, false);
        !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCall<To>, Err> map_instr(InstrCall<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  CallArgs<typename To::id_type> new_args;
  new_args.is_external = instr.arguments.is_external;

  for (size_t i = 0; i < instr.arguments.return_arguments.size(); ++i) {
    Type t = instr.data.return_arguments[i].first;
    VisitTypeSpace ts{&t, instr.data.return_arguments[i].second};
    auto r = v.visit_ident(std::move(instr.arguments.return_arguments[i]), ts,
                           true, false);
    if (!r)
      return unexpected(r.error());
    new_args.return_arguments.push_back(std::move(*r));
  }

  {
    auto r = v.visit_ident(std::move(instr.arguments.func), std::nullopt, false,
                           false);
    if (!r)
      return unexpected(r.error());
    new_args.func = std::move(*r);
  }

  for (size_t i = 0; i < instr.arguments.input_arguments.size(); ++i) {
    Type t = instr.data.input_arguments[i].first;
    VisitTypeSpace ts{&t, instr.data.input_arguments[i].second};
    auto r = v.visit(std::move(instr.arguments.input_arguments[i]), ts, false,
                     false);
    if (!r)
      return unexpected(r.error());
    new_args.input_arguments.push_back(std::move(*r));
  }

  return InstrCall<To>{instr.data, std::move(new_args)};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrClz<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = instr.data;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src, ts, /*is_dst*/ false, false); !r)
    return r;
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrClz<To>, Err> map_instr(InstrClz<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrClz<To>{instr.data, *dst, *src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCos<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = ScalarType::B32;

  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src, ts, /*is_dst*/ false, false); !r)
    return r;
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCos<To>, Err> map_instr(InstrCos<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = ScalarType::B32;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrCos<To>{instr.data, *dst, *src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCpAsync<Op>& instr,
                                Visitor<Op, Err>& v) {
  auto derive_scalarType = [&]() {
    ScalarType st;
    switch (instr.data.cp_size) {
      case CpAsyncCpSize::Bytes4:
        st = ScalarType::B32;
        break;
      case CpAsyncCpSize::Bytes8:
        st = ScalarType::B64;
        break;
      case CpAsyncCpSize::Bytes16:
        st = ScalarType::B128;
        break;
    }
    return st;
  };

  ScalarType st = derive_scalarType();
  Type t = make_scalar(st);
  VisitTypeSpace ts_dst{&t, instr.data.space};
  if (auto r = v.visit(instr.src_to, ts_dst, /*is_dst*/ true, false); !r)
    return r;
  auto ts_src = VisitTypeSpace{&t, StateSpace::Global};
  if (auto r = v.visit(instr.src_from, ts_src, /*is_dst*/ false, false); !r)
    return r;
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCpAsync<To>, Err> map_instr(InstrCpAsync<From> instr,
                                          VisitorMap<From, To, Err>& v) {
  auto derive_scalarType = [&]() {
    ScalarType st;
    switch (instr.data.cp_size) {
      case CpAsyncCpSize::Bytes4:
        st = ScalarType::B32;
        break;
      case CpAsyncCpSize::Bytes8:
        st = ScalarType::B64;
        break;
      case CpAsyncCpSize::Bytes16:
        st = ScalarType::B128;
        break;
    }
    return st;
  };

  ScalarType st = derive_scalarType();
  Type t = make_scalar(st);
  VisitTypeSpace ts_dst{&t, instr.data.space};
  auto dst = v.visit(std::move(instr.src_to), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());

  auto ts_src = VisitTypeSpace{&t, StateSpace::Global};
  auto src = v.visit(std::move(instr.src_from), ts_src, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrCpAsync<To>{instr.data, *dst, *src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCpAsyncCommitGroup& instr,
                                Visitor<Op, Err>& v) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCpAsyncCommitGroup, Err> map_instr(InstrCpAsyncCommitGroup instr,
                                                 VisitorMap<From, To, Err>& v) {
  return InstrCpAsyncCommitGroup{};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCpAsyncWaitGroup<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::U32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  return v.visit(instr.src_group, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCpAsyncWaitGroup<To>, Err> map_instr(
    InstrCpAsyncWaitGroup<From> instr, VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::U32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto src = v.visit(std::move(instr.src_group), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrCpAsyncWaitGroup<To>{*src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCpAsyncWaitAll& instr,
                                Visitor<Op, Err>& v) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCpAsyncWaitAll, Err> map_instr(InstrCpAsyncWaitAll instr,
                                             VisitorMap<From, To, Err>& v) {
  return InstrCpAsyncWaitAll{};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCreatePolicyFractional<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  return v.visit(instr.dst_policy, ts, /*is_dst*/ true, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCreatePolicyFractional<To>, Err> map_instr(
    InstrCreatePolicyFractional<From> instr, VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst_policy), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  return InstrCreatePolicyFractional<To>{instr.data, *dst};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCvt<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_dst = make_scalar(instr.data.to);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.from);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_dst, /*is_dst*/ true, false); !r)
    return r;
  if (auto r = v.visit(instr.src, ts_src, /*is_dst*/ false, false); !r)
    return r;
  if (instr.src2) {
    if (auto r = v.visit(*instr.src2, ts_src, /*is_dst*/ false, false); !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCvt<To>, Err> map_instr(InstrCvt<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t_dst = make_scalar(instr.data.to);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.from);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts_src, false, false);
  if (!src)
    return unexpected(src.error());
  std::optional<To> src2;
  if (instr.src2) {
    auto r = v.visit(std::move(*instr.src2), ts_src, false, false);
    if (!r)
      return unexpected(r.error());
    src2 = std::move(*r);
  }
  return InstrCvt<To>{instr.data, *dst, *src, src2};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCvtPack<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_dst = make_scalar(ScalarType::B32);  // dst 固定 b32
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.from);  // src1/src2 用 from
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  Type t_acc = make_scalar(ScalarType::B32);  // src3 accumulator b32
  VisitTypeSpace ts_acc{&t_acc, StateSpace::Reg};

  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts_src, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts_src, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src3, ts_acc, false, false); !r)
    return r;
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCvtPack<To>, Err> map_instr(InstrCvtPack<From> instr,
                                          VisitorMap<From, To, Err>& v) {
  Type t_dst = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.from);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  Type t_acc = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_acc{&t_acc, StateSpace::Reg};

  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts_src, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_src, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts_acc, false, false);
  if (!src3)
    return unexpected(src3.error());

  return InstrCvtPack<To>{instr.data, *dst, *src1, *src2, *src3};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCvta<Op>& instr,
                                Visitor<Op, Err>& v) {
  // cvta 操作数是地址值寄存器，大小依赖目标位宽，用 relaxed=true
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, /*relaxed=*/true); !r)
    return r;
  return v.visit(instr.src, ts, false, /*relaxed=*/true);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCvta<To>, Err> map_instr(InstrCvta<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, true);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, true);
  if (!src)
    return unexpected(src.error());
  return InstrCvta<To>{instr.data, *dst, *src};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrDiv<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = std::visit([](const auto& d) { return d.type_; }, instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrDiv<To>, Err> map_instr(InstrDiv<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = std::visit([](const auto& d) { return d.type_; }, instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrDiv<To>{instr.data, *dst, *src1, *src2};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrDp4a<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_c = make_scalar(instr.data.ctype());  // dst / src3 都是 ctype
  VisitTypeSpace ts_c{&t_c, StateSpace::Reg};
  Type t_a = make_scalar(instr.data.atype);
  VisitTypeSpace ts_a{&t_a, StateSpace::Reg};
  Type t_b = make_scalar(instr.data.btype);
  VisitTypeSpace ts_b{&t_b, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_c, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts_a, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts_b, false, false); !r)
    return r;
  return v.visit(instr.src3, ts_c, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrDp4a<To>, Err> map_instr(InstrDp4a<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t_c = make_scalar(instr.data.ctype());
  VisitTypeSpace ts_c{&t_c, StateSpace::Reg};
  Type t_a = make_scalar(instr.data.atype);
  VisitTypeSpace ts_a{&t_a, StateSpace::Reg};
  Type t_b = make_scalar(instr.data.btype);
  VisitTypeSpace ts_b{&t_b, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_c, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts_a, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_b, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts_c, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrDp4a<To>{instr.data, *dst, *src1, *src2, *src3};
}

// ── InstrEx2 ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrEx2<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrEx2<To>, Err> map_instr(InstrEx2<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrEx2<To>{instr.data, *dst, *src};
}

// ── InstrFma ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrFma<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, false, false); !r)
    return r;
  return v.visit(instr.src3, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrFma<To>, Err> map_instr(InstrFma<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrFma<To>{instr.data, *dst, *src1, *src2, *src3};
}

// ── InstrLd ────────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrLd<Op>& instr, Visitor<Op, Err>& v) {
  // dst: register holding the loaded value
  Type t_dst = instr.data.typ;
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  // src: address in the source state space
  Type t_src = instr.data.typ;
  VisitTypeSpace ts_src{&t_src, instr.data.state_space};
  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  return v.visit(instr.src, ts_src, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrLd<To>, Err> map_instr(InstrLd<From> instr,
                                     VisitorMap<From, To, Err>& v) {
  Type t_dst = instr.data.typ;
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = instr.data.typ;
  VisitTypeSpace ts_src{&t_src, instr.data.state_space};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts_src, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrLd<To>{instr.data, *dst, *src};
}

// ── InstrLg2 ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrLg2<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::F32);  // lg2 仅作用于 f32
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrLg2<To>, Err> map_instr(InstrLg2<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::F32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrLg2<To>{instr.data, *dst, *src};
}

// ── InstrMad ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMad<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = std::visit([](const auto& d) { return d.type_; }, instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, false, false); !r)
    return r;
  return v.visit(instr.src3, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMad<To>, Err> map_instr(InstrMad<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = std::visit([](const auto& d) { return d.type_; }, instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrMad<To>{instr.data, *dst, *src1, *src2, *src3};
}

// ── InstrMax ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMax<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = std::visit([](const auto& d) { return d.type_; }, instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMax<To>, Err> map_instr(InstrMax<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = std::visit([](const auto& d) { return d.type_; }, instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrMax<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrMembar ────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMembar& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMembar, Err> map_instr(InstrMembar instr,
                                     VisitorMap<From, To, Err>& /*v*/) {
  return instr;
}

// ── InstrMin ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMin<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = std::visit([](const auto& d) { return d.type_; }, instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMin<To>, Err> map_instr(InstrMin<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = std::visit([](const auto& d) { return d.type_; }, instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrMin<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrMov ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMov<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = instr.data.typ;
  VisitTypeSpace ts{&t, StateSpace::Reg};
  // relaxed=true：mov 可以携带立即数、地址标签等
  if (auto r = v.visit(instr.dst, ts, true, /*relaxed=*/true); !r)
    return r;
  return v.visit(instr.src, ts, false, /*relaxed=*/true);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMov<To>, Err> map_instr(InstrMov<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = instr.data.typ;
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, true);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, true);
  if (!src)
    return unexpected(src.error());
  return InstrMov<To>{instr.data, *dst, *src};
}

// ── InstrMul ───────────────────────────────────────────────────────────────
// mul.wide 时 dst 是 src 类型的双倍宽度
inline ScalarType mul_wide_dst_type(ScalarType t) {
  switch (t) {
    case ScalarType::U16:
      return ScalarType::U32;
    case ScalarType::U32:
      return ScalarType::U64;
    case ScalarType::S16:
      return ScalarType::S32;
    case ScalarType::S32:
      return ScalarType::S64;
    default:
      return t;
  }
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMul<Op>& instr,
                                Visitor<Op, Err>& v) {
  return std::visit(
      [&](const auto& d) -> expected<void, Err> {
        using D = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<D, MulInt>) {
          Type t_src = make_scalar(d.type_);
          VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
          if (d.control == MulIntControl::Wide) {
            Type t_dst = make_scalar(mul_wide_dst_type(d.type_));
            VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
            if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
              return r;
          } else {
            if (auto r = v.visit(instr.dst, ts_src, true, false); !r)
              return r;
          }
          if (auto r = v.visit(instr.src1, ts_src, false, false); !r)
            return r;
          return v.visit(instr.src2, ts_src, false, false);
        } else {  // ArithFloat
          Type t = make_scalar(d.type_);
          VisitTypeSpace ts{&t, StateSpace::Reg};
          if (auto r = v.visit(instr.dst, ts, true, false); !r)
            return r;
          if (auto r = v.visit(instr.src1, ts, false, false); !r)
            return r;
          return v.visit(instr.src2, ts, false, false);
        }
      },
      instr.data);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMul<To>, Err> map_instr(InstrMul<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  return std::visit(
      [&](const auto& d) -> expected<InstrMul<To>, Err> {
        using D = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<D, MulInt>) {
          Type t_src = make_scalar(d.type_);
          VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
          expected<To, Err> dst;
          if (d.control == MulIntControl::Wide) {
            Type t_dst = make_scalar(mul_wide_dst_type(d.type_));
            VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
            dst = v.visit(std::move(instr.dst), ts_dst, true, false);
          } else {
            dst = v.visit(std::move(instr.dst), ts_src, true, false);
          }
          if (!dst)
            return unexpected(dst.error());
          auto src1 = v.visit(std::move(instr.src1), ts_src, false, false);
          if (!src1)
            return unexpected(src1.error());
          auto src2 = v.visit(std::move(instr.src2), ts_src, false, false);
          if (!src2)
            return unexpected(src2.error());
          return InstrMul<To>{instr.data, *dst, *src1, *src2};
        } else {
          Type t = make_scalar(d.type_);
          VisitTypeSpace ts{&t, StateSpace::Reg};
          auto dst = v.visit(std::move(instr.dst), ts, true, false);
          if (!dst)
            return unexpected(dst.error());
          auto src1 = v.visit(std::move(instr.src1), ts, false, false);
          if (!src1)
            return unexpected(src1.error());
          auto src2 = v.visit(std::move(instr.src2), ts, false, false);
          if (!src2)
            return unexpected(src2.error());
          return InstrMul<To>{instr.data, *dst, *src1, *src2};
        }
      },
      instr.data);
}

// ── InstrMul24 ─────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMul24<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMul24<To>, Err> map_instr(InstrMul24<From> instr,
                                        VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrMul24<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrNanosleep ─────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrNanosleep<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::U32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrNanosleep<To>, Err> map_instr(InstrNanosleep<From> instr,
                                            VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::U32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrNanosleep<To>{*src};
}

// ── InstrNeg ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrNeg<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrNeg<To>, Err> map_instr(InstrNeg<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrNeg<To>{instr.data, *dst, *src};
}

// ── InstrNot ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrNot<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrNot<To>, Err> map_instr(InstrNot<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrNot<To>{instr.data, *dst, *src};
}

// ── InstrOr ────────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrOr<Op>& instr, Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrOr<To>, Err> map_instr(InstrOr<From> instr,
                                     VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrOr<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrPopc ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrPopc<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_dst = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  return v.visit(instr.src, ts_src, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrPopc<To>, Err> map_instr(InstrPopc<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t_dst = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts_src, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrPopc<To>{instr.data, *dst, *src};
}

// ── InstrPrmt ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrPrmt<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, false, false); !r)
    return r;
  return v.visit(instr.src3, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrPrmt<To>, Err> map_instr(InstrPrmt<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrPrmt<To>{*dst, *src1, *src2, *src3};
}

// ── InstrRcp ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrRcp<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrRcp<To>, Err> map_instr(InstrRcp<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrRcp<To>{instr.data, *dst, *src};
}

// ── InstrRem ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrRem<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrRem<To>, Err> map_instr(InstrRem<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrRem<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrRet ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrRet& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrRet, Err> map_instr(InstrRet instr,
                                  VisitorMap<From, To, Err>& /*v*/) {
  return instr;
}

// ── InstrRsqrt ─────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrRsqrt<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrRsqrt<To>, Err> map_instr(InstrRsqrt<From> instr,
                                        VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrRsqrt<To>{instr.data, *dst, *src};
}

// ── InstrSelp ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSelp<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, false, false); !r)
    return r;
  return v.visit(instr.src3, ts_pred, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSelp<To>, Err> map_instr(InstrSelp<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts_pred, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrSelp<To>{instr.data, *dst, *src1, *src2, *src3};
}

// ── InstrSet ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSet<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_dst = make_scalar(instr.data.dtype);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.base.type_);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts_src, false, false); !r)
    return r;
  return v.visit(instr.src2, ts_src, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSet<To>, Err> map_instr(InstrSet<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t_dst = make_scalar(instr.data.dtype);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.base.type_);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts_src, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_src, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrSet<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrSetBool ───────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSetBool<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_dst = make_scalar(instr.data.dtype);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.base.base.type_);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts_src, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts_src, false, false); !r)
    return r;
  return v.visit(instr.src3, ts_pred, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSetBool<To>, Err> map_instr(InstrSetBool<From> instr,
                                          VisitorMap<From, To, Err>& v) {
  Type t_dst = make_scalar(instr.data.dtype);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.base.base.type_);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts_src, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_src, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts_pred, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrSetBool<To>{instr.data, *dst, *src1, *src2, *src3};
}

// ── InstrSetp ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSetp<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.type_);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  if (auto r = v.visit(instr.dst1, ts_pred, true, false); !r)
    return r;
  if (instr.dst2) {
    if (auto r = v.visit(*instr.dst2, ts_pred, true, false); !r)
      return r;
  }
  if (auto r = v.visit(instr.src1, ts_src, false, false); !r)
    return r;
  return v.visit(instr.src2, ts_src, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSetp<To>, Err> map_instr(InstrSetp<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.type_);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  auto dst1 = v.visit(std::move(instr.dst1), ts_pred, true, false);
  if (!dst1)
    return unexpected(dst1.error());
  std::optional<To> dst2;
  if (instr.dst2) {
    auto r = v.visit(std::move(*instr.dst2), ts_pred, true, false);
    if (!r)
      return unexpected(r.error());
    dst2 = std::move(*r);
  }
  auto src1 = v.visit(std::move(instr.src1), ts_src, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_src, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrSetp<To>{instr.data, *dst1, dst2, *src1, *src2};
}

// ── InstrSetpBool ──────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSetpBool<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.base.type_);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  if (auto r = v.visit(instr.dst1, ts_pred, true, false); !r)
    return r;
  if (instr.dst2) {
    if (auto r = v.visit(*instr.dst2, ts_pred, true, false); !r)
      return r;
  }
  if (auto r = v.visit(instr.src1, ts_src, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts_src, false, false); !r)
    return r;
  return v.visit(instr.src3, ts_pred, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSetpBool<To>, Err> map_instr(InstrSetpBool<From> instr,
                                           VisitorMap<From, To, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  Type t_src = make_scalar(instr.data.base.type_);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  auto dst1 = v.visit(std::move(instr.dst1), ts_pred, true, false);
  if (!dst1)
    return unexpected(dst1.error());
  std::optional<To> dst2;
  if (instr.dst2) {
    auto r = v.visit(std::move(*instr.dst2), ts_pred, true, false);
    if (!r)
      return unexpected(r.error());
    dst2 = std::move(*r);
  }
  auto src1 = v.visit(std::move(instr.src1), ts_src, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_src, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts_pred, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrSetpBool<To>{instr.data, *dst1, dst2, *src1, *src2, *src3};
}

// ── InstrShflSync ──────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrShflSync<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_b32 = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_b32{&t_b32, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_b32, true, false); !r)
    return r;
  if (instr.dst_pred) {
    if (auto r = v.visit(*instr.dst_pred, ts_pred, true, false); !r)
      return r;
  }
  if (auto r = v.visit(instr.src, ts_b32, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src_lane, ts_b32, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src_opts, ts_b32, false, false); !r)
    return r;
  return v.visit(instr.src_membermask, ts_b32, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrShflSync<To>, Err> map_instr(InstrShflSync<From> instr,
                                           VisitorMap<From, To, Err>& v) {
  Type t_b32 = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_b32{&t_b32, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_b32, true, false);
  if (!dst)
    return unexpected(dst.error());
  std::optional<To> dst_pred;
  if (instr.dst_pred) {
    auto r = v.visit(std::move(*instr.dst_pred), ts_pred, true, false);
    if (!r)
      return unexpected(r.error());
    dst_pred = std::move(*r);
  }
  auto src = v.visit(std::move(instr.src), ts_b32, false, false);
  if (!src)
    return unexpected(src.error());
  auto src_lane = v.visit(std::move(instr.src_lane), ts_b32, false, false);
  if (!src_lane)
    return unexpected(src_lane.error());
  auto src_opts = v.visit(std::move(instr.src_opts), ts_b32, false, false);
  if (!src_opts)
    return unexpected(src_opts.error());
  auto src_membermask =
      v.visit(std::move(instr.src_membermask), ts_b32, false, false);
  if (!src_membermask)
    return unexpected(src_membermask.error());
  return InstrShflSync<To>{instr.data, *dst,      dst_pred,       *src,
                           *src_lane,  *src_opts, *src_membermask};
}

// ── InstrShf ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrShf<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_b32 = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_b32{&t_b32, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_b32, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src_a, ts_b32, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src_b, ts_b32, false, false); !r)
    return r;
  return v.visit(instr.src_c, ts_u32, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrShf<To>, Err> map_instr(InstrShf<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t_b32 = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_b32{&t_b32, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_b32, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src_a = v.visit(std::move(instr.src_a), ts_b32, false, false);
  if (!src_a)
    return unexpected(src_a.error());
  auto src_b = v.visit(std::move(instr.src_b), ts_b32, false, false);
  if (!src_b)
    return unexpected(src_b.error());
  auto src_c = v.visit(std::move(instr.src_c), ts_u32, false, false);
  if (!src_c)
    return unexpected(src_c.error());
  return InstrShf<To>{instr.data, *dst, *src_a, *src_b, *src_c};
}

// ── InstrShl ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrShl<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts_u32, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrShl<To>, Err> map_instr(InstrShl<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_u32, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrShl<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrShr ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrShr<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts_u32, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrShr<To>, Err> map_instr(InstrShr<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_u32, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrShr<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrSin ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSin<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::F32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSin<To>, Err> map_instr(InstrSin<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::F32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrSin<To>{instr.data, *dst, *src};
}

// ── InstrSqrt ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSqrt<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSqrt<To>, Err> map_instr(InstrSqrt<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrSqrt<To>{instr.data, *dst, *src};
}

// ── InstrSt ────────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSt<Op>& instr, Visitor<Op, Err>& v) {
  // src1: address operand in the target state space
  Type t_addr = instr.data.typ;
  VisitTypeSpace ts_addr{&t_addr, instr.data.state_space};
  // src2: value operand held in a register
  Type t_val = instr.data.typ;
  VisitTypeSpace ts_val{&t_val, StateSpace::Reg};
  if (auto r = v.visit(instr.src1, ts_addr, false, false); !r)
    return r;
  return v.visit(instr.src2, ts_val, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSt<To>, Err> map_instr(InstrSt<From> instr,
                                     VisitorMap<From, To, Err>& v) {
  Type t_addr = instr.data.typ;
  VisitTypeSpace ts_addr{&t_addr, instr.data.state_space};
  Type t_val = instr.data.typ;
  VisitTypeSpace ts_val{&t_val, StateSpace::Reg};
  auto src1 = v.visit(std::move(instr.src1), ts_addr, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_val, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrSt<To>{instr.data, *src1, *src2};
}

// ── InstrSub ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSub<Op>& instr,
                                Visitor<Op, Err>& v) {
  ScalarType st = get_scalar_type(instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSub<To>, Err> map_instr(InstrSub<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = get_scalar_type(instr.data);
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrSub<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrTrap ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTrap& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTrap, Err> map_instr(InstrTrap instr,
                                   VisitorMap<From, To, Err>& /*v*/) {
  return instr;
}

// ── InstrXor ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrXor<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrXor<To>, Err> map_instr(InstrXor<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrXor<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrTanh ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTanh<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTanh<To>, Err> map_instr(InstrTanh<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrTanh<To>{instr.data, *dst, *src};
}

// ── InstrVote ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrVote<Op>& instr,
                                Visitor<Op, Err>& v) {
  // Ballot → dst is b32; All/Any → dst is pred
  ScalarType dst_st = (instr.data.mode == VoteMode::Ballot) ? ScalarType::B32
                                                            : ScalarType::Pred;
  Type t_dst = make_scalar(dst_st);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts_pred, false, false); !r)
    return r;
  return v.visit(instr.src2, ts_u32, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrVote<To>, Err> map_instr(InstrVote<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  ScalarType dst_st = (instr.data.mode == VoteMode::Ballot) ? ScalarType::B32
                                                            : ScalarType::Pred;
  Type t_dst = make_scalar(dst_st);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_pred = make_scalar(ScalarType::Pred);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts_pred, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_u32, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrVote<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrReduxSync ─────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrReduxSync<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src, ts, false, false); !r)
    return r;
  return v.visit(instr.src_membermask, ts_u32, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrReduxSync<To>, Err> map_instr(InstrReduxSync<From> instr,
                                            VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data.type_);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  Type t_u32 = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_u32{&t_u32, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  auto src_membermask =
      v.visit(std::move(instr.src_membermask), ts_u32, false, false);
  if (!src_membermask)
    return unexpected(src_membermask.error());
  return InstrReduxSync<To>{instr.data, *dst, *src, *src_membermask};
}

// ── InstrLdMatrix ──────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrLdMatrix<Op>& instr,
                                Visitor<Op, Err>& v) {
  // dst: loaded matrix fragment (vector of B32) in register file
  Type t_dst = instr.data.get_loaded_type();
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  // src: shared memory address
  Type t_src = instr.data.get_loaded_type();
  VisitTypeSpace ts_src{&t_src, instr.data.state_space};
  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  return v.visit(instr.src, ts_src, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrLdMatrix<To>, Err> map_instr(InstrLdMatrix<From> instr,
                                           VisitorMap<From, To, Err>& v) {
  Type t_dst = instr.data.get_loaded_type();
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  Type t_src = instr.data.get_loaded_type();
  VisitTypeSpace ts_src{&t_src, instr.data.state_space};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts_src, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrLdMatrix<To>{instr.data, *dst, *src};
}

// ── InstrGridDepControl ────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrGridDepControl& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrGridDepControl, Err> map_instr(InstrGridDepControl instr,
                                             VisitorMap<From, To, Err>& /*v*/) {
  return instr;
}

// ── InstrMma ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMma<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_d = instr.data.dtype();
  VisitTypeSpace ts_d{&t_d, StateSpace::Reg};
  Type t_a = instr.data.atype();
  VisitTypeSpace ts_a{&t_a, StateSpace::Reg};
  Type t_b = instr.data.btype();
  VisitTypeSpace ts_b{&t_b, StateSpace::Reg};
  Type t_c = instr.data.ctype();
  VisitTypeSpace ts_c{&t_c, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_d, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts_a, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts_b, false, false); !r)
    return r;
  return v.visit(instr.src3, ts_c, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMma<To>, Err> map_instr(InstrMma<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t_d = instr.data.dtype();
  VisitTypeSpace ts_d{&t_d, StateSpace::Reg};
  Type t_a = instr.data.atype();
  VisitTypeSpace ts_a{&t_a, StateSpace::Reg};
  Type t_b = instr.data.btype();
  VisitTypeSpace ts_b{&t_b, StateSpace::Reg};
  Type t_c = instr.data.ctype();
  VisitTypeSpace ts_c{&t_c, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_d, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts_a, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_b, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts_c, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrMma<To>{instr.data, *dst, *src1, *src2, *src3};
}

// ── InstrCopysign ──────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCopysign<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  return v.visit(instr.src2, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCopysign<To>, Err> map_instr(InstrCopysign<From> instr,
                                           VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  return InstrCopysign<To>{instr.data, *dst, *src1, *src2};
}

// ── InstrPrefetch ──────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrPrefetch<Op>& instr,
                                Visitor<Op, Err>& v) {
  // src is a memory address; no data type — use relaxed check
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, instr.data.space};
  return v.visit(instr.src, ts, false, true /*relaxed*/);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrPrefetch<To>, Err> map_instr(InstrPrefetch<From> instr,
                                           VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, instr.data.space};
  auto src = v.visit(std::move(instr.src), ts, false, true /*relaxed*/);
  if (!src)
    return unexpected(src.error());
  return InstrPrefetch<To>{instr.data, *src};
}

// ── InstrSad ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSad<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts, false, false); !r)
    return r;
  return v.visit(instr.src3, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSad<To>, Err> map_instr(InstrSad<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(instr.data);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrSad<To>{instr.data, *dst, *src1, *src2, *src3};
}

// ── InstrDp2a ──────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrDp2a<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_c = make_scalar(instr.data.ctype());
  VisitTypeSpace ts_c{&t_c, StateSpace::Reg};
  Type t_a = make_scalar(instr.data.atype);
  VisitTypeSpace ts_a{&t_a, StateSpace::Reg};
  Type t_b = make_scalar(instr.data.btype);
  VisitTypeSpace ts_b{&t_b, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_c, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src1, ts_a, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src2, ts_b, false, false); !r)
    return r;
  return v.visit(instr.src3, ts_c, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrDp2a<To>, Err> map_instr(InstrDp2a<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  Type t_c = make_scalar(instr.data.ctype());
  VisitTypeSpace ts_c{&t_c, StateSpace::Reg};
  Type t_a = make_scalar(instr.data.atype);
  VisitTypeSpace ts_a{&t_a, StateSpace::Reg};
  Type t_b = make_scalar(instr.data.btype);
  VisitTypeSpace ts_b{&t_b, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_c, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts_a, false, false);
  if (!src1)
    return unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts_b, false, false);
  if (!src2)
    return unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts_c, false, false);
  if (!src3)
    return unexpected(src3.error());
  return InstrDp2a<To>{instr.data, *dst, *src1, *src2, *src3};
}

// Instruction<Op> variant entry point
template <OperandLike Op, typename Err>
expected<void, Err> visit_instruction(const Instruction<Op>& instr,
                                      Visitor<Op, Err>& v) {
  return std::visit([&](const auto& i) { return visit_instr(i, v); }, instr);
}

// map Instruction<From> → Instruction<To> entry point
template <OperandLike From, OperandLike To, typename Err>
expected<Instruction<To>, Err> map_instruction(Instruction<From> instr,
                                               VisitorMap<From, To, Err>& v) {
  return std::visit(
      [&](auto&& i) -> expected<Instruction<To>, Err> {
        auto r = map_instr(std::move(i), v);
        if (!r)
          return unexpected(r.error());
        return Instruction<To>{std::move(*r)};
      },
      std::move(instr));
}

};  // namespace ptx_frontend
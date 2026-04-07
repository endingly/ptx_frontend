#pragma once
#include "ptx_ir.hpp"
#include "ptx_visit.hpp"

namespace ptx_frontend {

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
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrAbs<To>, Err> map_instr(InstrAbs<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data.type_;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return tl::unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return tl::unexpected(src.error());
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
    return tl::unexpected(dst.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return tl::unexpected(src3.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return tl::unexpected(src3.error());
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
    return tl::unexpected(src.error());
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
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBar<To>, Err> map_instr(InstrBar<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  std::optional<To> src2;
  if (instr.src2) {
    auto r = v.visit(std::move(*instr.src2), ts, false, false);
    if (!r)
      return tl::unexpected(r.error());
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
    return tl::unexpected(dst1.error());
  auto src_barrier =
      v.visit(std::move(instr.src_barrier), ts_b32, false, false);
  if (!src_barrier)
    return tl::unexpected(src_barrier.error());
  auto src_predicate =
      v.visit(std::move(instr.src_predicate), ts_pred, false, false);
  if (!src_predicate)
    return tl::unexpected(src_predicate.error());
  auto src_negate_predicate =
      v.visit(std::move(instr.src_negate_predicate), ts_pred, false, false);
  if (!src_negate_predicate)
    return tl::unexpected(src_negate_predicate.error());

  std::optional<To> src_threadcount;
  if (instr.src_threadcount) {
    auto r = v.visit(std::move(*instr.src_threadcount), ts_b32, false, false);
    if (!r)
      return tl::unexpected(r.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return tl::unexpected(src3.error());
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
    return tl::unexpected(dst.error());
  auto src1 = v.visit(std::move(instr.src1), ts, false, false);
  if (!src1)
    return tl::unexpected(src1.error());
  auto src2 = v.visit(std::move(instr.src2), ts, false, false);
  if (!src2)
    return tl::unexpected(src2.error());
  auto src3 = v.visit(std::move(instr.src3), ts, false, false);
  if (!src3)
    return tl::unexpected(src3.error());
  auto src4 = v.visit(std::move(instr.src4), ts, false, false);
  if (!src4)
    return tl::unexpected(src4.error());
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
    return tl::unexpected(dst.error());
  auto src_a = v.visit(std::move(instr.src_a), ts, false, false);
  if (!src_a)
    return tl::unexpected(src_a.error());
  auto src_b = v.visit(std::move(instr.src_b), ts, false, false);
  if (!src_b)
    return tl::unexpected(src_b.error());
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
    return tl::unexpected(src.error());
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
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBrev<To>, Err> map_instr(InstrBrev<From> instr,
                                       VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return tl::unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return tl::unexpected(src.error());
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
      return tl::unexpected(r.error());
    new_args.return_arguments.push_back(std::move(*r));
  }

  {
    auto r = v.visit_ident(std::move(instr.arguments.func), std::nullopt, false,
                           false);
    if (!r)
      return tl::unexpected(r.error());
    new_args.func = std::move(*r);
  }

  for (size_t i = 0; i < instr.arguments.input_arguments.size(); ++i) {
    Type t = instr.data.input_arguments[i].first;
    VisitTypeSpace ts{&t, instr.data.input_arguments[i].second};
    auto r = v.visit(std::move(instr.arguments.input_arguments[i]), ts, false,
                     false);
    if (!r)
      return tl::unexpected(r.error());
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
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrClz<To>, Err> map_instr(InstrClz<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  ScalarType st = instr.data;
  Type t = make_scalar(st);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return tl::unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return tl::unexpected(src.error());
  return InstrClz<To>{instr.data, *dst, *src};
}

};  // namespace ptx_frontend
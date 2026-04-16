#pragma once
#include "ptx_ir/base.hpp"
#include "ptx_ir/instr.hpp"
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
    // CallDetails::return_arguments may be unpopulated (e.g. when the parser
    // has no type information at the call site); guard against out-of-bounds.
    std::optional<VisitTypeSpace> ts;
    if (i < instr.data.return_arguments.size()) {
      Type t = instr.data.return_arguments[i].first;
      ts = VisitTypeSpace{&t, instr.data.return_arguments[i].second};
    }
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
    // Same guard for input_arguments.
    std::optional<VisitTypeSpace> ts;
    if (i < instr.data.input_arguments.size()) {
      Type t = instr.data.input_arguments[i].first;
      ts = VisitTypeSpace{&t, instr.data.input_arguments[i].second};
    }
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
          if (d.mode == MulIntControl::Wide) {
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
          if (d.mode == MulIntControl::Wide) {
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
  Type t_d = make_scalar(instr.data.dtype);
  VisitTypeSpace ts_d{&t_d, StateSpace::Reg};
  Type t_a = make_scalar(instr.data.atype);
  VisitTypeSpace ts_a{&t_a, StateSpace::Reg};
  Type t_b = make_scalar(instr.data.btype);
  VisitTypeSpace ts_b{&t_b, StateSpace::Reg};
  Type t_c = make_scalar(instr.data.dtype);
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
  Type t_d = make_scalar(instr.data.dtype);
  VisitTypeSpace ts_d{&t_d, StateSpace::Reg};
  Type t_a = make_scalar(instr.data.atype);
  VisitTypeSpace ts_a{&t_a, StateSpace::Reg};
  Type t_b = make_scalar(instr.data.btype);
  VisitTypeSpace ts_b{&t_b, StateSpace::Reg};
  Type t_c = make_scalar(instr.data.dtype);
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

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrFence<Op>& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrFence<To>, Err> map_instr(InstrFence<From> instr,
                                        VisitorMap<From, To, Err>& /*v*/) {
  return InstrFence<To>{instr.data};
}

// ── InstrRed ───────────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrRed<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = instr.data.typ;
  VisitTypeSpace ts_addr{&t, instr.data.space};
  VisitTypeSpace ts_src{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.addr, ts_addr, false, false); !r)
    return r;
  return v.visit(instr.src, ts_src, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrRed<To>, Err> map_instr(InstrRed<From> instr,
                                      VisitorMap<From, To, Err>& v) {
  Type t = instr.data.typ;
  VisitTypeSpace ts_addr{&t, instr.data.space};
  VisitTypeSpace ts_src{&t, StateSpace::Reg};
  auto addr = v.visit(std::move(instr.addr), ts_addr, false, false);
  if (!addr)
    return unexpected(addr.error());
  auto src = v.visit(std::move(instr.src), ts_src, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrRed<To>{instr.data, *addr, *src};
}

// ── InstrMbarrierInit ──────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMbarrierInit<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_addr = make_scalar(ScalarType::B64);
  Type t_count =
      make_scalar(ScalarType::U32);  // note: ptx8.0+ requires this to u64
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Shared};
  VisitTypeSpace ts_count{&t_count, StateSpace::Reg};
  if (auto r = v.visit(instr.addr, ts_addr, false, false); !r)
    return r;
  return v.visit(instr.count, ts_count, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMbarrierInit<To>, Err> map_instr(InstrMbarrierInit<From> instr,
                                               VisitorMap<From, To, Err>& v) {
  Type t_addr = make_scalar(ScalarType::B64);
  Type t_count = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Shared};
  VisitTypeSpace ts_count{&t_count, StateSpace::Reg};
  auto addr = v.visit(std::move(instr.addr), ts_addr, false, false);
  if (!addr)
    return unexpected(addr.error());
  auto count = v.visit(std::move(instr.count), ts_count, false, false);
  if (!count)
    return unexpected(count.error());
  return InstrMbarrierInit<To>{*addr, *count};
}

// ── InstrMbarrierInval ─────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMbarrierInval<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Shared};
  return v.visit(instr.addr, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMbarrierInval<To>, Err> map_instr(InstrMbarrierInval<From> instr,
                                                VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Shared};
  auto addr = v.visit(std::move(instr.addr), ts, false, false);
  if (!addr)
    return unexpected(addr.error());
  return InstrMbarrierInval<To>{*addr};
}

// ── InstrMbarrierArrive ────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMbarrierArrive<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_b64 = make_scalar(ScalarType::B64);
  Type t_u64 = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_shared{&t_b64, StateSpace::Shared};
  VisitTypeSpace ts_token{&t_b64, StateSpace::Reg};
  VisitTypeSpace ts_count{&t_u64, StateSpace::Reg};
  if (instr.token.has_value()) {
    if (auto r = v.visit(*instr.token, ts_token, true, false); !r)
      return r;
  }
  if (auto r = v.visit(instr.addr, ts_shared, false, false); !r)
    return r;
  if (instr.count.has_value()) {
    if (auto r = v.visit(*instr.count, ts_count, false, false); !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMbarrierArrive<To>, Err> map_instr(
    InstrMbarrierArrive<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_b64 = make_scalar(ScalarType::B64);
  Type t_u64 = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_shared{&t_b64, StateSpace::Shared};
  VisitTypeSpace ts_token{&t_b64, StateSpace::Reg};
  VisitTypeSpace ts_count{&t_u64, StateSpace::Reg};
  std::optional<To> token;
  if (instr.token.has_value()) {
    auto r = v.visit(std::move(*instr.token), ts_token, true, false);
    if (!r)
      return unexpected(r.error());
    token = std::move(*r);
  }
  auto addr = v.visit(std::move(instr.addr), ts_shared, false, false);
  if (!addr)
    return unexpected(addr.error());
  std::optional<To> count;
  if (instr.count.has_value()) {
    auto r = v.visit(std::move(*instr.count), ts_count, false, false);
    if (!r)
      return unexpected(r.error());
    count = std::move(*r);
  }
  return InstrMbarrierArrive<To>{instr.data, std::move(token), *addr,
                                 std::move(count)};
}

// ── InstrMbarrierTestWait ──────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMbarrierTestWait<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  Type t_b64 = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  VisitTypeSpace ts_shared{&t_b64, StateSpace::Shared};
  VisitTypeSpace ts_token{&t_b64, StateSpace::Reg};
  if (auto r = v.visit(instr.done, ts_pred, true, false); !r)
    return r;
  if (auto r = v.visit(instr.addr, ts_shared, false, false); !r)
    return r;
  return v.visit(instr.token_or_parity, ts_token, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMbarrierTestWait<To>, Err> map_instr(
    InstrMbarrierTestWait<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  Type t_b64 = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  VisitTypeSpace ts_shared{&t_b64, StateSpace::Shared};
  VisitTypeSpace ts_token{&t_b64, StateSpace::Reg};
  auto done = v.visit(std::move(instr.done), ts_pred, true, false);
  if (!done)
    return unexpected(done.error());
  auto addr = v.visit(std::move(instr.addr), ts_shared, false, false);
  if (!addr)
    return unexpected(addr.error());
  auto tok = v.visit(std::move(instr.token_or_parity), ts_token, false, false);
  if (!tok)
    return unexpected(tok.error());
  return InstrMbarrierTestWait<To>{instr.data, *done, *addr, *tok};
}

// ── InstrMbarrierTryWait ───────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMbarrierTryWait<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  Type t_b64 = make_scalar(ScalarType::B64);
  Type t_u64 = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  VisitTypeSpace ts_shared{&t_b64, StateSpace::Shared};
  VisitTypeSpace ts_token{&t_b64, StateSpace::Reg};
  VisitTypeSpace ts_timeout{&t_u64, StateSpace::Reg};
  if (auto r = v.visit(instr.done, ts_pred, true, false); !r)
    return r;
  if (auto r = v.visit(instr.addr, ts_shared, false, false); !r)
    return r;
  if (auto r = v.visit(instr.token_or_parity, ts_token, false, false); !r)
    return r;
  if (instr.timeout_ns.has_value()) {
    if (auto r = v.visit(*instr.timeout_ns, ts_timeout, false, false); !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMbarrierTryWait<To>, Err> map_instr(
    InstrMbarrierTryWait<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  Type t_b64 = make_scalar(ScalarType::B64);
  Type t_u64 = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  VisitTypeSpace ts_shared{&t_b64, StateSpace::Shared};
  VisitTypeSpace ts_token{&t_b64, StateSpace::Reg};
  VisitTypeSpace ts_timeout{&t_u64, StateSpace::Reg};
  auto done = v.visit(std::move(instr.done), ts_pred, true, false);
  if (!done)
    return unexpected(done.error());
  auto addr = v.visit(std::move(instr.addr), ts_shared, false, false);
  if (!addr)
    return unexpected(addr.error());
  auto tok = v.visit(std::move(instr.token_or_parity), ts_token, false, false);
  if (!tok)
    return unexpected(tok.error());
  std::optional<To> timeout;
  if (instr.timeout_ns.has_value()) {
    auto r = v.visit(std::move(*instr.timeout_ns), ts_timeout, false, false);
    if (!r)
      return unexpected(r.error());
    timeout = std::move(*r);
  }
  return InstrMbarrierTryWait<To>{instr.data, *done, *addr, *tok,
                                  std::move(timeout)};
}

// ── InstrMbarrierExpectTx ──────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMbarrierExpectTx<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_addr = make_scalar(ScalarType::B64);
  Type t_count = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Shared};
  VisitTypeSpace ts_count{&t_count, StateSpace::Reg};
  if (auto r = v.visit(instr.addr, ts_addr, false, false); !r)
    return r;
  return v.visit(instr.tx_count, ts_count, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMbarrierExpectTx<To>, Err> map_instr(
    InstrMbarrierExpectTx<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_addr = make_scalar(ScalarType::B64);
  Type t_count = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Shared};
  VisitTypeSpace ts_count{&t_count, StateSpace::Reg};
  auto addr = v.visit(std::move(instr.addr), ts_addr, false, false);
  if (!addr)
    return unexpected(addr.error());
  auto tx_count = v.visit(std::move(instr.tx_count), ts_count, false, false);
  if (!tx_count)
    return unexpected(tx_count.error());
  return InstrMbarrierExpectTx<To>{instr.data, *addr, *tx_count};
}

// ── InstrMbarrierCompleteTx ────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMbarrierCompleteTx<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_addr = make_scalar(ScalarType::B64);
  Type t_count = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Shared};
  VisitTypeSpace ts_count{&t_count, StateSpace::Reg};
  if (auto r = v.visit(instr.addr, ts_addr, false, false); !r)
    return r;
  return v.visit(instr.tx_count, ts_count, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMbarrierCompleteTx<To>, Err> map_instr(
    InstrMbarrierCompleteTx<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_addr = make_scalar(ScalarType::B64);
  Type t_count = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Shared};
  VisitTypeSpace ts_count{&t_count, StateSpace::Reg};
  auto addr = v.visit(std::move(instr.addr), ts_addr, false, false);
  if (!addr)
    return unexpected(addr.error());
  auto tx_count = v.visit(std::move(instr.tx_count), ts_count, false, false);
  if (!tx_count)
    return unexpected(tx_count.error());
  return InstrMbarrierCompleteTx<To>{instr.data, *addr, *tx_count};
}

// ── InstrStMatrix ──────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrStMatrix<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_addr = make_scalar(ScalarType::B64);
  Type t_src = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Shared};
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  if (auto r = v.visit(instr.addr, ts_addr, false, false); !r)
    return r;
  for (const auto& src : instr.srcs) {
    if (auto r = v.visit(src, ts_src, false, false); !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrStMatrix<To>, Err> map_instr(InstrStMatrix<From> instr,
                                           VisitorMap<From, To, Err>& v) {
  Type t_addr = make_scalar(ScalarType::B64);
  Type t_src = make_scalar(ScalarType::B32);
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Shared};
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  auto addr = v.visit(std::move(instr.addr), ts_addr, false, false);
  if (!addr)
    return unexpected(addr.error());
  std::vector<To> srcs;
  srcs.reserve(instr.srcs.size());
  for (auto& src : instr.srcs) {
    auto r = v.visit(std::move(src), ts_src, false, false);
    if (!r)
      return unexpected(r.error());
    srcs.push_back(std::move(*r));
  }
  return InstrStMatrix<To>{instr.data, *addr, std::move(srcs)};
}

// ── InstrWgmmaMmaAsync ─────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrWgmmaMmaAsync<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_d = make_scalar(instr.data.dtype);
  Type t_desc = make_scalar(ScalarType::B64);  // smem descriptor handle
  VisitTypeSpace ts_d{&t_d, StateSpace::Reg};
  VisitTypeSpace ts_desc{&t_desc, StateSpace::Reg};
  for (const auto& d : instr.d_srcs) {
    if (auto r = v.visit(d, ts_d, true, false); !r)
      return r;
  }
  // A: register (atype) or smem descriptor (b64), use relaxed check
  if (auto r = v.visit(instr.a, ts_desc, false, /*relaxed*/ true); !r)
    return r;
  return v.visit(instr.b, ts_desc, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrWgmmaMmaAsync<To>, Err> map_instr(InstrWgmmaMmaAsync<From> instr,
                                                VisitorMap<From, To, Err>& v) {
  Type t_d = make_scalar(instr.data.dtype);
  Type t_desc = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_d{&t_d, StateSpace::Reg};
  VisitTypeSpace ts_desc{&t_desc, StateSpace::Reg};
  std::vector<To> d_srcs;
  d_srcs.reserve(instr.d_srcs.size());
  for (auto& d : instr.d_srcs) {
    auto r = v.visit(std::move(d), ts_d, true, false);
    if (!r)
      return unexpected(r.error());
    d_srcs.push_back(std::move(*r));
  }
  auto a = v.visit(std::move(instr.a), ts_desc, false, true);
  if (!a)
    return unexpected(a.error());
  auto b = v.visit(std::move(instr.b), ts_desc, false, false);
  if (!b)
    return unexpected(b.error());
  return InstrWgmmaMmaAsync<To>{instr.data, std::move(d_srcs), *a, *b};
}

// ── InstrTcgen05Alloc ──────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05Alloc<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_cols = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Reg};
  VisitTypeSpace ts_cols{&t_cols, StateSpace::Reg};
  if (auto r = v.visit(instr.tptr, ts_ptr, true, false); !r)
    return r;
  return v.visit(instr.ncols, ts_cols, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05Alloc<To>, Err> map_instr(InstrTcgen05Alloc<From> instr,
                                               VisitorMap<From, To, Err>& v) {
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_cols = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Reg};
  VisitTypeSpace ts_cols{&t_cols, StateSpace::Reg};
  auto tptr = v.visit(std::move(instr.tptr), ts_ptr, true, false);
  if (!tptr)
    return unexpected(tptr.error());
  auto ncols = v.visit(std::move(instr.ncols), ts_cols, false, false);
  if (!ncols)
    return unexpected(ncols.error());
  return InstrTcgen05Alloc<To>{instr.data, *tptr, *ncols};
}

// ── InstrTcgen05Dealloc ────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05Dealloc<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_cols = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Reg};
  VisitTypeSpace ts_cols{&t_cols, StateSpace::Reg};
  if (auto r = v.visit(instr.tptr, ts_ptr, false, false); !r)
    return r;
  return v.visit(instr.ncols, ts_cols, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05Dealloc<To>, Err> map_instr(
    InstrTcgen05Dealloc<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_cols = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Reg};
  VisitTypeSpace ts_cols{&t_cols, StateSpace::Reg};
  auto tptr = v.visit(std::move(instr.tptr), ts_ptr, false, false);
  if (!tptr)
    return unexpected(tptr.error());
  auto ncols = v.visit(std::move(instr.ncols), ts_cols, false, false);
  if (!ncols)
    return unexpected(ncols.error());
  return InstrTcgen05Dealloc<To>{instr.data, *tptr, *ncols};
}

// ── InstrTcgen05Ld ─────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05Ld<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_dst = make_scalar(instr.data.elem_type);
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_off = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Tmem};
  VisitTypeSpace ts_off{&t_off, StateSpace::Reg};
  for (const auto& d : instr.dsts) {
    if (auto r = v.visit(d, ts_dst, true, false); !r)
      return r;
  }
  if (auto r = v.visit(instr.tptr, ts_ptr, false, false); !r)
    return r;
  if (instr.offset.has_value()) {
    if (auto r = v.visit(*instr.offset, ts_off, false, false); !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05Ld<To>, Err> map_instr(InstrTcgen05Ld<From> instr,
                                            VisitorMap<From, To, Err>& v) {
  Type t_dst = make_scalar(instr.data.elem_type);
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_off = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Tmem};
  VisitTypeSpace ts_off{&t_off, StateSpace::Reg};
  std::vector<To> dsts;
  dsts.reserve(instr.dsts.size());
  for (auto& d : instr.dsts) {
    auto r = v.visit(std::move(d), ts_dst, true, false);
    if (!r)
      return unexpected(r.error());
    dsts.push_back(std::move(*r));
  }
  auto tptr = v.visit(std::move(instr.tptr), ts_ptr, false, false);
  if (!tptr)
    return unexpected(tptr.error());
  std::optional<To> offset;
  if (instr.offset.has_value()) {
    auto r = v.visit(std::move(*instr.offset), ts_off, false, false);
    if (!r)
      return unexpected(r.error());
    offset = std::move(*r);
  }
  return InstrTcgen05Ld<To>{instr.data, std::move(dsts), *tptr,
                            std::move(offset)};
}

// ── InstrTcgen05St ─────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05St<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_src = make_scalar(instr.data.elem_type);
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_off = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Tmem};
  VisitTypeSpace ts_off{&t_off, StateSpace::Reg};
  if (auto r = v.visit(instr.tptr, ts_ptr, false, false); !r)
    return r;
  if (instr.offset.has_value()) {
    if (auto r = v.visit(*instr.offset, ts_off, false, false); !r)
      return r;
  }
  for (const auto& s : instr.srcs) {
    if (auto r = v.visit(s, ts_src, false, false); !r)
      return r;
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05St<To>, Err> map_instr(InstrTcgen05St<From> instr,
                                            VisitorMap<From, To, Err>& v) {
  Type t_src = make_scalar(instr.data.elem_type);
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_off = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_src{&t_src, StateSpace::Reg};
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Tmem};
  VisitTypeSpace ts_off{&t_off, StateSpace::Reg};
  auto tptr = v.visit(std::move(instr.tptr), ts_ptr, false, false);
  if (!tptr)
    return unexpected(tptr.error());
  std::optional<To> offset;
  if (instr.offset.has_value()) {
    auto r = v.visit(std::move(*instr.offset), ts_off, false, false);
    if (!r)
      return unexpected(r.error());
    offset = std::move(*r);
  }
  std::vector<To> srcs;
  srcs.reserve(instr.srcs.size());
  for (auto& s : instr.srcs) {
    auto r = v.visit(std::move(s), ts_src, false, false);
    if (!r)
      return unexpected(r.error());
    srcs.push_back(std::move(*r));
  }
  return InstrTcgen05St<To>{instr.data, *tptr, std::move(offset),
                            std::move(srcs)};
}

// ── InstrTcgen05Wait ───────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05Wait<Op>& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05Wait<To>, Err> map_instr(
    InstrTcgen05Wait<From> instr, VisitorMap<From, To, Err>& /*v*/) {
  return InstrTcgen05Wait<To>{instr.data};
}

// ── InstrTcgen05Shift ──────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05Shift<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Tmem};
  return v.visit(instr.tptr, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05Shift<To>, Err> map_instr(InstrTcgen05Shift<From> instr,
                                               VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B32);
  VisitTypeSpace ts{&t, StateSpace::Tmem};
  auto tptr = v.visit(std::move(instr.tptr), ts, false, false);
  if (!tptr)
    return unexpected(tptr.error());
  return InstrTcgen05Shift<To>{*tptr};
}

// ── InstrTcgen05CommitArrival ──────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05CommitArrival<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_mbar = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Tmem};
  VisitTypeSpace ts_mbar{&t_mbar, StateSpace::Shared};
  if (auto r = v.visit(instr.tptr, ts_ptr, false, false); !r)
    return r;
  return v.visit(instr.mbar, ts_mbar, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05CommitArrival<To>, Err> map_instr(
    InstrTcgen05CommitArrival<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_ptr = make_scalar(ScalarType::B32);
  Type t_mbar = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Tmem};
  VisitTypeSpace ts_mbar{&t_mbar, StateSpace::Shared};
  auto tptr = v.visit(std::move(instr.tptr), ts_ptr, false, false);
  if (!tptr)
    return unexpected(tptr.error());
  auto mbar = v.visit(std::move(instr.mbar), ts_mbar, false, false);
  if (!mbar)
    return unexpected(mbar.error());
  return InstrTcgen05CommitArrival<To>{*tptr, *mbar};
}

// ── InstrTcgen05Relinquish ─────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05Relinquish<Op>& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05Relinquish<To>, Err> map_instr(
    InstrTcgen05Relinquish<From> /*instr*/, VisitorMap<From, To, Err>& /*v*/) {
  return InstrTcgen05Relinquish<To>{};
}

// ── InstrTcgen05Fence ──────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05Fence<Op>& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05Fence<To>, Err> map_instr(
    InstrTcgen05Fence<From> instr, VisitorMap<From, To, Err>& /*v*/) {
  return InstrTcgen05Fence<To>{instr.data};
}

// ── InstrTcgen05Cp ─────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05Cp<Op>& instr,
                                Visitor<Op, Err>& v) {
  // dst: tmem ptr (b32), src: smem addr (b64), count: u32
  Type t_tmem = make_scalar(ScalarType::B32);
  Type t_smem = make_scalar(ScalarType::B64);
  Type t_cnt = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_tmem{&t_tmem, StateSpace::Tmem};
  VisitTypeSpace ts_smem{&t_smem, StateSpace::Shared};
  VisitTypeSpace ts_cnt{&t_cnt, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_tmem, false, false); !r)
    return r;
  if (auto r = v.visit(instr.src, ts_smem, false, false); !r)
    return r;
  return v.visit(instr.count, ts_cnt, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05Cp<To>, Err> map_instr(InstrTcgen05Cp<From> instr,
                                            VisitorMap<From, To, Err>& v) {
  Type t_tmem = make_scalar(ScalarType::B32);
  Type t_smem = make_scalar(ScalarType::B64);
  Type t_cnt = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_tmem{&t_tmem, StateSpace::Tmem};
  VisitTypeSpace ts_smem{&t_smem, StateSpace::Shared};
  VisitTypeSpace ts_cnt{&t_cnt, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_tmem, false, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts_smem, false, false);
  if (!src)
    return unexpected(src.error());
  auto count = v.visit(std::move(instr.count), ts_cnt, false, false);
  if (!count)
    return unexpected(count.error());
  return InstrTcgen05Cp<To>{instr.data, *dst, *src, *count};
}

// ── InstrTcgen05MbarrierExpectTx ───────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTcgen05MbarrierExpectTx<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_mbar = make_scalar(ScalarType::B64);
  Type t_count = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_mbar{&t_mbar, StateSpace::Shared};
  VisitTypeSpace ts_count{&t_count, StateSpace::Reg};
  if (auto r = v.visit(instr.mbar, ts_mbar, false, false); !r)
    return r;
  return v.visit(instr.tx_count, ts_count, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTcgen05MbarrierExpectTx<To>, Err> map_instr(
    InstrTcgen05MbarrierExpectTx<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_mbar = make_scalar(ScalarType::B64);
  Type t_count = make_scalar(ScalarType::U64);
  VisitTypeSpace ts_mbar{&t_mbar, StateSpace::Shared};
  VisitTypeSpace ts_count{&t_count, StateSpace::Reg};
  auto mbar = v.visit(std::move(instr.mbar), ts_mbar, false, false);
  if (!mbar)
    return unexpected(mbar.error());
  auto tx_count = v.visit(std::move(instr.tx_count), ts_count, false, false);
  if (!tx_count)
    return unexpected(tx_count.error());
  return InstrTcgen05MbarrierExpectTx<To>{*mbar, *tx_count};
}

// ── InstrClusterBarrier ────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrClusterBarrier<Op>& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrClusterBarrier<To>, Err> map_instr(
    InstrClusterBarrier<From> instr, VisitorMap<From, To, Err>& /*v*/) {
  return InstrClusterBarrier<To>{instr.data};
}

// ── InstrSetMaxNReg ────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrSetMaxNReg<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::U32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  return v.visit(instr.count, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrSetMaxNReg<To>, Err> map_instr(InstrSetMaxNReg<From> instr,
                                             VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::U32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto count = v.visit(std::move(instr.count), ts, false, false);
  if (!count)
    return unexpected(count.error());
  return InstrSetMaxNReg<To>{instr.data, *count};
}

// ── InstrGetCtaRank ────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrGetCtaRank<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_dst = make_scalar(ScalarType::U32);
  Type t_addr = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Generic};
  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  return v.visit(instr.addr, ts_addr, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrGetCtaRank<To>, Err> map_instr(InstrGetCtaRank<From> instr,
                                             VisitorMap<From, To, Err>& v) {
  Type t_dst = make_scalar(ScalarType::U32);
  Type t_addr = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_dst{&t_dst, StateSpace::Reg};
  VisitTypeSpace ts_addr{&t_addr, StateSpace::Generic};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto addr = v.visit(std::move(instr.addr), ts_addr, false, false);
  if (!addr)
    return unexpected(addr.error());
  return InstrGetCtaRank<To>{*dst, *addr};
}

template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrBrkpt& /*instr*/,
                                Visitor<Op, Err>& /*v*/) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrBrkpt, Err> map_instr(InstrBrkpt /*instr*/,
                                    VisitorMap<From, To, Err>& /*v*/) {
  return InstrBrkpt{};
}

// ── InstrElectSync ─────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrElectSync<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  Type t_mask = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  VisitTypeSpace ts_mask{&t_mask, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_pred, true, false); !r)
    return r;
  return v.visit(instr.membermask, ts_mask, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrElectSync<To>, Err> map_instr(InstrElectSync<From> instr,
                                            VisitorMap<From, To, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  Type t_mask = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  VisitTypeSpace ts_mask{&t_mask, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_pred, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto mask = v.visit(std::move(instr.membermask), ts_mask, false, false);
  if (!mask)
    return unexpected(mask.error());
  return InstrElectSync<To>{*dst, *mask};
}

// ── InstrDiscard ───────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrDiscard<Op>& instr,
                                Visitor<Op, Err>& v) {
  // addr is a pointer in global or local space; line size encodes b64/b128
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, instr.data.space};
  return v.visit(instr.addr, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrDiscard<To>, Err> map_instr(const InstrDiscard<From>& instr,
                                          VisitorMap<From, To, Err>& v) {
  // addr is a pointer in global or local space; line size encodes b64/b128
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, instr.data.space};
  auto addr = v.visit(instr.addr, ts, false, false);
  if (!addr)
    return unexpected(addr.error());
  return InstrDiscard<To>{instr.data, *addr};
}

// ── InstrMovMatrix ─────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrMovMatrix<Op>& instr,
                                Visitor<Op, Err>& v) {
  // movmatrix.sync.aligned.m8n8.trans.b16 [dst], [src];
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Shared};
  if (auto r = v.visit(instr.dst, ts, true, false); !r)
    return r;
  return v.visit(instr.src, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrMovMatrix<To>, Err> map_instr(InstrMovMatrix<From> instr,
                                            VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Shared};
  auto dst = v.visit(std::move(instr.dst), ts, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts, false, false);
  if (!src)
    return unexpected(src.error());
  return InstrMovMatrix<To>{instr.data, *dst, *src};
}

// ── InstrCpAsyncBulk ───────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCpAsyncBulk<Op>& instr,
                                Visitor<Op, Err>& v) {
  // cp.async.bulk.shared::cluster.global [dst], [src], bytes;
  Type t_ptr = make_scalar(ScalarType::B64);
  Type t_sz = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_dst{&t_ptr, StateSpace::SharedCluster};
  VisitTypeSpace ts_src{&t_ptr, StateSpace::Global};
  VisitTypeSpace ts_sz{&t_sz, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_dst, true, false); !r)
    return r;
  if (auto r = v.visit(instr.src, ts_src, false, false); !r)
    return r;
  return v.visit(instr.size, ts_sz, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCpAsyncBulk<To>, Err> map_instr(InstrCpAsyncBulk<From> instr,
                                              VisitorMap<From, To, Err>& v) {
  Type t_ptr = make_scalar(ScalarType::B64);
  Type t_sz = make_scalar(ScalarType::U32);
  VisitTypeSpace ts_dst{&t_ptr, StateSpace::SharedCluster};
  VisitTypeSpace ts_src{&t_ptr, StateSpace::Global};
  VisitTypeSpace ts_sz{&t_sz, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_dst, true, false);
  if (!dst)
    return unexpected(dst.error());
  auto src = v.visit(std::move(instr.src), ts_src, false, false);
  if (!src)
    return unexpected(src.error());
  auto size = v.visit(std::move(instr.size), ts_sz, false, false);
  if (!size)
    return unexpected(size.error());
  return InstrCpAsyncBulk<To>{*dst, *src, *size};
}

// ── InstrCpAsyncBulkTensor ─────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCpAsyncBulkTensor<Op>& instr,
                                Visitor<Op, Err>& v) {
  bool to_shared =
      instr.data.direction == CpAsyncBulkTensorDir::GlobalToSharedCluster;
  Type t_ptr = make_scalar(ScalarType::B64);
  Type t_coord = make_scalar(ScalarType::U32);
  StateSpace dst_space =
      to_shared ? StateSpace::SharedCluster : StateSpace::Global;
  VisitTypeSpace ts_dst{&t_ptr, dst_space};
  VisitTypeSpace ts_tmap{&t_ptr, StateSpace::Global};
  VisitTypeSpace ts_coord{&t_coord, StateSpace::Reg};
  if (auto r = v.visit(instr.dst, ts_dst, to_shared, false); !r)
    return r;
  if (auto r = v.visit(instr.tensormap, ts_tmap, false, false); !r)
    return r;
  for (const auto& c : instr.coords) {
    if (auto r = v.visit(c, ts_coord, false, false); !r)
      return r;
  }
  if (instr.offsets) {
    for (const auto& off : *instr.offsets) {
      if (auto r = v.visit(off, ts_coord, false, false); !r)
        return r;
    }
  }
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCpAsyncBulkTensor<To>, Err> map_instr(
    InstrCpAsyncBulkTensor<From> instr, VisitorMap<From, To, Err>& v) {
  bool to_shared =
      instr.data.direction == CpAsyncBulkTensorDir::GlobalToSharedCluster;
  Type t_ptr = make_scalar(ScalarType::B64);
  Type t_coord = make_scalar(ScalarType::U32);
  StateSpace dst_space =
      to_shared ? StateSpace::SharedCluster : StateSpace::Global;
  VisitTypeSpace ts_dst{&t_ptr, dst_space};
  VisitTypeSpace ts_tmap{&t_ptr, StateSpace::Global};
  VisitTypeSpace ts_coord{&t_coord, StateSpace::Reg};
  auto dst = v.visit(std::move(instr.dst), ts_dst, to_shared, false);
  if (!dst)
    return unexpected(dst.error());
  auto tmap = v.visit(std::move(instr.tensormap), ts_tmap, false, false);
  if (!tmap)
    return unexpected(tmap.error());
  std::vector<To> coords;
  coords.reserve(instr.coords.size());
  for (auto& c : instr.coords) {
    auto r = v.visit(std::move(c), ts_coord, false, false);
    if (!r)
      return unexpected(r.error());
    coords.push_back(std::move(*r));
  }
  std::optional<std::vector<To>> offsets;
  if (instr.offsets) {
    std::vector<To> offs;
    offs.reserve(instr.offsets->size());
    for (auto& off : *instr.offsets) {
      auto r = v.visit(std::move(off), ts_coord, false, false);
      if (!r)
        return unexpected(r.error());
      offs.push_back(std::move(*r));
    }
    offsets = std::move(offs);
  }
  return InstrCpAsyncBulkTensor<To>{instr.data, *dst, *tmap, std::move(coords),
                                    std::move(offsets)};
}

// ── InstrCpAsyncBulkCommitGroup ────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCpAsyncBulkCommitGroup&,
                                Visitor<Op, Err>&) {
  return {};
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCpAsyncBulkCommitGroup, Err> map_instr(
    InstrCpAsyncBulkCommitGroup, VisitorMap<From, To, Err>&) {
  return InstrCpAsyncBulkCommitGroup{};
}

// ── InstrCpAsyncBulkWaitGroup ──────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrCpAsyncBulkWaitGroup<Op>& instr,
                                Visitor<Op, Err>& v) {
  Type t = make_scalar(ScalarType::U32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  return v.visit(instr.n, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrCpAsyncBulkWaitGroup<To>, Err> map_instr(
    InstrCpAsyncBulkWaitGroup<From> instr, VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::U32);
  VisitTypeSpace ts{&t, StateSpace::Reg};
  auto n = v.visit(std::move(instr.n), ts, false, false);
  if (!n)
    return unexpected(n.error());
  return InstrCpAsyncBulkWaitGroup<To>{*n};
}

// ── InstrTensormapReplace ──────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTensormapReplace<Op>& instr,
                                Visitor<Op, Err>& v) {
  // tensormap.replace [tmap_ptr], new_val;
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_ptr{&t, StateSpace::Global};
  VisitTypeSpace ts_val{&t, StateSpace::Reg};
  if (auto r = v.visit(instr.tmap_ptr, ts_ptr, false, false); !r)
    return r;
  return v.visit(instr.new_val, ts_val, false, /*relaxed*/ true);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTensormapReplace<To>, Err> map_instr(
    InstrTensormapReplace<From> instr, VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_ptr{&t, StateSpace::Global};
  VisitTypeSpace ts_val{&t, StateSpace::Reg};
  auto tmap = v.visit(std::move(instr.tmap_ptr), ts_ptr, false, false);
  if (!tmap)
    return unexpected(tmap.error());
  auto val = v.visit(std::move(instr.new_val), ts_val, false, true);
  if (!val)
    return unexpected(val.error());
  return InstrTensormapReplace<To>{instr.data, *tmap, *val};
}

// ── InstrTensormapCpFenceProxy ─────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrTensormapCpFenceProxy<Op>& instr,
                                Visitor<Op, Err>& v) {
  // tensormap.cp_fenceproxy.acquire.sem.scope.shared::cta.b128 [addr];
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Shared};
  return v.visit(instr.addr, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrTensormapCpFenceProxy<To>, Err> map_instr(
    InstrTensormapCpFenceProxy<From> instr, VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Shared};
  auto addr = v.visit(std::move(instr.addr), ts, false, false);
  if (!addr)
    return unexpected(addr.error());
  return InstrTensormapCpFenceProxy<To>{instr.data, *addr};
}

// ── InstrPrefetchu ─────────────────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrPrefetchu<Op>& instr,
                                Visitor<Op, Err>& v) {
  // prefetchu.L1 [addr];  addr in generic space
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Generic};
  return v.visit(instr.addr, ts, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrPrefetchu<To>, Err> map_instr(InstrPrefetchu<From> instr,
                                            VisitorMap<From, To, Err>& v) {
  Type t = make_scalar(ScalarType::B64);
  VisitTypeSpace ts{&t, StateSpace::Generic};
  auto addr = v.visit(std::move(instr.addr), ts, false, false);
  if (!addr)
    return unexpected(addr.error());
  return InstrPrefetchu<To>{instr.level, *addr};
}

// ── InstrClusterLaunchControl ──────────────────────────────────────────────
template <OperandLike Op, typename Err>
expected<void, Err> visit_instr(const InstrClusterLaunchControl<Op>& instr,
                                Visitor<Op, Err>& v) {
  // try_cancel: no dst;  query_cancel: pred output dst
  Type t_pred = make_scalar(ScalarType::Pred);
  Type t_ptr = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Generic};
  if (instr.dst) {
    if (auto r = v.visit(*instr.dst, ts_pred, true, false); !r)
      return r;
  }
  return v.visit(instr.addr, ts_ptr, false, false);
}

template <OperandLike From, OperandLike To, typename Err>
expected<InstrClusterLaunchControl<To>, Err> map_instr(
    InstrClusterLaunchControl<From> instr, VisitorMap<From, To, Err>& v) {
  Type t_pred = make_scalar(ScalarType::Pred);
  Type t_ptr = make_scalar(ScalarType::B64);
  VisitTypeSpace ts_pred{&t_pred, StateSpace::Reg};
  VisitTypeSpace ts_ptr{&t_ptr, StateSpace::Generic};
  std::optional<To> dst;
  if (instr.dst) {
    auto r = v.visit(std::move(*instr.dst), ts_pred, true, false);
    if (!r)
      return unexpected(r.error());
    dst = std::move(*r);
  }
  auto addr = v.visit(std::move(instr.addr), ts_ptr, false, false);
  if (!addr)
    return unexpected(addr.error());
  return InstrClusterLaunchControl<To>{instr.data, std::move(dst), *addr};
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
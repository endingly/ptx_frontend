#pragma once
#include "ptx_ir/base.hpp"

namespace ptx_frontend {

// ============================================================
// § 8  Data structs for instruction semantics
//       (one-to-one with ast.rs structs / enums)
// ============================================================

// --- RoundingMode ---
enum class RoundingMode { NearestEven, Zero, NegativeInf, PositiveInf };

// --- LdStQualifier ---
enum class LdStQualifier { Weak, Volatile, Relaxed, Acquire, Release };
struct LdStQualifierData {
  LdStQualifier kind = LdStQualifier::Weak;
  MemScope scope{};  // used for Relaxed / Acquire / Release
};

// --- Cache operators ---
enum class LdCacheOperator { Cached, L2Only, Streaming, LastUse, Uncached };
enum class StCacheOperator { Writeback, L2Only, Streaming, Writethrough };
enum class CpAsyncCacheOperator { Cached, L2Only };

// --- ArithInteger / ArithFloat / ArithDetails ---
struct ArithInteger {
  ScalarType type_;
  bool saturate = false;
};
struct ArithFloat {
  ScalarType type_;
  RoundingMode rounding = RoundingMode::NearestEven;
  std::optional<bool> flush_to_zero;  // nullopt = not applicable
  bool saturate = false;
  bool is_fusable = false;  // see ZLUDA comment about fused mul/add
};
using ArithDetails = std::variant<ArithInteger, ArithFloat>;

// --- TypeFtz (Abs, Neg, Rsqrt, Ex2) ---
struct TypeFtz {
  std::optional<bool> flush_to_zero;
  ScalarType type_;
};

// --- FlushToZero (Sin, Cos, Lg2, Ex2 approx path) ---
struct FlushToZero {
  bool flush_to_zero;
};

// --- MulIntControl ---
enum class MulIntControl { Low, High, Wide };

// --- MulDetails ---
struct MulInt {
  ScalarType type_;
  MulIntControl control;
};
using MulDetails = std::variant<MulInt, ArithFloat>;

// --- Mul24Details ---
enum class Mul24Control { Lo, Hi };
struct Mul24Details {
  ScalarType type_;
  Mul24Control control;
};

// --- MadDetails ---
struct MadInt {
  MulIntControl control;
  bool saturate;
  ScalarType type_;
};
using MadDetails = std::variant<MadInt, ArithFloat>;

// --- CarryKind / CarryDetails / MadCarryDetails ---
enum class CarryKind { CarryIn, CarryOut, CarryInCarryOut };
struct CarryDetails {
  CarryKind kind;
  ScalarType type_;
};
struct MadCarryDetails {
  CarryKind kind;
  MulIntControl control;
  ScalarType type_;
};

// --- MinMaxFloat / MinMaxDetails ---
struct MinMaxFloat {
  std::optional<bool> flush_to_zero;
  bool nan;
  ScalarType type_;
};
enum class MinMaxSign { Signed, Unsigned };
struct MinMaxInt {
  MinMaxSign sign;
  ScalarType type_;
};
using MinMaxDetails = std::variant<MinMaxInt, MinMaxFloat>;

// --- DivDetails ---
struct DivFloat {
  ScalarType type_;
  std::optional<bool> flush_to_zero;
  enum class Kind { Approx, ApproxFull, Rounding } kind;
  RoundingMode rounding = RoundingMode::NearestEven;
};
enum class DivSign { Unsigned, Signed };
struct DivInt {
  DivSign sign;
  ScalarType type_;
};
using DivDetails = std::variant<DivInt, DivFloat>;

// --- RcpKind / RcpData ---
struct RcpCompliant {
  RoundingMode rounding;
};
using RcpKind = std::variant<std::monostate /*Approx*/, RcpCompliant>;
struct RcpData {
  RcpKind kind;
  std::optional<bool> flush_to_zero;
  ScalarType type_;
};

// --- LdDetails / StData ---
struct LdDetails {
  LdStQualifierData qualifier;
  StateSpace state_space;
  LdCacheOperator caching = LdCacheOperator::Cached;
  Type typ;
  bool non_coherent = false;
};
struct StData {
  LdStQualifierData qualifier;
  StateSpace state_space;
  StCacheOperator caching = StCacheOperator::Writeback;
  Type typ;
};

// --- MovDetails ---
struct MovDetails {
  Type typ;
};

// --- RetData ---
struct RetData {
  bool uniform = false;
};

// --- BarData / BarRedData ---
enum class Reduction { And, Or, Popc };
struct BarData {
  bool aligned;
};
struct BarRedData {
  bool aligned;
  Reduction pred_reduction;
};

// --- ShflSync ---
enum class ShuffleMode { Up, Down, Bfly, Idx };
struct ShflSyncDetails {
  ShuffleMode mode;
};

// --- ShfDetails (funnel shift) ---
enum class ShiftDirection { Left, Right };
enum class FunnelShiftMode { Clamp, Wrap };
struct ShfDetails {
  ShiftDirection direction;
  FunnelShiftMode mode;
};

// --- ShrData ---
enum class RightShiftKind { Arithmetic, Logical };
struct ShrData {
  ScalarType type_;
  RightShiftKind kind;
};

// --- CvtMode / CvtDetails ---
struct CvtModeZeroExtend {};
struct CvtModeSignExtend {};
struct CvtModeTruncate {};
struct CvtModeBitcast {};
struct CvtModeIntSatSigned {};
struct CvtModeIntSatUnsigned {};
struct CvtModeFPExtend {
  std::optional<bool> flush_to_zero;
  bool saturate;
};
struct CvtModeFPTruncate {
  RoundingMode rounding;
  bool is_integer_rounding;
  std::optional<bool> flush_to_zero;
  bool saturate;
  bool relu;
};
struct CvtModeFPRound {
  std::optional<RoundingMode> integer_rounding;
  std::optional<bool> flush_to_zero;
  bool saturate;
};
struct CvtModeSignedFromFP {
  RoundingMode rounding;
  std::optional<bool> flush_to_zero;
};
struct CvtModeUnsignedFromFP {
  RoundingMode rounding;
  std::optional<bool> flush_to_zero;
};
struct CvtModeFPFromSigned {
  RoundingMode rounding;
  bool saturate;
};
struct CvtModeFPFromUnsigned {
  RoundingMode rounding;
  bool saturate;
};

using CvtMode =
    std::variant<CvtModeZeroExtend, CvtModeSignExtend, CvtModeTruncate,
                 CvtModeBitcast, CvtModeIntSatSigned, CvtModeIntSatUnsigned,
                 CvtModeFPExtend, CvtModeFPTruncate, CvtModeFPRound,
                 CvtModeSignedFromFP, CvtModeUnsignedFromFP,
                 CvtModeFPFromSigned, CvtModeFPFromUnsigned>;
struct CvtDetails {
  ScalarType from;
  ScalarType to;
  CvtMode mode;
};

// --- CvtaDetails ---
enum class CvtaDirection { GenericToExplicit, ExplicitToGeneric };
struct CvtaDetails {
  StateSpace state_space;
  CvtaDirection direction;
};

// --- SetpCompareOp ---
enum class SetpCompareInt {
  Eq,
  NotEq,
  UnsignedLess,
  UnsignedLessOrEq,
  UnsignedGreater,
  UnsignedGreaterOrEq,
  SignedLess,
  SignedLessOrEq,
  SignedGreater,
  SignedGreaterOrEq,
};
enum class SetpCompareFloat {
  Eq,
  NotEq,
  Less,
  LessOrEq,
  Greater,
  GreaterOrEq,
  NanEq,
  NanNotEq,
  NanLess,
  NanLessOrEq,
  NanGreater,
  NanGreaterOrEq,
  IsNotNan,
  IsAnyNan,
};
using SetpCompareOp = std::variant<SetpCompareInt, SetpCompareFloat>;

enum class SetpBoolPostOp { And, Or, Xor };

struct SetpData {
  ScalarType type_;
  std::optional<bool> flush_to_zero;
  SetpCompareOp cmp_op;
};
struct SetpBoolData {
  SetpData base;
  SetpBoolPostOp bool_op;
  bool negate_src3;
};
struct SetData {
  ScalarType dtype;
  SetpData base;
};
struct SetBoolData {
  ScalarType dtype;
  SetpBoolData base;
};

// --- AtomDetails ---
enum class AtomSemantics { Relaxed, Acquire, Release, AcqRel };
enum class AtomicOp {
  And,
  Or,
  Xor,
  Exchange,
  Add,
  IncrementWrap,
  DecrementWrap,
  SignedMin,
  UnsignedMin,
  SignedMax,
  UnsignedMax,
  FloatAdd,
  FloatMin,
  FloatMax,
  CASCompareSwap
};
struct AtomDetails {
  Type typ;
  AtomSemantics semantics;
  MemScope scope;
  StateSpace space;
  AtomicOp op;
};
struct AtomCasDetails {
  ScalarType type_;
  AtomSemantics semantics;
  MemScope scope;
  StateSpace space;
};

// --- VoteDetails ---
enum class VoteMode { All, Any, Ballot };
struct VoteDetails {
  VoteMode mode;
  bool negate;
};

// --- ReduxSyncData ---
struct ReduxSyncData {
  ScalarType type_;
  Reduction reduction;
};

// --- LdMatrixDetails ---
enum class LdStMatrixShape {
  M8N8,   // ldmatrix original
  M16N8,  // stmatrix
  M8N16,  // stmatrix
};

enum class MatrixNumber { X1, X2, X4 };
struct LdMatrixDetails {
  LdStMatrixShape shape;
  MatrixNumber number;
  bool transpose;
  StateSpace state_space;
  ScalarType type_;

  // TODO: deperate
  Type get_loaded_type() const {
    uint8_t count = number == MatrixNumber::X1   ? 1
                    : number == MatrixNumber::X2 ? 2
                                                 : 4;
    return make_vector(count, ScalarType::B32);
  }
};

// --- MmaDetails ---
enum class MatrixLayout { RowMajor, ColMajor };

// mma/wmma shape (row × col × K-depth)
enum class MmaShape {
  // warp-level mma (represent the legacy .ptx 7.0 wmma shapes)
  M8N8K4,    // f64
  M16N8K8,   // tf32
  M16N8K16,  // f16, bf16
  M16N8K32,  // s8, u8, s4, u4, b1
  M16N8K64,  // e4m3, e5m2 (FP8, sm_90+)
};

struct MmaDetails {
  MmaShape shape;
  MatrixLayout alayout, blayout;
  ScalarType dtype;  // accumulator / output type
  ScalarType atype;  // A-matrix element type
  ScalarType btype;  // B-matrix element type
  bool saturate;     // .satfinite (integer path)
};

// --- Dp4aDetails / Dp2aDetails ---
struct Dp4aDetails {
  ScalarType atype, btype;
  ScalarType ctype() const {
    return (atype == ScalarType::U32 && btype == ScalarType::U32)
               ? ScalarType::U32
               : ScalarType::S32;
  }
};
enum class Dp2aControl { Low, High };
struct Dp2aData {
  ScalarType atype, btype;
  Dp2aControl control;
  ScalarType ctype() const {
    return (atype == ScalarType::U32 && btype == ScalarType::U32)
               ? ScalarType::U32
               : ScalarType::S32;
  }
};

// --- CpAsyncDetails ---
enum class CpAsyncCpSize { Bytes4, Bytes8, Bytes16 };
struct CpAsyncDetails {
  CpAsyncCacheOperator caching;
  StateSpace space;  // dst space
  CpAsyncCpSize cp_size;
  std::optional<uint64_t> src_size;
};

// --- CreatePolicyFractionalDetails ---
enum class EvictionPriority { Normal, PreferL2, LastUse, NoAllocate };
struct CreatePolicyFractionalDetails {
  EvictionPriority primary_priority;
  float fraction;
};

// --- PrefetchData ---
enum class CacheLevel { L1, L2 };
struct PrefetchData {
  StateSpace space;
  CacheLevel level;
};

// --- BmskMode ---
enum class BmskMode { Clamp, Wrap };

// --- GridDepControlAction ---
enum class GridDepControlAction { LaunchDependents, WaitOnDependent };

// --- CallDetails / CallArgs ---
struct CallDetails {
  bool uniform = false;
  std::vector<std::pair<Type, StateSpace>> return_arguments;
  std::vector<std::pair<Type, StateSpace>> input_arguments;
};

template <IdentLike Id>
struct CallArgs {
  std::vector<Id> return_arguments;
  Id func;
  std::vector<ParsedOperand<Id>> input_arguments;
  bool is_external = false;  // see ZLUDA hack comment
};

enum class FenceSemantics { Sc, AcqRel, Acquire, Release };

// fence.sc.cta / fence.acq_rel.gpu .eg
struct FenceThread {
  std::optional<FenceSemantics> sem;
  MemScope scope;
};

// fence.acquire/release.sync_restrict::shared::{cta,cluster}.scope
enum class FenceSyncRestrictSpace { SharedCta, SharedCluster };

struct FenceSyncRestrict {
  FenceSemantics sem;  // .acquire or .release
  FenceSyncRestrictSpace restrict_space;
  MemScope scope;  // only .cluster
};

// fence.proxy.tensormap::generic.sem.scope — one proxy
// fence.proxy.tensormap::generic.acquire.scope [addr], size — acquire with address operand
struct FenceProxyTensormapUnidir {
  FenceSemantics sem;  // .release or .acquire
  MemScope scope;
  // .acquire with [addr], size as InstrFence operands
};

// fence.op_restrict.release.scope
// .op_restrict = { .mbarrier_init } (only for mbarrier.init)
struct FenceOpRestrict {
  MemScope scope;
};

// fence.proxy.proxykind — double proxy，non sem/scope operand
// .proxykind = { .alias, .async, .async.global,
//                .async.shared::cta, .async.shared::cluster }
enum class FenceProxyKind {
  Alias,
  Async,
  AsyncGlobal,
  AsyncSharedCta,
  AsyncSharedCluster,
};

struct FenceProxyBidir {
  FenceProxyKind kind;
};

// fence.proxy.async::generic.sem.sync_restrict::shared::{cta,cluster}.scope
struct FenceProxyAsyncGeneric {
  FenceSemantics sem;
  FenceSyncRestrictSpace restrict_space;
  MemScope scope;
};

using FenceDetails = std::variant<FenceThread, FenceSyncRestrict,
                                  FenceOpRestrict, FenceProxyTensormapUnidir,
                                  FenceProxyBidir, FenceProxyAsyncGeneric>;

struct RedDetails {
  AtomSemantics semantics;
  MemScope scope;
  StateSpace space;
  AtomicOp op;
  Type typ;
};

// mbarrier.init.shared::cta.b64 [addr], count;
struct MbarrierInitDetails {};

// mbarrier.inval.shared::cta.b64 [addr];
struct MbarrierInvalDetails {};

// mbarrier.arrive[.noComplete].shared::cta.b64 [token=], [addr] {, count};
// mbarrier.arrive.release.{cta,cluster}.shared::cta.b64 [token=], [addr];
// mbarrier.arrive_drop.shared::cta.b64 [addr];
struct MbarrierArriveDetails {
  bool drop;                              // arrive_drop vs arrive
  bool no_complete;                       // .noComplete variant
  std::optional<MemScope> release_scope;  // nullopt = no release sem
};

// mbarrier.test_wait[.parity].shared::cta.b64 done, [addr], token_or_parity;
struct MbarrierTestWaitDetails {
  bool parity;
};

// mbarrier.try_wait[.parity].shared::cta.b64 done, [addr], token_or_parity {, ns};
struct MbarrierTryWaitDetails {
  bool parity;
  bool has_timeout;
};

// mbarrier.expect_tx.release.cta.shared::cta.b64 [addr], txcount;
struct MbarrierExpectTxDetails {
  MemScope scope;
};

// mbarrier.complete_tx.release.cta.shared::cta.b64 [addr], txcount;
struct MbarrierCompleteTxDetails {
  MemScope scope;
};

struct StMatrixDetails {
  LdStMatrixShape shape;  // see 2.2
  MatrixNumber number;    // X1, X2, X4
  bool transpose;
  // state_space is always .shared, encoded here for type checking
};

enum class WgmmaShape {
  // f16 / bf16 / e4m3 / e5m2 variants, K=16 (f16/bf16) or K=32 (fp8)
  M64N8K16,
  M64N16K16,
  M64N24K16,
  M64N32K16,
  M64N40K16,
  M64N48K16,
  M64N56K16,
  M64N64K16,
  M64N72K16,
  M64N80K16,
  M64N88K16,
  M64N96K16,
  M64N104K16,
  M64N112K16,
  M64N120K16,
  M64N128K16,
  M64N192K16,
  M64N256K16,
  // tf32, K=8
  M64N8K8,
  M64N16K8,
  M64N32K8,
  M64N64K8,
  M64N128K8,
  // s8/u8, K=32
  M64N8K32,
  M64N16K32,
  M64N32K32,
  M64N64K32,
  M64N128K32,
  M64N256K32,
  // f64, K=4
  M64N8K4,
  M64N16K4,
};

enum class WgmmaInput { Register, Shared };  // A matrix source

struct WgmmaMmaAsyncDetails {
  WgmmaShape shape;
  MatrixLayout blayout;  // B always in shared memory, column-major or row-major
  ScalarType dtype;      // accumulator type: f32, f16, bf16, f64, s32
  ScalarType atype;      // A element type
  ScalarType btype;      // B element type (always in shared memory)
  WgmmaInput a_source;   // A comes from register or shared memory
  bool scale_d;          // .scale_d (initialize D with 0 or 1)
  std::optional<bool> flush_to_zero;
  bool saturate;  // .satfinite (integer path)
};

enum class Tcgen05Shape {
  Shape16x64b,
  Shape16x128b,
  Shape16x256b,
  Shape32x32b,
  Shape32x64b,
  Shape32x128b,
  Shape16x32bx2,  // 双 CTA 变体
};

enum class Tcgen05CtaGroup { Cta1, Cta2 };

struct Tcgen05AllocDetails {
  Tcgen05CtaGroup cta_group;
};
struct Tcgen05DeallocDetails {};

struct Tcgen05LdStDetails {
  Tcgen05Shape shape;
  bool transpose;        // .b16x2 行序变体
  bool pack;             // .pack modifier
  bool has_offset;       // 可选 column offset 操作数
  ScalarType elem_type;  // b16 / b32 / b8x4 等
};

enum class Tcgen05WaitKind { Ld, St };
struct Tcgen05WaitDetails {
  Tcgen05WaitKind kind;
};

enum class Tcgen05FenceKind { BeforeThreadSync, AfterThreadSync };
struct Tcgen05FenceDetails {
  Tcgen05FenceKind kind;
};

struct Tcgen05ShiftDetails {};
struct Tcgen05CommitArrivalDetails {};
struct Tcgen05RelinquishDetails {};

enum class Tcgen05CpShape { Shape128x, Shape4x256b };
enum class Tcgen05CpDir { SmemToTmem, TmemToSmem };
struct Tcgen05CpDetails {
  Tcgen05CpShape shape;
  Tcgen05CpDir direction;
};

struct Tcgen05MbarrierExpectTxDetails {};

/* barrier.cluster.arrive.aligned */

enum class ClusterBarrierOp { Arrive, ArriveDrop, Wait };
struct ClusterBarrierDetails {
  ClusterBarrierOp op;
  bool aligned;
};

// setmaxnreg.{inc,dec}.sync.aligned.u32 regcount;
enum class SetMaxNRegOp { Inc, Dec };
struct SetMaxNRegDetails {
  SetMaxNRegOp op;
};

// barrier.cluster see 3.7

// cp.async.bulk.shared::cluster.global [dst], [src], bytes;
struct CpAsyncBulkDetails {};

// cp.async.bulk.tensor.Nd.shared::cluster.global [dst], [tensormap], coords…;
enum class TensorDim { D1, D2, D3, D4, D5 };
enum class CpAsyncBulkTensorDir { GlobalToSharedCluster, SharedCtaToGlobal };
struct CpAsyncBulkTensorDetails {
  TensorDim dim;
  CpAsyncBulkTensorDir direction;
};

// tensormap.replace.tile.field.b1024 [tmapPtr], newval;
enum class TensormapReplaceField {
  GlobalAddress,
  Rank,
  BoxDimensions,
  GlobalStrides,
  ElementStrides,
  InterleaveLayout,
  SwizzlingMode,
  FillMode,
};
struct TensormapReplaceDetails {
  TensormapReplaceField field;
};

// tensormap.cp_fenceproxy.acquire.sys.shared::cta.b128 [addr];
struct TensormapCpFenceProxyDetails {
  AtomSemantics sem;
  MemScope scope;
};

// elect.sync — non data struct (only have operand -> dst pred, membermask）

// discard.{global,local}.b{64,128} [addr];
struct DiscardDetails {
  StateSpace space;
};

// movmatrix.sync.aligned.m8n8.trans.b16 dst, src; (sm_90a)
struct MovMatrixDetails {
  MatrixNumber number;
  bool transpose;
};

// clusterlaunchcontrol
enum class CLCOp { TryCancel, QueryCancel };
struct ClusterLaunchControlDetails {
  CLCOp op;
};

};  // namespace ptx_frontend
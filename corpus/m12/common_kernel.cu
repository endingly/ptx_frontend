// SPDX-License-Identifier: MIT
//
// Project-authored M12 common-kernel fixture. Each named kernel pins one
// minimal inline-PTX form for its roadmap issue; it is not natural compiler emission.
// Regenerate this and the natural-emission lane with:
// python3 python/scripts/regenerate_m12_corpus.py --nvcc /path/to/nvcc

#define M12_ASM(...) asm volatile("{" __VA_ARGS__ "}")

extern "C" __global__ void m12_i02_set() { M12_ASM(".reg .u32 r0, r1, r2; .reg .f32 f0; .reg .pred p0; set.eq.u32.u32 r0, r1, r2; set.lt.and.f32.s32 f0, r1, r2, p0;"); }
extern "C" __global__ void m12_i03_setp() { M12_ASM(".reg .u32 r0, r1; .reg .pred p0, p1, p2; setp.eq.u32 p0|p1, r0, r1; setp.lt.and.s32 p0|p1, r0, r1, p2;"); }
extern "C" __global__ void m12_i04_slct() { M12_ASM(".reg .u32 r0, r1, r2, r3; .reg .u64 rd0, rd1, rd2; .reg .f32 f0; slct.u32.s32 r0, r1, r2, r3; slct.ftz.u64.f32 rd0, rd1, rd2, f0;"); }
extern "C" __global__ void m12_i05_add() { M12_ASM(".reg .u32 r0, r1, r2; .reg .u64 rd0, rd1, rd2; .reg .f32 f0, f1, f2; add.u32 r0, r1, r2; add.s32 r0, r1, r2; add.u64 rd0, rd1, rd2; add.f32 f0, f1, f2;"); }
extern "C" __global__ void m12_i06_sub() { M12_ASM(".reg .u32 r0, r1, r2; .reg .u64 rd0, rd1, rd2; .reg .f32 f0, f1, f2; sub.u32 r0, r1, r2; sub.s32 r0, r1, r2; sub.u64 rd0, rd1, rd2; sub.f32 f0, f1, f2;"); }
extern "C" __global__ void m12_i07_mul() { M12_ASM(".reg .u32 r0, r1, r2; .reg .u64 rd0; .reg .f32 f0, f1, f2; mul.hi.u32 r0, r1, r2; mul.wide.u32 rd0, r1, r2; mul.rn.f32 f0, f1, f2;"); }
extern "C" __global__ void m12_i08_mad() { M12_ASM(".reg .u32 r0, r1, r2, r3; .reg .u64 rd0, rd1; .reg .f32 f0, f1, f2, f3; mad.lo.s32 r0, r1, r2, r3; mad.wide.u32 rd0, r1, r2, rd1; mad.rn.f32 f0, f1, f2, f3;"); }
extern "C" __global__ void m12_i09_fma() { M12_ASM(".reg .f16 h0, h1, h2, h3; .reg .f32 f0, f1, f2, f3; .reg .f64 d0, d1, d2, d3; fma.rn.f16 h0, h1, h2, h3; fma.rn.f32 f0, f1, f2, f3; fma.rn.f64 d0, d1, d2, d3;"); }
extern "C" __global__ void m12_i10_div() { M12_ASM(".reg .u32 r0, r1, r2; .reg .f32 f0, f1, f2; .reg .f64 d0, d1, d2; div.s32 r0, r1, r2; div.rn.f32 f0, f1, f2; div.rn.f64 d0, d1, d2;"); }
extern "C" __global__ void m12_i11_rem() { M12_ASM(".reg .u32 r0, r1, r2; rem.s32 r0, r1, 0; rem.u32 r0, r1, r2;"); }
extern "C" __global__ void m12_i12_min() { M12_ASM(".reg .u32 r0, r1, r2; .reg .f32 f0, f1, f2; min.s32 r0, r1, r2; min.NaN.f32 f0, f1, f2;"); }
extern "C" __global__ void m12_i13_max() { M12_ASM(".reg .u32 r0, r1, r2; .reg .f32 f0, f1, f2; max.s32 r0, r1, r2; max.NaN.f32 f0, f1, f2;"); }
extern "C" __global__ void m12_i14_abs() { M12_ASM(".reg .u32 r0, r1; .reg .f32 f0, f1; abs.s32 r0, r1; abs.f32 f0, f1;"); }
extern "C" __global__ void m12_i15_neg() { M12_ASM(".reg .u32 r0, r1; .reg .f32 f0, f1; .reg .b32 b0, b1; neg.s32 r0, r1; neg.f32 f0, f1; neg.f16x2 b0, b1;"); }
extern "C" __global__ void m12_i16_lop3() { M12_ASM(".reg .u32 r0, r1, r2, r3; lop3.b32 r0, r1, r2, r3, 0x1a;"); }
extern "C" __global__ void m12_i17_shf() { M12_ASM(".reg .u32 r0, r1, r2; shf.l.clamp.b32 r0, r1, r2, 8; shf.r.wrap.b32 r0, r1, r2, 8;"); }
extern "C" __global__ void m12_i18_prmt() { M12_ASM(".reg .u32 r0, r1, r2, r3; prmt.b32 r0, r1, r2, 0x5410; prmt.b32.f4e r0, r1, r2, r3;"); }
extern "C" __global__ void m12_i19_popc() { M12_ASM(".reg .u32 r0, r1; popc.b32 r0, r1;"); }
extern "C" __global__ void m12_i20_clz() { M12_ASM(".reg .u32 r0, r1; .reg .b64 b0; clz.b32 r0, r1; clz.b64 r0, b0;"); }
extern "C" __global__ void m12_i21_bfind() { M12_ASM(".reg .u32 r0, r1; bfind.shiftamt.u32 r0, r1;"); }
extern "C" __global__ void m12_i22_bfe() { M12_ASM(".reg .u32 r0, r1; bfe.u32 r0, r1, 0, 8;"); }
extern "C" __global__ void m12_i23_bfi() { M12_ASM(".reg .u32 r0, r1, r2; bfi.b32 r0, r1, r2, 0, 8;"); }
extern "C" __global__ void m12_i24_brev() { M12_ASM(".reg .u32 r0, r1; brev.b32 r0, r1;"); }
extern "C" __global__ void m12_i25_cvt() { M12_ASM(".reg .u32 r0; .reg .f32 f0; .reg .b32 b0; cvt.rn.f32.s32 f0, r0; cvt.rzi.u32.f32 r0, f0; cvt.rn.f16x2.f32 b0, f0, f0;"); }
extern "C" __global__ void m12_i26_cvt_pack() { M12_ASM(".reg .u32 r0, r1, r2, r3; cvt.pack.sat.u8.s32.b32 r0, r1, r2, r3;"); }
extern "C" __global__ void m12_i27_isspacep() { M12_ASM(".reg .u64 rd0; .reg .pred p0; isspacep.global p0, rd0;"); }
extern "C" __global__ void m12_i28_ld_global_nc() { M12_ASM(".reg .u32 r0; .reg .u64 rd0; ld.global.nc.L1::no_allocate.u32 r0, [rd0];"); }
extern "C" __global__ void m12_i29_prefetchu() { M12_ASM(".reg .u64 rd0; prefetchu.L1 [rd0];"); }
extern "C" __global__ void m12_i30_createpolicy() { M12_ASM(".reg .u64 rd0; createpolicy.fractional.L2::evict_last.b64 rd0, 0.5;"); }
extern "C" __global__ void m12_i31_applypriority() { M12_ASM(".reg .u64 rd0; applypriority.global.L2::evict_normal [rd0], 128;"); }
extern "C" __global__ void m12_i32_discard() { M12_ASM(".reg .u64 rd0; discard.global.L2 [rd0], 128;"); }

#if __CUDA_ARCH__ == 900
extern "C" __global__ void m12_i33_setmaxnreg() { M12_ASM("setmaxnreg.inc.sync.aligned.u32 192;"); }
#endif

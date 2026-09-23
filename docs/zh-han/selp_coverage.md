# SELP 覆盖情况

Frontend 建模 [PTX ISA 9.3 §9.7.6.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-selp) 的 ordinary `selp` form。唯一的 machine-readable contract 是 `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`。Parsing、owned resolution 和 checker 保留选定 type 及 operand；frontend 不执行选择运算。

`selp.type d, a, b, c` 接受 `.b16/.b32/.b64`、`.u16/.u32/.u64`、`.s16/.s32/.s64` 以及 `.f32/.f64`。destination 是 register；`a` 和 `b` 是 register-or-immediate source，按选定 type 和 PTX 普通 register 兼容规则检查。Predicate source `c` 接受普通或取反的 predicate register，也接受 integer predicate constant。Constant 遵循 PTX 的 C truth rule：零为 false、非零为 true，`!` 对结果取反。不接受 predicate special register 或 floating constant。这依据 [PTX ISA 9.3 §4.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#predicate-constants) 及 CUDA 13.3 `ptxas` 对 `selp.s32` 使用 `!%p` 和 `1` 的 probe。四个 operand 均为必需，`.type` 之外不接受 instruction modifier。现有 `selp_u32` generated public variant 继续对应 `.u32`，其余 type 使用与之不重叠的 `selp_scalar` variant。

全部 form 的 PTX minimum 为 1.0。`.f64` 另需 `sm_13`；其余 type 没有本 instruction 额外的 target minimum。Target-aware checker 拒绝在更早 SM 上使用 `.f64`，owned-IR revalidation 会拒绝被改成选定 variant domain 之外的 type。Frontend 不求值 predicate，也不复制选定 value。

[C++ SELP 测试](../../submod/resolved_ir/test/test_selp_completeness.cpp) 覆盖经验证 module 中的所有 type、已有 U32 variant、无效 modifier/operand、声明 type 不匹配、`.f64` target 边界以及被修改的 public IR。[Python spec 测试](../../python/tests/spec/test_selp_completeness.py) 验证不重叠的 type domain、value availability 和 operand descriptor。Ordinary `set`、half/bfloat `set` 与更广的 `slct` form 属于后续独立覆盖工作。

C++ package version 从 0.1.0 升至 0.2.0，因为 `Selp::U32::predicate.value` 现在保存 `ResolvedPredicateSource`，原先为 `ResolvedPredicate`。读取 register predicate 的 consumer 需先检查 variant alternative，再使用 `std::get<ResolvedPredicate>(value)`；source text 为 integer predicate 时还需处理 `ResolvedPredicateConstant`。`Selp::U32` variant identity 不变。这是 source 与 binary API 变更；installed C++ consumer 需使用新 header 与 library 重新构建。

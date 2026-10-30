# 浮点 `div` 覆盖

`div.f32` 与 `div.f64` 额外支持 PTX 1.0–1.3 的 exact omitted-control form。前者解释为 approximate+FTZ；后者为无 FTZ 的 round-to-nearest-even，接受 SM >=13 或启用 `map_f64_to_f32` 的精确 generic SM10/11/12。两者在 PTX >=1.4 或遗漏 mode 却写出 `.ftz` 时拒绝。 source omission 与 typed interpretation 分离；见[公共 contract](../resolved_ir_design.md#历史浮点控制省略形式)。

生成模型保留既有 integer 与 round-to-nearest floating division form，并加入 [PTX 9.3 §9.7.3.8](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-div) 的 explicit mode。每种 mode 都是 typed Resolved IR variant，因此 `.approx`、`.full` 与 `.rnd` 不会成为可任意组合的 unchecked spelling flag。frontend 建模 parsing、owned resolution 与 target validation，不执行 division。

| Form | Minimum | Contract |
| --- | --- | --- |
| `div.approx{.ftz}.f32`、`div.full{.ftz}.f32` | PTX 1.4 / 所有 target architecture | 固定 typed `.approx` 或 `.full`；`.ftz` optional；没有 rounding modifier。 |
| `div.{rn,rz,rm,rp}{.ftz}.f32` | PTX 1.4 / SM 20 | 必须有 typed rounding；`.ftz` optional。 |
| `div.rn.f64` | PTX 1.4 / SM >=13，或精确 generic SM10/11/12 加 `map_f64_to_f32` | 固定 round-to-nearest；没有 `.ftz`。 |
| `div.{rz,rm,rp}.f64` | PTX 1.4 / SM 20 | 必须有 directed rounding；没有 `.ftz`。 |

每个 floating source 可为 floating literal 或同宽 native floating/bit register container；destination 可为相应 native 或 bit register container。integer literal 和 integer register container、错误宽度、sink、PTX >=1.4 时遗漏 explicit mode、混合 `.approx`/`.full`/rounding form，以及 FP64 `.ftz` 都会被拒绝。

历史 CUDA 13.3.73 证据使用 `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`。60 个 module 的 matrix 有 39 个预期接受、20 个拒绝，以及一个 permissive assembler acceptance：`rcp.rn.ftz.f64`。该不符合 canonical syntax 的 form 仍由 frontend 拒绝。DIV row 验证四个 rounded direction、两个 source position、literal 与 bit container。

[explicit reciprocal 与 square-root form](unary_float_coverage.md)、[浮点 transcendental](transcendental_coverage.md) 与 [浮点 MIN/MAX](min_max_coverage.md) 已单独建模。[浮点与 mixed ADD/SUB](add_sub_coverage.md) 完成对账；ADD/SUB 的 contract 见该文档。

已实现的 mapped-FP64 可用替代条件
（[#287](https://github.com/endingly/ptx_frontend/issues/287)）严格限于 PTX >=1.4、
精确 generic SM10/11/12 加 `map_f64_to_f32` 时的 explicit `div.rn.f64`、`rcp.rn.f64`
和 `sqrt.rn.f64`。Native 可用条件仍为 SM >=13，无须该 option。
Directed rounding、approximate-FTZ form 和其他 FP64 instruction family 不增加该替代条件。

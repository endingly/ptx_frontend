# 浮点 reciprocal 与 square-root 覆盖

PTX 1.0–1.3 还接受近似且 FTZ 的 `rcp.f32`、`sqrt.f32`、`rsqrt.f32`，无 FTZ 的 RN `rcp.f64`、`sqrt.f64`，以及无 FTZ 的近似 `rsqrt.f64`。FP64 要求 SM >=13；`rcp.f64` 和 `sqrt.f64` 也允许精确 generic SM10/11/12 加 `map_f64_to_f32`。原生 SM13 source header 要求 PTX >=1.2；映射 SM10/11 支持 PTX1.0，SM12 要求 PTX1.2。裸形式从 PTX1.4 起拒绝。`RcpImplicitF64` 与显式 `RcpRnF64` 分离，仅暴露静态 RN 解释，不伪造写入的 `.rn`、`.approx` 或 `.ftz` 控制。历史 CUDA 13.3.33 assembler 差异仅作为证据，不作为 source 接受规则。source omission 与类型化解释分离，见[公共契约](../resolved_ir_design.md#历史浮点控制省略形式)。

生成模型覆盖 [reciprocal](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rcp)、[square root](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sqrt)、[reciprocal square root](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rsqrt) 的 explicit form，也覆盖特殊的 [RCP](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rcp-approx-ftz-f64) 与 [RSQRT](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-rsqrt-approx-ftz-f64) FP64 approximate-FTZ instruction。approximate 与 rounded mode 是独立 typed variant；frontend 进行 resolution 和 validation，不执行数值 operation。

| Form | Minimum | Contract |
| --- | --- | --- |
| `rcp.approx{.ftz}.f32`、`sqrt.approx{.ftz}.f32`、`rsqrt.approx{.ftz}.f32` | PTX 1.4 / 所有 target architecture | 固定 `.approx`；FP32 `.ftz` optional。 |
| `rcp.rnd{.ftz}.f32`、`sqrt.rnd{.ftz}.f32` | PTX 2.0 / SM 20 | 必须有 typed `rn`、`rz`、`rm` 或 `rp`；FP32 `.ftz` optional。 |
| `rcp.rn.f64`、`sqrt.rn.f64` | PTX 1.4 / SM >=13，或精确 generic SM10/11/12 加 `map_f64_to_f32` | 固定 typed round-to-nearest；没有 `.ftz`。 |
| `rcp.{rz,rm,rp}.f64`、`sqrt.{rz,rm,rp}.f64` | PTX 2.0 / SM 20 | 必须有 directed rounding；没有 `.ftz`。 |
| `rsqrt.approx.f64` | PTX 1.4 / SM 13 | 固定 `.approx`；没有 `.ftz`。 |
| `rcp.approx.ftz.f64` | PTX 2.1 / SM 20 | 固定 `.approx` 且必须有 typed `.ftz`。 |
| `rsqrt.approx.ftz.f64` | PTX 4.0 / SM 20 | 固定 `.approx` 且必须有 typed `.ftz`。 |

每个 source 可为 floating literal 或同宽 native floating/bit register container，destination 可为匹配的 native/bit container。integer literal 和 integer container、错误宽度、sink、PTX >=1.4 时遗漏 mode、混合 approximate/rounding spelling，以及所有 rounded FP64 `.ftz` spelling 都会被拒绝。历史探针记录 CUDA 13.3.73 `ptxas` 曾 permissively 接受 `rcp.rn.ftz.f64`；canonical PTX syntax 排除它，因此 frontend 会拒绝它。

历史 CUDA 13.3.73 证据使用 `/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`：60 个 matrix module 有 39 个预期接受、20 个拒绝和一个已记录的 permissive FP64-FTZ acceptance；四个 focused special-FP64 immediate/container probe 都通过。

[浮点 transcendental](transcendental_coverage.md) 与 [浮点 MIN/MAX](min_max_coverage.md) 已单独建模。[浮点与 mixed ADD/SUB](add_sub_coverage.md) 完成对账；ADD/SUB 的 contract 见该文档。

已实现的 mapped-FP64 可用替代条件
（[#287](https://github.com/endingly/ptx_frontend/issues/287)）严格限于 PTX >=1.4、
精确 generic SM10/11/12 加 `map_f64_to_f32` 时的 explicit `div.rn.f64`、`rcp.rn.f64`
和 `sqrt.rn.f64`。Native 可用条件仍为 SM >=13，无须该 option。
Directed rounding、approximate-FTZ form 和其他 FP64 instruction family 不增加该替代条件。

# 核心 opcode 完整性审计

## 范围与证据边界

本审计记录 11 个常用 PTX operation name 的状态：`mov`、`add`、`sub`、`mul`、
`setp`、`ld`、`st`、`bar`、`bra`、`exit` 与 `fma`。它是
[issue #51](https://github.com/endingly/ptx_frontend/issues/51) 的证据记录；生成出的
variant 集合非空不等于实现了完整 PTX ISA 9.3。

规范性基准是 archived [PTX ISA 9.3 instruction
set](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)，
包括 operand、availability、state space、memory ordering 以及独立命名的 instruction
规则。下表的 database 证据由
`ptx_frontend.code_gen.database.load_codegen_database()` 从
`python/code_gen/resources/ptx_spec` 载入；它是派生 syntax descriptor、Resolved IR 和
target-aware checker 的唯一输入，并不是第二份 coverage registry。

execution 列有意使用狭窄的历史基线：
[`ptxsim@2ea9c476f362eeeee93de703e6e4191c8f47d3a6`](https://github.com/endingly/ptxsim/tree/2ea9c476f362eeeee93de703e6e4191c8f47d3a6)，
该版本固定到 `ptx_frontend@fdb5ef575087b530c2cd6db6cb3631cf430a8ce0`。它可作为下游证据，
但旧 pin 或 simulator handler 都不能证明完整 PTX 9.3 支持。本审计没有执行 hardware
验证；simulator execution 与 NVIDIA GPU 行为是不同的证据类别。

本文中：

- **Syntax/model/frontend** 指只针对所列 YAML 边界的 generated syntax descriptor、
  generated Resolved IR layout 与 checker；它不表示 manual 的所有合法 spelling 都能被接收。
- **Execution** 只指固定版本的 simulator family contract；不表示 physical GPU、memory
  model 或公共 variant/modifier support API。
- carry/state operation 和独立命名 operation 是独立的完成单元。例如，
  `add.cc`/`addc`、`sub.cc`/`subc`、`brx`、`barrier` 与 `mbarrier` 不能悄悄计入普通
  `add`、`sub`、`bra` 或 `bar` 覆盖。

## 可复现的 frontend-model 证据

载入 canonical `ptx-instr/v1` database 后共有 69 个 instruction name。下表列出相关项，
以便审阅已审计的 model boundary，而不把 YAML 复制成手工维护的 ledger。

| Opcode | YAML variant 数 | 从 canonical database 载入的 variant/layout 边界 |
| --- | ---: | --- |
| `mov` | 3 | scalar（scalar/pack/unpack layout）、`v4.u32`、predicate |
| `add` | 9 | f32/f32x2/f64、half、bfloat、mixed f32、integer、saturating、packed saturating |
| `sub` | 8 | f32/f32x2/f64、half、bfloat、mixed f32、integer、optional saturating |
| `mul` | 5 | `rn.f32`、`lo.u32`、`hi.u32`、`wide.u32`、`wide.s32` |
| `setp` | 5 | `lt.u32`、`ge.s32`、`lt.and.u32`、`eq.u32` pair、`lt.and.s32` pair |
| `ld` | 7 | generic/explicit scalar 与 vector、两个 global cache-hint form、global noncoherent L1 no-allocate |
| `st` | 6 | generic/explicit scalar 与 vector，加两个 global cache-hint form |
| `bar` | 11 | CTA sync/arrive/reduction spelling 与 warp sync layout |
| `bra` | 1 | 带可选 `.uni` 的 direct branch |
| `exit` | 1 | bare exit |
| `fma` | 16 | [FMA coverage](fma_coverage.md) 所述完整 frontend FMA contract |

生成器 model 说明这些项为何同时影响三个 frontend stage：
[syntax descriptor](python_generator_model.md#syntax-model) 接收 variant/layout，而相同的
normalized model 生成 [Resolved-IR binding](python_generator_model.md#resolved-model)，供
resolution 与 checking 使用。验收仍受每个 variant 的 operand、modifier、availability 与
negative diagnostic 约束。

### 复现命令

下列命令使用 repository virtual environment 中已安装的 editable package，是产生上述计数的
实际命令，而不是臆造的 registry。

```sh
.venv/bin/python - <<'PY'
from pathlib import Path
from ptx_frontend.code_gen.database import load_codegen_database

wanted = {"mov", "add", "sub", "mul", "setp", "ld", "st", "bar", "bra", "exit", "fma"}
database = load_codegen_database(spec_dir=Path("python/code_gen/resources/ptx_spec"))
for instruction in database.instructions:
    if instruction.opcode in wanted:
        print(instruction.opcode, len(instruction.variants),
              [variant.name for variant in instruction.variants])
PY
```

## PTX 9.3 到 model 的差集

下表是实际的规范比较，不是 variant count 的复述。“已覆盖”表示该 type/modifier/layout family
在 generated frontend 边界内，不表示 simulator 或 hardware execution。

| Opcode 与主要 PTX 9.3 section | 已覆盖的 frontend family | 具体差集与结论 | 固定 execution 边界 |
| --- | --- | --- | --- |
| `mov`: [普通 move](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov) 与 [pack/unpack](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov-2) | 已建模 `.pred`、scalar `.b16/.b32/.b64`、`.u16/.u32/.u64`、`.s16/.s32/.s64`、`.f32/.f64`、register/immediate/address/function/supported-special-register source，以及 2/4 element bit pack/unpack。`mov.v4.u32` 只用于 [§10](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#special-registers-clusterid) 所列 four-element cluster special register，并非 §9.7.9.3/§9.7.9.4 的一般 `.v4` modifier。 | manual 只允许 `.b128` 用于 pack/unpack，但 common scalar variant 也接收 `.b128`，这是需修复的 over-admission。合法 negated `mov.pred` source 在 resolution 前被拒绝；[#126](https://github.com/endingly/ptx_frontend/issues/126) 负责。故 MOV 非 frontend-complete。 | 历史 scalar execution 仅 `b32`/`u32`/`b64`，且 special-register read 很窄。[ptxsim#21](https://github.com/endingly/ptxsim/issues/21) 负责 whole-op execution。 |
| `add`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-add)、[floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-add)、[half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-add)、[mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-add) | 已建模全部 regular-manual family：integer scalar/packed 及合法 `.sat`；f32/f32x2/f64 rounding 与 `.ftz`/`.sat`；f16/f16x2/bf16/bf16x2；以及 mixed `.f32.{f16,bf16}`。每 form 表示其 availability。 | **Frontend 结论：对四个 ordinary `add` section 完整。** [`add.cc`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions-add-cc) 与 [`addc`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions-addc) 是独立命名且有状态的 operation，不计入。 | pin 执行全部 9 个 projected regular form/23 条 type path；carry state 明确排除。 |
| `sub`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-sub)、[floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sub)、[half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-sub)、[mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-sub) | 已建模所有 regular-manual integer、f32/f32x2/f64、f16/f16x2/bf16/bf16x2 与 mixed `.f32.{f16,bf16}` family，含其合法 rounding/FTZ/saturation 边界。 | **Frontend 结论：对四个 ordinary `sub` section 完整。** [`sub.cc`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions-sub-cc) 与 [`subc`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions-subc) 是独立 condition-code operation。 | pin 执行全部 8 个 projected regular form/17 条 type path；borrow state 排除。 |
| `mul`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-mul)、[floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mul)、[half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-mul) | 仅建模 `lo.u32`、`hi.u32`、`wide.u32`、`wide.s32` 与 register-only `rn.f32`。 | 缺少 `u16/u64/s16/s32/s64` 的 `.hi/.lo` form，和 `.wide.u16/.wide.s16`；`.wide.u64/.wide.s64` 不是合法 PTX form，因此刻意不要求。也缺少 f32 default-rounding spelling `mul.f32`、directed rounding、`.ftz`、`.sat`、f32x2、f64，以及全部 f16/f16x2/bf16/bf16x2 form 与合法 `.rn/.ftz/.sat`。**非 frontend-complete。** | pin 也仅执行相同 5 个 form。 |
| `setp`: [ordinary](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) 与 [half](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp) | 仅建模五个 example：`lt.u32`、`ge.s32`、`lt.and.u32`、`eq.u32` pair、`lt.and.s32` pair。 | 缺少 ordinary `.b16/.b32/.b64`、其他 integer、f32/f64 type；完整 `eq/ne/lt/le/gt/ge/lo/ls/hi/hs/equ/neu/ltu/leu/gtu/geu/num/nan` compare set；optional `.and/.or/.xor`、negated combine predicate、single/pair sink destination 与 f32 `.ftz`；也缺少全部 f16/f16x2/bf16/bf16x2 family。**非 frontend-complete。** | pin 只执行同样五个 form。 |
| `ld`: [ld](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld) 与 [ld.global.nc](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld-global-nc) | scalar 加 v2/v4 与受约束 v8/v4-64 vector layout 覆盖 `.b8/.b16/.b32/.b64`、signed/unsigned 8–64、f32/f64；generic 与 `.const/.global/.local/.param{::entry,::func}/.shared`；以及 selected weak/volatile/relaxed/acquire、scope、cache、alignment 和两个 cache-hint variant。 | 缺少 `.b128`、`.shared::cta/.shared::cluster`、完整 legal L1/L2 eviction、prefetch-size、cache-policy、`.unified` 与 `ld.global.nc` type/cache/vector matrix，以及 MMIO/consistency/scope/cache 的完整 legality cross-product。**非 frontend-complete。** | 仅 ordinary transfer；没有完整 ordering、MMIO、cache/noncoherent 或 function-parameter resource。 |
| `st`: [st](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st) | 对应 modeled `ld` scalar/vector type subset、generic/explicit space、selected semantics/scope/cache、alignment 与两个 cache-hint variant。 | 缺少 `.b128`、`.shared::cta/.shared::cluster`、legal L1/L2 eviction/cache-policy cross-product、`.unified` 及完整 volatile/relaxed/release/MMIO/scope legality matrix。**非 frontend-complete。** | 仅 ordinary transfer；[ptxsim#24](https://github.com/endingly/ptxsim/issues/24) 负责 whole `ld`/`st` execution。 |
| `bar`: [CTA `bar`/`barrier`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar) 与 [warp sync](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar-warp-sync) | `bar{.cta}.sync`、`arrive`、`red.popc.u32`、`red.{and,or}.pred`、其 immediate/register 和 required/omitted-count layout、negated reduction predicate 与 `bar.warp.sync` 均已建模并保留 availability。 | **Frontend 结论：对 `bar` 与 `bar.warp.sync` 完整。** `barrier{.cta}`（其 `.aligned` semantic 独立）、`barrier.cluster`、asynchronous barrier 与 `mbarrier` 是独立命名 operation，不计入。 | pin 执行相应 warp/CTA/reduction form，但这不是 hardware proof。 |
| `bra`: [direct branch](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-bra) | 已建模并检查 label target、optional `.uni` 与 ordinary/negated predicate guard。 | **Frontend 结论：对 direct `bra` 完整。** `brx.idx`、call、return 是独立命名 control-flow operation。 | pin 为 direct branch family 更新 authoritative PC。 |
| `exit`: [exit](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-exit) | 已建模 sole bare form 与 ordinary predicate guard。 | **Frontend 结论：对 `exit` 完整。** `ret` 与 `trap` 是独立 opcode。 | pin 处理 conditional exit 与 barrier release；未测 hardware 行为。 |
| `fma`: [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-fma)、[half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-fma)、[mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-fma) | 全部 16 个 FMA variant、operand contract、modifier 和 target availability 已在 [FMA coverage](fma_coverage.md) 中文档化并独立测试。 | **Frontend 结论：对已文档化 FMA family 完整。** 此处复用现有证据，不从 variant count 推断。 | pin 没有 FMA execution semantics。[ptxsim#34](https://github.com/endingly/ptxsim/issues/34) 仅是狭窄 `.oob` hardware observation。 |

## 完成与后续策略

已有的下游 tracker 分别是 whole-op `mov` 的 [ptxsim#21](https://github.com/endingly/ptxsim/issues/21)、
`ld`/`st` execution 的 [ptxsim#24](https://github.com/endingly/ptxsim/issues/24)，以及 whole-op
execution claim enforcement 的 [ptxsim#23](https://github.com/endingly/ptxsim/issues/23)。frontend
[#126](https://github.com/endingly/ptx_frontend/issues/126) 是已经存在的具体 `mov.pred`
syntax/layout blocker。

本审计未记录任何新建的外部 issue。#51 要求每个未覆盖 frontend family 都有专属 remediation
tracking；在创建并关联下列 whole-operation follow-up 前，该验收仍待完成，而不能拆成
variant ticket：

1. 具有 owned implicit-state contract 的 carry/condition-code `add.cc`/`addc` 与
   `sub.cc`/`subc`；
2. 修正 MOV `.b128` scalar-layout over-admission；[#126](https://github.com/endingly/ptx_frontend/issues/126)
   仍是已建单的独立 negated-predicate defect；
3. 超出当前 5 个 form 的完整 PTX 9.3 `mul` model/check；
4. 超出当前 5 个 form 的完整 PTX 9.3 `setp` model/check；
5. 独立的 `ld`、`st` 逐 form frontend completeness 工作，并链接 ptxsim#24 的必要 runtime
   semantics；以及
6. 若要求的是 execution 而不是现有 frontend model，则新建 whole-FMA simulator execution
   contract。

每项 follow-up 都必须使用 canonical YAML/database path、保持 negative diagnostic，并在接受的
frontend form 改变后重新验证 downstream projection。不得恢复 retired handwritten opcode
coverage registry，也不得引入 simulator runtime variant-support API。

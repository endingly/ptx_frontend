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

载入 canonical `ptx-instr/v1` database 后共有 71 个 instruction name。下表列出相关项，
以便审阅已审计的 model boundary，而不把 YAML 复制成手工维护的 ledger。

| Opcode | YAML variant 数 | 从 canonical database 载入的 variant/layout 边界 |
| --- | ---: | --- |
| `mov` | 4 | scalar（含 `.b16/.b32/.b64` pack/unpack）、`.b128` pack/unpack、`v4.u32`、predicate |
| `add` | 11 | 普通形式与 [carry-out](carry_coverage.md) |
| `addc` | 4 | [carry-in 与可选 carry-out](carry_coverage.md) |
| `sub` | 10 | 普通形式与 [borrow-out](carry_coverage.md) |
| `subc` | 4 | [borrow-in 与可选 borrow-out](carry_coverage.md) |
| `mul` | 23 | [MUL 覆盖](mul_coverage.md) |
| `setp` | 18 | [SETP 覆盖](setp_coverage.md) |
| `ld` | 40 | [LD 与 noncoherent load 覆盖](ld_coverage.md) |
| `st` | 24 | [ST 覆盖](st_coverage.md) |
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

wanted = {"mov", "add", "addc", "sub", "subc", "mul", "setp", "ld", "st", "bar", "bra", "exit", "fma"}
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
| `mov`: [普通 move](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov) 与 [pack/unpack](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov-2) | 已建模 `.pred`、scalar `.b16/.b32/.b64`、`.u16/.u32/.u64`、`.s16/.s32/.s64`、`.f32/.f64`、register/immediate/address/function/supported-special-register source，以及 2/4 element bit pack/unpack。`Mov::Scalar` 保留 scalar 及 `.b16/.b32/.b64` pack/unpack layout；只有 fixed-type `Mov::B128PackUnpack` 接收 `.b128`。`mov.pred` 接收 plain 或 negated predicate register、规范化为 Boolean 值的整数谓词常量，以及 plain 或 negated predicate special register；destination 仍必须是未取反 predicate register。`mov.v4.u32` 只用于 [§10](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#special-registers-clusterid) 所列 four-element cluster special register，并非 §9.7.9.3/§9.7.9.4 的一般 `.v4` modifier。 | **Frontend 结论：已建模的 ordinary move 与 pack/unpack form 保持 PTX 的 type 与 negation 边界。**fixed `.b128` variant 阻止 scalar `.b128`，同时保留既有较窄 pack/unpack layout 的 public contract。 | 历史 scalar execution 仅 `b32`/`u32`/`b64`，且 special-register read 很窄。[ptxsim#21](https://github.com/endingly/ptxsim/issues/21) 负责 whole-op execution。 |
| `add`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-add)、[floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-add)、[half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-add)、[mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-add) | 已建模全部 regular-manual family：integer scalar/packed 及合法 `.sat`；f32/f32x2/f64 rounding 与 `.ftz`/`.sat`；f16/f16x2/bf16/bf16x2；以及 mixed `.f32.{f16,bf16}`。每 form 表示其 availability。 | 普通算术章节已建模。独立的扩展精度 add/`addc` 契约见[进位/借位覆盖](carry_coverage.md)，具有类型化隐式状态影响和单独的目标可用性测试。 | pin 执行全部 9 个 projected regular form/23 条 type path；carry state 明确排除。 |
| `sub`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-sub)、[floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-sub)、[half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-sub)、[mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-sub) | 已建模所有 regular-manual integer、f32/f32x2/f64、f16/f16x2/bf16/bf16x2 与 mixed `.f32.{f16,bf16}` family，含其合法 rounding/FTZ/saturation 边界。 | 普通算术章节已建模。独立的扩展精度 sub/`subc` 契约见[进位/借位覆盖](carry_coverage.md)，具有类型化隐式状态影响和单独的目标可用性测试。 | pin 执行全部 8 个 projected regular form/17 条 type path；borrow state 排除。 |
| `mul`: [integer](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-arithmetic-instructions-mul)、[floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-mul)、[half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-mul) | 当前 canonical schema 已建模 [MUL 覆盖](mul_coverage.md) 中描述的整数和浮点形式。 | 修饰符、操作数、目标可用性及下游再验证边界见 [MUL 覆盖](mul_coverage.md)。 | 历史 pin 仅执行原先的 5 个 form。 |
| `setp`: [ordinary](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) 与 [half](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp) | 当前 canonical schema 已建模 [SETP 覆盖](setp_coverage.md) 中描述的整数和浮点形式。 | 修饰符、操作数、目标可用性及下游再验证边界见 [SETP 覆盖](setp_coverage.md)。 | pin 只执行同样五个 form。 |
| `ld`: [ld](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld) 与 [ld.global.nc](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld-global-nc) | Canonical family 及其目标/操作数边界见 [LD 覆盖](ld_coverage.md)。 | 生成模型覆盖文档列出的修饰符组合、地址来源及 public-IR 再验证；runtime semantics 仍单独负责。 | 仅 ordinary transfer；没有完整 ordering、MMIO、cache/noncoherent 或 function-parameter resource。 |
| `st`: [st](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st) | Canonical family 及其目标/操作数边界见 [ST 覆盖](st_coverage.md)。 | 生成模型覆盖文档列出的修饰符组合、地址来源及 public-IR 再验证；runtime semantics 仍单独负责。 | 仅 ordinary transfer；[ptxsim#24](https://github.com/endingly/ptxsim/issues/24) 负责 whole `ld`/`st` execution。 |
| `bar`: [CTA `bar`/`barrier`](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar) 与 [warp sync](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar-warp-sync) | `bar{.cta}.sync`、`arrive`、`red.popc.u32`、`red.{and,or}.pred`、其 immediate/register 和 required/omitted-count layout、negated reduction predicate 与 `bar.warp.sync` 均已建模并保留 availability。 | **Frontend 结论：对 `bar` 与 `bar.warp.sync` 完整。** `barrier{.cta}`（其 `.aligned` semantic 独立）、`barrier.cluster`、asynchronous barrier 与 `mbarrier` 是独立命名 operation，不计入。 | pin 执行相应 warp/CTA/reduction form，但这不是 hardware proof。 |
| `bra`: [direct branch](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-bra) | 已建模并检查 label target、optional `.uni` 与 ordinary/negated predicate guard。 | **Frontend 结论：对 direct `bra` 完整。** `brx.idx`、call、return 是独立命名 control-flow operation。 | pin 为 direct branch family 更新 authoritative PC。 |
| `exit`: [exit](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#control-flow-instructions-exit) | 已建模 sole bare form 与 ordinary predicate guard。 | **Frontend 结论：对 `exit` 完整。** `ret` 与 `trap` 是独立 opcode。 | pin 处理 conditional exit 与 barrier release；未测 hardware 行为。 |
| `fma`: [floating](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-fma)、[half/bfloat](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-floating-point-instructions-fma)、[mixed](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-fma) | 全部 16 个 FMA variant、operand contract、modifier 和 target availability 已在 [FMA coverage](fma_coverage.md) 中文档化并独立测试。 | **Frontend 结论：对已文档化 FMA family 完整。** 此处复用现有证据，不从 variant count 推断。 | pin 没有 FMA execution semantics。[ptxsim#34](https://github.com/endingly/ptxsim/issues/34) 仅是狭窄 `.oob` hardware observation。 |

## 完成与后续策略

已有的下游 tracker 分别是 whole-op `mov` 的 [ptxsim#21](https://github.com/endingly/ptxsim/issues/21)、
`ld`/`st` execution 的 [ptxsim#24](https://github.com/endingly/ptxsim/issues/24)，以及 whole-op
execution claim enforcement 的 [ptxsim#23](https://github.com/endingly/ptxsim/issues/23)。frontend
[#126](https://github.com/endingly/ptx_frontend/issues/126) 记录历史的具体 `mov.pred`
syntax/layout gap；当前 canonical schema 已接收 negated predicate source，同时仍不接收
negated destination。

#51 的 whole-operation frontend follow-up 已完成关联，且没有拆成 variant ticket：

1. [#132](https://github.com/endingly/ptx_frontend/issues/132) 负责 carry/condition-code
   `add.cc`/`addc` 与 `sub.cc`/`subc` 的 implicit-state contract；
2. [#127](https://github.com/endingly/ptx_frontend/issues/127) 记录 MOV 先前 `.b128` 的
   scalar-layout over-admission；它与
   [#126](https://github.com/endingly/ptx_frontend/issues/126) 一起由当前
   scalar/pack-unpack variant split 处理；
3. [#128](https://github.com/endingly/ptx_frontend/issues/128) 补齐超出原先 5 个 form 的 PTX
   9.3 `mul` model/check；
4. [#129](https://github.com/endingly/ptx_frontend/issues/129) 补齐超出原先 5 个 form 的 PTX
   9.3 `setp` model/check；以及
5. [#130](https://github.com/endingly/ptx_frontend/issues/130) 和
   [#131](https://github.com/endingly/ptx_frontend/issues/131) 分别负责 `ld`、`st` 的逐 form
   frontend completeness，并链接 ptxsim#24 的必要 runtime semantics。

现有 frontend [#53](https://github.com/endingly/ptx_frontend/issues/53) 与 simulator
[ptxsim#25](https://github.com/endingly/ptxsim/issues/25) 是 whole-FMA tracker；其 closed 状态仅为
去重证据，并非新的 execution verification。每项 frontend follow-up 都必须使用 canonical
YAML/database path、保持 negative diagnostic，并在接受的 frontend form 改变后重新验证
downstream projection。不得恢复 retired handwritten opcode coverage registry，也不得引入
simulator runtime variant-support API。创建这些链接只建立 owner，不改变上文每项 operation 的
completeness 结论或历史 execution evidence。

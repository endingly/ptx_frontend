# `feat/m13-cluster-proxy-mbarrier` 全量 Review

> 仓库：`endingly/ptx_frontend`  
> 分支：`feat/m13-cluster-proxy-mbarrier`  
> 对比基线：`main` @ `1c4547f65c888ee92b1933a20f9a74b380b96953`  
> 审查 Head：`558a656d353fe4b173eea3ffd5477a181a1d691e`  
> 对应 PR：[#37 — feat: implement M13 cluster, proxy, and mbarrier support](https://github.com/endingly/ptx_frontend/pull/37)  
> 审查日期：2026-08-31  
> 结论：**REQUEST CHANGES — 当前不建议合入 `main`**

---

## 1. 结论摘要

本分支以 27 个提交实现 M13-I01～M13-I24，并通过 3 个耦合提交收束 mbarrier domain、cluster capability 与 synchronization corpus。整体架构方向正确：大部分能力以 YAML 事实、公共 domain、生成 descriptor 和统一 checker 扩展完成，没有把现代指令语义散落成 opcode-specific C++ 特判。

但是，当前最终树仍存在 **2 项 HIGH 级 instruction-local legality 错误**：

1. 合法的 `match.all.sync` destination sink 形式会在 resolver 阶段被拒绝；
2. `mbarrier.arrive` / `mbarrier.arrive_drop` 的立即数 `count` 缺少 PTX ISA 上限检查，因而接受规范明确禁止的输入。

两项问题都落在本里程碑声称已经闭环的 frontend identity / instruction-local legality 范围内，不能以“runtime synchronization semantics 不属于 frontend”为由推迟。现有 Linux CI 成功只说明当前测试集通过，并未覆盖上述规范边界。

此外，M13-C03 负例矩阵过浅，未锁定本轮新增的关键边界；`project_roadmap.v2.md` 仍将全部 M13 issue 标为未开始，与分支和 PR 的完成声明相冲突。

### 1.1 严重度统计

| 严重度 | 数量 | 合入判断 |
| --- | ---: | --- |
| CRITICAL | 0 | — |
| HIGH | 2 | **必须修复后再合入** |
| MEDIUM | 2 | 应在本 PR 内完成 |
| LOW | 1 | 建议本 PR 或紧邻后续提交处理 |

### 1.2 推荐修复顺序

```text
R01 match.all.sync sink domain
        |
        +--> targeted resolver/checker tests
        |
R02 mbarrier count immediate range
        |
        +--> targeted checker tests
        |
        v
R03 扩充 M13-C03 corpus matrix
        |
        v
R04 更新 roadmap / closure evidence
        |
        v
R05 收紧 plural alignment 兼容 API（可独立）
```

---

## 2. 审查范围与证据

### 2.1 变更规模

- 27 commits；
- 44 changed files；
- 约 `+8769 / -364`；
- 主要变更面：
  - PTX synchronization/proxy/mbarrier YAML；
  - C++ backend spelling/domain；
  - Python normalize/model/code generation；
  - Resolved IR 与 checker descriptor；
  - target capability catalog；
  - M13 sm90a/sm100 corpus 与 C++ tests；
  - generated coverage/accounting artifacts。

### 2.2 验证状态

PR 描述报告：

- Python tests：218/218；
- Debug CTest：606/606；
- `gen_all.py`：11 outputs；
- synchronization corpus 只验证 frontend resolve/check，不执行 runtime synchronization semantics。

GitHub 上 Head `558a656d` 对应的 **Linux CI 已成功完成**。

本次审查环境无法解析 `github.com` 域名，因此没有在本地重新 clone、build 或执行测试。本文结论来自：

1. PR 最终聚合差异；
2. 27 个提交与 M13 issue 的逐项映射；
3. YAML、generator、resolver、checker、target catalog 与 corpus test 的静态数据流审查；
4. NVIDIA PTX ISA 9.3 对应章节的规范对照。

因此，本报告对代码语义问题给出了可复现输入和预期 diagnostic；修复 agent 必须在可构建环境中把这些用例真正加入门禁。

### 2.3 规范基线

本报告使用仓库已冻结的 PTX ISA 9.3 / CUDA 13.3 archive：

- [PTX ISA 9.3 — Parallel Thread Execution](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/)
- 重点章节：§9.7.14.11 `match.sync`、§9.7.14.16 `mbarrier`、§9.7.14.18～19 `clusterlaunchcontrol`。

---

# 3. Findings

## R01 — HIGH — `match.all.sync` 的合法 destination sink 形式被 resolver 拒绝

### 问题

PTX ISA 对 `match.all.sync` 允许使用 `_` 替换任意一个 destination operand。当前实现复用了 `reg` / `shfl_dest` 表示，却没有为 `match.all` 启用完整 sink topology：

- 无 predicate 的 layout 把 destination 建模为普通 `reg`，因此 `_` 无法进入 resolved value；
- 带 predicate 的 layout 使用 `shfl_dest`，但没有设置 `allow_destination_sink: true`；
- `resolve_shfl_destination()` 在该 flag 为 false 时显式拒绝 data destination `_`；
- predicate destination 仍无条件调用 `resolve_predicate_identifier()`，所以 `d|_` 同样无法表达。

这会把合法 PTX 当作非法输入拒绝。

### 位置

- 引入提交：[`9f341154` — feat: support M13 match sync slices](https://github.com/endingly/ptx_frontend/commit/9f341154622ae35a46bfbfa117607a5b7bdf0254)
- `instructions/ptx_spec/parallel_synchronization_and_communication.yaml`
  - `match_sync_without_predicate`
  - `match_sync_with_predicate`
  - `match_all_sync`
- `submod/resolved_ir/src/ptx_resolved_ir.cpp`
  - `resolve_shfl_destination()`
- 可能需要扩展的公共类型：
  - `ResolvedShflSyncDestination`
  - 或新增只属于 match-result topology、但 target-independent 的 destination pair 类型。

### 规范证据

PTX ISA 9.3 §9.7.14.11 对 `.all` 说明：sink `_` 可以替换任一 destination operand。也就是说，至少必须正确表达并检查：

```ptx
match.all.sync.b32 _|%p0, %r1, 0xffffffff;
match.all.sync.b32 %r0|_, %r1, 0xffffffff;
```

是否允许省略 predicate 时使用单独 `_`，应按该节完整 grammar/semantic text 固化，不应由当前 `shfl_dest` 偶然行为决定。

### 最小复现

```ptx
.version 9.3
.target sm_90a
.address_size 64

.visible .entry k() {
  .reg .pred %p0;
  .reg .b32 %r1;
  match.all.sync.b32 _|%p0, %r1, 0xffffffff;
  ret;
}
```

**规范预期：** parse、resolve、target-aware check 均成功。  
**当前静态代码路径：** `_` 到达 `resolve_shfl_destination()` 后因 `allow_destination_sink == false` 被拒绝。

第二个必须锁定的复现：

```ptx
.visible .entry k() {
  .reg .pred %p0;
  .reg .b32 %r0, %r1;
  match.all.sync.b32 %r0|_, %r1, 0xffffffff;
  ret;
}
```

当前 predicate 侧没有 sink 表示，也会被拒绝。

### 影响

- 合法 PTX 9.3 无法进入 Resolved IR；
- M13-I03 的 `match.sync` topology 不完整；
- 以 compiler-generated 或手写 PTX 为输入时会出现 false negative；
- coverage ledger 将该 slice 标为 supported，但公共表示实际不能覆盖规范 grammar；
- 现有 corpus 正例无法证明完整 destination topology。

### 建议修复

1. **不要简单地全局放宽 `shfl_dest`。** `shfl.sync`、`match.any.sync` 和 `match.all.sync` 的 sink 规则并不相同，避免用一个 boolean 继续叠加指令私有语义。
2. 为 match-all destination 建立明确 topology，例如：

   ```text
   ResolvedMatchAllDestination
     data: optional<ResolvedRegisterRef>
     predicate: optional<ResolvedPredicateRef>
   ```

   并在 descriptor 中声明合法组合。
3. 至少允许：
   - `d|p`
   - `_|p`
   - `d|_`
4. 明确拒绝：
   - `_ | _`
   - sink 出现在 source/membermask；
   - `match.any.sync` 使用 match-all-only sink topology。
5. source range 必须分别保留到 data 与 predicate/sink，diagnostic 不能退化为整条指令范围。
6. 若继续复用现有 destination 类型，应将“data sink”和“predicate sink”拆成独立 capability，而不是单个 `allow_destination_sink`。
7. 修改手写 YAML / generator / resolver 后重新生成，**不得手工编辑 generated C++**。

### 必须新增的测试

| 用例 | 预期 |
| --- | --- |
| `match.all.sync.b32 %r0|%p0, ...` | 成功 |
| `match.all.sync.b32 _|%p0, ...` | 成功 |
| `match.all.sync.b32 %r0|_, ...` | 成功 |
| `match.all.sync.b32 _|_, ...` | 失败，定位 destination pair |
| `match.any.sync.b32 _|%p0, ...` | 按规范拒绝 |
| sink 使用旧于指令可用版本/target | `UnsupportedAvailability` 或既有一致 diagnostic |

### 验收标准

- 上述合法用例完成 parse → resolve → check；
- 非法组合有稳定 diagnostic kind 和精确 source range；
- 新测试在当前 Head 上至少有一个失败，修复后全部通过；
- coverage selector / generated descriptor 与真实可表达 topology 一致。

---

## R02 — HIGH — `mbarrier.arrive` / `arrive_drop` 接受超出 ISA 上限的立即数 `count`

### 问题

本分支为 `mbarrier.arrive` 与 `mbarrier.arrive_drop` 增加了带 `count` 的 operand layout，但只声明：

- operand kind 为 `reg_or_imm`；
- type 为 `u32`；
- register width 为 exact；
- barrier address 8-byte aligned。

没有附加 `immediate_range` 约束。因此，只要立即数能够表示为 `u32`，checker 就不会拒绝超出 mbarrier pending-arrival counter 最大范围的值。

PTX ISA 9.3 给出的布局上限为：

- layout v0：pending arrival count 最大 `2^20 - 1 = 1,048,575`；
- layout v1：pending arrival count 最大 `2^9 - 1 = 511`。

在 frontend 不跟踪 barrier object 当前 layout 的前提下，仍然可以安全执行一个 **对所有 layout 都成立的必要条件**：立即数 `count` 不得超过 v0 的全局最大值 `1,048,575`。当前实现连这一 instruction-local 必要条件也没有检查。

### 位置

- 引入提交：
  - [`997c2430` — feat: support M13 mbarrier arrival](https://github.com/endingly/ptx_frontend/commit/997c24308e72285948c235e3e0e0c98135b0af7b)
  - [`95e259c6` — feat: support M13 mbarrier arrival drop](https://github.com/endingly/ptx_frontend/commit/95e259c60f5edf9fdcf21155f0e56562f993067d)
- `instructions/ptx_spec/parallel_synchronization_and_communication.yaml`
  - `$mbarrier_arrive_cta_count`
  - `$mbarrier_arrive_cluster_count`
  - 各 `mbarrier_arrive_*` count-bearing layout
  - 各 `mbarrier_arrive_drop_*` count-bearing layout
  - `.noComplete` 相关 count-bearing variant（若共享同一 operand pattern，也必须一并覆盖）
- `python/code_gen/normalize.py`
  - 已有 `immediate_range` normalization 能力，可直接复用；无需新增 C++ opcode special case。
- generated checker descriptor / `ptx_resolved_ir_checker.cpp`
  - 现有通用 immediate-range checker 对 register operand 跳过、对 immediate 执行静态范围检查，正适合本场景。

### 最小复现

```ptx
.version 9.3
.target sm_90a
.address_size 64

.shared .align 8 .b64 bar;

.visible .entry k() {
  .reg .b64 %state;
  mbarrier.arrive.shared.b64 %state, [bar], 1048576;
  ret;
}
```

`1048576 == 2^20`，超出所有合法 mbarrier layout 的 pending-arrival counter 范围。

**规范预期：** target-aware checker 拒绝该立即数。  
**当前 descriptor：** 只有 address alignment，没有 `count` immediate range，因此该输入会越过本地 legality check。

同类复现：

```ptx
mbarrier.arrive_drop.shared.b64 %state, [bar], 1048576;
```

以及本分支已建模的 `.noComplete` count form。

### 影响

- frontend 接受规范明确非法的 PTX；
- M13-I14 / I15 的 instruction-local legality 未闭环；
- downstream simulator/adapter 会收到表面上“已检查”的非法 count；
- corpus 与 coverage ledger 对支持状态产生过度承诺；
- v0/v1 lifecycle domain 的边界没有被公共 constraint 数据表达。

### 建议修复

1. 对所有显式 `count` 的 `mbarrier.arrive` / `arrive_drop` / `.noComplete` layout 添加：

   ```yaml
   constraints:
     - kind: immediate_range
       operand: count
       minimum: 0
       maximum: 1048575
   ```

   如果 variant 已有 `address_alignment`，应并列保留两项，而不是覆盖。
2. 保持 register-valued count 的行为：frontend 无法证明运行时值，通用 checker 应跳过数值范围检查。
3. **不要在当前阶段静态强制 v1 的 511 上限，除非 IR 已能证明对应 barrier object 的 layout。** 否则会把 lifecycle-dependent fact 错塞进 instruction-local checker。
4. 若规范对某个具体 layout-qualified form提供更严格且可由该指令自身确定的上限，可在相应 variant 单独收紧。
5. 将范围事实保留在 YAML/descriptor，不要新增 `if opcode == mbarrier` 的 C++ 分支。

### 必须新增的测试

对 `arrive`、`arrive_drop` 以及 `.noComplete` 分别覆盖：

| `count` | 形态 | 预期 |
| ---: | --- | --- |
| `0` | immediate | 按规范允许/拒绝，固定当前边界语义 |
| `1` | immediate | 成功 |
| `1048575` | immediate | 成功 |
| `1048576` | immediate | `ImmediateValueMismatch` 或项目统一 range diagnostic |
| `%r0` | register | resolve/check 成功，数值留给 runtime |

同时覆盖至少：

- generic/shared；
- `.shared::cta`；
- `.shared::cluster` 且具备 cluster capability；
- source range 指向 `count` operand，而不是整条指令。

### 验收标准

- `1048576` 在所有 count-bearing form 上稳定被拒绝；
- `1048575` 不被误拒；
- register operand 不因静态未知而失败；
- 约束由生成 descriptor 驱动；
- 新测试在当前 Head 上能复现缺口。

---

## R03 — MEDIUM — M13-C03 corpus 负例矩阵不足，无法证明新增边界已闭环

### 问题

`test_m13_synchronization_corpus.cpp` 的模块级正例能证明两个 corpus 文件可 parse/resolve/check，但负例只有四类：

1. global address 用于 `mbarrier.inval`；
2. `fence.proxy.tensormap::generic.acquire` 使用错误 size；
3. `barrier.cluster.arrive` 缺少 capability / PTX 版本过旧；
4. `clusterlaunchcontrol.query_cancel` response register 类型错误。

这四个用例没有覆盖 M13 中最容易出错的 topology 与边界，因此 R01、R02 能在整个 PR 的测试全部成功时仍然存在。

### 位置

- 引入提交：[`558a656d` — test: add M13 synchronization corpus](https://github.com/endingly/ptx_frontend/commit/558a656d353fe4b173eea3ffd5477a181a1d691e)
- `submod/resolved_ir/test/test_m13_synchronization_corpus.cpp`
- `corpus/m13/synchronization_sm90a.ptx`
- `corpus/m13/synchronization_sm100.ptx`
- `corpus/m13/manifest.json`

### 缺失的关键矩阵

至少应增加以下边界：

| Family | 正邻接边界 | 负邻接边界 |
| --- | --- | --- |
| `match.all.sync` | `_|p`、`d|_` | `_|_`、错误地给 `match.any` 使用 sink |
| mbarrier count | `1048575` | `1048576` |
| mbarrier state sink | sink 在允许的 PTX/SM 上 | PTX 7.0 与 7.1 availability 边界；cluster form 要求 sink |
| parity | immediate `0` / `1` | immediate `2` |
| phase type/report | primary + reportPredicate/reportValue 合法 topology | conditional 使用不允许的 report topology |
| `clusterlaunchcontrol.try_cancel` | response 16B + mbarrier 8B | 两个地址分别制造错位，确保两项约束都执行 |
| multicast availability | exact `sm_100a` / family `sm_100f` 的支持路径 | generic `sm_100` 不应因数值相同而误获 family feature |
| cluster capability | 已有 capability | 无 capability；同时验证 diagnostic range |

### 影响

- C03 目前只能证明“有一些正例和四个负例”，不能证明 M13 public domain 与 checker 完整连接；
- 回归门禁对 topology、范围、双地址 constraint 和 exact/family target 的保护不足；
- 后续 M14～M18 会复用 mbarrier/proxy/cluster domain，缺口会被放大；
- 其他 agent 可能把 Linux CI 绿色误认为规范闭环。

### 建议修复

1. 将负例改为 table-driven / parameterized matrix，至少保存：

   ```text
   source
   target profile
   expected resolve/check phase
   expected diagnostic kind
   expected source range token
   ```

2. 对每个失败边界增加相邻成功用例，避免修复时用过宽拒绝掩盖问题。
3. corpus module 测试继续负责真实多指令路径；小粒度 topology/range 用例可放入 checker/resolver unit test，C03 只需建立一张汇总 gate。
4. 对 `clusterlaunchcontrol.try_cancel` 两个 alignment 分别构造失败，防止 plural constraint 被意外退化为首项。
5. M13 corpus manifest 应继续记录 profile/provenance；不要把 synthetic unit snippets伪装成 external compiler corpus。

### 验收标准

- R01、R02 的复现先在当前 Head 上失败；
- 修复后由 targeted unit tests 和 M13 closure test 双重覆盖；
- 每个负例断言 diagnostic kind 与非零、尽可能精确的 source range；
- sm90a/sm100 profile 的正例均继续通过；
- generated outputs clean，无手工漂移。

---

## R04 — MEDIUM — Roadmap 仍将 M13 全部标为未开始，与当前分支完成声明冲突

### 问题

PR 描述声称实现 M13-I01～I24 和 C01～C03；27 个提交也与这 27 个 issue/closure 一一对应。但当前分支中的 `.agents/project_roadmap.v2.md` 仍把：

- M13-I01～M13-I24；
- M13-C01～M13-C03

全部标为 `⬜`。

该文件不在本 PR 的 changed-file list 中，因此不是“状态更新遗漏在 diff 的其他位置”，而是没有进行 milestone closure 文档回写。

### 位置

- 分支级问题；应在最终 closure commit 之后修复；
- `.agents/project_roadmap.v2.md`
  - `# 11. M13：Cluster、proxy 与 mbarrier`
- PR #37 body 与 roadmap status table。

### 影响

- roadmap、PR、coverage ledger 和 corpus manifest 之间事实不一致；
- 后续 agent 会把 M13 判断为未开始，可能重复实现同一 issue；
- M14 依赖项无法从权威 roadmap 获得可信状态；
- 本项目此前明确要求 milestone 最后检查文档漂移，本分支没有完成该闭环。

### 建议修复

1. **先修复 R01、R02、R03，再把 M13 标为完成。** 当前不应仅靠文档 commit 掩盖功能缺口。
2. 将 M13-I01～I24、C01～C03 状态更新为与最终代码事实一致的值。
3. 为 M13 增加“闭环证据”，至少说明：
   - 规范基线；
   - generated artifacts / descriptor gate；
   - sm90a 与 sm100 corpus；
   - cluster capability 的统一路径；
   - mbarrier token/phase/layout domain；
   - runtime barrier phase、跨线程/跨 CTA protocol proof 明确不在本 milestone 执行。
4. 明确本 milestone 的冻结边界：

   ```text
   frontend identity
   + instruction-local legality
   + target/profile availability
   + protocol metadata exposure
   != runtime synchronization execution
   ```

5. 检查 `instructions/opcode_coverage.yaml`、`ptx_inventory_accounting.yaml` 与 roadmap 对 M13 slice 的状态是否存在第二处漂移。

### 验收标准

- roadmap 与最终实现、PR 描述、coverage ledger 无矛盾；
- M13 closure evidence 可由仓库内测试持续验证，而不是记录一次性数字；
- 不宣称 runtime synchronization semantics 已实现；
- M14 agent 可直接从 roadmap 确认依赖已满足及仍属 out-of-scope 的部分。

---

## R05 — LOW — `VariantSpec.address_alignment` 兼容属性会静默丢弃 plural constraint

### 问题

本分支把单个 address alignment 扩展为：

```python
address_alignments: tuple[ResolvedAddressAlignmentConstraint, ...]
```

这是正确方向，`clusterlaunchcontrol.try_cancel` 也确实需要同时检查：

- `response`：16-byte alignment；
- `mbarrier`：8-byte alignment。

但 Python model 仍提供兼容属性：

```python
@property
def address_alignment(self):
    return self.address_alignments[0] if self.address_alignments else None
```

当 variant 有两项约束时，任何遗留 consumer 读取 singular property 都会静默只看到第一项。当前生成 checker 已使用 plural path，尚未发现 in-tree functional failure；问题在于兼容 API 会把未来的错误隐藏成“看似仍然工作”。

### 位置

- `python/ir/resolved_ir.py`
  - `ResolvedVariant.address_alignments`
  - `ResolvedVariant.address_alignment`
- `instructions/ptx_spec/parallel_synchronization_and_communication.yaml`
  - `clusterlaunchcontrol_try_cancel_*` 的两项 `address_alignment`。

### 影响

- 新增 consumer 或遗留测试可能无声漏掉第二个地址约束；
- plural migration 缺乏 fail-fast 行为；
- 后续 TMA/tensor async 指令很可能拥有更多多地址 constraint，该兼容视图会继续制造隐性风险。

### 建议修复

选择一种明确策略：

1. singular property 只在 `len == 1` 时返回约束；`len > 1` 时抛出明确异常；或
2. 删除/正式 deprecate singular property，并迁移所有 caller；或
3. 返回 `None` 且在 docstring 中声明 plural variant 不可通过该视图读取，但 fail-fast 优于静默 `None`。

同时增加 model test：两项 alignment 时不得只返回首项。

### 验收标准

- 没有公共/内部 API 能静默把 N 项 alignment 截断为 1 项；
- generated checker 继续逐项执行全部约束；
- `clusterlaunchcontrol.try_cancel` 的两个错位用例分别失败。

---

# 4. 正向评价

以下设计应保留，不建议在修复时回退：

## 4.1 数据驱动边界总体正确

M13 的大多数语义通过 YAML、normalize/model、generated descriptor 和统一 checker 扩展，未在 resolver/checker 中堆叠大量 opcode name 判断。这为 M14 的 TMA/bulk copy 和后续 WGMMA/TCGEN05 保留了可扩展路径。

## 4.2 plural address alignment 基础设施是必要且正确的

`clusterlaunchcontrol.try_cancel` 同时拥有 response 与 mbarrier 两个不同 alignment。分支把 descriptor 从 singular 扩展为 plural，并让 checker 遍历执行，方向正确。R05 只是要求收紧遗留兼容视图，而不是回退 plural model。

## 4.3 target capability 与 exact/family target 没有退化为纯 SM 数字比较

`ptx_target.cpp` 继续通过显式 profile catalog 维护：

- numeric architecture；
- generic / architecture-specific / family-specific flavor；
- enabled family features；
- capability set。

M13 的 cluster 与 multicast availability 使用了 capability、exact target 和 family 条件，符合 M11 的架构约束。

## 4.4 mbarrier state token / sink 建模是可复用的公共 domain

分支没有把 mbarrier state 当作普通无类型寄存器处理，而是建立：

- register token；
- sink；
- register-or-sink；
- sink availability；
- phase type / layout domain。

这为 M14 async copy completion 和后续 consumer 暴露协议 metadata 提供了合理基础。

## 4.5 默认 modifier 的“值 + 是否显式出现”处理具有一致性

可选 modifier 使用默认 semantic value，同时依靠 source locations 判断是否显式出现，以便 value-specific availability 不误伤旧 spelling。该设计需要继续由测试保护，但当前生成链路整体是一致的。

## 4.6 已建立 sm90a / sm100 的模块级正例

两个 profile 的完整 module 能进入 parse → resolve → check，且运行时同步语义被明确标为 not-run，没有虚假宣称 simulator execution 已完成。

---

# 5. 27 个提交逐项审查映射

> 说明：以下审查以每个提交对应的 M13 issue 为索引，检查其在最终 Head 中留下的聚合状态；本次没有对 27 个中间 tree 分别重新构建。因此“未发现阻断问题”表示最终聚合差异中未发现与该提交直接相关的独立 blocker，不代表形式化证明该提交绝对无缺陷。

| # | Commit | 对应工作 | 审查结论 |
| ---: | --- | --- | --- |
| 1 | [`3f35bfff`](https://github.com/endingly/ptx_frontend/commit/3f35bfff1cf9253b83c2d5c079baad38a9b6742d) | M13-I01 `bar.warp.sync` | 未发现独立 blocker；建议由 C03 增加 mask/availability 邻接边界 |
| 2 | [`173af137`](https://github.com/endingly/ptx_frontend/commit/173af13719550c42a1b28ecdd889c6554fd57938) | M13-I02 `barrier.cluster` | 未发现独立 blocker；capability/old PTX 已有负例 |
| 3 | [`9f341154`](https://github.com/endingly/ptx_frontend/commit/9f341154622ae35a46bfbfa117607a5b7bdf0254) | M13-I03 `match.sync` | **R01 HIGH**：`match.all.sync` sink topology 不完整 |
| 4 | [`2d861b48`](https://github.com/endingly/ptx_frontend/commit/2d861b48033cb90510e858720d5f953d3d540e12) | M13-I04 `redux.sync` | 未发现独立 blocker；exact/family availability 应保留 matrix test |
| 5 | [`3e05d1ff`](https://github.com/endingly/ptx_frontend/commit/3e05d1ffcf8c12a7f5eb82abae940639f0b69e02) | M13-I05 `elect.sync` | 未发现独立 blocker；destination sink 已显式建模 |
| 6 | [`82e98db3`](https://github.com/endingly/ptx_frontend/commit/82e98db3d6398abf572a815f5a07cc119ac813ba) | M13-I08 `griddepcontrol` | 未发现独立 blocker |
| 7 | [`3832e10d`](https://github.com/endingly/ptx_frontend/commit/3832e10d4461005da47bcc5c06025b65dccaf6e8) | M13-I06 `mapa` | generic/shared address mapping 与 type topology 未见阻断问题 |
| 8 | [`ca884ab5`](https://github.com/endingly/ptx_frontend/commit/ca884ab58b77e2535e19fc8240dc2f7da0e66506) | M13-I07 `getctarank` | 未发现独立 blocker |
| 9 | [`ff424f31`](https://github.com/endingly/ptx_frontend/commit/ff424f319123a21fe6e329714ddfbb09c18207d3) | M13-I09 mbarrier operand domain | 公共 token/sink domain 方向正确；R01 不应通过复用该域规避 match-specific topology |
| 10 | [`32d4d972`](https://github.com/endingly/ptx_frontend/commit/32d4d9726ec065e0c93f2649a673d69c083d74f2) | M13-I10 `mbarrier.init` | 未发现独立 blocker；layout qualifier availability 需继续保留显式/默认测试 |
| 11 | [`3e4d1925`](https://github.com/endingly/ptx_frontend/commit/3e4d192560fd616bd932fcf27e5247f77dbcf130) | M13-I11 `mbarrier.inval` | state-space 负例已覆盖 |
| 12 | [`1b171462`](https://github.com/endingly/ptx_frontend/commit/1b17146263c8c351e9f53e22efaffa5af51dae24) | M13-I12 `expect_tx` | 未发现独立 blocker；runtime tx-count state 不属于本轮 instruction-local review |
| 13 | [`8ed07a93`](https://github.com/endingly/ptx_frontend/commit/8ed07a939549250863598a5da4d4bc1b1c0b85af) | M13-I13 `complete_tx` | 未发现独立 blocker |
| 14 | [`997c2430`](https://github.com/endingly/ptx_frontend/commit/997c24308e72285948c235e3e0e0c98135b0af7b) | M13-I14 `mbarrier.arrive` | **R02 HIGH**：immediate count 缺少全局上限检查 |
| 15 | [`95e259c6`](https://github.com/endingly/ptx_frontend/commit/95e259c60f5edf9fdcf21155f0e56562f993067d) | M13-I15 `arrive_drop` | **R02 HIGH**：同类 count 缺口 |
| 16 | [`1ff45bd0`](https://github.com/endingly/ptx_frontend/commit/1ff45bd031827a59b02ae4c215d862f986d09a13) | M13-I16 `cp.async.mbarrier.arrive` | 未发现独立 blocker；runtime completion 不在本轮范围 |
| 17 | [`f93934d5`](https://github.com/endingly/ptx_frontend/commit/f93934d58da6b14bb207919fc41e39610f753080) | M13-I17 `mbarrier.test_wait` basic | 未发现独立 blocker；basic slice 不要求额外扩展所有 sem/scope cross-product |
| 18 | [`e74d7ae9`](https://github.com/endingly/ptx_frontend/commit/e74d7ae96edf6d4e4a977e1b72dbcc77b953e45a) | M13-I18 `mbarrier.try_wait` basic | 未发现独立 blocker；time hint 与 parity 应纳入 R03 matrix |
| 19 | [`319857e3`](https://github.com/endingly/ptx_frontend/commit/319857e3a4a5001d2f163613a3ad6ca39f4a4ff2) | M13-I19 PTX 9.3 wait extensions | 未发现独立 blocker；phase/report topology 测试不足，归入 R03 |
| 20 | [`ac367dce`](https://github.com/endingly/ptx_frontend/commit/ac367dceb437bb7e82c99d545d2477f947b0ee93) | M13-I20 `pending_count` | 未发现独立 blocker；默认 layout 的显式出现判定应由现有 loc tests 保护 |
| 21 | [`03c136a2`](https://github.com/endingly/ptx_frontend/commit/03c136a278ef632fea268c10b3d154de5bcab02b) | M13-I21 `check_layout` | 未发现独立 blocker |
| 22 | [`3d8d208f`](https://github.com/endingly/ptx_frontend/commit/3d8d208ff09241cca7e511748b5e01d61939e0f1) | M13-I22 proxy fence | 未发现独立 blocker；错误 size 已有负例 |
| 23 | [`9e1d348d`](https://github.com/endingly/ptx_frontend/commit/9e1d348d6d39edb52834b31ce6696c5a16014c4a) | M13-I23 `try_cancel` | plural alignment 正确；**R05 LOW** 要求兼容 API fail-fast；R03 应分别测试两地址错位 |
| 24 | [`31b60a97`](https://github.com/endingly/ptx_frontend/commit/31b60a97606c37e3c02fc3406201cf351d51a56a) | M13-I24 `query_cancel` | response type 负例已覆盖；vector sink topology 未发现可确认 blocker |
| 25 | [`d2118bb6`](https://github.com/endingly/ptx_frontend/commit/d2118bb65102083ceda236dc60d6366cc49fa279) | M13-C01 lifecycle domain | domain 统一方向正确；R02 应作为 lifecycle operand fact 加入 descriptor |
| 26 | [`0bdc72a6`](https://github.com/endingly/ptx_frontend/commit/0bdc72a6da8adca3669fa6c6ce906ad7fc0be8d1) | M13-C02 cluster capability | 未发现 capability 分叉 blocker；exact/family matrix 应在 R03 加强 |
| 27 | [`558a656d`](https://github.com/endingly/ptx_frontend/commit/558a656d353fe4b173eea3ffd5477a181a1d691e) | M13-C03 synchronization corpus | **R03 MEDIUM**；同时未完成 roadmap closure，见 **R04** |

---

# 6. 修复 Agent 拆分建议

## 6.1 Agent A — 修复 `match.all.sync` destination domain

**负责 finding：** R01  
**主要文件：**

- `instructions/ptx_spec/parallel_synchronization_and_communication.yaml`
- `python/code_gen/model.py` / `normalize.py`（若需新 topology flag/domain）
- `python/ir/resolved_ir.py`
- `python/code_gen/gen_resolved_*`
- `submod/resolved_ir/include/ptx_resolved_ir.hpp`
- `submod/resolved_ir/src/ptx_resolved_ir.cpp`
- resolver/checker tests

**要求：** 不得通过把 `_` 当成普通 identifier 或全局放宽 `shfl_dest` 规避类型建模。

## 6.2 Agent B — 修复 mbarrier immediate count 边界

**负责 finding：** R02  
**主要文件：**

- `instructions/ptx_spec/parallel_synchronization_and_communication.yaml`
- generated checker descriptors（只通过 generator 更新）
- `test_ptx_resolved_ir_checker.cpp` 或相应 targeted test

**要求：** 优先复用现有 `immediate_range`，不要新增 opcode-specific checker。

## 6.3 Agent C — 扩充 M13-C03 closure matrix

**负责 finding：** R03  
**主要文件：**

- `submod/resolved_ir/test/test_m13_synchronization_corpus.cpp`
- 相关 resolver/checker unit tests
- 必要时 `corpus/m13/*.ptx` 与 manifest

**要求：** 先与 Agent A/B 确认最终 diagnostic kind，避免用字符串脆弱匹配代替结构化断言。

## 6.4 Agent D — Roadmap 与 closure evidence

**负责 finding：** R04  
**主要文件：**

- `.agents/project_roadmap.v2.md`
- 必要时 coverage/accounting 文档，但不能手工改 generated artifact

**要求：** 等 R01～R03 合入当前分支后再标记 M13 完成。

## 6.5 Agent E — plural alignment API hardening

**负责 finding：** R05  
**主要文件：**

- `python/ir/resolved_ir.py`
- Python model/generator tests

该项与主要语义修复低耦合，可并行。

### 6.6 冲突矩阵

| Agent | A | B | C | D | E |
| --- | --- | --- | --- | --- | --- |
| A | — | **高：同一 synchronization YAML** | 中：共享 targeted tests | 低 | 中：可能共享 IR model |
| B | **高** | — | 中：共享 checker tests | 低 | 低 |
| C | 中 | 中 | — | 低 | 低 |
| D | 低 | 低 | 低 | — | 低 |
| E | 中 | 低 | 低 | 低 | — |

### 6.7 推荐协作方式

- **A 与 B 不建议同时直接修改同一 YAML。** 可以：
  1. 由一个 agent 统一持有 `parallel_synchronization_and_communication.yaml`，另一个先提交测试/模型；或
  2. 先合 R01，再让 B rebase 后加 range constraints。
- C 在 A/B 的 diagnostic 与 final topology 稳定后补 closure matrix。
- D 最后执行，以最终可验证事实回写 roadmap。
- 每个 agent 都只修改手写源，最后统一运行 generator；generated 文件若发生冲突，删除冲突结果后从手写源重新生成，不要人工拼接。

---

# 7. 建议验证矩阵

> 下列命令应以仓库 README / preset 的正式调用方式为准；不要为本报告另造第二套 CI 流程。

## 7.1 Targeted tests

1. `match.all.sync`：
   - data sink；
   - predicate sink；
   - double sink rejection；
   - `match.any` 不被误放宽。
2. mbarrier count：
   - `1048575`；
   - `1048576`；
   - register-valued count；
   - arrive / noComplete / arrive_drop。
3. `clusterlaunchcontrol.try_cancel`：
   - response misalignment；
   - mbarrier misalignment；
   - 两者正确时通过。
4. target profile：
   - generic `sm_100`；
   - exact `sm_100a`；
   - family `sm_100f`；
   - capability 缺失。

## 7.2 Generator consistency

- 运行仓库规定的 `gen_all.py`；
- 确认 output 数量和 topology 无意外变化；
- 再次运行应无 diff；
- 禁止只更新 generated C++ 而遗漏 YAML/Python source。

## 7.3 全量测试

- Python test suite；
- Debug CTest；
- 若 CI 同时包含 Release/installed consumer，也必须通过；
- Linux CI 必须在最终 Head 成功；
- `git diff --check`；
- working tree clean。

## 7.4 文档一致性

至少比较：

```text
.agents/project_roadmap.v2.md
instructions/opcode_coverage.yaml
instructions/ptx_inventory_accounting.yaml
corpus/m13/manifest.json
PR description
```

确认以下事实一致：

- M13 instruction-local frontend closure 完成；
- simulator/runtime synchronization semantics 未实现、未运行；
- supported slice 仍可标为 partial，不得冒充完整历史 cross-product；
- M14 的前置 domain 已准备好。

---

# 8. Definition of Done

只有全部满足以下条件后，PR #37 才建议合入：

- [ ] R01：合法 `match.all.sync` sink topology 可以 resolve/check；非法组合稳定拒绝；
- [ ] R02：所有 count-bearing `arrive` / `arrive_drop` 形式拒绝 immediate `1048576`，接受 `1048575`；
- [ ] R03：M13-C03 增加 topology、range、dual-alignment、target flavor/capability 的正负邻接矩阵；
- [ ] R04：roadmap 与实现状态一致，并明确 runtime semantics 边界；
- [ ] R05：plural alignment 不再通过 singular compatibility property 静默截断，或明确记录后续 issue 并证明无在树 caller；
- [ ] 生成器重复运行无 diff；
- [ ] Python tests 全部通过；
- [ ] Debug/Release/installed-consumer 等仓库现有门禁全部通过；
- [ ] Linux CI 在最终 Head 成功；
- [ ] `git diff --check` 通过；
- [ ] PR 描述中的验证数字或表述更新为最终事实；
- [ ] 未手工编辑 generated source；
- [ ] 没有把 runtime mbarrier phase/protocol proof 错塞进 instruction-local checker。

---

# 9. 最终判断

M13 的架构性工作已经基本成形，尤其是 cluster capability、mbarrier typed token/sink、phase/layout domain、plural alignment 和数据驱动生成链路，为 M14 以后奠定了正确基础。

但该 PR 当前仍同时存在：

- **合法 PTX 被拒绝**；
- **非法 PTX 被接受**。

二者都直接违反 frontend 的核心职责，并且现有 closure corpus 没有捕获。因此当前状态应保持 **REQUEST CHANGES**。完成 R01～R04 后再发起一轮针对最终 diff 的复审；R05 可在同轮低风险修复，避免 plural constraint API 在后续里程碑扩散。

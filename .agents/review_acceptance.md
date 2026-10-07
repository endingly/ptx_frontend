# #50 审阅整改验收协调记录

## 用途、快照与范围

本文件是 [#50](https://github.com/endingly/ptx_frontend/issues/50) 的本地验收协调记录，
不是 GitHub issue 的替代品，也不修改其历史 review 或原生 sub-issue 状态。

- 起点为 `origin/main` 的 `d07bd8e`；该提交是当前分支的祖先。本轮只记录该基线之后
  实际提交、工作树证据和最终 gate，不以分支名、PR 或 closed 状态推断修复正确性。
- 通过 `GET /repos/endingly/ptx_frontend/issues/50/sub_issues` 取得的当前 native 快照有
  **38** 个 child（P01 7 个、P02 29 个、P03 2 个）。#50 body 中“7 + 24 + 2 = 33”是
  历史计数；本记录不回写 GitHub 来修正它。
- #50 仍只协调 frontend correctness、organization 和 integration review scope。
  [#60](https://github.com/endingly/ptx_frontend/issues/60) 的更广 backend-consumer handoff
  独立验收；完成本记录不会宣称 #60 完成。
- [#59](https://github.com/endingly/ptx_frontend/issues/59) 需要硬件验证，按本轮约束明确
  跳过，不能用本分支的 host/simulator 检查代替。

## 基线已并入的 native child

`d07bd8e` 是以下 native child 的本轮起始已并入基线：

`#66 #67 #69 #70 #71 #72 #73 #74 #77 #78 #79 #80 #81 #82 #83 #84 #85
#93 #94 #95 #96 #97 #98 #99 #104 #106 #109 #112 #116`。

它们的历史问题、各自 acceptance 和已有证据仍由 child issue 所有。本行仅划分本轮
实施范围：不因 native closed 状态而重新宣称或验证这些旧修复。

## 本分支的九个剩余 child

下表以实际 commit 和已报告的 focused evidence 记录本轮结果。`通过`只表示对应命令
或 gate 已实际报告成功；它不是整个分支的最终 integration 结论。

| Child | 本轮提交/状态 | 已有针对性证据 | 最终汇总 |
| --- | --- | --- | --- |
| #75 | `621e629 fix(lexer): track CRLF in block comments` | 该项 focused lexer/CST Debug build 与测试已报告通过。 | 已通过最终 Debug/CTest gate。 |
| #76 | `36766ca docs(cmake): align codegen component contract` | 安装 CMake component/documentation contract 已按 focused package 路径检查。 | 已通过最终 Debug/CTest 与 package gate。 |
| #86 | `be7be4b refactor(semantic): normalize function contracts` | semantic/resolved/package focused verification 已由 owner 报告；提交经过与 #89 的隔离修正。 | 已通过最终 Debug/CTest gate；不以强类型重构本身证明 #60/W02/W04 完成。 |
| #87 | `3a07838 refactor(resolved-ir): partition handwritten resolver responsibilities` | `ptx_frontend_resolved_ir` focused Debug build 已报告通过，随后 resolver extraction 进入当前提交。 | 已通过最终 Debug/CTest gate。 |
| #88 | `d831872 refactor(resolved-ir): handwrite module model containers` | full Debug build、generation self-heal 和 installed package consumer 已报告通过。 | 已通过最终 gate；不把此证据扩展成 #60 handoff 完成。 |
| #89 | `06cc062 test(resolved-ir): split module coverage by capability` | `ResolvedModule.*` 209/209、decimal/warp/modern focused tests 18/18 已通过；原 195 个 TEST 名称保留。 | 已通过最终 Debug/CTest gate；保持与 #86 ABI contract 的提交隔离。 |
| #90 | `88be399 build(cmake): prefix private component targets` | embedded-parent 和 installed-consumer 集成路径已报告通过；公共 aliases/export names 保留。 | 已通过最终 Debug/CTest 与 package gate。 |
| #91 | `3407123 style(ci): enforce handwritten C++ conventions (#91)` | naming/legacy-interface audit、formatter/全树机械 format、post-format Debug build、CTest 与 package consumer 均已通过。 | 已纳入最终 gate。 |
| #92 | `23d9ed1 fix(semantic): preserve binary bitwise signedness` | focused `test_semantic` 45/45 通过。 | 已通过最终 Debug/CTest gate；其修复不等同 #59 的 `.b128` 硬件 oracle。 |

以上提交均在 `d07bd8e..HEAD` 的实际 log 中。提交 message 和 focused test 报告是可追溯
证据；没有把 native status 当作新的正确性证明。

## 同轮但不属于 #50 child 的工作

- #123 的 `641c51c` 与 #125 的 `52280f8` 是额外独立 scope，不能计入 #50 native
  completion，也不应改变 #50 child 的 acceptance。
- #62 的 `b411e29` 是参数声明剩余范围分类，独立于 #50；它不替代 #60/W10。
- #60 的 `cf80148` 是更广 backend-consumer handoff 的独立提交。其当前 source Debug
  验证已通过，但该事实不把 #60 的独立 acceptance 计入 #50。
- #51 的 `05a6669 docs: audit core opcode completeness (#51)` 提供 archived PTX 9.3 与
  simulator pin 的审计记录。其 whole-op frontend gaps 已关联 #127（MOV `.b128` layout）、#128
  （`mul`）、#129（`setp`）、#130（`ld`）、#131（`st`）和 #132（add/sub carry-condition
  contract）；创建 tracking 不等同这些 implementation 已完成。#126 是初始快照之后新建的具体
  `mov.pred` 缺口，仍保持独立；现有 #53 与 ptxsim#25 用于 FMA tracker 去重，不改变历史 pin
  execution 证据。本轮不自动把这些后续项扩大为实现任务。

## 统一收尾结果与余项

本轮 final-gate 的可复现命令为：

```sh
cmake --build --preset ci-linux-gcc-debug --parallel
ctest --preset ci-linux-gcc-debug --parallel 2 --output-on-failure
.venv/bin/python -m unittest_parallel -s python/tests -t python -p 'test_*.py' --level=module -j 2
ctest --preset ci-python-and-package-consumer --parallel 2 --output-on-failure -R '^ptx_frontend\.package_consumer$'
python3 .github/scripts/check_clang_format.py --clang-format clang-format-21
```

在 `cf80148` 之后、全树 format 之后，当前工作树已报告以下实际结果：

- post-format Debug source build exit 0；
- post-format Debug CTest 834/834 通过，80.96 秒，包含 embedded-parent、generation
  self-heal 和 topology coverage；
- Python database/generator checks 203/203 通过（`-j2`，6.650 秒）；
- post-format installed package consumer 1/1 通过，77.87 秒；
- formatter 对 116 个 tracked source 通过，helper tests 19/19 通过；协调者还以 byte
  comparison 确认 72 个 C++ 改动均与 `clang-format-21` 对 `cf80148` 的输出完全一致。

这些结果替代了先前“尚未运行”的 final-gate 占位文字；#91 的 `3407123` 已补齐最后的
formatter/naming acceptance。因此，本记录所列九个剩余 native child（#75、#76、#86、#87、
#88、#89、#90、#91、#92）已完成本分支的 scoped coordination acceptance。

该结论只基于本记录中的实际提交和 gate 证据：它不把 #60 的独立 backend-consumer handoff
算作 #50 child completion，不把 #51 已关联的 #126、#127、#128、#129、#130、#131、#132
implementation 视为完成，也不以 host/simulator 测试取代 #59 的硬件验证限制。协调者可据此
进行 #50 的最终处理；不得改写这些独立边界。

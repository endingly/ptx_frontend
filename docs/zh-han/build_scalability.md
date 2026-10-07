# 生成模型的构建扩展性

本文记录本地构建成本基线，不是 ISA 一致性测试、运行时 benchmark 或 CI 计时门槛。
测量对象为关闭测试与运行时 benchmark 的前端库。仅供测量的 YAML 变更不会作为
opcode 扩展交付。

## 提交与环境

被测源码为 `bacdfe008188c6302dcbaa668561092f09390b45`，于 2026-09-15 从 Git
单独归档，包含 execution-predicate 重新校验修复。测量期间工作树内的文档修改不会
进入该归档。

| 设置 | 记录值 |
| --- | --- |
| 构建 | Debug；Ninja；8 个并行编译任务 |
| 编译器 | GCC/G++ 15.2.0，Ubuntu `15.2.0-16ubuntu1` |
| CMake / Ninja / Python | 4.2.3 / 1.13.2 / 3.14.4 |
| 主机 CPU | AMD Ryzen 7 H 260；环境可见 6 核 / 12 个逻辑 CPU |
| 内存 | 环境可见约 23 GiB RAM 和 8 GiB swap |
| cgroup 限额 | CPU 为 `max 100000`；内存为 `max` |
| 编译缓存 | 使用 ccache 4.12.3 launcher，但通过 `CCACHE_DISABLE=1` 绕过缓存 |
| 依赖 | 使用已有 vcpkg 安装目录；关闭 manifest 安装 |

准备阶段已有 ccache 统计为 12,590 次可缓存调用、4,611 次命中、7,979 次未命中。
这是主机历史计数，不是本次测量的命中数；实验既不清空也不依赖这些缓存。不清空 OS
页缓存。依赖发现、本地 Python 包准备和配置与前端生成、编译的计时分开记录。

复用的 `x64-linux` 依赖目录包含 fmt 12.1.0 与 magic-enum 0.9.7；另有 GTest
1.17.0 和 Google Benchmark 1.9.5，但本次不构建或执行它们。源码 manifest 的 vcpkg
baseline 为 `256acc64012b23a13041d8705805e1f23b43a024`。依赖下载与缓存清空不属于
计时内容。

## 方法

四种场景使用同一组隔离的源码与构建目录，固定编译器、选项、依赖路径和并行度，
不同时运行其他构建。

1. **清洁构建：** 配置空输出目录，先计时 `resolved_ir_codegen`，再对已有生成文件
   的前端库构建计时。两个阶段分列，不静默排除生成成本。
2. **无变更增量：** 不修改输入，再次执行相同前端库构建。
3. **手写实现变更：** 在 `submod/resolved_ir/src/ptx_resolved_ir.cpp` 的
   `resolve_fields()` 内加入 `static_assert(true);`，以无行为变化的编辑测量实现文件
   的失效范围。随后恢复该文件并完成基线构建，不将恢复工作计入下一场景。
4. **规范 YAML form 变更：** 在 `python/code_gen/resources/ptx_spec/arithmetic.yaml`
   的 `abs_f32` 后加入下面的 variant，分别测量重新生成和编译。该受控增长样本复用
   现有 operand primitive，不表示功能验收，也不修改工作分支的规范数据库。

```yaml
      - name: abs_f16
        availability: {ptx: "6.5", sm: 53}
        modifiers:
          - {name: type, kind: type, domain: scalar_types, presence: fixed, value: f16}
        operands: $unary_scalar_register
        examples:
          - {ptx: "abs.f16 %h0, %h1;", valid: true}
```

依据构建日志统计实际执行的 C++ object 编译次数，而非 Ninja 总步骤数；生成、归档
和链接属于独立工作。同时记录生成文件内容哈希与时间戳，因为重写内容不变的输出
也可能使依赖失效。安装后 consumer 的编译与库编译分开测量；本次纯文档修改不运行
consumer 功能测试。

生成拓扑保持现状：按 category 分组的实现源码，加上共享 descriptor/dispatch，以及
包含指令定义、`ResolvedInstruction` union 和 reference visitor 的公共模型头文件。
Variant 数只描述模型规模，不代表整个 opcode 已完成。验收要求见
[扩展指南](yaml_instruction_spec.md)。

## 库构建结果

以下均为单次运行的墙钟秒数，不是相对另一提交的统计对照。生成后的库构建包含归档；
清洁场景还包含 lexer 生成，因此该列不是纯编译器 CPU 时间。

| 场景 | 配置 / 重新配置 | Resolved IR 生成 | 随后的库构建 | 实际编译的 C++ translation unit 数 |
| --- | ---: | ---: | ---: | ---: |
| 前端 object 清洁构建 | 3.25 | 43.94 | 92.55 | 29 |
| 输入不变 | — | 0 | 0.04 | 0 |
| 手写实现变更 | — | 0 | 18.08 | 1 |
| 增加一个 YAML form | 3.09 | 37.31 | 70.45 | 18 |

首次配置误选了其他 Python 环境，在编译前失败。表内成功配置是固定解释器与本地
被测 Python 包后的重试，可能复用首次配置的编译器探测结果。计时阶段开始前不存在
前端 object 和生成输出。准备失败、恢复手写实现及安装 SDK 均不计入表内。分阶段
计时也不等于一次连续的端到端秒表读数。

| 生成模型指标 | 基线 | YAML 样本 |
| --- | ---: | ---: |
| Opcode / variant 数 | 71 / 365 | 71 / 366 |
| `resolved_ir.gen.hpp` 字节数 | 499,538 | 500,143 |
| 三个公共头文件总字节数 | 517,565 | 518,170 |
| 全部 13 个生成文件总字节数 | 16,040,402 | 16,054,262 |

仅五个生成文件内容变化：公共模型头，以及 syntax、resolution、checker descriptor
和 arithmetic 的实现源码；其他八个哈希不变。其中 checker 与 resolution 公共头
内容未变，但时间戳更新。18 个 Resolved IR translation unit 全部重新编译，包括
arithmetic 以外的 category；其余 11 个前端 translation unit 未重新编译。本实验
没有将共享头内容变化的成本与重写内容不变输出的成本单独分离。

模型头 SHA-256 从
`f7309e1e3e8c1acf1f5cf4ec4e671fc3295ab9473d457209c4a784281f1aa333`
变为 `fbfee5b046870003902c14d6c5317e0502f2bba6dd53343b359a00b3c407d28e`。

## Consumer 与内存观察

两个已安装 SDK 分别使用空构建目录，通过被测提交的
`submod/resolved_ir/test/package_consumer` 项目，仅编译
`ptx_frontend_model_consumer` target。未运行可执行程序或 CTest。

| 已安装 SDK | Consumer 构建墙钟时间 | C++ translation unit 数 |
| --- | ---: | ---: |
| 基线 | 13.71 s | 1 |
| YAML 样本 | 13.73 s | 1 |

这是两个独立的 consumer 清洁编译，不是 consumer 增量失效实验。0.02 秒差异不能
证明新增 form 带来的影响。SDK 安装及恢复基线库的非计时构建均不计入结果。

环境没有 `/usr/bin/time`。另对按字节数最大的生成源码
`resolved_ir_checker_descriptor.gen.cpp`（5,538,307 字节）做了一次独立的直接 GCC
编译，以 Python `resource.getrusage(resource.RUSAGE_CHILDREN)` 记录内存。最大 RSS
为 466,020 KiB（约 455 MiB），墙钟时间 4.77 秒。这只是一次编译进程/子进程的
高水位样本，**不是**所有 translation unit 的最大值、8 个任务的总和或并行构建
内存上限。本次未收集这些全构建内存指标。

## 复现命令

准备好具有被测项目构建依赖的 Python 环境及兼容的已有 vcpkg 目录。计时前安装
本地归档包，确保 namespace 指向被测代码，而非其他 editable install。替换以下
三个绝对环境路径；测量期间不安装或下载依赖。

```sh
ptx_python=/absolute/path/to/venv/bin/python
ptx_deps=/absolute/path/to/vcpkg_installed
ptx_toolchain=/absolute/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
ptx_run=$(mktemp -d)
mkdir "$ptx_run/src"
git archive bacdfe008188c6302dcbaa668561092f09390b45 | tar -x -C "$ptx_run/src"
"$ptx_python" -m pip install --no-deps --no-build-isolation \
  --target "$ptx_run/python-package" "$ptx_run/src/python"
export PYTHONPATH="$ptx_run/python-package" CCACHE_DISABLE=1
cmake -S "$ptx_run/src" -B "$ptx_run/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
  -DPTX_FRONTEND_BUILD_BENCHMARKS=OFF \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-15 -DCMAKE_CXX_COMPILER=/usr/bin/g++-15 \
  -DPython3_EXECUTABLE="$ptx_python" \
  -DCMAKE_TOOLCHAIN_FILE="$ptx_toolchain" -DVCPKG_MANIFEST_MODE=OFF \
  -DVCPKG_INSTALLED_DIR="$ptx_deps" -DCMAKE_PREFIX_PATH="$ptx_deps/x64-linux" \
  -DCMAKE_INSTALL_PREFIX="$ptx_run/install" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
time -p cmake --build "$ptx_run/build" --parallel 8 --target resolved_ir_codegen --verbose
time -p cmake --build "$ptx_run/build" --parallel 8 --target ptx_frontend_resolved_ir --verbose
time -p cmake --build "$ptx_run/build" --parallel 8 --target ptx_frontend_resolved_ir --verbose
```

分别保留每条命令的 stdout/stderr。应用手写实现变更后重复库构建，再恢复文件并在
下一轮计时外重建。应用上述 YAML 变更，分别计时重新配置、生成和库构建。
在计时外通过 `cmake --install` 保存各 SDK。各 SDK 的 consumer 配置使用相同
编译器、Debug、toolchain、`VCPKG_INSTALLED_DIR` 和关闭的 manifest/cache，将
`CMAKE_PREFIX_PATH` 指向该 SDK，并计时
`cmake --build <consumer-build> --parallel 8 --target ptx_frontend_model_consumer --verbose`。

RSS 样本从 `compile_commands.json` 选定源码条目，在其记录的工作目录执行原编译
命令，改用独立 object 输出，并在全新 Python 进程的 `subprocess.run` 前后采集
`RUSAGE_CHILDREN`。这次额外编译不计入四个场景的耗时。原始日志、准确 patch、
哈希和环境快照保留于 `/tmp/ptx136-measure-20260915-144810-123299`；该临时路径
不是复现步骤的可移植依赖。

## 决策

保留当前布局。手写实现变更保持局部，无变更构建没有编译工作。YAML 样本确实暴露
了较广的 Resolved IR 失效范围：模型头增加 605 字节，伴随 18 个 translation unit
重新编译。这支持持续观察 form 增长成本，但不证明历史性能回退，也不构成全局 IR
重设计的必要性或已建立的体积/时间阈值。

如果后续重复测量确认该成本影响贡献者工作流，可分别调查保持未变输出内容/时间戳、
减少完整 union 依赖或按 category 拆分公共模型头。必须用相同输入比较候选方案，
再决定采用哪一种。不能仅为改善秒表数据而削弱已有的生成 self-heal、topology、
embedded-parent 或 installed-package 契约，也不能依据本次共享主机的单次 Debug
测量加入硬性 CI 计时门槛。

## 2026-09-29 测试构建补充实验

前文仅测量库构建，不能据此预测 Resolved IR 测试构建成本。新的未提交候选方案按
opcode 拆分生成的声明和定义，在 `model/<category>/<opcode>.gen.hpp` 下提供
完整的单 op 头文件，并将单 op 测试用例分到独立的编译单元。Category 头文件聚合
单 op 头文件；仅含模型的头文件仍独立存在，以保持 AST module 为不完整类型。
候选方案保留了 130 个 suite 中全部 887 个互不重复的 GTest 用例。

下表是同一主机上从空 Ninja 构建目录对 `test_resolved_ir` 目标进行的单次、同条件
**清洁构建**。基线为 `origin/main` 的 `1fe66d6`；候选方案为基于该提交、尚未
提交的 `perf/issue-203-parallelism-restart` 工作树。两者均使用 GCC 15.2、C 和
C++ Debug 选项设为 `-g0`、相同 vcpkg toolchain、启用测试、禁用 ccache，以及
6 个并行构建任务。各自使用所属源码工作树中的 Python 包。构建时间不包括配置。
没有并行运行其他构建。

| 文件布局 | 测试目标清洁构建 | 编译的 C++ object 数 | 构建目录 | 编译器 RSS 采样和峰值 | 最低采样 `MemAvailable` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Main 基线 | 450 秒 | 123 | 4.7 GiB | 15,632 MiB | 6,397 MiB |
| 单 op 候选方案 | 530 秒 | 251 | 5.3 GiB | 15,645 MiB | 6,280 MiB |
| 单 op 候选方案加定向模块测试拆分 | 494 秒 | 261 | 5.4 GiB | 10,964 MiB | 10,849 MiB |
| 上述方案再将 memory/vector 测试分为六份（已撤回） | 565 秒 | 266 | 5.7 GiB | 10,311 MiB | 11,117 MiB |

在此次清洁构建对比中，候选方案慢 80 秒（17.8%），也没有显著改善内存余量。
生成代码的 object 从 25 个增至 93 个，测试 object 从 78 个增至 138 个。
根据 Ninja 日志汇总的各 object 编译墙钟时长，两组分别增加约 207 和 288 秒。
这些时长可重叠，不是 CPU 用时。测试拆分
包括 55 个单 op 文件（204 个用例）、4 个跨 op 文件（10 个用例）和 8 个模块
文件（211 个用例）；其他既有测试保持原状。审计未发现可整体删除的重复模块
测试用例。Fence 测试中的一个重复 Membar 子断言已去除，专门的 Membar 断言
仍保留。

每秒采样一次 `cc1plus` 进程 RSS 并求和；共享页可能被重复计算，且该值不是
cgroup 内存峰值。本机数据不能证明较小的 CI runner 也安全。前文历史数据的
测试范围不同，不能与此处测试目标的构建时间直接比较。

第三行是在相同编译器、选项、toolchain 和 6 并行条件下额外进行的一次全新构建。
它借鉴 PR #209，仅拆分两个已测得的热点：`test_module_snapshot.cpp` 中的
typed projection 实例化，以及 `test_module_source_associations.cpp` 中的
source-association 用例。后者构造额外的已知 `Ret` 时也不再复制完整的
`ResolvedInstruction` 联合。测试 object 从 138 个增至 148 个，但新拆分
object 中最长的一项为 66.5 秒；原两个文件分别耗时 144.0 和 146.5 秒。
整个目标比未拆分的候选方案快 36 秒，但仍比 main 慢 44 秒。编译器 RSS 采样
和峰值比未拆分候选方案低 4,681 MiB。130 个 suite 中的 887 个测试全部通过。
这一次本地测量支持保留定向拆分供继续评审，但尚未满足 issue 的清洁构建速度
目标，也不能证明 CI 完全放开并行度安全。

第四行是在第三行候选方案上开展的独立后续试验：将 2,432 行的
`test_resolved_module_memory_vectors.cpp` 按语义分成六个源文件，保留全部
48 个测试正文以及完整的 887 个用例。原文件在第三行并行构建中耗时 102.2 秒；
单独编译耗时 80.6 秒，最大 RSS 为 3,328 MiB。尽管单个源文件变小，新的
清洁目标构建却慢了 71 秒。测试 object 编译时长之和从 1,743.1 增至
2,118.6 秒（各 object 墙钟时长之和），而编译器 RSS 采样和峰值只下降
653 MiB。新增五个编译单元
重复解析聚合头可能是原因之一，但本实验未单独测出其影响。六分组改动已撤回；
该行只保留负面结果证据，不表示最终交付的文件布局。另对原大文件做了一次
`-fsyntax-only -ftime-report` 探查：GCC 报告的 21.44 秒中，有 15.70 秒
属于模板实例化。该探查不生成目标代码，不能替代完整构建计时；它表明单纯减少
源文件行数并不能消除模板开销。

定向测试拆分之前的另一次试验，仅为包含 Resolved IR 聚合头的 29 个测试源文件
启用可选 PCH，并未对
整个目标使用聚合 PCH。在相同的 `-g0`、6 并行条件下，清洁构建约 400 秒时
进展到 269 步中的第 170 步，编译器 RSS 采样和已达 17,800 MiB，
`MemAvailable` 降至 4,427 MiB，因此停止了构建。构建未完成，不能提供
有效的总耗时或测试结果；试验性 CMake 配置已撤回。另一次探索性 `-g2`
构建使用本机 Ninja 默认的 14 并行，编译器 RSS 采样和达到 21,914 MiB，
`MemAvailable` 仅剩 121 MiB；该构建也已停止，不属于清洁构建对照。
本地可为方便而提高并行度，但这些结果不支持解除 Debug CI 并行限制。

## 2026-09-29 本机 Clang 对照

定向模块测试拆分后的 C++ 源码没有随纯文档提交 `092aac3` 改变。下列新增的
`test_resolved_ir` 清洁构建与上表第三行使用相同源码、Ninja、C 与 C++ 的
Debug `-g0`、禁用 ccache、vcpkg toolchain 和本机环境。每次从独立的空构建
目录开始，构建时间不含配置。Clang 为 Ubuntu Clang 21.1.8，对照 GCC
15.2.0。三次均编译 261 个 C++ object，887 个 Resolved IR 测试全部通过。

| 编译器 | 并行任务 | 目标清洁构建 | 编译器 RSS 采样和峰值 | 最低采样 `MemAvailable` | 构建目录 |
| --- | ---: | ---: | ---: | ---: | ---: |
| GCC 15.2 | 6 | 494 秒 | 10,964 MiB | 10,849 MiB | 5.4 GiB |
| Clang 21.1.8 | 6 | 492 秒 | 4,947 MiB | 17,031 MiB | 3.0 GiB |
| Clang 21.1.8 | 14 | 346 秒 | 8,865 MiB | 13,898 MiB | 3.0 GiB |

同为 6 并行时，2 秒差异不足以证明 Clang 编译更快；但 Clang 的编译器 RSS
采样和峰值低 6,017 MiB。使用本机 Ninja 默认的 14 并行后，Clang 比自身
6 并行快 146 秒，编译器 RSS 采样和仍低于 GCC 6 并行。这是利用内存余量
运行更多并发任务所得的收益，不是同并行度下的编译器速度优势。Clang 对既有
头文件重复发出 `-Wdefaulted-function-deleted` 警告；测量时未屏蔽这些警告，
它们不影响测试通过。

RSS 数字是每秒采样一次并求和：GCC 统计 `cc1plus`，Clang 统计
`clang++-21` 与 `clang-21`。共享页可能重复计算，这不是完整进程树或
cgroup 峰值。这些本机单次结果不能证明 CI 上的最佳并行度或内存安全，
也不能证明单 op 文件布局在 Clang 下快于 main。

## 2026-10-04 OwnedInstruction 编译后续测量

本次以 #216 之后的 main `386aebb` 为基线，对比同一源码工作树中尚未提交的
测试局部优化和私有生成器优化。每次 `test_resolved_ir` 构建均从独立的空 Ninja
目录开始，使用 Clang 21.1.8、Debug、6 个并行任务、禁用 ccache、相同的
已安装 vcpkg 依赖，以及 C/C++ Debug 选项 `-g0`。测试目标另追加
`-gline-tables-only`；生成代码与库对象仍使用 `-g0`。计时包含生成、263 个
C++ 对象编译及链接，不包含配置；测量期间没有其他并发编译。

| 源码 | 测试目标清洁构建 | C++ 对象数 | Ninja 步骤数 |
| --- | ---: | ---: | ---: |
| Main 基线 | 279.37 秒 | 263 | 280 |
| 仅缩小测试 checker 调用及头文件依赖 | 278.26 秒 | 263 | 280 |
| 再加入按家族过滤的 owned 投影及立即实例化的索引式引用访问器（中间方案） | 277.22 秒 | 263 | 280 |
| 最终方案：延迟实例化的索引访问器与精确 opcode 测试头文件 | 254.40 秒 | 263 | 280 |

前两个候选方案与基线间约 1–2 秒的差异落在单次运行波动范围内。最终方案
在这次匹配的单次测量中快 24.97 秒（8.9%）。测试对象编译时间区间从
142.37 秒缩短到 119.86 秒；Resolved IR 库对象区间仍约为 90 秒。
Ninja 中可重叠的各对象墙钟时长之和从 1,245.89 秒降至 1,112.25 秒；
该和既不是 CPU 用时，也不是完整构建用时。测试改动删除了仅为调用既有
`OwnedInstruction::check` 而实例化所有 opcode 的访问器。typed 投影测试
改为复制选中家族的既有 `OwnedInstruction` owner，在未选中的源码位置保留
空 owner，从而避开对所选 opcode record 类型构成的 variant 深拷贝。
部分普通测试改为包含精确 opcode 头文件，不再依赖类别或聚合头；显式验证
公开头文件兼容性的测试仍保留这些头文件。生成引用访问器在受限的泛型 lambda
中按规范 variant 与 operand-layout 索引分派，保持回调顺序及
`std::bad_variant_access` 兜底分支。

使用同一构建的 `compile_commands.json`、将对象写入临时路径，并串行单独
编译所得的归因数据如下：

| 编译单元 | 改动前 | 改动后 |
| --- | ---: | ---: |
| collective typed 投影测试 | 27.52 秒 | 2.75 秒 |
| module typed 投影测试 | 20.75 秒 | 2.60 秒 |
| 生成的 Cp 源文件 | 28.73 秒 | 26.51 秒 |
| 生成的 Mbarrier 源文件 | 26.03 秒 | 24.03 秒 |

投影对比的前后两次都使用基线生成头文件；生成源文件对比则仅在两次编译间
重新生成引用访问器。最初的直接索引切换使两个未改动的聚合头测试编译
分别从 5.34 秒升至 8.29 秒、从 5.34 秒升至 8.24 秒。让切换分支依赖
精确 opcode 后，两者恢复到 5.58 秒和 5.53 秒，同时保留生成源文件的
编译收益。最终清洁构建的 Ninja 日志中，最长对象为 Cp（28.59 秒）、
Mbarrier（24.73 秒）和 instruction-variants 测试（16.83 秒）；
两个投影对象不再居首。最终构建与基线一样发现 133 个
suite、910 个测试，GTest 列表哈希相同，且 910 个测试全部通过。
Resolved IR 的 119 个 Python 测试和 generation-plan 的 25 个测试也通过。
本次未采样可比的完整构建编译器 RSS 和 cgroup 内存峰值。
本地日志位于 `/tmp/ptx-compile-baseline.hfBBvj`、
`/tmp/ptx-compile-candidate.Yarq9p`、`/tmp/ptx-compile-measured.IDV8uT`
和 `/tmp/ptx-compile-narrow.dBDSvt`；这些临时路径不可移植。

这些数据只覆盖当前 main，不覆盖待合并的 matrix 或 tensor 分支。合并
引用访问器生成逻辑时，共享物理存储的 matrix layout 必须保留逻辑 variant
到存储 variant 的映射；当前 main 的直接索引只适用于逻辑与物理备选项一致
的情形。单 opcode 的 Cp 与 Mbarrier 源文件仍是明显编译成本：其 7.8 和
5.8 MiB 的生成定义合并了描述符、resolver、checker 与 owner bridge。
本实验尚不能证明拆分生成文件拓扑是安全或更快的方案。

## 2026-10-04 Descriptor 分区原型

本次以此前最终布局的 `125615b3ba48a0b45030d8806a5f76796a26396f` 为基线，
测量了一个尚未提交、现已撤回的通用原型。当时的实验性 CMake cache 列表
`PTX_RESOLVED_IR_DESCRIPTOR_PARTITION_OPCODES` 显式选择 canonical opcode，
将其 syntax、resolved、checker descriptor storage 放入单独的私有源文件；默认
列表为空。本次只选取 `cp` 与 `mbarrier` 测量，生成器测试还用 `add` 验证了
同一机制。选中 opcode 的公开强类型 getter 保持原签名，转发至私有 accessor。
两条指令生成的六个 storage struct 正文与基线逐字节相同。

基线与原型都使用 Clang 21.1.8、Ninja、Debug `-g0`、相同 vcpkg 依赖树、
禁用 ccache、6 个并行任务，以及 `test_resolved_ir` 目标。测试另加
`-gline-tables-only`。配置时间不计入构建。基线输出在
`/tmp/ptx-compile-narrow.dBDSvt`；原型输出、编译器 trace 与日志在
`/tmp/ptx-descriptor-proto.JZ9pyk`。这些临时路径不是可移植的复现输入。

| 清洁目标构建 | 基线 | 选中分区的原型 |
| --- | ---: | ---: |
| 单次墙钟耗时 | 254.40 秒 | 246.89 秒 |
| C++ object / Ninja 步骤 | 263 / 280 | 266 / 283 |
| Resolved IR 测试 | 910 / 133 suite | 911 / 134 suite |

原型增加两个 descriptor object 和一个 descriptor 生命周期测试 object。
原有 910 个测试全部保留在发现列表中，新增测试也通过。完整目标的墙钟差异
只来自共享主机上的单次运行，不能归因于分区：新测试 object 和构建调度也有影响。
另按各自 `compile_commands.json` 用临时 object 输出，串行单独编译所选对象：

| 指令 | 基线源文件 | 原型强类型源文件 + descriptor 源文件 | 单独编译的强类型源文件 RSS 峰值：基线 → 原型 |
| --- | ---: | ---: | ---: |
| Cp | 25.974 秒 | 24.105 + 2.863 = 26.968 秒 | 1,468,576 → 1,446,336 KiB |
| Mbarrier | 22.698 秒 | 22.070 + 2.561 = 24.631 秒 | 1,263,028 → 1,213,404 KiB |

两个独立 descriptor 编译进程的 RSS 峰值分别为 249,664 KiB 与
240,652 KiB。表内 RSS 是单进程高水位，不是 6 并行或完整构建的内存峰值。
串行合计编译时间对 Cp 增加 0.994 秒，对 Mbarrier 增加 1.933 秒。
另外的 Clang `-ftime-trace` 记录：Cp 的 `ExecuteCompiler` 从基线
29.631 秒变为 27.065 + 2.907 = 29.972 秒；Mbarrier 从 25.899 秒
变为 24.131 + 2.741 = 26.872 秒。耗时靠前的函数实例化仍包含
`std::expected<T>` 构造及嵌套的 `std::variant` 移动/拷贝 visitor，
单项约 2–2.5 秒。嵌套 trace 事件会重叠，不能独立相加。

选中分区的构建通过全部 911 个测试；再次构建没有工作。已安装的
`examples/conversion_consumer` 配置、链接和运行均通过，私有 accessor 头
未被安装。在另一构建目录中将选项从选中切换为空后，生成器恢复原布局，
四个分区文件从输出清单和生成目录中删除。这两个测量样本不支持默认启用
descriptor 分区。本次原型的代码、测试、构建改动及 lexer 文件命名改动均已撤回，
仅保留这份中英文测量记录；当前代码不提供上述实验选项。今后的布局方案需要
新的测量结果与 core review。`.gen.hpp` / `.gen.cpp` 命名要求仅适用于
Python 生成的文件，不适用于 Flex lexer 输出。

## 2026-10-05 活跃 Resolved IR 迁移测量

本节保留直接类模块仍使用 `resolved_ir_experiment` 名称时的测量记录。当前模块已使用
规范的 `resolved_ir` 路径与目标；下方命令和产物名称按测量当时保留。
`tools/owned_ir_experiment/README.md` 的早期运行期数字使用不同的测试程序、
工作负载、处理流程及驱动，不能与本节的 Google Benchmark 测量直接比较。

本节测量基于 `5f8d639` 的未提交活跃 `resolved_ir_experiment` 迁移，
以 10 月 4 日最终构建的**历史记录**为参照；未重新构建或运行旧实现。
两次清洁测试目标构建均使用 Clang 21.1.8、Ninja、C/C++ Debug `-g0`、
仅测试目标附加 `-gline-tables-only`、6 个并行任务、禁用 ccache、相同的
已有 vcpkg 依赖目录，以及 CMake 4.3.3。新目标通过临时
`CMAKE_PROJECT_TOP_LEVEL_INCLUDES` 延迟 hook 附加测试标志。配置时间
不计入，生成、编译、归档和链接计入。新目标从空构建目录开始，期间没有
其他构建。历史记录没有足够信息确认主机硬件和负载相同；两个墙钟时间
都只是单次观测。

| 清洁 Debug 目标指标 | 历史 `test_resolved_ir` | 活跃 `resolved_ir_experiment_tests` |
| --- | ---: | ---: |
| 目标构建墙钟时间 | 254.40 秒 | 270.85 秒 |
| C++ object / Ninja 步骤 | 263 / 280 | 263 / 274 |
| 生成输出边的墙钟跨度 | 57.17 秒 | 105.61 秒 |
| Resolved 库 object / 编译区间 | 102 / 90.28 秒 | 103 / 66.83 秒 |
| 测试 object / 编译区间 | 150 / 119.86 秒 | 149 / 96.64 秒 |
| 可重叠的 C++ object 墙钟时间之和 | 1,112.25 秒 | 949.65 秒 |
| 列出的 GTest case / suite | 910 / 133 | 914 / 134 |

新目标总耗时多 16.45 秒（6.5%），因此这次结果未显示清洁构建提速。
库和测试的编译区间较短，但生成跨度长 48.44 秒。旧生成器有 7 条
可重叠输出边，新生成器有 1 条；表内跨度是经过的时间，不是可相加的
CPU 时间。编译区间也彼此重叠。模型大小与测试清单均已变化，不能把
总耗时差额归因于单一源码。新目标的 914 个测试只通过
`--gtest_list_tests` 列出，本次测量未运行。新 Ninja 日志中耗时最长的
object 是生成的 Cp（13.99 秒）、Mbarrier（12.05 秒）和
instruction-variants 测试（6.85 秒）。

活跃模块新增可选、不会安装的 `frontend_experiment_symbol_table_scaling`
benchmark 目标，由默认 `OFF` 的既有 `PTX_FRONTEND_BUILD_BENCHMARKS`
选项控制。驱动保留历史 fixture 生成、case 名称、校验和 checksum；
仅 Resolved IR 头文件路径与可执行文件错误标签不同。历史模块和驱动未改动。
计时前在单独的临时 vcpkg 目录安装 Google Benchmark 1.9.5。新 Release
目标使用 Clang 21.1.8、`-O3 -DNDEBUG`、禁用 ccache，构建并行度为 6；
清洁构建耗时 196.71 秒，这是准备成本。运行时主机为 AMD Ryzen 9
5950X，可见 32 个逻辑 CPU、约 23 GiB 内存，cgroup 无 CPU 或内存限额。

下表为 Google Benchmark 5 次重复的**每次迭代 real time 中位数，单位毫秒**；
每次重复至少采样 0.1 秒。每个生成样本有 N 条有效 `mov.u32` 指令和
2N 个已绑定 operand 引用。普通单 scope 样本存储 N+1 个 symbol，紧凑
单 scope 样本存储 2 个；nested 样本使用 2 个函数和词法 block。
`parse` 解析完整源码并验证 AST；`resolve_module` 从预解析 AST 开始，
包含自身的绑定、解析、检查、结果验证和析构。两项耗时不是互不重叠的
阶段。选中的 34 个 case（16 种形状 × 2 种操作，另加 2 个 corpus
case）共 170 条重复记录，全部通过校验，无 benchmark 错误。每次重复
的迭代数为 2 至 4,815。

| 逻辑寄存器数 | 声明 | Scope | Parse | Resolve module |
| ---: | --- | --- | ---: | ---: |
| 1,000 | ordinary | single | 1.334 | 5.799 |
| 1,000 | ordinary | nested | 1.284 | 6.029 |
| 1,000 | compact | single | 0.766 | 3.362 |
| 1,000 | compact | nested | 0.787 | 3.425 |
| 2,000 | ordinary | single | 2.810 | 12.303 |
| 2,000 | ordinary | nested | 2.594 | 11.891 |
| 2,000 | compact | single | 1.525 | 6.591 |
| 2,000 | compact | nested | 1.467 | 6.656 |
| 4,000 | ordinary | single | 6.257 | 28.724 |
| 4,000 | ordinary | nested | 6.294 | 25.023 |
| 4,000 | compact | single | 3.083 | 15.989 |
| 4,000 | compact | nested | 2.996 | 14.018 |
| 8,000 | ordinary | single | 22.624 | 55.134 |
| 8,000 | ordinary | nested | 12.525 | 52.340 |
| 8,000 | compact | single | 6.692 | 28.600 |
| 8,000 | compact | nested | 7.108 | 28.875 |
| M12 `natural_kernel_sm80.ptx` corpus | — | — | 0.029 | 0.193 |

共享主机的采样噪声明显：ordinary/single N1000 的 parse real time 变异系数
为 24.43%，N8000 为 20.83%。没有更多受控重复实验时，不应把表内布局
差异直接解释为因果效果。

在受检索的受控历史记录和历史测量输出中，没有旧 benchmark 驱动的运行时
JSON、CSV 或数字计时汇总。因此表内数据仅是新模块的运行时基线，
**不能**作为旧版到新版的运行时提速数据。单迭代预检、多迭代 JSON、
日志、Ninja 日志、编译命令及临时测试标志 hook 保存在
`/tmp/ptx-resolved-measure.qC5wI0`；该路径仅是本机临时记录，不可移植。
Release 运行命令为：

```sh
timeout 300s /tmp/ptx-resolved-measure.qC5wI0/release/submod/resolved_ir_experiment/benchmark/frontend_experiment_symbol_table_scaling \
  --benchmark_filter='symbol_table_scaling/(parse|resolve_module|corpus_parse|corpus_resolve_module)/' \
  --benchmark_min_time=0.1s --benchmark_repetitions=5 \
  --benchmark_out=/tmp/ptx-resolved-measure.qC5wI0/runtime-parse-resolve-sampled.json \
  --benchmark_out_format=json
```

按上述标志另行完成配置后，实际计时的构建命令为：

```sh
CCACHE_DISABLE=1 cmake --build /tmp/ptx-resolved-measure.qC5wI0/debug \
  --parallel 6 --target resolved_ir_experiment_tests
CCACHE_DISABLE=1 cmake --build /tmp/ptx-resolved-measure.qC5wI0/release \
  --parallel 6 --target frontend_experiment_symbol_table_scaling
```

两个构建的准确配置缓存和临时测试标志 hook 均保存在 artifact 目录。
依赖安装与配置在计时命令之前完成。

## Review 分支上直接语义类的 variant 增量构建

这是一组独立的单次增量测量：源码以
`63369f8bc16f284662ffda4da1848e2d726edc8d` 为父提交，并包含本次修改的递归输入
与 owned 坐标修复；没有重做上面的历史清洁构建。构建为 Debug，使用 Ninja 1.13.2、
CMake 4.3.3、Clang 21.1.8、3 个并行编译任务和 6 个 generator 产物 writer。
Production 标志为 `-g0 -std=gnu++23`，resolved-IR 测试目标另外使用
`-gline-tables-only`。两者都使用 ccache 4.12.3 和已有 `x64-linux` 依赖树。
5 GiB 编译缓存接近满额；没有并发运行其他构建。

先将 `ptx_frontend_resolved_ir` 与 `test_resolved_ir` 构建到无工作基线，随后在
`instructions/ptx_spec/arithmetic.yaml` 的 `abs_s16` 后临时加入一个**合成**
`abs_s8` variant：

```yaml
      - name: abs_s8
        availability: {ptx: "9.3", sm: 120}
        modifiers: [{name: type, kind: type, domain: scalar_types, presence: fixed, value: s8}]
        examples: [{ptx: "abs.s8 %b0, %b1;", valid: true}]
```

这是实际的 generator 输入，产生了 `AbsS8` final class 和
`InstructionKind::AbsS8` 枚举值；它不表示 `abs.s8` 是受支持的 PTX 形式，交付源码中也
没有该样本。通过普通 `resolved_ir_codegen` 构建先完成 CMake 重新配置与生成，
再分别计时两个目标。下表的 object 数来自 Ninja 实际执行的编译步骤，
不是目标中声明的源码数量：

| 增量步骤 | 墙钟时间 | 实际编译的 C++ object |
| --- | ---: | ---: |
| 两个目标的无变更基线 | 无工作 | 0 |
| 重新配置及 `resolved_ir_codegen` | 38.003 秒 | 0 |
| `ptx_frontend_resolved_ir` | 142.840 秒 | 96：93 个生成源、3 个手写源 |
| 随后的 `test_resolved_ir` | 176.301 秒 | 129 个测试源 |

Production 和测试编译合计增加 225 次 ccache miss、0 次 hit。
公共 base 头文件发生变化，因此本来未修改的 opcode 源文件，例如
`resolved_ir_data_movement_cvta.gen.cpp` 和
`resolved_ir_parallel_synchronization_and_communication_bar.gen.cpp`，仍被编译。
无关的测试源 `test_select_variant_cp.cpp` 与 `test_select_variant_xor.cpp` 也被编译。
这些按目标统计的候选集展示了公共 base 依赖中保留全局 `InstructionKind` 目录的成本；
不能据此推断清洁构建提速，或其他主机与缓存状态下的成本。移除临时 variant 后，
两个目标已重建回普通生成状态；再次构建无工作。

复现时先构建两个目标并确认无工作，然后加入上述 variant，在同一配置好的构建目录
按顺序运行：

```sh
cmake --build <build-dir> --target resolved_ir_codegen --parallel 3
ninja -C <build-dir> -j 3 -d explain ptx_frontend_resolved_ir
ninja -C <build-dir> -j 3 -d explain test_resolved_ir
```

完成后删除临时 variant，并重新构建两个目标，然后再做测试或其他对比。
测量使用 `/tmp/ptx-six-cold.drYe8K/build`；本机编译步骤日志位于
`/tmp/ptx-pr235-review.J2S3oW`，不是可移植的产物。

递归 CMake 输入修复还通过普通 `resolved_ir_codegen` 目标验证。
临时 `instructions/ptx_spec/__review_probe__/nop.yaml` 使用
`ptx-instr/v1`、`miscellaneous` category、`control_flow` codegen category、
一个无 operand 的 `nop_probe_a` variant。增加文件后发现 `nop.gen.hpp`
及其对应源文件；仅将 variant 名改为 `nop_probe_b` 后类重新生成；移除 YAML 后
构建重新配置，并删除两个生成文件。输入成员列表在增加及移除时改变，
样本文件不在交付源码中。复现时在子目录创建带普通 schema 头和一个 bare variant
的 YAML；在增加、修改 variant 名、删除文件后各运行一次
`cmake --build <build-dir> --target resolved_ir_codegen`，检查生成的 model leaf 和
构建树内 `submod/resolved_ir/resolved_spec_inputs.txt`。

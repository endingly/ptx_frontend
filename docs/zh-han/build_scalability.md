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

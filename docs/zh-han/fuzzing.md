# 有界前端模糊测试

[当前支持范围](../sdk_support.md)仍是唯一支持权威。此可选开发构建需要 Clang、
libFuzzer、ASan/UBSan 运行库、Flex、Python 生成器依赖、fmt 和 magic_enum。
沿用普通构建的依赖及工具链参数（例如 CI preset 中的 vcpkg 工具链）。Linux 可用
`PTX_FRONTEND_LLD_LINKER` 指定兼容的 `ld.lld`。使用独立构建目录，保持
`BUILD_TESTING=OFF`；普通 GCC 构建与已安装 SDK 不继承插桩或 fuzz 运行库选项。

```sh
CC=clang CXX=clang++ cmake -S . -B out/build/fuzz-asan -G Ninja \
  -DPTX_FRONTEND_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE="-O1 -gline-tables-only -DNDEBUG" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build out/build/fuzz-asan --parallel 2 \
  --target fuzz_lexer_cst fuzz_frontend fuzz_frontend_canary
python3 fuzz/run_smoke.py --runs 2000 --seconds 60 --seed 93
```

`fuzz_lexer_cst` 检查词法分析有限终止与 CST 无损保留输入。
`fuzz_frontend` 将源码限制为 64 KiB，解析独立指令和模块语法，仅将无诊断的模块
AST 送入绑定、声明/操作数解析及严格验证。解析、恢复、绑定、类型、不支持的操作及
缺失上下文错误均为正常结果。意外异常和严格成功后的不变量破坏会使运行失败。
不设置逐 token 过滤，不逐输入调用 ptxas，也不开展公开 IR 修改模糊测试。

解析器与源码缓冲区销毁后才解析 AST 的语义。严格成功后，销毁 AST，再验证独立
拥有数据的 IR，检查模块复制构造、复制赋值、移动构造/赋值、函数复制/移动及指令
clone 保持 `RequireCompleteContext` 验证成功。草稿或 AvailableContext 成功不作
有效性判据。内部 `fuzz_frontend_canary` 验证带版本的 C1 硬件向量 `mov` 和标量
立即数 `st` 确实到达严格成功及所有权阶段，并重放非法字节、恢复、缺失上下文、
深度边界和输入上限。普通 `BUILD_TESTING=ON` 构建也将此 canary 注册到 CTest。

实现插桩覆盖 base、lexer、CST、syntax、binding、semantic 和 resolved IR，包含
生成的 resolved 源码；common 内联代码通过这些使用方插桩。选项保持私有，不改变
导出的 target 接口。插桩库仅用于独立 fuzz 构建；安装 SDK 应使用普通构建。

辅助脚本复制手写小种子 `fuzz/corpus`、已有 CST 恢复种子及项目 MIT 自然输入
`corpus/ptx93_real_inputs/generated`、`corpus/m12` 到被忽略的 `out/fuzz-smoke`。
原始语料保持只读。脚本先运行 canary，再对两个目标分别确定性重放初始语料
（`-runs=0`），随后进行固定随机种子的短期 campaign：每个目标最多 2,000 次执行
或 60 秒，RSS 上限 2 GiB，单输入 5 秒，变异长度上限 64 KiB。lexer/CST harness
保留已有较大输入契约；frontend 在解析前拒绝超限输入。解析树深度上限 128，
嵌套块深度上限 256，详见当前支持范围。

`run-metadata.json` 记录二进制、脚本、构建配置哈希及实际 ASan/UBSan 环境。
`summary.json` 记录精确命令、返回码和墙钟耗时；`seeds.json` 记录原始路径、大小及
SHA-256。`initial-corpora.json` 记录每目标实际初始 scratch 文件与哈希，包含恢复运行
保留的发现。日志、变异语料与失败 artifact 均留在 scratch。需要相同初始语料时使用
全新 `--scratch out/fuzz-smoke-fresh`；复用目录会保留先前发现。固定种子与预算在
同一构建上可复现 smoke；墙钟限制和覆盖变化仍可能改变变异序列。字典为
`fuzz/ptx.dict`。

使用相同二进制与 sanitizer 环境重放失败：

```sh
ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  out/build/fuzz-asan/fuzz_frontend out/fuzz-smoke/fuzz_frontend/artifacts/crash-HASH
```

保留失败字节、命令、日志、工具链与版本；先用 libFuzzer 的 `-minimize_crash=1
-exact_artifact_path=out/minimized` 最小化，再单独提交语义评审后处理生产语义。
正常的严格拒绝不是缺陷。

`Frontend fuzz smoke` 工作流复用 Linux 依赖设置，使用独立 Clang/ASan/UBSan
构建。每目标最多 2,000 次或 30 秒，job 上限 45 分钟，失败日志、语料/artifact
及构建配置保留七天。此 smoke 不证明完整 PTX 语义覆盖，也不替代确定性回归测试。

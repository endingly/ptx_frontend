# CI 工作流与缓存写入职责

| 工作流 | 触发事件 | 检查范围 |
| --- | --- | --- |
| `linux-ci.yml` | PR 打开/更新/重新打开、每月定时、手动触发 | 完整 GCC Debug / Release build/test preset；检查名保持 `Debug`、`Release` |
| `python-and-package-consumer.yml` | 同上，完整验收事件 | Python 测试、CI helper 测试及完整 installed-package consumer；检查名保持 `test` |
| `integration-smoke.yml` | push 到 `main` 或 `dev` | 完整 GCC Debug/Release 构建预热缓存；仅 Debug 运行 installed-package consumer |
| `release-wheel.yml` | push `v*` tag | 既有 wheel 构建、smoke 与 release 发布 |

完整 PR 验收仍是合并门禁。Integration 缓存预热不具备同等覆盖，也不能代替直接 push 前
应有的验证。每月与手动触发的完整验收继续保留；按事件/工作流区分的 concurrency group
避免 integration push 取消这些任务。Release 发布行为不变。

## Integration 缓存预热

两个 integration job 使用与 PR 完全相同的普通 configure/build preset：
`ci-linux-gcc-debug` 与 `ci-linux-gcc-release`，在实际 PR 构建目录中预热生产库、
单测可执行文件及默认构建中的生成 fixture，并按 build type 区分编译缓存命名空间。
Integration job 不调用 C++ 全量测试或 Python 测试；构建测试可执行文件时，仍可能
执行正常的 CMake/GoogleTest 测试发现命令。

Debug 额外只运行既有 installed-package 验收测试：

```sh
ctest --preset ci-python-and-package-consumer \
  -R '^ptx_frontend\.package_consumer$' --output-on-failure --no-tests=error
```

它的普通及 relocation consumer 构建与 PR package job 使用相同临时路径和编译参数。
外层测试注册用脚本对应的 `PTX_CMAKE_*` 参数名转发 compiler 与 compiler launcher，
同时继承父构建的模块扫描策略（本项目使用头文件，该策略为 OFF），避免不必要的模块
编译参数阻止 consumer 缓存，使嵌套构建在启用 ccache 时确实使用缓存。
这替代了 integration job 中独立路径的 smoke consumer，避免 Debug 编译工作目录
不同导致完整 consumer 无法复用其对象缓存。Release 只构建。提高预热覆盖的代价是
每次 integration push 都构建两种配置；省去的是完整测试执行，而非必要的编译工作。

## 本地 library-only smoke

安装通常需要的 Python generator 依赖并配置 vcpkg 环境后：

```sh
cmake --preset ci-integration-smoke
cmake --build --preset ci-integration-smoke
ctest --preset ci-integration-smoke --output-on-failure --no-tests=error
```

也可在本地运行等价的 `cmake --workflow --preset ci-integration-smoke`。
此轻量 preset 不再是 integration workflow 的缓存预热构建。
构建只指定 `resolved_ir`，其依赖闭包会构建全部待安装项目库与生成的公共头文件，
不会构建单元测试可执行文件。Configure 仍启用测试并需要相应依赖；这不是免依赖或
免代码生成的构建。

Smoke preset 显式开启 `PTX_FRONTEND_ENABLE_PACKAGE_SMOKE_TEST`，且只选择
`ptx_frontend.package_consumer_smoke`。该测试安装 package，检查公共 header/resource
及 private file 不应泄露的边界，单独 configure/build 既有公共 consumer，再运行它。
Consumer 会验证 parser/resolver/checker 行为、拥有自身数据的 declaration metadata
与 FMA metadata，而不只是链接成功。CTest 没有选中测试时会报错。

既有 `ptx_frontend.package_consumer` 仍是完整测试，包含 relocation、非默认 data path
及 package discovery 负例。即使注册了额外 smoke 测试，完整测试也显式禁用 smoke-only
行为。两个测试使用各自独立的 install/build 临时目录。

Smoke 与普通 Debug preset 共用 `out/build/ci-linux-gcc-debug` 及其 install prefix，
使编译缓存中的对象使用匹配的构建路径。不要在同一 checkout 中并发 configure/build
这两个 preset。恢复普通验收前，使用 `ci-linux-gcc-debug` 重新 configure，关闭可选
smoke 注册。Release 使用独立 build 目录。

## 缓存复用及限制

工作流共用 `.github/actions/setup-linux` 完成系统包、Python 依赖及 vcpkg setup。
缓存身份包含实际安装的工具链与 runner image；vcpkg 使用 manifest 的精确 builtin
baseline。依赖二进制 archive 与源文件下载分别缓存，由指定 job 写入共享依赖/下载
缓存；其他 job 只恢复并使用。

Integration 与 PR 的 Debug job 共用编译缓存命名空间，Release 另有共用命名空间。
Python/package job 恢复 Debug 缓存，但不上传较小的 library/consumer-only 快照，
避免它成为替代完整构建快照的最新缓存。完整构建 job 每次运行可保存新快照，使仅源文件
变化时也能保存新对象；这不绕过 ccache 的内容校验。

预热覆盖普通 Debug/Release 构建图及 Debug package consumer 的编译路径，但不保证
源文件、头文件、编译参数变化后仍命中，也不保证覆盖测试内重新配置的构建变体或已被
淘汰的对象。本仓库没有需要额外预热的 Clang 验收矩阵。匹配构建路径是刻意的：ccache
默认会把 Debug 编译工作目录计入缓存 hash，见 [ccache 路径哈希规则](https://ccache.dev/manual/latest.html#config_hash_dir)。

GitHub 允许 PR 恢复 default/base branch 缓存，但 PR merge-ref 的缓存不能预热
default branch 或其他 PR。保留 integration branch 的缓存写入，避免依赖不可能的
反向复用。见 [GitHub 缓存访问规则](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache)。
新的 cache-key 命名空间首次运行会 miss；缓存淘汰及 runner/toolchain 变化也会引起
miss。实际命中率与端到端节省需要观察 hosted run，本地 smoke 成功不能证明这些指标。

第三方 action 引用固定到提交，并检查 workflow 与本地 composite action 中的引用。
CI helper 回归测试在 Python 验收 job 中运行。本次拆分的相关本地验证包括工作流 lint、
helper 测试、两种完整 build preset 及既有 Debug package consumer；若改变
frontend 行为，仍需运行通常的项目门禁。

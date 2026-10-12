# 已安装的 CMake Components

安装后的 `ptx_frontend` CMake package 除 C++ target components 外，只公开一个非 target 数据 component：`ptx_spec`。

每个 C++ component 在 `find_package` 后和通过 `add_subdirectory` 嵌入源码树时都使用稳定的公开 target 名称 `ptx_frontend::<component>`。源码构建的实际 target 使用 `ptx_frontend_` 前缀，因此不会与 parent project 的通用 target 名称冲突；这些实现名称不是公共 API。

活跃的 Resolved IR 目标为 `ptx_frontend::resolved_ir`，
`ptx_frontend::ptx_frontend` 会链接它。接口使用直接语义形式类和
`std::unique_ptr<Instruction>` 所有权。

## 静态库与共享库

`BUILD_SHARED_LIBS` 默认值为 `OFF`。配置源码构建时设置
`-DBUILD_SHARED_LIBS=ON` 可选择共享库，显式设置 `-DBUILD_SHARED_LIBS=OFF`
则选择静态归档。嵌入构建时，请在 `add_subdirectory(ptx_frontend)` 之前设置该变量。
两种模式应使用独立的 build 与 install prefix；每个安装包只包含所选择的模式。

七个编译组件（`base`、`lexer`、`cst`、`syntax`、`binding`、`semantic` 和
`resolved_ir`）遵循该设置。Linux 共享模式生成七个
`libptx_frontend_<component>.so` 库。`common` 和 `ptx_frontend` umbrella
继续是 interface target。两种模式使用相同的 target 名称、组件选择方式和传递依赖。

CMake 会为共享组件启用位置无关代码（PIC）。链接到共享组件的任何静态依赖，
包括静态 `fmt`，也必须使用 PIC 构建。本项目不会修改 imported dependency 的
构建设置。下游查找安装包仍需要提供 `fmt` 和 `magic_enum`。

安装后的 Linux 共享组件会在 install runtime search path 中追加 `$ORIGIN`，
使安装前缀迁移后仍能找到同目录的组件库。Consumer 必须使运行时 loader 能找到
安装的 library 目录；链接 imported CMake target 通常会为 build-tree 可执行文件
提供适当的 runtime path。部署时还须通过正常 loader 配置提供共享的第三方依赖。
现有 Linux Clang CI 继续使用默认静态模式。共享模式已在本地通过完整 Release
构建、C++ 测试及安装目录迁移后的独立 installed consumer 验证。

共享构建已在 Linux 验证。Windows 共享配置会明确报错，因为 DLL export/import
支持尚未审计；macOS 共享构建尚未验证。共享模式不新增稳定 ABI 或 SONAME 版本
保证。Frontend 更新后，应使用匹配的安装头文件、库和兼容的 C++ toolchain
重新构建 consumer。现有 `SameMinorVersion` CMake package 版本兼容规则保持不变。

## `ptx_spec`

consumer 可以通过以下方式请求通用 PTX ISA specification：

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec)
```

随后 package 会定义：

- `ptx_frontend_PTX_SPEC_DIR`：已安装的公共 PTX instruction YAML 目录；
- `ptx_frontend_PTX_SPEC_SCHEMA`：已安装的 `ptx-instr-v1.schema.yaml` 路径；
- `ptx_frontend_PTX_CPP_BACKEND_SPEC`：已安装的 C++ backend 映射 YAML 路径；
- `ptx_frontend_PTX_CPP_BACKEND_SCHEMA`：已安装的 backend schema 路径。

PTX specification 的 canonical source 位于 `python/src/ptx_frontend/spec/resources/ptx_spec`，同时也作为 Python package data 发布。CMake 的 `ptx_spec` component 将独立 raw data 安装至 `share/ptx_frontend/ptx_spec` 和 `share/ptx_frontend/ptx-instr-v1.schema.yaml`。`instructions/ptx_spec` 是源码构建使用的仓库路径；tracked `instructions` symlink 指向 Python resource directory，两者是同一份源码数据。源码生成递归追踪该目录中的 YAML，并与 Python loader 一样排除 `.schema.yaml` 文件。文件集合变化也会使生成失效，避免移除输入后遗留过时产物。

C++ backend 映射以 `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` 为源码输入，安装至 `share/ptx_frontend/ptx_cpp_backend_spec/ptx_frontend.yaml`；schema 安装至 `share/ptx_frontend/ptx-cpp-backend-v2.schema.yaml`。请求 `ptx_spec` 时会检查四项资源，导出路径相对于 package 的安装前缀计算，因此安装目录可整体迁移。

## Python model 复用

安装后的 CMake package 不再导出 codegen component，也不提供 `ptx_frontend_generate()` helper。代码生成属于 frontend 源码构建以及下游项目自有 generator 的实现细节。

需要复用规范化 PTX specification model 的 Python consumer，应当使用公共 `ptx_frontend.spec` namespace：

```python
from ptx_frontend.spec.database import load_packaged_spec_database
from ptx_frontend.spec.model import InstructionSpec

database = load_packaged_spec_database()
```

`ptx_frontend.spec` 是面向下游的 Python API，提供可复用的 instruction model、database loader、normalization helper 和 resource accessor，同时与 frontend 自身使用完全相同的底层 model 类型。consumer 应将 `ptx-instr/v1` schema 视为稳定的数据契约。

`ptx_frontend.code_gen` 继续作为 frontend 源码构建所需的实现 namespace。其会随 wheel 打包的 `cli`、`context`、`plan` 与 `emit` modules 构成确定性的 in-tree generator：冻结 context 只投影一次 backend alias，同一 plan 决定稳定的 listing 与 manifest 顺序。`--jobs` 大于一时，各产物的 emission 完成顺序可变；生成字节不依赖 worker 顺序。CLI 默认使用六个 worker，`--jobs 1` 保持串行生成。源码构建通过 `PTX_FRONTEND_CODEGEN_JOBS` CMake cache 变量设置单进程产物 writer 预算（默认 `6`）。一个聚合构建命令根据贡献文件及成员清单的时间戳选择变动的 category，再在同一 Python 进程中生成这些 category 和所需共享产物。单独的配置阶段描述来自一次归一化的规格快照，并将来源依赖分配给有贡献的 category。源码构建图位于 `cmake/ptx_resolved_ir_codegen.cmake`；`submod/resolved_ir/CMakeLists.txt` 使用其返回的生成源码和 include 路径。新的下游代码不应依赖这些实现 API。仓库专用的 corpus tools 位于 `tools/corpus`，不打入 wheel；wheel 也不安装 `ptx-frontend-codegen` console script。

## 测试 profile

Linux Clang 21 configure preset 为静态归档选择 `/usr/bin/llvm-ar-21` 和
`/usr/bin/llvm-ranlib-21`。共享 Linux CI setup 通过 `llvm-21` package 安装这些工具。
本地 preset 用户也需要该 package，或者在 CMake 命令行覆盖 `CMAKE_AR` 和
`CMAKE_RANLIB`。GCC preset 和不使用这些 preset 的配置继续采用 CMake 的 toolchain
选择，包括通过 `add_subdirectory` 嵌入的构建。

`BUILD_TESTING=ON` 会构建活跃的 C++ 测试套件，包括
`resolved_ir_smoke` 和 `test_resolved_ir`。
GCC 和 Clang 的 Debug/Release preset 都启用该选项。可在独立构建目录中通过
`examples/resolved_ir_consumer` 检查已安装的 `resolved_ir` 组件。

## 前提与测试边界

Python 安装要求 Python 3.14 或更高版本。loader 位于
`ptx_frontend.spec.database`，model type 位于 `ptx_frontend.spec.model`，
resource helper 位于 `ptx_frontend.spec.resources`；namespace 根不重新导出这些 API。
Linux Clang 配置除 preset 的 LLVM archive tool 外还要求 `ld.lld-21` 或 `ld.lld`；
CI 安装 `lld-21`。CTest preset 排除 `python_test`，installed C++ public API consumer
由 Clang Debug Linux CI 的独立 step 验证。Python unit lane 需单独运行。

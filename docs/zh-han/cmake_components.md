# 已安装的 CMake Components

安装后的 `ptx_frontend` CMake package 除 C++ target components 外，只公开一个非 target 数据 component：`ptx_spec`。

每个 C++ component 在 `find_package` 后和通过 `add_subdirectory` 嵌入源码树时都使用稳定的公开 target 名称 `ptx_frontend::<component>`。源码构建的实际 target 使用 `ptx_frontend_` 前缀，因此不会与 parent project 的通用 target 名称冲突；这些实现名称不是公共 API。

活跃的 Resolved IR 目标为 `ptx_frontend::resolved_ir`，
`ptx_frontend::ptx_frontend` 会链接它。接口使用直接语义形式类和
`std::unique_ptr<Instruction>` 所有权。

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

PTX specification 的 canonical source 位于 `python/src/ptx_frontend/spec/resources/ptx_spec`，同时也作为 Python package data 发布。CMake 的 `ptx_spec` component 将独立 raw data 安装至 `share/ptx_frontend/ptx_spec` 和 `share/ptx_frontend/ptx-instr-v1.schema.yaml`。`instructions/ptx_spec` 是源码构建使用的仓库输入目录。源码生成递归追踪该目录中的 YAML，并与 Python loader 一样排除 `.schema.yaml` 文件。文件集合变化也会使生成失效，避免移除输入后遗留过时产物。

C++ backend 映射以 `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` 为源码输入，安装至 `share/ptx_frontend/ptx_cpp_backend_spec/ptx_frontend.yaml`；schema 安装至 `share/ptx_frontend/ptx-cpp-backend-v2.schema.yaml`。请求 `ptx_spec` 时会检查四项资源，导出路径相对于 package 的安装前缀计算，因此安装目录可整体迁移。

## Python model 复用

安装后的 CMake package 不再导出 codegen component，也不提供 `ptx_frontend_generate()` helper。代码生成属于 frontend 源码构建以及下游项目自有 generator 的实现细节。

需要复用规范化 PTX specification model 的 Python consumer，应当使用公共 `ptx_frontend.spec` namespace：

```python
from ptx_frontend.spec import load_packaged_spec_database
from ptx_frontend.spec.model import InstructionSpec

database = load_packaged_spec_database()
```

`ptx_frontend.spec` 是面向下游的 Python API，提供可复用的 instruction model、database loader、normalization helper 和 resource accessor，同时与 frontend 自身使用完全相同的底层 model 类型。consumer 应将 `ptx-instr/v1` schema 视为稳定的数据契约。

`ptx_frontend.code_gen` 继续作为 frontend 源码构建所需的实现 namespace。其会随 wheel 打包的 `cli`、`context`、`plan` 与 `emit` modules 构成确定性的 in-tree generator：冻结 context 只投影一次 backend alias，同一 plan 决定稳定的 listing 与 manifest 顺序。`--jobs` 大于一时，各产物的 emission 与 formatting 完成顺序可变；生成字节不依赖 worker 顺序。CLI 默认使用六个 worker，`--jobs 1` 保持串行生成。源码构建通过 `PTX_FRONTEND_CODEGEN_JOBS` CMake cache 变量设置单进程产物 writer 预算（默认 `6`）。一个聚合构建命令根据贡献文件及成员清单的时间戳选择变动的 category，再在同一 Python 进程中生成这些 category 和所需共享产物。单独的配置阶段描述来自一次归一化的规格快照，并将来源依赖分配给有贡献的 category。源码构建图位于 `cmake/ptx_resolved_ir_codegen.cmake`；`submod/resolved_ir/CMakeLists.txt` 使用其返回的生成源码和 include 路径。新的下游代码不应依赖这些实现 API。仓库专用的 corpus tools 位于 `tools/corpus`，不打入 wheel；wheel 也不安装 `ptx-frontend-codegen` console script。

## 测试 profile

`BUILD_TESTING=ON` 会构建活跃的 C++ 测试套件，包括
`resolved_ir_smoke` 和 `test_resolved_ir`。
GCC 和 Clang 的 Debug/Release preset 都启用该选项。可在独立构建目录中通过
`examples/resolved_ir_consumer` 检查已安装的 `resolved_ir` 组件。

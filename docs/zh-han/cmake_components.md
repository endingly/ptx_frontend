# 已安装的 CMake Components

安装后的 `ptx_frontend` CMake package 除 C++ target components 外，只公开一个非 target 数据 component：`ptx_spec`。

## `ptx_spec`

consumer 可以通过以下方式请求通用 PTX ISA specification：

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec)
```

随后 package 会定义：

- `ptx_frontend_PTX_SPEC_DIR`：已安装的公共 PTX instruction YAML 目录；
- `ptx_frontend_PTX_SPEC_SCHEMA`：已安装的 `ptx-instr-v1.schema.yaml` 路径。

PTX specification 的 canonical source 位于 `python/code_gen/resources/ptx_spec`，同时也作为 Python package data 发布。CMake 的 `ptx_spec` component 将独立 raw data 安装至 `share/ptx_frontend/ptx_spec` 和 `share/ptx_frontend/ptx-instr-v1.schema.yaml`。`instructions/ptx_spec` 仅保留为源码树兼容 symlink。

仓库自身的 C++ backend policy 仍位于 `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml`；它不属于公共 `ptx_spec` component，也不会被导出。

## Python model 复用

安装后的 CMake package 不再导出 codegen component，也不提供 `ptx_frontend_generate()` helper。代码生成属于 frontend 源码构建以及下游项目自有 generator 的实现细节。

需要复用规范化 PTX specification model 或 packaged specification resources 的 Python consumer，可以单独安装 `ptx_frontend` wheel。当前 wheel 通过 `ptx_frontend.code_gen.model` 暴露 model；长期应将 `ptx-instr-v1` schema 视为稳定的数据契约。后续可以把可复用 specification model 收窄为类似 `ptx_frontend.spec` 的公共 namespace，而无需重新引入 CMake codegen component。

# 已安装的 CMake Components

安装后的 `ptx_frontend` 包除 C++ target components 外，还公开两个非 target component。

## `ptx_spec`

consumer 可以通过以下方式请求通用 PTX ISA specification：

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec)
```

随后 package 会定义：

- `ptx_frontend_PTX_SPEC_DIR`：已安装的公共 PTX instruction YAML 目录；
- `ptx_frontend_PTX_SPEC_SCHEMA`：已安装的 `ptx-instr-v1.schema.yaml` 路径。

PTX specification 的 canonical source 位于 `python/code_gen/resources/ptx_spec`，并作为 Python `code_gen` 的 package data 一起发布。CMake 的 `ptx_spec` component 直接暴露同一份已安装 package resources，不再维护第二份安装副本。`instructions/ptx_spec` 仅保留为源码树兼容 symlink。

仓库自身的 C++ backend policy 仍位于 `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml`；它不是 Python package resource，也不属于 `ptx_spec`，不会被导出。

## `codegen`

需要生成 C++ artifact 的 consumer 可以请求：

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec codegen)
```

`codegen` component 安装 `gen_all.py` 所需的 Python dependency closure，其中包含 PTX specification 与 validation schema，并通过 package config 提供 `ptx_frontend_generate()`。consumer 必须显式指定自己的 backend specification：

```cmake
ptx_frontend_generate(
    TARGET generate_my_backend
    SPEC_DIR "${ptx_frontend_PTX_SPEC_DIR}"
    BACKEND_SPEC "${CMAKE_CURRENT_SOURCE_DIR}/my_backend.yaml"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
add_dependencies(my_target generate_my_backend)
```

`TARGET` 为必填项，因此一个项目可生成多个 backend 或 output directory。helper 会声明
生成文件，但该 target 默认不会构建；请用 `add_dependencies()` 将其连接到 consumer target。

安装后的 codegen 不会回退到 `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml`，也不要求原始仓库仍然存在。当前 Python import package 暂时保持 `base`、`code_gen` 与 `ir`；迁移到 `ptx_frontend` namespace 的工作单独跟踪。

CMake package 会在设置 `ptx_frontend_ptx_spec_FOUND` 或 `ptx_frontend_codegen_FOUND` 前检查实际安装资源，因此 component 不会因为名称位于白名单中就产生假阳性的 FOUND 状态。

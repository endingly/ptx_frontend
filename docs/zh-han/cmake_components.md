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

仓库自身的 C++ backend policy 不属于 `ptx_spec`，不会随该 component 导出。

## `codegen`

需要生成 C++ artifact 的 consumer 可以请求：

```cmake
find_package(ptx_frontend CONFIG REQUIRED COMPONENTS ptx_spec codegen)
```

`codegen` component 安装 `gen_all.py` 所需的 Python dependency closure 及 validation schema，并通过 package config 提供 `ptx_frontend_generate()`。consumer 必须显式指定自己的 backend specification：

```cmake
ptx_frontend_generate(
    SPEC_DIR "${ptx_frontend_PTX_SPEC_DIR}"
    BACKEND_SPEC "${CMAKE_CURRENT_SOURCE_DIR}/my_backend.yaml"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
```

安装后的 codegen 不会回退到 `instructions/ptx_cpp_backend_spec/ptx_frontend.yaml`，也不要求原始仓库仍然存在。当前 Python import package 暂时保持 `base`、`code_gen` 与 `ir`；迁移到 `ptx_frontend` namespace 的工作单独跟踪。

CMake package 会在设置 `ptx_frontend_ptx_spec_FOUND` 或 `ptx_frontend_codegen_FOUND` 前检查实际安装资源，因此 component 不会因为名称位于白名单中就产生假阳性的 FOUND 状态。

# PTX 9.3 WGMMA 覆盖范围

Frontend 将五种 WGMMA 拼写建模为 2,151 个精确逻辑形式：1,092 个 dense `wgmma.mma_async`、1,056 个 sparse `wgmma.mma_async.sp`，以及 `wgmma.fence`、`wgmma.commit_group`、`wgmma.wait_group` 各一个。规范数据来自安装包中的 `asynchronous_warpgroup_matrix_multiply_accumulate.yaml`。全部形式都要求 `sm_90a` target family。Dense MMA 与控制指令从 PTX 8.0 开始，sparse MMA 从 PTX 8.2 开始，混合 `.s8.u8` / `.u8.s8` 从 PTX 8.4 开始。普通 `sm_90` 不满足 `sm_90a` 要求。

Dense 形式覆盖 `.f16`、`.bf16`、`.tf32`、FP8 `.e4m3`/`.e5m2`、可带 `.satfinite` 的 `.s8`/`.u8` 整数形式，以及 `.b1.and.popc`。Sparse 形式覆盖对应浮点与整数类型，使用一个 `.b32` metadata 寄存器和按类型限制的 selector：`.f16`/`.bf16` 与 `.tf32` 可取 0 或 1，FP8 与整数只能取 0。浮点 A/B immediate scale 必须分别为 -1 或 1；`.f16`/`.bf16` descriptor 形式还暴露合法 transpose control。A 来源可以是不透明的 64 位 shared-memory descriptor，也可以是四寄存器 fragment；B 始终是 shared-memory descriptor。D 寄存器 fragment 可读写。其数量由 shape 和 accumulator 类型决定，N=256 时 packed `.f16` 为 64 个，`.f32`/`.s32` 为 128 个。普通 vector 与 selector operand 的限额独立保留。

`MatrixInstructionDescriptor` 保留 M/N/K、element 与 register 类型、row/column placement、精确 fragment 数量、A 来源位置、sparse metadata kind 和 `WarpGroup128` participation identity。`ResolvedSharedMatrixDescriptor` 只保留绑定的 `.b64` 寄存器；运行时地址、对齐、swizzle、布局和跨 warp 一致性由生产者与使用者保证。每个 WGMMA 语义形式都是带直接类型化操作数和不可变 `matrix_topology` 的 final `Instruction` 类。MMA 类通过 `matrix_descriptor()` 返回该拓扑；protocol-control 类没有矩阵描述符。`Instruction::check()` 核对可变操作数、选中布局和目标可用性。独立的 `WgmmaGroup` completion identity 与 issue/register-fence/commit/wait action 暴露 protocol obligation；frontend 不证明动态 warpgroup 收敛、指令序列顺序、寄存器读取前完成或 GPU 数值结果。

`wgmma.wait_group` 接受非负整数常量，owned immediate 使用 `.u64`。PTX 9.3 没有给出数值上限，因此 frontend 不加入未记载的 0..7 或 32 位限制。源码符号和转换后位值都保留供 checker 核对。CUDA 13.3 `ptxas` 接受 wait count 为 2^32 的完整 module，并拒绝 `-1`；这只是抽样 assembler 证据，不构成新的 ISA 上限。

归档 PTX 9.3 的 dense integer syntax 列表省略 N=240 和 N=256，但 WGMMA shape 表与 integer fragment 表包含两者。Frontend 纳入这两个尺寸，CUDA 13.3 `ptxas` 也能汇编对应代表性整数形式。此处遵循 shape 与 fragment 契约，并明确记录 syntax 列表的差异。

代码生成保留全部 2,151 个精确类 identity，并划分为有界的 64-form shard。Public catalogue getter 返回由静态存储支持的 span，不依赖 source AST 生命周期。[安装包 matrix consumer](../../examples/matrix_consumer/README.md) 测试跨 shard 边界的 WGMMA 形式以及控制/MMA descriptor 的区别。

分别运行 C++ 和 Python 检查；已配置的 CTest preset 不包含 `python_test`：

```sh
PYTHONPATH="$PWD/python/src" ctest --preset ci-linux-clang-debug --parallel 1 --output-on-failure
PYTHONPATH="$PWD/python/src" ctest --test-dir out/build/ci-linux-clang-debug -C Debug -R '^python_test$' --parallel 1 --output-on-failure
```

依据：[NVIDIA PTX ISA 9.3 的 asynchronous warpgroup-level matrix instructions](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-instructions)。

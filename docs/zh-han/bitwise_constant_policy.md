# 位运算常量表达式的符号性

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#bitwise-constant-policy) 为唯一维护依据；本文保留技术参考与历史证据。

二元 `&`、`|`、`^` 使用通常算术转换：两个输入均有符号时，结果有符号；
否则结果无符号。一元 `~` 仍然产生无符号结果。该策略影响后续比较和右移，
但不改变位运算本身产生的 64 位数据。

## 规范分歧与选择

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#bitwise-constant-policy--specification-conflict-and-decision)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/bitwise_constant_policy.md#specification-conflict-and-decision)。

## 求值与使用位置转换

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#bitwise-constant-policy--evaluation-and-use-conversion)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/bitwise_constant_policy.md#evaluation-and-use-conversion)。

## 复现

在仓库根目录执行，输出放在源码目录之外：

```sh
ptxas --version
ptxas -arch=sm_80 submod/semantic/test/fixtures/bitwise_signedness.ptx -o /tmp/bitwise_signedness.cubin
readelf -x .nv.global.init /tmp/bitwise_signedness.cubin
```

2026-09-12 的观察环境为 Linux、NVIDIA CUDA 13.1、`ptxas V13.1.115`，
源 PTX 版本 9.1，目标 `sm_80`，无额外选项：

| 夹具表达式 | 按小端字节解码后的 64 位值 |
| --- | --- |
| `((-1 & -1) < 0)` | `0000000000000001` |
| `((-1 \| 0) < 0)` | `0000000000000001` |
| `((-1 ^ 0) < 0)` | `0000000000000001` |
| `((-4 \| 0) >> 1)` | `fffffffffffffffe` |
| `(((.s64)(-4 \| 0)) >> 1)` | `fffffffffffffffe` |
| `((-1 & -1U) < 0)` | `0000000000000000` |
| `((~0) < 0)` | `0000000000000000` |
| `((-1 + 0) < 0)` | `0000000000000001` |

完整夹具将八个结果保存在一个可见全局数组中。C++ 回归还检查最终存储转换
之前的符号性，覆盖每个二元位运算的混合符号情况。

这些结果来自目标文件字节，不是 GPU 执行或回读。已安装汇编器不支持 PTX 9.3，
此观察不声称所有工具链版本均等价，也不对 `.b128` 高位扩展或物理内存行为作出结论。

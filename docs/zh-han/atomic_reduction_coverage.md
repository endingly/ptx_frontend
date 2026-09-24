# PTX 9.3 原子操作与归约覆盖范围

前端依据 PTX ISA 9.3 §9.7.14.5–6 建模 `atom` 和 `red` 的同步标量 global-memory
子集。前端解析、解析为 owned IR，并检查指令形状、操作数、已知地址来源、对齐和目标
可用性；不执行原子操作。

| 指令 | 操作与类型 | 操作数 | 旧式形式最低要求 | 显式 `.relaxed.cta` 最低要求 |
| --- | --- | --- | --- | --- |
| `atom.global` | `.add`、`.min`、`.max` × `.u32`、`.s32` | `dst, [address], src` | PTX 1.1 / SM 11 | PTX 6.0 / SM 70 |
| `atom.global` | `.inc`、`.dec` × `.u32` | `dst, [address], src` | PTX 1.1 / SM 11 | PTX 6.0 / SM 70 |
| `atom.global` | `.and`、`.or`、`.xor`、`.exch` × `.b32` | `dst, [address], src` | PTX 1.1 / SM 11 | PTX 6.0 / SM 70 |
| `atom.global` | `.cas.b32` | `dst, [address], compare, swap` | PTX 1.1 / SM 11 | PTX 6.0 / SM 70 |
| `red.global` | `.add`、`.min`、`.max` × `.u32`、`.s32` | `[address], src` | PTX 1.2 / SM 11 | PTX 6.0 / SM 70 |
| `red.global` | `.inc`、`.dec` × `.u32` | `[address], src` | PTX 1.2 / SM 11 | PTX 6.0 / SM 70 |
| `red.global` | `.and`、`.or`、`.xor` × `.b32` | `[address], src` | PTX 1.2 / SM 11 | PTX 6.0 / SM 70 |
| `atom.global` | `.add.u64`、`.exch.b64`、`.cas.b64` | `dst, [address], src`（`cas` 为 `dst, [address], compare, swap`） | PTX 1.2 / SM 12 | PTX 6.0 / SM 70 |
| `atom.global` | `.min`、`.max` × `.u64`、`.s64`；`.and`、`.or`、`.xor` × `.b64` | `dst, [address], src` | PTX 3.1 / SM 32 | PTX 6.0 / SM 70 |
| `red.global` | `.add.u64` | `[address], src` | PTX 1.2 / SM 12 | PTX 6.0 / SM 70 |
| `red.global` | `.min`、`.max` × `.u64`、`.s64`；`.and`、`.or`、`.xor` × `.b64` | `[address], src` | PTX 3.1 / SM 32 | PTX 6.0 / SM 70 |

旧式形式省略 memory semantics 和 scope 后缀；ISA 的实际默认值分别为 relaxed
semantics 和 GPU scope。显式形式要求 `.relaxed.cta`；
`atom.relaxed.cta.global` 与 `atom.global.relaxed.cta` 两种顺序均可解析，
`red` 的相应形式亦然。不同的 resolved variant 保留源码中是否写出了限定符。
目标操作数必须是兼容指令 32 位或 64 位类型的寄存器；每个值来源可为兼容的
寄存器或使用普通窄化转换的整数立即数。对于来源已知的地址，要求其为 global，
并按类型满足四字节或八字节对齐。

当前边界要求显式 `.global`。省略 state space 表示 generic 寻址，不属于该子集。
其他操作与类型组合，包括 64 位 `.add.s64`、`.inc/.dec`、带 `.u64/.s64`
后缀的位操作，以及 `red.cas`、`red.exch`，尚未支持。其他 state space、
memory order/scope、cache policy、向量形式、bit-bucket、`red.async` 和
`multimem.red.async` 也不在当前子集内。
`.inc` 与 `.dec` 的源操作数提供运行时界限；前端保留其类型化操作数，不模拟更新。

C++ 包版本为 0.5.0。新增的 64 位命名分支扩展了公开的 `Atom::Variant` 与
`Red::Variant`，改变了源码和二进制 API；consumer 需使用匹配的已安装头文件和库
重新构建。先前 0.3.0 的改动使公开成员
`Atom::GlobalRelaxedCtaAddU32::src` 和 `Red::GlobalRelaxedCtaAddU32::src`
现在持有 `RegOrImm`。读取寄存器时使用
`std::get<ResolvedRegisterRef>(value.src.value)`；读取立即数时使用
`std::get<ResolvedImmediate>(value.src.value)`；新增的一源分支使用相同的操作数
表示。Python wheel 独立保持 0.1.0b1。

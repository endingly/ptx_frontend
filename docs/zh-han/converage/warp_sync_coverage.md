# PTX 9.3 shuffle 与 vote 覆盖范围

前端建模 PTX ISA 9.3 §9.7.9.6 和 §9.7.14.10 文档中的 `shfl.sync`、
`vote.sync` 及下表旧式 shuffle 和 vote 形式。它解析并保留选定操作、操作数布局及源谓词取反，再检查局部
操作数类型和目标可用性。前端不执行 warp 操作，也不证明运行时成员参与或收敛。

| 指令 | 支持形式 | 目标操作数 | 源操作数与掩码 | 可用性 |
| --- | --- | --- | --- | --- |
| `shfl.sync` | `.up.b32`、`.down.b32`、`.bfly.b32`、`.idx.b32` | 32 位兼容寄存器 `d`，或与 `.pred` 寄存器组成 `d|p` | 32 位兼容寄存器 `a`；寄存器或立即数形式的 `.u32` 控制量 `b`、`c` 和 `membermask` | PTX 6.0 / SM 30 |
| `shfl` | `.up.b32`、`.down.b32`、`.bfly.b32`、`.idx.b32` | 32 位兼容寄存器 `d`，或与 `.pred` 寄存器组成 `d|p` | 32 位兼容寄存器 `a`；寄存器或立即数形式的 `.u32` 控制量 `b` 和 `c`；没有 membermask | PTX 3.0 / 精确 generic `sm_30`、`sm_32`、`sm_35`、`sm_37`、`sm_50`、`sm_52`、`sm_53`、`sm_60`、`sm_61`、`sm_62` |
| `vote.sync` | `.all.pred`、`.any.pred`、`.uni.pred` | `.pred` 寄存器 `d` | `.pred` 寄存器 `a` 或 `!a`；寄存器或立即数形式的 `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote.sync` | `.ballot.b32` | 32 位兼容寄存器 `d` | `.pred` 寄存器 `a` 或 `!a`；寄存器或立即数形式的 `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote` | `.all.pred`、`.any.pred`、`.uni.pred` | `.pred` 寄存器 `d` | `.pred` 寄存器 `a` 或 `!a`；没有 membermask | PTX 1.2 / 精确 generic `sm_12`、`sm_13`、`sm_20`、`sm_30`、`sm_32`、`sm_35`、`sm_37`、`sm_50`、`sm_52`、`sm_53`、`sm_60`、`sm_61`、`sm_62` |
| `vote` | `.ballot.b32` | 32 位兼容寄存器 `d` | `.pred` 寄存器 `a` 或 `!a`；没有 membermask | PTX 1.2 / 精确 generic `sm_20`、`sm_30`、`sm_32`、`sm_35`、`sm_37`、`sm_50`、`sm_52`、`sm_53`、`sm_60`、`sm_61`、`sm_62` |

shuffle 的谓词输出可省略。成对输出必须包含两个寄存器；任一位置均不接受
`_`，且输出谓词不能取反。所有受支持的 vote 模式都允许源谓词取反，而目标
必须是与该形式类型一致的寄存器。同步形式必须提供成员掩码；旧式 shuffle 和 vote 形式拒绝额外掩码，
且没有 `.sync`。独立公共 shuffle form `ShflUpB32`、`ShflDownB32`、`ShflBflyB32` 与
`ShflIdxB32` 保留选定的单独或成对目标、数据源、b/c 控制量及 owned source range。
其 optional `dst_register` 和 `dst_shfl_destination` field 保留源码所写目标布局；
成对数据与谓词结果复用已有 typed destination 路径，不构造 sync 或 membermask field。
上表精确 generic catalog target 是旧式 SM 30–69 区间中受支持的部分，
`sm_63` 等未收录 profile 仍不受支持。旧式 shuffle 拒绝 `sm_20`、`sm_70` 及更高
target，并要求 PTX 3.0。四个独立公共 vote form `VoteAllPred`、`VoteAnyPred`、`VoteUniPred` 与
`VoteBallotB32` 保留谓词取反与 owned source range，不构造 sync 或 mask field。
availability 仅限上表列出的精确 supported catalog profile，不代表所有历史 GPU profile。
它们拒绝 `sm_70` 及更高 target，也不覆盖历史 PTX < 6.4 / `sm_70` 情形。

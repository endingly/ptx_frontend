# SLCT 覆盖情况

Frontend 建模 [PTX ISA 9.3 §9.7.6.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-slct) 的全部普通 `slct` form。Canonical descriptor 位于 `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`。Parsing、owned resolution 与 checker 保留 type 和 operand contract；frontend 不执行选择运算。

`slct{.ftz}.dtype.stype d, a, b, c` 在 `c` 非负时选择 `a`，否则选择 `b`。被选中的 data 按原位复制，不作转换。浮点 selector 的负零等于零，因此选择 `a`；NaN selector 选择 `b`。`.ftz` 只 flush `c` 的 subnormal value，且仅在 `.stype` 为 `.f32` 时可用。

| 字段 | 支持的值与 operand form |
| --- | --- |
| `.dtype` | `.b16/.b32/.b64`、`.u16/.u32/.u64`、`.s16/.s32/.s64`、`.f32/.f64` |
| `.stype` | `.s32` 或 `.f32` |
| `d` | 与 `.dtype` 兼容的 register |
| `a`、`b` | 与 `.dtype` 兼容的 register 或数值 immediate |
| `c` | 与 `.stype` 兼容的 register 或数值 immediate |

四个 operand 均必需。Register 兼容性以 `.dtype` 的名义 type 为准：`.b32` data 可使用同宽的浮点或整数 register，但 `.u32` data 拒绝 `.f32` register，`.f32` data 拒绝 `.u32` register。Selector 独立采用 `.s32` 或 `.f32` type，并应用同宽 register policy。两个 generated public variant 为 `Slct::S32` 与 `Slct::F32`；后者保留 typed 可选 `ftz` 字段。全部 form 最低需要 PTX 1.0。无论 selector type 如何，`.f64` selected data 另需 `sm_13` 或更高。

[C++ 测试](../../submod/resolved_ir/test/test_slct_completeness.cpp) 覆盖全部 22 个 type/selector 组合、literal、声明 register container、非法 modifier/immediate、target 边界、owned lifetime 及变异后的 public IR。[Python spec 测试](../../python/tests/spec/test_slct_completeness.py) 验证完整 type domain、operand descriptor 与 `.f64` value gate。安装后的 [consumer](../../examples/conversion_consumer/main.cpp) 使用两个 public variant，并重新验证 owned instruction。

在 0.2.0 C++ package 中，原固定的 `Slct::U32S32` 与 `Slct::FtzU64F32` alternative 替换为 `Slct::S32` 与 `Slct::F32`。Consumer 应读取 `dtype.value` 区分 selected data type，并读取 `Slct::F32` 的 `ftz.value`。`src_true`、`src_false` 和 `selector` 现在保存 `RegOrImm`；根据情况使用 `std::get_if<ResolvedRegisterRef>` 或 `std::get_if<ResolvedImmediate>`。这改变了 public source 与 binary contract；installed consumer 需使用匹配的 header 和 library 重新构建。

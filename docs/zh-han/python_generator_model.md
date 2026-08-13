# Python 生成器模型设计

## 目的

Python 层是 YAML 与生成 C++ 之间的唯一建模层。它不应直接拼接 YAML 字典，也不应
复刻 C++ 的存储细节。其职责是将声明式 PTX 事实规范化为不可变 dataclass，再从同一
模型派生 Syntax、Resolved 与 checker 的生成产物。

```text
YAML files
  -> CodegenDatabase / normalized InstructionSpec
  -> SyntaxInstructionDescriptor + ResolvedInstruction
  -> generated C++ header and descriptor sources
```

## 输入数据库

`code_gen.database` 递归发现 `instructions/ptx_spec/**/*.yaml`，按路径排序加载，并
保证所有文件使用相同 schema 版本及 opcode 全局唯一。`InstructionSpec` 的最小稳定
模型位于 `code_gen.model`：

```python
InstructionSpec(opcode, syntax, variants)
VariantSpec(name, availability, modifiers, operand_layouts, rule)
OperandLayoutSpec(name, operands)
ModifierSpec(name, kind, presence, values, value, token, default)
OperandSpec(name, kind, role, access, type_expression)
```

该模型只保存生成当前 frontend 所需的字段。YAML 中的文档、example、constraint 等
尚未被 generator 使用的元数据，不应悄悄混入 C++ 表示。

## Normalization

`code_gen.normalize` 负责将 schema 合法但书写方式不同的 YAML 收敛为一个模型：

- 展开 type-set 的 `$name` 引用；
- 将 operand 的 `type: {expr: modifier(type)}` 解析为
  `OperandTypeExpression(MODIFIER, modifier_name="type")`；固定 scalar（如
  `u32`）则解析为 `FIXED_SCALAR`；
- 展开命名 `operand_patterns`；
- 将旧式 `operands` 升格为唯一的 `OperandLayoutSpec("default", ...)`；
- 拒绝一个 variant 同时出现 `operands` 与 `operand_layouts`；
- 拒绝同一 variant 内重复的 layout name。

因此后续代码只消费 `variant.operand_layouts`，不再维持两套 operand layout 判定
逻辑。normalizer 是兼容输入的边界，生成器本身不承担兼容分支。

## Syntax 模型

`ir.syntax_ast` 从 `InstructionSpec` 构建源语法 descriptor 模型：

```python
SyntaxInstructionDescriptor(opcode, variants)
SyntaxVariantDescriptor(variant_id, modifiers, operand_layouts)
SyntaxModifierDescriptor(kind_id, presence, allowed_spellings)
SyntaxOperandLayoutDescriptor(layout_id, kind, slots)
```

它只回答源码是否能写成该 variant/layout：modifier spelling、presence、AST operand
shape 与 slot 数量。当前 `OperandLayoutKind.FLAT` 和 `reg`、`imm`、`reg_or_imm`、
`pred`、`pred_or_not` shape 映射已实现；后者保留 `!%pN` 的取反语法。新的 AST shape
必须先扩展本模型和 C++ 基础 ABI。

## Resolved 模型

`ir.resolved_ir` 把相同的 `InstructionSpec` 映射为 C++ resolved field 设计：

```python
ResolvedInstruction(opcode, cpp_name, variants)
ResolvedVariant(variant_id, modifier_fields, modifier_bindings,
                operand_layouts, availability, rule)
ResolvedOperandLayout(layout_id, cpp_name, fields, bindings)
ResolvedField(name, value_cpp_type, origin, storage, ...)
ResolvedModifierBinding(source_kind_id, target_field_id, default_value)
ResolvedOperandBinding(target_field_id, type_expression, role, access, ...)
```

字段 origin 区分 `MODIFIER` 与 `OPERAND`；storage 区分 per-instance 的 `WithLocs<T>`
和 fixed modifier 的 `STATIC_CONSTANT`。`ResolvedOperandBinding` 是 C++ checker 与
resolver 共用的语义契约，保存目标 field、结构化类型表达式、role、access 和允许 shape。
其 descriptor 只有 `None`、`FixedScalar(ScalarType)` 与 `ModifierField(field_id)`；
因此两个 C++ 消费者都不再解析 YAML 表达式字符串。

optional modifier 的 YAML `default` 会在模型转换时成为 typed
`ResolvedModifierBinding.default_value`，并进入 resolved descriptor。公共 resolver
据此构造 `WithLocs<bool>` 或 `WithLocs<ScalarType>`；省略时 `locs` 为空，显式书写时
使用源码值与源码位置。Syntax descriptor 只描述 spelling/presence，不复制语义 default。

同一 variant 的多个 layout 可复用同名 field，前提是其定义完全一致；否则模型构建应
失败，而不是让生成结果含糊。

## C++ emitter 与产物

`python/scripts/gen_all.py` 协调生成，输出固定为：

| 输出 | emitter | 内容 |
| --- | --- | --- |
| `public/resolved_ir.gen.hpp` | `gen_resolved_ir.py` | opcode structs、`resolve<T>`、`check<T>` 特化 |
| `private/syntax_descriptor.gen.cpp` | `gen_syntax_ast_arch.py` | source syntax descriptors 与 getter |
| `private/resolved_descriptor.gen.cpp` | `gen_resolved_descriptor.py` | resolved field/binding descriptors 与 getter |
| `private/resolved_ir_checker_descriptor.gen.cpp` | `gen_resolved_checker_descriptor.py` | availability/rule descriptors 与 getter |

生成的公开头在构建树中仍平铺于 `public` include root；CMake 会将这个特定
文件安装为 `include/ptx_ir/resolved/resolved_ir.gen.hpp`。

每个输出文件只打开一次外层 namespace。private descriptor storage 位于单一匿名或
`generated_detail` namespace，getter 位于 `ptx_frontend::resolved_ir`；所有 generated
checker specialization 位于单一 `checker` namespace。

## 生成规则

- YAML identifier 经统一转换得到 deterministic PascalCase C++ 名称；碰撞必须报错。
- 一个 layout 时，operand 字段直接放在 variant 中；多个 layout 时，生成嵌套
  `*Operands` structs 与 `std::variant` payload。
- `ResolvedOperandLayoutTag` 始终与 syntax/resolved descriptor 中的 layout 索引对应。
- 生成器只决定必要的 C++ 语法，不能把 `direct`、`sub_variant` 等旧 backend 选项
  重新暴露为模型字段。
- 类型/role/access/shape 尚未得到 C++ 支持时，应在 Python 模型构建时抛出
  `ValueError`，而非生成半正确代码。

## 测试与变更方式

`python/tests/ir` 直接测试 YAML -> normalized model -> descriptor/emitted source 的
结构。每次新增模型字段应同时测试：normalization、对应 IR model、生成文本中应有的
ABI 片段。C++ 测试则验证真实 parser、resolver 与 checker 闭环。

推荐顺序：先扩展 schema 与 normalized dataclass，再扩展 Syntax/Resolved model，最后
修改 emitter 与测试。不要让 emitter 从原始 YAML 读取新字段，这会绕过一致性检查。

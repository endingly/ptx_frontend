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

`ptx_frontend.code_gen.database` 递归发现 canonical 的
`python/code_gen/resources/ptx_spec/**/*.yaml`（源码树可通过兼容 symlink
`instructions/ptx_spec` 访问），按路径排序加载，并
保证所有文件使用相同 schema 版本；同 opcode 的定义随后合并。`InstructionSpec` 的最小稳定
模型位于 `ptx_frontend.code_gen.model`：

```python
InstructionSpec(opcode, variants, syntax_forms, source_categories,
                codegen_category)
VariantSpec(name, availability, modifiers, operand_layouts, rule)
OperandLayoutSpec(name, operands)
ModifierSpec(name, kind, presence, values, value, token, default)
OperandSpec(name, kind, role, access, type_expression)
```

该模型只保存生成当前 frontend 所需的字段。YAML 中的文档、example、constraint 等
尚未被 generator 使用的元数据，不应悄悄混入 C++ 表示。

database 在合并 opcode 后验证 selector 语言：每个 variant 内，活动 modifier slot 的
spelling 集合必须两两不交；不同 variant 接受的无序 spelling 集合必须互斥。slot name
只在 variant 内有意义，所以同一 spelling 可以跨 variant 绑定不同 slot。校验通过后，
C++ matcher 才能对每个候选 variant 做确定性的局部绑定，同时保持 modifier 顺序无关。

## Normalization

`ptx_frontend.code_gen.normalize` 负责将 schema 合法但书写方式不同的 YAML 收敛为一个模型：

- 统一展开 `type_sets` 与 `value_sets` 的 `$name` 引用，并拒绝两者同名；
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

`ptx_frontend.ir.syntax_ast` 从 `InstructionSpec` 构建源语法 descriptor 模型：

```python
SyntaxInstructionDescriptor(opcode, variants)
SyntaxVariantDescriptor(variant_id, modifiers, operand_layouts)
SyntaxModifierDescriptor(kind_id, presence, allowed_spellings)
SyntaxOperandLayoutDescriptor(layout_id, kind, slots)
```

它只回答源码是否能写成该 variant/layout：variant-local modifier slot 的 spelling、presence、AST operand
shape 与 slot 数量。当前 `OperandLayoutKind.FLAT` 和 `reg`、`imm`、`reg_or_imm`、
`pred`、`pred_or_not` shape 映射已实现；后者保留 `!%pN` 的取反语法。新的 AST shape
必须先扩展本模型和 C++ 基础 ABI。

## Resolved 模型

`ptx_frontend.ir.resolved_ir` 把相同的 `InstructionSpec` 映射为 C++ resolved field 设计：

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
据此构造 `WithLocs<bool>`、`WithLocs<ScalarType>` 或
`WithLocs<RoundingMode>`；省略时 `locs` 为空，显式书写时
使用源码值与源码位置。Syntax descriptor 只描述 spelling/presence，不复制语义 default。

同一 variant 的多个 layout 可复用同名 field，前提是其定义完全一致；否则模型构建应
失败，而不是让生成结果含糊。

## C++ emitter 与产物

`python/scripts/gen_all.py` 原子生成 Resolved IR 阶段所需的公共声明、运行期映射、
dispatch、按 category 分片的实现以及 descriptor：

| 输出 | emitter | 内容 |
| --- | --- | --- |
| `public/resolved_ir.gen.hpp` | `gen_resolved_ir.py` | 全部 opcode structs，以及 `resolve<T>`、`check<T>` 的显式特化声明 |
| `private/resolved_value_domains.gen.hpp` | `gen_resolved_value_domains.py` | resolver 使用的运行期 value-domain lookup table |
| `private/resolved_ir_dispatch.gen.cpp` | `gen_resolved_ir.py` | opcode-independent resolve/check dispatch |
| `private/resolved_ir_<category>.gen.cpp` | `gen_resolved_ir.py` | 该 category 下两组显式特化的 out-of-line 定义 |
| `private/syntax_descriptor.gen.cpp` | `gen_syntax_ast_arch.py` | source syntax descriptors 与 getter |
| `private/resolved_descriptor.gen.cpp` | `gen_resolved_descriptor.py` | resolved field/binding descriptors 与 getter |
| `private/resolved_ir_checker_descriptor.gen.cpp` | `gen_resolved_checker_descriptor.py` | availability/rule descriptors 与 getter |

生成的公开头在 `submod/resolved_ir` 的构建树中仍平铺于 `generated/public` include
root。`submod/resolved_ir` include 工程级的 `cmake/generate_ptx_frontend.cmake`；
该 helper 原子调用 `gen_all.py`，负责列出输出、生成文件并将其编译进 `resolved_ir`
target。顶层只提供 submodule 编排与 facade target。

`syntax_descriptor.gen.cpp` 虽描述 source syntax，但它实现的是 generated Resolved IR
opcode 类型的 getter，并由 variant selection/resolution 消费；在生成器依赖边界改变前，
它仍与其他 `gen_all.py` 输出一起归属 `resolved_ir`，不按文件名拆入 `syntax` submodule。

公共头不包含生成函数体。生成分片使用归一化后的 `codegen_category`；它与记录 PTX
文档归属的 `source_categories` 分离。同 opcode 的全部 YAML 定义必须使用同一
`codegen_category`，生成脚本据此产生稳定的 category 源文件，
并由 CMake 编译进 `resolved_ir` library。这样 consumer 仍只有一个 include 入口，
但复杂的 `std::visit`、lambda、resolve builder 只在库内编译一次。

每个输出文件只打开一次外层 namespace。private descriptor storage 位于单一匿名或
`generated_detail` namespace，getter 位于 `ptx_frontend::resolved_ir`；checker
specialization 声明位于公共头的单一 `checker` namespace，每个 category 实现文件也只
打开一次对应 namespace。

所有 emitter 从规范化后的 C++ backend domain 获取语义值对应的 C++ 类型与表达式。
生成文件不会默认嵌入 wall-clock 时间；若构建环境提供
标准 `SOURCE_DATE_EPOCH`，生成警告会使用该确定性 UTC 时间，否则明确标记时间已省略。
因此相同 ISA spec、backend spec 和生成器输入会产生 byte-identical 内容。

### Backend 配置边界

`instructions/ptx_cpp_backend_spec/ptx_frontend.yaml` 及其
`instructions/schemas/ptx-cpp-backend-v1.schema.yaml` 作为独立的 C++ backend
配置层。`ptx_frontend.code_gen.cpp_backend` 将 `domains` 规范化为 `DomainBackend`，Syntax、Resolved、
checker emitter 只通过 typed lookup 读取 C++ 拼写。查询接口的 domain 参数必须使用
`CppDomain` 枚举成员，例如 `CppDomain.SCALAR_TYPES`，不接受裸字符串。当前 domain
覆盖 scalar type、
rounding mode、resolved value type/kind、modifier presence、operand role/access/shape、
type-expression kind 与 checker modifier kind。

backend spec 不应重复表达 `ptx_spec` 中的 PTX ISA 语义，也不应影响
`InstructionSpec` 的规范化结果。`DomainBackend` 与 `CodegenUnit` 已进入当前生成路径；
`InstructionBackend` 与 `EmitBackend` 仍为未来的 per-instruction override 保留，当前
`instructions` mapping 为空，也不能改变 resolved IR 的 variant/layout 结构。emitter
不得直接读取原始 YAML 字典。loader 会先执行 JSON Schema 校验，再检查当前生成路径所需
domain 是否齐全；缺失 domain/value 必须在生成期报告 `ValueError`。CMake 将 backend
YAML 与 schema 都列为生成依赖，修改任何 C++ 映射都会触发重新生成。

需要在运行期从 PTX 源码 suffix 解析值的 domain 声明
`runtime_lookup: ptx_suffix`。生成器会把对应映射生成到 private 的
`resolved_value_domains.gen.hpp`，并以 `inline constexpr std::array` 保存。
手写 resolver 只保留一份通用 suffix 查找算法，不再重复 scalar type 或 rounding mode
的映射数据；未标记的 domain 仍仅用于生成期，不会产生运行期查找表。

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

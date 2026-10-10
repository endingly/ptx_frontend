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

`ptx_frontend.spec.database` 递归发现 canonical 的
`python/src/ptx_frontend/spec/resources/ptx_spec/**/*.yaml`，按路径排序加载，并
保证所有文件使用相同 schema 版本；同 opcode 的定义随后合并。`InstructionSpec` 的最小稳定
模型位于 `ptx_frontend.spec.model`：

```python
InstructionSpec(opcode, variants, syntax_forms, source_categories,
                codegen_category)
VariantSpec(name, availability, modifiers, operand_layouts, rule, ..., modifier_order_aliases)
OperandLayoutSpec(name, operands)
ModifierSpec(name, kind, presence, values, value, token, default)
OperandSpec(name, kind, role, access, type_expression)
```

该模型只保存生成当前 frontend 所需的字段。YAML 中的文档、example、constraint 等
尚未被 generator 使用的元数据，不应悄悄混入 C++ 表示。

`get_packaged_spec_database()` 在每个进程首次成功调用时验证并合并打包的规格，
此后在进程生命周期内返回同一个快照。并发首次调用只初始化一次；加载失败后可以重试。
调用方应按约定将整个模型视为只读：冻结的 dataclass 仍包含可变的嵌套字典和列表，
修改它们会影响其他调用方。需要局部修改时先使用 `copy.deepcopy()`。
兼容模块 `ptx_frontend.code_gen.database` 也导出同一接口。`load_*` 函数始终重新
读取指定的指令 YAML，包括打包规格；若需要看到快照建立后的文件修改，应使用这些函数。
其他规格目录不会进入缓存。

database 在合并 opcode 后验证 selector 语言：只有 required/fixed slot 的有序位置
能够消除绑定歧义时，活动 modifier slot 才可以共享 spelling。同一 variant 的规范
序列与显式 `modifier_order_aliases` 若有交集，必须产生相同绑定；不同 variant 接受
的序列必须互斥。slot name 只在 variant 内有意义，所以同一 spelling 可以跨 variant
绑定不同 slot。这些校验使 C++ matcher 能确定性地局部绑定，而不接受任意源码顺序。

## Normalization

`ptx_frontend.spec.normalize` 负责将 schema 合法但书写方式不同的 YAML 收敛为一个模型：

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
SyntaxVariantDescriptor(variant_id, modifiers, operand_layouts, modifier_order_aliases=())
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
ResolvedField(name, value_kind, origin, storage, ...)
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

Modifier value 采用表驱动处理。`ir.resolved_value_kind` 定义语义身份，
`ir.resolved_value_policy` 统一 modifier kind 映射、Python 值类型、optional default
支持范围和诊断名称。C++ domain 与 descriptor member 映射归
`code_gen.resolved_value_traits` 所有；emitter 共用其中的值转换和 descriptor 初始化
函数，不再按 C++ 类型名称字符串分派。现有调用方仍可从 `ir.resolved_ir` 导入
`ResolvedValueKind`。
Descriptor 表达式映射使用 `ResolvedValueKind` 枚举作为查询键；C++ 成员名字符串
仅用于输出拼写。

归一化后的 discriminator enum 是严格的 `Enum` 成员：YAML spelling 只在归一化边界
转换一次，后续不能与 raw string 混用。`spec.semantic_domains` 保存不可变的已建模 PTX
词表，并区分可拼写值与 optional 的 default-only sentinel。它会在构建 Resolved IR 前
校验展开 value set、fixed value、default、scalar expression、state space 与
special-register compatibility。该词表刻意包含当前 C++ backend 尚未映射的合法 PTX
形式；这类 IR 仍合法，缺失 C++ mapping 会在生成期作为明确的 capability error 报告。
因此 `ResolvedField` 不再提供 C++ type 或 expression property，只有 code generation
helper 在输出时投影这些表示。

## C++ emitter 与产物

`python -m ptx_frontend.code_gen` 生成 Resolved IR 所需的直接类公共声明、
运行期映射、dispatch 及按 category 分片的实现：

一次 generation run 中，`GenerationContext` 保存唯一的有序 binding 序列：每个
normalized `InstructionSpec` 都与其一次 lowered、backend-projected 的
`ResolvedInstruction` 配对。每个 binding 从 source opcode 派生一个 canonical C++
instruction type name，并要求 resolved model 精确携带该名称。Syntax emission 与
category selection 读取 binding 的 source 一侧和 type identity；model、descriptor、resolver
与 checker emission 读取 resolved 一侧。为兼容性保留的 resolved tuple 由 binding 派生，
因此 source 与 resolved 的顺序或 C++ type identity 不能独立漂移。

context 构造会在任何 emitter 创建目录或写文件之前执行有限的结构 preflight：canonical
binding C++ type name 必须唯一，且每个 resolved operand payload kind 都必须有 module-reference
policy。binding 构造自身会拒绝 resolved C++ name 与 source-derived type name 不同的情况，
包括 direct 构造和 `dataclasses.replace`。它只验证冻结 snapshot，而不声称能预测所有
rendering 或 filesystem 失败。

| 输出 | emitter | 内容 |
| --- | --- | --- |
| `public/ptx_frontend/resolved_ir/ptx_instruction_base.gen.hpp` | `emit.resolved_model` | `Instruction` 基类、opaque identity 枚举声明与 observer 契约 |
| `public/ptx_frontend/resolved_ir/ptx_instruction_catalogue.gen.hpp` | `emit.resolved_model` | 供宽入口使用的完整具名 `Opcode`、`InstructionKind` 枚举项 |
| `public/ptx_frontend/resolved_ir/identity/<category>/<opcode>.gen.hpp` | `emit.resolved_model` | 具名且带类型的 opcode/form 常量，不包含指令类定义 |
| `public/ptx_frontend/resolved_ir/model/<category>/<opcode>.gen.hpp` | `emit.resolved_model` | 稳定的逐 opcode 聚合头：小 opcode 直接定义类，大 opcode 引入有界形式分片；声明 descriptor getter 与 resolver |
| `public/ptx_frontend/resolved_ir/model/<category>/<opcode>_forms_NNN.gen.hpp` | `emit.resolved_model` | 大 opcode 中一个最多 64 形式的规范分片的 final 类声明 |
| `public/ptx_frontend/resolved_ir/ptx_resolved_ir.gen.hpp` | `emit.resolved_model` | 全部 opcode 头的聚合 |
| `private/resolved_value_domains.gen.hpp` | `emit.value_domains` | resolver 使用的运行期 value-domain lookup table |
| `private/resolved_ir_dispatch.gen.cpp` | `emit.resolved_dispatch` | 跨 opcode 的解析分发 |
| `private/resolved_ir_<category>_<opcode>.gen.cpp` | `emit.resolved_source` | 稳定的逐 opcode resolver、selector 与 descriptor getter 入口；小 opcode 还包含未分片的方法及 descriptor 行 |
| `private/resolved_ir_<category>_<opcode>_{methods,descriptors}_NNN.gen.cpp` | `emit.resolved_source` | 大 opcode 规范形式分片的有界方法定义与静态 descriptor 行 |

C++ backend YAML 为每个 `codegen_category` 显式分配唯一的 8 位 ID。在完整的
normalized snapshot 内，各 category 的 opcode 按顺序取得从 1 开始的编号，
各 opcode 的 canonical resolved form 也按顺序取得从 1 开始的编号。
`GenerationContext` 在选择 category 或形式分片产物之前构造唯一的 8/8/16 位
identity map。缺失的 category ID、重复或越界的 category ID，以及超出位宽的
opcode/form 编号都会使生成失败。Category 前缀固定；插入 opcode 可能重编号
同 category 中后续 opcode，插入形式可能重编号同 opcode 中后续形式。
这些整数是生成身份，不承诺二进制 ABI，也不是永久的逐 opcode/form 注册表。

基础头只前向声明作用域 identity 枚举。每个 opcode 的
`identity/<category>/<opcode>.gen.hpp` 从完整 context 的 map 定义具名且带类型的常量。
窄 model 头和形式分片头引用这些常量来定义 `Form::opcode` 和 `Form::kind`，
无需引入全部具名枚举项。完整 catalogue 也引用这些局部常量，生成的数值分配
只出现在 identity 头中。
直接使用 `Opcode::` 或 `InstructionKind::` 成员时需包含
`ptx_instruction_catalogue.gen.hpp`；宽聚合头包含该目录头，保留宽入口用法。
全局 resolver dispatch 使用目录头，但不包含所有 final form 类。

生成的公开头位于 `submod/resolved_ir` 构建树的
`generated/public/ptx_frontend/resolved_ir`，安装后相对于 `include` 保持相同布局。
私有生成源码和支持头保留在 `generated/private`，不安装。
源码构建由 `submod/resolved_ir/CMakeLists.txt` 调用
`cmake/ptx_resolved_ir_codegen.cmake`；该 helper 使用 Python codegen CLI 的
`--describe-build` 模式取得唯一的 category 输入、category 产物和共享产物计划。
一个聚合构建命令在同一 Python 进程中生成发生变化的 category 及所需共享产物；
该进程只加载一次 backend、归一化 instruction model、context 和 plan。配置阶段的
描述命令是独立进程。各 category 的完成戳追踪贡献它的规格文件及输入成员清单；
共享产物追踪全部规格文件。schema、backend mapping 和 generator 源码会使全部
category 失效。CMake 重新配置可检测输入的新增和移除。Ninja 可根据缺失的产物或
完成戳触发修复，Makefile target 则在接受干净的完成戳前检查这些文件。batch
在写入前删除所选完成戳，因此即使后来失败时已经修复缺失文件，下次仍会重试。
所需 category 成功后才写共享产物，之后删除过时产物并发布 manifest。失败时可能
留下部分已写入产物，但保留上一次成功的 manifest。增量选择按构建时间戳判断，
不保证发现内容已变但输入时间戳未变的情况。CLI 默认使用六个产物 writer
（`--jobs 6`），`--jobs 1` 串行执行。CMake 通过
`PTX_FRONTEND_CODEGEN_JOBS`（默认 `6`）设置该单进程预算。无变化构建不会启动
generator Python 进程。每个产物先在同目录写入 candidate，比较原始字节后仅在变化时原子替换。
生成过程不调用也不依赖 `clang-format`。

Syntax descriptor storage 提供供 variant selection/resolution 使用的逐 opcode
自由函数 getter。未分片 opcode 的 syntax、resolved、checker descriptor 行位于同一源文件；
已分片 opcode 的公共 getter 留在稳定入口源文件，行数据位于私有 descriptor 分片。

直接类路径保留 YAML schema 与 normalized instruction model。每个语义形式是
`Instruction` 的 final 子类：公共字段是直接成员，layout 专有字段是带类型的
optional。解析返回 `std::unique_ptr<Instruction>`。生成的逐 opcode 源文件及必要时的
方法分片定义 out-of-line 解析、检查、克隆和引用遍历方法。中央 dispatch 调用逐 opcode
解析器，不使用 instruction union。

完整 opcode 公共头不包含生成的 resolver/checker 函数体。小型手写
`ptx_resolved_ir_selection.hpp` 提供通用选择适配器；单个 opcode model 头仍可在 syntax AST
不完整时使用。安装的 `ptx_resolved_ir.hpp` 包含生成聚合头和 module 解析 API。
生成分片使用归一化后的 `codegen_category`；它与记录 PTX
文档归属的 `source_categories` 分离。同 opcode 的全部 YAML 定义必须使用同一
`codegen_category`，生成脚本据此为每个 opcode 保留稳定的公共聚合头与私有入口源文件。
形式超过 64 个时，声明、方法与 descriptor 行按规范顺序拆成每片最多 64 个形式的
确定性分片，由 CMake 编入 `resolved_ir` library。consumer 仍使用同一逐 opcode include
入口；分片不承诺新增形式对其他代码完全没有编译影响。

公共 syntax、resolved 与 checker descriptor getter 返回 `const&` 且为 `noexcept`。
大 opcode 的每个 getter 使用有界且只执行一次的 function-local `static const std::array`
把规范顺序的静态分片行拼接为连续、生命周期稳定的存储，不使用 heap allocation。
公共聚合头与精确 final 类身份仍是兼容边界；没有引入可变 logical-form tag 或 opcode 包装。

生成器将同目录 candidate 的原始输出字节与已有 artifact 比较；输出相同（包括
output manifest）时保留 modification time。产物变化时保留已有文件权限；生成或替换
失败时清理 candidate，后续可重试。consumer 可包含聚合头或单个 opcode 头。

比较与选择规范生成 `comparison_and_selection` 分区。窄 consumer 可包含
`model/comparison_and_selection/set.gen.hpp` 等单个头；聚合头提供所有形式。

每个输出文件只打开一次外层 namespace。private descriptor storage 位于单一匿名或
`generated_detail` namespace，getter 位于 `ptx_frontend::resolved_ir`；checker
specialization 声明位于公共头的单一 `checker` namespace，每个 opcode 实现文件也只
打开一次对应 namespace。

所有 emitter 从规范化后的 C++ backend domain 获取语义值对应的 C++ 类型与表达式。
生成文件不会默认嵌入 wall-clock 时间；若构建环境提供
标准 `SOURCE_DATE_EPOCH`，生成警告会使用该确定性 UTC 时间，否则明确标记时间已省略。
因此相同 ISA spec、backend spec 和生成器输入会产生 byte-identical 内容。

backend lookup helper 必须接收由 context 或 emitting call 显式传入的 `CodegenUnit`。
生成器没有 process-global active backend、配置步骤或 backend cache，因此独立的 generation
snapshot 不会选择彼此的 C++ spelling。

### Backend 配置边界

`python/src/ptx_frontend/spec/resources/ptx_cpp_backend_spec/ptx_frontend.yaml` 及其
`python/src/ptx_frontend/spec/resources/ptx-cpp-backend-v2.schema.yaml` 构成独立的 C++ backend 映射层。
`ptx_frontend.code_gen.cpp_backend` 将 `domains` 规范化为 `DomainBackend`，Syntax、Resolved、
checker emitter 只通过 typed lookup 读取 C++ 拼写。查询接口的 domain 参数必须使用
`CppDomain` 枚举成员，例如 `CppDomain.SCALAR_TYPES`，不接受裸字符串。当前 domain
覆盖 scalar type、
rounding mode、resolved value type/kind、modifier presence、operand role/access/shape、
type-expression kind 与 checker modifier kind。

backend spec 不应重复表达 `ptx_spec` 中的 PTX ISA 语义，也不应影响
`InstructionSpec` 的规范化结果。其唯一的生成输入是 ISA schema version、backend schema
version 和封闭的 C++ mapping domain 集合；不接受 per-instruction layout、emit、namespace、
include 或 category policy。`CodegenUnit` 只保存这些输入。emitter 不得直接读取原始 YAML
字典。loader 会在 v2 schema 校验之前，以迁移诊断拒绝已退休的
`ptx-cpp-backend/v1`；consumer 必须将 import 和 construction 迁移到收窄后的
`CodegenUnit(spec_schema, backend_schema, domains)` contract。缺失、未知或无 mapping 的
domain/value 必须在生成期报告 `ValueError`。CMake 将 backend YAML 与 schema 都列为生成
依赖，修改任何 C++ 映射都会触发重新生成。

迁移 backend 文件时，将 schema tag 和 YAML-language-server header 从 v1 改为 v2，随后删除
`target`、`category`、`namespace`、`includes`、`common`、`emit_kinds` 与 `instructions`。
删除已退休的 `modifier_value_cpp_types`、`operand_value_cpp_types` domain，以及 value 内的
`token` 或 `aliases`。`Emit*`、`InstructionBackend`、`ModifierBackend`、`OperandBackend`
不再可 import；改用 `DomainBackend` 和收窄后的 `CodegenUnit`。PTX ISA 文件继续使用
`ptx-instr/v1`。

需要在运行期从 PTX 源码 suffix 解析值的 domain 声明
`runtime_lookup: ptx_suffix`。生成器会把对应映射生成到 private 的
`resolved_value_domains.gen.hpp`，并以 `inline constexpr std::array` 保存。
手写 resolver 只保留一份通用 suffix 查找算法，不再重复 scalar type 或 rounding mode
的映射数据；标记 domain 的 `cpp_type` 决定生成表的 value type。未标记
`runtime_lookup` 时，`cpp_type` 仅为 type annotation，不决定 instruction field type；后者由
`resolved_value_cpp_types` mapping 决定。未标记的 domain 仍仅用于生成期，不会产生运行期查找表。

## 生成规则

- YAML identifier 经统一转换得到 deterministic PascalCase C++ 名称；碰撞必须报错。
- 每个 final 形式直接存放共有 operand 字段，只在部分 layout 中存在的字段用强类型
  optional。固定的小型 operand domain 本身可以使用强类型 variant；opcode owner
  仍是 `unique_ptr<Instruction>`。
- `ResolvedOperandLayoutTag` 始终与 syntax/resolved descriptor 中的 layout 索引对应。
- 生成器只决定必要的 C++ 语法，不能把 `direct`、`sub_variant` 等旧 backend 选项
  重新暴露为模型字段。
- 类型/role/access/shape 尚未得到 C++ 支持时，应在 Python 模型构建时抛出
  `ValueError`，而非生成半正确代码。

## 测试与变更方式

`python/tests` 验证打包的规格输入、normalization、Syntax/Resolved model，以及
generator 输出和 CLI 行为。其中 `python/tests/ir` 直接测试 YAML -> normalized
model -> descriptor/emitted source 的结构。每次新增模型字段应同时测试：
normalization、对应 IR model、生成文本中应有的 ABI 片段。冻结的 `corpus/` fixture、
provenance ledger 和 `tools/corpus` 维护属于独立的仓库证据，不在这个 Python
generator 测试套件的验收范围内。C++ 测试则验证真实 parser、resolver 与 checker 闭环。

推荐顺序：先扩展 schema 与 normalized dataclass，再扩展 Syntax/Resolved model，最后
修改 emitter 与测试。不要让 emitter 从原始 YAML 读取新字段，这会绕过一致性检查。

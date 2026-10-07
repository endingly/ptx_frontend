# Resolved IR / Resolved IR 模块

This is the active resolved-IR module. The shared instruction database generates
92 opcode families, 965 distinct semantic form classes, and their 1,205
supported layouts. Each form is a final subclass of
`ptx_frontend::resolved_ir::Instruction`; no opcode wrapper, instruction union,
or outer owner value is part of this interface. Public generated headers use
the `.gen.hpp` suffix and live under
`<ptx_frontend/resolved_ir/>`. A narrow per-opcode header is available
under `model/<category>/<opcode>.gen.hpp`; the aggregate entry point is
`ptx_resolved_ir.hpp`.
Model-only headers leave the syntax AST incomplete but provide owned diagnostic
types and selector declarations. Include syntax parsing headers to supply an
AST to a standalone resolver; include `ptx_resolved_ir_resolution.hpp` for
module and bound-context resolution. The handwritten aggregate entry point
includes that resolution header.

`resolveInstruction` and per-opcode functions such as `resolveAdd` return
`expected<unique_ptr<Instruction>, ResolveDiagnostic>`. Each class implements
virtual `check`, `clone`, and synchronous reference visitation through the
internal typed `IReferenceObserver`. Common fields are direct members;
layout-specific fields are typed optionals. A selected layout must agree with
required and forbidden optional fields before a checker or observer dereferences
them. Borrowed reference values and locations are valid only during callbacks;
callbacks must not mutate the instruction payload.

`ResolvedFunction::body` owns `unique_ptr<Instruction>` entries. Function copy
clones instructions and metadata, while module resolution and AST-free module
validation retain source identity, declaration bindings, and target policies.
Call `resolveAndValidateModule` or `validateModule` for complete target
validation; local instruction checking does not replace it.

The normal build exports `ptx_frontend::resolved_ir`. Generation uses the
canonical lowering plan and backend metadata. With `BUILD_TESTING=ON`, this
module registers `resolved_ir_smoke` and `test_resolved_ir`.

```sh
cmake --preset ci-linux-clang-debug
cmake --build out/build/ci-linux-clang-debug --target test_resolved_ir -j 3
ctest --test-dir out/build/ci-linux-clang-debug/submod/resolved_ir --output-on-failure
```

The previous variant-based implementation has been removed. The direct-class
suite retains the `test_resolved_ir` target name and applicable test cases.

本目录是活跃的 Resolved IR 模块。共享指令数据库生成 92 个 opcode 类别、965 个
独立语义形式 final 类及其 1,205 种 layout。每个形式都继承
`ptx_frontend::resolved_ir::Instruction`；公开接口不再使用 opcode 包装类、
指令 union 或外层 owner 值。公开生成头使用 `.gen.hpp` 后缀，位于
`<ptx_frontend/resolved_ir/>` 下；可选择单个 opcode 的窄头，
也可包含聚合头 `ptx_resolved_ir.hpp`。
仅包含 model 头时，Syntax AST 保持不完整类型，但已提供拥有值的诊断类型与
selector 声明。独立指令解析需要另外包含语法解析头；module 或绑定上下文解析
需要包含 `ptx_resolved_ir_resolution.hpp`。手写聚合入口已包含该解析头。

`resolveInstruction` 和 `resolveAdd` 等逐 opcode 入口返回
`expected<unique_ptr<Instruction>, ResolveDiagnostic>`。每个类实现虚函数
`check`、`clone` 及通过内部强类型 `IReferenceObserver` 进行的同步引用遍历。
公共字段是直接成员，layout 专有字段是带类型的 optional；检查器或观察者在
解引用之前先验证所选 layout 与必需/禁止字段是否一致。借用的引用值和位置仅在
回调期间有效；回调不得修改指令载荷。

`ResolvedFunction::body` 持有 `unique_ptr<Instruction>`。复制函数时深拷贝
指令及元数据；module 解析与脱离 AST 的校验继续保留来源身份、声明绑定和目标
策略。完整目标校验使用 `resolveAndValidateModule` 或 `validateModule`，局部
指令检查不能代替它。

普通构建导出 `ptx_frontend::resolved_ir`。生成过程使用规范 lowering
计划和后端元数据。`BUILD_TESTING=ON` 时注册 `resolved_ir_smoke` 和
`test_resolved_ir`。原来的 variant 实现已移除；直接类测试套件沿用
`test_resolved_ir` 目标名，并保留适用的测试案例。

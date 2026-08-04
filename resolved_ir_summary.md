# Resolved IR 修改思路

## 对 `sub_variant` 的澄清

此前将 `sub_variant` 一概视为应移除的“C++ 布局策略”，这一理解不准确。

需要移除的是：YAML 以 `direct`、`sub_struct`、`sub_variant` 为名，允许作者为了
任意 C++ 存储排布而选择的 backend mechanism。它会让 spec 的语义与 C++ 表示细节
耦合，也会让生成器承担不属于 PTX 模型的布局判断。

不应移除的是：为了表达真实 PTX semantic form 或不同 operand layout 而使用的嵌套
struct 与 `std::variant`。这种内部 variant 是 Resolved IR 的语义组成部分。

## 推荐的 opcode 内部组织

ResolvedInstruction 的顶层 variant 按 opcode 组织：

```cpp
using ResolvedInstruction = std::variant<Add, Atom, Ld, St /* ... */>;
```

每个 opcode 在内部按 form 的实际结构选择表示。

### 相同 operand layout：form enum + shared operands

```cpp
struct Add {
  enum class Form { U32, SatS32, U16x2, SatU8x4 };

  struct Operands {
    RegisterId dst;
    RegOrImmediate src1;
    RegOrImmediate src2;
  };

  Form form;
  Operands operands;
};
```

该模型避免为每个 form 生成一个顶层 struct，也不会把 `.sat`、`.s32` 等已经由
`Form` 唯一确定的事实重复存储为字段。

### 不同 operand layout：common + semantic sub-variant

```cpp
struct Atom {
  struct Common {
    StateSpace space;
  };

  struct Basic {
    RegisterId dst;
    ResolvedAddress addr;
    ResolvedValue value;
  };

  struct CompareAndSwap {
    RegisterId dst;
    ResolvedAddress addr;
    ResolvedValue compare;
    ResolvedValue replacement;
  };

  Common common;
  std::variant<Basic, CompareAndSwap> operands;
};
```

此处 `Basic` 与 `CompareAndSwap` 不是为了排布而分组；它们对应不同的 PTX operand
layout，因此应由生成模型显式描述。

## 生成模型的调整

YAML 应提供稳定 form ID、modifier constraint、availability 与 resolved operand
layout。normalization 据此判断：

1. 哪些字段是 opcode 的 common field；
2. 哪些 form 只需一个 form enum；
3. 哪些 form 需要生成内部 payload variant。

template 负责将这个已经确定的语义模型翻译为 C++，而不再根据 `emit.kind` 决定 IR
的语义组织。

## 需要修订的既有观点

此前文档中“每个 form 必须生成一个 flat struct”的结论应废弃。正确原则是：

> Resolved IR 应以最简单、不会表达非法状态的方式保存已选定的 PTX form；当 form
> 共享 layout 时使用 tag，当 layout 不同时使用语义 sub-variant。


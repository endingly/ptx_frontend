已将专用 .reg declaration 重构为通用 variable declaration，设计参考 NVIDIA 官方 PTX Variables 语法
  (https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#variables)。

  当前支持：

  - Module scope 与 function scope declaration
  - .reg
  - .param
  - .local
  - .shared
  - .global
  - .const
  - .visible、.extern、.weak linkage qualifier
  - .align
  - .v2 / .v4 vector type
  - %r<3> register bank
  - 多维 array，例如 scratch[19][19]
  - 无长度 array
  - 简单 array dimension token expression

  主要公共类型现在是：

  - CstVariableDeclaration
  - CstVariableDeclarator
  - CstArrayDimension
  - AstVariableDeclaration
  - AstVariableDeclarator
  - AstArrayDimension
  - AstStateSpace

  原来的 CstRegisterDeclaration、AstRegisterDeclaration 等专用类型已删除，没有保留重复兼容层。

  主要实现位于：

  - include/ptx_ir/cst/ptx_cst.hpp
  - src/ptx_cst_parser.cpp
  - include/ptx_ir/syntax/ptx_syntax_ast.hpp
  - src/ptx_syntax_lower.cpp

  覆盖矩阵和设计文档也已更新。

  验证结果：

  - Python：43/43
  - Debug CTest：90/90
  - package consumer：通过
  - git diff --check：通过

  目前 variable declaration 尚缺 initializer、fixed address，以及真正结构化的 constant-expression AST。下一步可
  优先补 initializer 和 constant expression；完成后 declaration grammar 才基本足以支撑正式符号表。
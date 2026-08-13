已完成 binding-aware module resolution 阶段：

  - 新增 resolveModule()，模块持有 SymbolTable，函数使用 SymbolId：src/ptx_ir/
    ptx_resolved_module.cpp:7

  - ResolvedRegisterRef 现在携带声明 SymbolId、parameterized index 和声明类型，
    同时支持 named register：include/ptx_ir/resolved/ptx_resolved_ir.hpp:182

  - 生成器支持带 ResolveContext 的 opcode 分派，standalone resolver 保持兼容：
    python/code_gen/gen_resolved_ir.py:107

  - module resolution 会诊断未声明寄存器、非 .reg symbol 和 predicate 类型不匹
    配。

  - checker 已能校验寄存器声明类型。
  - 安装后 package consumer 也覆盖了 resolveModule()。
  - 补充 named、parameterized、predicate、类型错误和 standalone 行为测试：test/
    resolve_and_check/test_resolved_module.cpp:23

  - 中英文设计文档与 next_step.md:40 已更新。

  验证结果：Debug CTest 108/108 全部通过，git diff --check 通过。

  下一阶段是 special register/external symbol 分类，以及真正未声明 reference 的
  精确诊断。
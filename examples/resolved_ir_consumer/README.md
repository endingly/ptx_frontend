# Installed resolved IR consumer

This consumer checks the installed `resolved_ir` and `semantic` CMake components,
one representative exact-form resolver/checker path, strict module resolution
and validation, native F16x2 vector parameter classification/layout after source release,
and the installed `ptx_spec` resource paths. It is an
installation smoke test, not the full
module semantic suite.

```sh
cmake --install out/build/ci-linux-clang-debug --prefix /tmp/ptx-resolved-install
cmake -S examples/resolved_ir_consumer -B /tmp/ptx-resolved-consumer \
  -DCMAKE_PREFIX_PATH=/tmp/ptx-resolved-install
cmake --build /tmp/ptx-resolved-consumer
/tmp/ptx-resolved-consumer/resolved_ir_consumer
```

The Debug installed-header check also inspects CST/AST `array_dimensions`,
normalized function/local-prototype `array_extents`, numeric/opaque owned axes,
and derived total-count ABI compatibility after syntax destruction and module copy.
The removed singular fields and replacements are documented in
[parameter declarations](../../docs/us-en/parameter_declarations.md).

Debug 安装头文件检查还覆盖 CST/AST `array_dimensions`、function/local-prototype
normalized `array_extents`、numeric/opaque owned 轴及 syntax 销毁和 module copy
后的总元素数 ABI。删除字段与替代字段见[parameter 声明](../../docs/zh-han/parameter_declarations.md)。

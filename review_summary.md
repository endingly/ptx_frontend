• 总体判断：当前“Syntax AST → Resolved IR → Checker”的大方向是合理的，descriptor 分层也已经站稳。但我不建议立刻大规模
  扩展指令；应先完成一轮基础正确性与可扩展性加固。

  ## 主要问题

  1. 高优先级：安装后的 CMake 包不可用

  我实际安装到 /tmp 验证后发现：

  - ptx_frontendConfig.cmake 写入了绝对源码路径 /root/code/ptx_frontend/ptx_frontendTargets.cmake。
  - 头文件安装在 include/ptx_frontend/ptx_ir/...，但导出 target 只暴露 include。
  - Config 没有为公开依赖执行 find_dependency(fmt) 和 find_dependency(magic_enum)。

  根因位于 cmake/install_project_targets.cmake:98 和 CMakeLists.txt:118。

  建议增加一个真正的外部 consumer smoke test：安装库后，由独立 CMake 工程执行 find_package(ptx_frontend)、include 公
  共头文件并链接。

  2. 高优先级：resolve<T> 没有验证 opcode

  include/ptx_ir/resolved/ptx_resolved_ir.hpp:233 只匹配 modifier，没有检查：

  ast.opcode.syntax.text == T::get_syntax_descriptor().Opcode_name

  因此一个拥有相同 modifier/operand 形状的其他 opcode 可能被 resolve<Add> 接受。公共 resolve 入口应首先检查 opcode，
  并增加错误 opcode 测试。

  3. 高优先级：寄存器身份被过早破坏

  include/ptx_ir/resolved/ptx_resolved_ir.hpp:158 只保存数字，而 src/ptx_ir/ptx_resolved_ir.cpp:246 只提取结尾数字。

  结果是 %r1、%rd1 等最终都变成 {1}；而 predicate resolver 也没有验证 %p 类别，理论上可能把 %r1 当 predicate。

  这会阻碍后续符号表、声明类型和寄存器类别检查。建议在符号表完成前至少保留 interned spelling；最终应使用类似：

  struct ResolvedRegisterRef {
    SymbolId declaration;
    RegisterClass register_class;
  };

  4. 高优先级：Syntax AST 目前并不真正“忠于源码”

  include/ptx_ir/syntax/ptx_syntax_ast.hpp:13 只保存节点首 token 的 leading trivia。解析器的 src/
  ptx_syntax_parser.cpp:56 通过字符串拼接重建文本，中间 token 的空白、注释以及部分标点 trivia 会被丢弃。

  例如地址、vector pack、predicate guard、分隔符前的注释不能完整 round-trip。

  需要明确二选一：

  - 若 Syntax AST 需要支持格式化、重写和源码保真：为 SourceRange 加 source ID/byte offset，并保留完整 token span/
    CST。

  - 若只服务诊断和 resolution：文档中不应再宣称它是 lossless/source-faithful。

  我倾向于增加轻量 token-backed CST，否则以后再补会影响所有 AST 节点。

  5. 高优先级：rule_id 尚未真正工作

  Checker descriptor 保存了 rule_id，注释也声称 generated wrapper 会调用规则，但生成的 python/code_gen/
  gen_resolved_ir.py:210 目前只调用：

  - availability check
  - modifier value availability
  - layout/tag check
  - operand shape/type check

  没有任何 rule dispatch，RuleViolation 也没有使用。也就是说 YAML 中的：

  rule: parallel_sync_and_communication.bar_sync

  现在只是字符串档案，不会执行语义规则。

  扩展指令前应设计 typed rule registry，例如生成：

  check_rule<rules::BarSync>(selected, context);

  6. 高优先级：literal lexer 与 resolver 的能力不一致

  Lexer 接受 1U、0x10U 和十进制浮点数，见 src/ptx_lexer.l:89。但 src/ptx_ir/ptx_resolved_ir.cpp:341 始终用
  from_chars(uint64_t) 解析：

  - U 后缀不会被剥离；
  - 十进制浮点不会被解析；
  - 没有按目标位宽做范围检查；
  - 负数直接在 64 位上取补码，没有明确按目标类型截断。

  建议让 AstImmediate 保存 lexical kind，然后按 literal kind 与目标 ScalarType 进行结构化转换。

  7. 中优先级：optional modifier 的 schema 能力超过 resolver

  Schema 允许 optional modifier 带任意 default，见 instructions/schemas/ptx-instr-v1.schema.yaml:488。实际 resolver：

  - optional flag 缺失时固定写入 false；
  - optional type 缺失时不写 field；
  - generated initializer 随后仍会强制读取该 field，见 python/code_gen/gen_resolved_ir.py:507。

  当前 Add 恰好只使用 default: false，所以测试通过。短期应限制 schema 只允许这种已支持形式；长期把 optional 成员生成
  为 std::optional<WithLocs<T>>，并真正使用 YAML default。

  8. 中优先级：schema 与 YAML examples 没有进入正式生成门禁

  python/code_gen/database.py:33 直接 safe_load → normalize，不会调用 JSON Schema validator。未知字段可能被静默忽略。
  独立的 python/scripts/validate_yaml.py:21 没有接入 CMake/CTest。

  另外，YAML 中的 examples 完全没有执行。随着指令增加，这些 example 应自动生成参数化测试，至少覆盖：

  YAML example → parse → selectVariant → resolve → check

  9. 中优先级：生成头文件尚未具备完整 PTX 的编译规模

  当前只有 Add 和 Bar，公开生成头已经约 3200 行、128 KiB，而且把所有 resolve<T> 与 check<T> 实现 inline 到公共头文件
  中。

  完整 PTX 后会明显增加每个 consumer TU 的解析和模板实例化成本。建议在继续扩展前调整为：

  - 公共头只生成结构体和显式特化声明；
  - 特化实现放入按 category 分片的 .gen.cpp；
  - 按指令类别拆分 generated header，并提供可选 umbrella header。

  这里需要恢复显式特化声明：当特化定义位于 .cpp 时，声明必须在调用点可见，以避免编译器尝试实例化主模板。

  10. 其他应清理的问题

  - include/ptx_ir/source_loc.hpp:106 的 move constructor 标记 noexcept，但内部 vector::push_back 可能抛异常。
  - src/ptx_ir/base.cpp:5 对 ScalarType::Invalid 返回 Bit，虽然枚举中已有 ScalarKind::Invalid。
  - include/ptx_ir/base.hpp:55 返回会分配内存的 std::string，却标记为 noexcept。
  - base.hpp 中旧 ParsedOperand、Variable、MethodDeclaration 等类型没有外部使用，应拆除或迁移，避免与新 IR 并存。
  - instructions/ptx_cpp_backend_spec、旧 backend schema 和 _shared 数据目前没有消费者，应删除或明确标记为未来设计。
  - ScalarType 的 Python→C++ 映射在多个生成器中重复，应集中为唯一 registry。
  - 生成文件写入当前时间，破坏可复现生成；所有 Python 文件（包括测试）又都是 codegen 依赖，会造成不必要的全量重新生
    成。

  - CMakeLists.txt:9 混用了 CCACHE_FOUND option 与 find_program 结果，应拆成 PTX_USE_CCACHE 和 CCACHE_EXECUTABLE。

  ## 建议执行顺序

  1. 修复安装包、opcode 校验、literal 和 WithLocs noexcept 等明确错误。
  2. 决定 Syntax AST 是否真的需要 lossless，并在继续扩展 AST 前定案。
  3. 建立 register/symbol identity 和 rule checker ABI。
  4. 强制 schema 校验，并执行 YAML examples。
  5. 将 generated resolve/check 实现迁出公共头，再开始批量扩展指令。
  6. 最后清除旧 IR/backend 资产与重复映射。

  当前 Debug、Release 各 51 项 CTest 均通过，两份现有 YAML 也通过 schema validator，git diff --check 无错误。我没有修
  改工作区源码。
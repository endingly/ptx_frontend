# Tensor Memory named-mask 边界

### TCGEN 必需的 named output-lane mask

恰有24个既有普通 unscaled、非 WS 的 dense/sparse MMA mask layout 在 brace 之外接纳 named source。公共 physical carrier 顺序为 B32/U32/S32/F32/F16x2，native F16x2 同时追加到两种形式。仅改变 named permission 与该公共 carrier list。必需的 `disable_output_lane` member 仍为 `WithLocs<ResolvedRegisterVector>`，kind、B32 type、Element policy、layout identity 与 syntax arity1..8 均不变。不恢复已删除的省略 mask layout，不合成零/default mask，也不从 mask register 推断 descriptor word 或执行结果。

Named Element lowering 复用独立建立的 actual-declaration-width inference：普通 General `.reg .v2/.v4` whole declaration、兼容 physical type、至多128位 extent、完整 source range 与精确 NamedProjection lane。不猜4、不按 opcode/target 选择，不添加 ordinary namedv8，也不改变 fixed Element/Aggregate 行为。既有 CTA-group checker 仍要求 group1 恰有4 entry、group2 恰有8 entry。Group1 `.v4` 合法；group1 `.v2` 与 group2 ordinary named `.v4` 在完整检查时失败。Lowering draft 或缺少上下文的延期不证明 group 合法。对普通 declaration syntax，group2 仍使用八项 brace。

当前实际 payload、whole-base SymbolId/type/class/width 与全部4/8 lane provenance/range 仍具权威性；callback 仅同步借用，copy/assignment/clone 后从新 owner 重新取得 view。对另一个真实且兼容 declaration 的一致 retarget 可通过当前检查，而不同的 saved operand AST 仍独立产生 source-identity mismatch。Special/predicate/array/component-as-whole/InstructionOnly/wrong-width declaration 与 null sink lane 不获得权限。Matrix named machinery 是独立验收的 prerequisite，该 mask facet 不重新实现它。其余 mask、WS/MX/collector/ashift/scaled role、target DNF/profile window 与 required-presence/cardinality logic 保持不变。记录 delivery 完成前仍需实际 current Debug、fresh unique installed 七库 consumer 证据及独立 actual-diff 验收。

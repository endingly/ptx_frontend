# Prepared dense tf32 MMA installed consumer

This example uses the installed dense tf32 owned form and known-operation
query. Configure it against a `ptx_frontend` installation that includes tf32.

Run `tcgen_mma_tf32_consumer`. It checks Table 42 K8 shape/path rows and a
caller-known missing-fact query, then parses a complete canonical tf32 module
and inspects selected borrowed roles **after the syntax tree is destroyed**.
The source uses shared A, an F32 bit-mask register vector, written predicate
negation and D scale15. The example validates frontend representation only;
it does not execute on a GPU or authenticate runtime descriptor words.

The source uses the documented `.mma.cta_group.kind` order. The exact `.mma.kind.cta_group` alias is approved for both typed groups but is deliberately left to focused tests: group 1 alias assembly was observed, while group 2 extension is an Authority grammar inference. The legal TMA omitted/group1/group2 mixed-body integration test remains deferred until both instruction families are present together.

# Prepared dense tf32 MMA installed consumer

This example uses the installed exact `Tcgen05MmaTf32` form and known-operation
query. Configure it against a `ptx_frontend` installation that includes tf32.

Run `tcgen_mma_tf32_consumer`. It checks Table 42 K8 shape/path rows and a
caller-known missing-fact query, then parses a complete canonical tf32 module
and inspects selected borrowed roles **after the syntax tree is destroyed**.
The source uses shared A, an F32 bit-mask register vector, written predicate
negation and D scale15. The example validates frontend representation only;
it does not execute on a GPU or authenticate runtime descriptor words.

The source uses the documented `.mma.cta_group.kind` order. The exact `.mma.kind.cta_group` alias is covered in focused tests for both typed groups: group 1 alias assembly was observed, while group 2 extension is a grammar inference. TMA omitted/group1/group2 forms may coexist in one body; TCGEN's distinct uniform-group rule must not be applied to them.

# Dense i8 MMA installed consumer

Configure the directory against the package installation and
run `tcgen_mma_i8_consumer`. It checks selected Table 42 K32/small-N rows and
a caller-known missing-descriptor report, then parses canonical i8 PTX and
examines borrowed resolved roles **after the syntax tree is destroyed**. The
source uses a shared A descriptor, F32 bit-mask register entries and written
predicate negation. It has no scale operand or source saturation modifier.

This example validates frontend representation, not GPU execution, encoded
live descriptor contents or numerical saturation. The adjacent qualifier-order
alias has separate positive assembly evidence for both groups and an Authority
source disposition; this example deliberately uses the canonical order. The real
TMA omitted/group-1/group-2 mixed-body edge remains deferred until an
authorized combined revision contains both instruction families.

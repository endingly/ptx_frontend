# Dense f8f6f4 MMA installed consumer

Configure against an installed `ptx_frontend` package and run
`tcgen_mma_f8f6f4_consumer`. The program checks the Table 42 K32 rows and
that a caller-known low-bit A type retains its packing obligation. It parses
a canonical shared-A form with F32 bit-mask entries and a negated predicate,
then checks borrowed owned roles after the syntax tree is destroyed.

A successful run validates the installed frontend representation. It does not
prove physical low-bit matrix packing, live descriptor contents, Tensor Memory
allocation, GPU completion, or numerical execution.

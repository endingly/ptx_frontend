# `testp` and `copysign` coverage

This first floating-point slice models [PTX 9.3 §9.7.3.1](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-testp)
and [§9.7.3.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-copysign)
through the normal YAML, typed resolved IR, and target-aware checker path. It
does not execute floating-point operations.

`testp` accepts `finite`, `infinite`, `number`, `notanumber`, `normal`, and
`subnormal` for `.f32` and `.f64`, writes a predicate, and requires PTX 2.0 /
`sm_20`. The property is represented by the typed `TestProperty` domain, not a
comparison operator. `normal` follows the ISA classification contract; this
frontend does not evaluate values. Its source accepts typed floating literals
or same-width floating/bit register containers; integer literals and
wrong-width containers are rejected.

`copysign.f32` and `copysign.f64` also require PTX 2.0 / `sm_20`. Its first
source supplies the sign and its second source supplies the magnitude. Both
source positions admit typed floating literals or same-width floating/bit
register containers; integer literals and wrong-width containers are rejected.
Neither instruction permits a destination sink; `testp` also rejects a negated
predicate source.

PTXAS evidence was collected with CUDA 13.3.73 on `sm_90` using
`/usr/local/cuda/bin/ptxas -arch=sm_90 <module>.ptx -o <temporary>.o`: 54
probes, 29 accepted and 25 rejected. Five follow-up sink/negated-predicate
probes were all rejected. The probes cover both source positions, floating
immediates, same-width bit containers, invalid integer literals and widths, and
predicate destinations. That assembler cannot target `sm_20`, so the PTX 2.0 /
`sm_20` boundary is enforced by frontend checker tests rather than assembler
target evidence.

Later Issue 142 slices audit the existing ADD/SUB and mixed-family contracts,
then cover MAD/DIV, reciprocal and square-root families, transcendentals, and
MIN/MAX; they are intentionally not part of this document's implementation
scope.

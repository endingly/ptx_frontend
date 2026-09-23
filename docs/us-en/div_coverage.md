# Floating `div` coverage

The generated model preserves the existing integer and round-to-nearest floating
division forms, then extends them with the explicit modes in [PTX 9.3 §9.7.3.8](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#floating-point-instructions-div).
Each mode is a typed Resolved IR variant, so `.approx`, `.full`, and `.rnd` cannot
be combined as unchecked spelling flags. The frontend models parsing, owned
resolution, and target validation; it does not execute division.

| Form | Minimum | Contract |
| --- | --- | --- |
| `div.approx{.ftz}.f32`, `div.full{.ftz}.f32` | PTX 1.4 / all target architectures | Fixed typed `.approx` or `.full`; optional `.ftz`; no rounding modifier. |
| `div.{rn,rz,rm,rp}{.ftz}.f32` | PTX 1.4 / SM 20 | Required typed rounding; optional `.ftz`. |
| `div.rn.f64` | PTX 1.4 / SM 13 | Fixed round-to-nearest; no `.ftz`. |
| `div.{rz,rm,rp}.f64` | PTX 1.4 / SM 20 | Required directed rounding; no `.ftz`. |

Every floating source accepts a floating literal or a same-width native floating
or bit register container; the destination accepts the corresponding native or
bit register container. Integer literals and integer register containers, wrong
widths, sinks, missing explicit modes, mixed `.approx`/`.full`/rounding forms,
and FP64 `.ftz` are rejected.

CUDA 13.3.73 evidence used `/usr/local/cuda/bin/ptxas -arch=sm_90
<module>.ptx -o <temporary>.o`. Its 60-module matrix had 39 expected accepts,
20 rejects, and one permissive assembler acceptance: `rcp.rn.ftz.f64`. That
unrelated noncanonical spelling remains rejected by the frontend. The DIV rows
confirm all four rounded directions, both source positions, literals, and bit
containers. Historical omitted-mode DIV for PTX 1.0–1.3 and
`.target map_f64_to_f32` behavior are deliberately excluded because they need a
target-profile contract outside these explicit forms.

[Explicit reciprocal and square-root forms](unary_float_coverage.md) and [floating transcendentals](transcendental_coverage.md) are now modelled separately. MIN/MAX and the existing ADD/SUB audit remain outside this document's scope.

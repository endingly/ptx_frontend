# SLCT Coverage

The frontend models every ordinary `slct` form in [PTX ISA 9.3 §9.7.6.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-slct). Its canonical descriptor is `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`. Parsing, owned resolution, and checking preserve the type and operand contract; the frontend does not execute the selection.

`slct{.ftz}.dtype.stype d, a, b, c` selects `a` when `c` is nonnegative and `b` otherwise. The selected data is copied bit-for-bit without conversion. A floating selector treats negative zero as zero, so it selects `a`; a NaN selector selects `b`. `.ftz` flushes subnormal values of `c` only, and is available only when `.stype` is `.f32`.

| Field | Supported values and operand form |
| --- | --- |
| `.dtype` | `.b16/.b32/.b64`, `.u16/.u32/.u64`, `.s16/.s32/.s64`, `.f32/.f64` |
| `.stype` | `.s32` or `.f32` |
| `d` | Register compatible with `.dtype` |
| `a`, `b` | Registers or numeric immediates compatible with `.dtype` |
| `c` | Register or numeric immediate compatible with `.stype` |

All four operands are required. Register compatibility follows the nominal `.dtype`: a `.b32` data type can use same-width floating or integer registers, whereas `.u32` data rejects an `.f32` register, and `.f32` data rejects an `.u32` register. The selector uses its own `.s32` or `.f32` type with the same-width register policy. The two generated public variants are `Slct::S32` and `Slct::F32`; the latter retains a typed optional `ftz` field. Both start at PTX 1.0. `.f64` selected data requires `sm_13` or higher, regardless of selector type.

The [C++ tests](../../submod/resolved_ir/test/test_slct_completeness.cpp) exercise all 22 type/selector combinations, literals, declared register containers, invalid modifiers and immediates, target boundaries, owned lifetime, and mutated public IR. The [Python spec tests](../../python/tests/spec/test_slct_completeness.py) check exact type domains, operand descriptors, and the `.f64` value gate. The installed [consumer](../../examples/conversion_consumer/main.cpp) uses both public variants and revalidates owned instructions.

In the 0.2.0 C++ package, the earlier fixed `Slct::U32S32` and `Slct::FtzU64F32` alternatives are replaced by `Slct::S32` and `Slct::F32`. Consumers should inspect `dtype.value` to distinguish selected data types and `ftz.value` in `Slct::F32`. `src_true`, `src_false`, and `selector` now carry `RegOrImm`; use `std::get_if<ResolvedRegisterRef>` or `std::get_if<ResolvedImmediate>` as appropriate. This changes the public source and binary contract; rebuild installed consumers with the matching headers and library.

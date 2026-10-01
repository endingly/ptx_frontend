# Tensor Memory register transfers

The frontend models PTX 9.3 `tcgen05.ld`, `tcgen05.st`, `tcgen05.ld.red`, and
`tcgen05.wait::{ld,st}` in the same canonical instruction category as Tensor
Memory allocation. These operations have distinct load and store completion
identities. A wait names prior matching operations issued by the executing
thread; the owned IR does not claim that an operation has already completed.

| Operation | Supported controls | Register fragment |
| --- | --- | --- |
| `ld` | Five data shapes; `.x1`–`.x128` subject to the table below; optional `.pack::16b` | Writable brace fragment of scalar 32-bit General registers |
| `st` | The same shapes and repeats; optional `.unpack::16b` | Readable brace fragment of scalar 32-bit General registers |
| `ld.red` | `.32x32b` or `.16x32bx2`; `.x2`–`.x128`; min/max; `.f32` with independent optional `.abs` and `.NaN`, or `.u32`/`.s32` without them | Writable brace fragment plus a separate scalar 32-bit reduction result |
| `wait` | Exactly `.wait::ld` or `.wait::st`, then `.sync.aligned` | No operands |

The shape/count contract follows the archived PTX 9.3 Tables 52 and 53.
`.32x32b`, `.16x64b`, and `.16x32bx2` use N registers for `.xN` through
`.x128`; `.16x128b` uses 2N registers through `.x64`; `.16x256b` uses 4N
registers through `.x32`. The 128-register ceiling is explicit for this
fragment kind. The omitted fragment maximum remains 64, and ordinary PTX
vectors retain their separate eight-element limit. Pack and unpack do not
change the register count.

```ptx
.version 9.0
.target sm_110a
.address_size 64
.entry kernel() {
  .reg .b32 %r<2>;
  .reg .b32 %taddr;
  .reg .u32 %red;
  tcgen05.ld.sync.aligned.32x32b.x2.b32 {%r0, %r1}, [%taddr];
  tcgen05.wait::ld.sync.aligned;
  tcgen05.ld.red.sync.aligned.32x32b.x2.min.u32 {%r0, %r1}, %red, [%taddr];
  tcgen05.wait::ld.sync.aligned;
  tcgen05.st.sync.aligned.32x32b.x2.b32 [%taddr], {%r0, %r1};
  tcgen05.wait::st.sync.aligned;
  ret;
}
```

`[taddr]` is a simple bracketed Tensor Memory address with the accepted
unsigned-32 use conversion; ordinary bracket offsets and byte-offset rules
are not admitted. The split `.16x32bx2` shape has a separate integer
`immHalfSplitoff`: trailing for load and reduction, between address and
fragment for store. Its owned value retains evaluated 64-bit source bits,
signed/unsigned source kind, and source location. The fixed ISA text does
not give this operand a use width, unit, range, or alignment, so the frontend
does not invent one or compute a second address. Once the AST is discarded,
structural source-kind corruption is detectable, while replacing one valid
source constant with another cannot be authenticated from that value alone.

The formal reduction spelling places `min` or `max` before the scalar type;
the documented example places the type first. Both tested orders resolve to
one typed identity and retain written modifier locations. This alias admits
no other permutations and no float controls on integer reductions.

Base load/store/wait require exact `sm_100a` from PTX 8.6, family `sm_100f`
from 8.8, or exact `sm_110a`/family `sm_110f` from 9.0. Reduction requires
family `sm_103f` from PTX 8.8 or family `sm_110f` from 9.0, with catalog
inheritance for qualified exact targets. Generic and unrelated targets do
not substitute for these introductions.

The frontend checks written forms, known register declarations, exact
fragment counts, source provenance, modifier domains, and target profiles.
Warp participation, uniform address values, allocation validity, matching
dynamic waits, and ordering through control flow remain runtime obligations.
There is no GPU execution or timing proof. Tensor Memory copy and shift, and
specialized commit/fence behavior, remain separate open contracts.

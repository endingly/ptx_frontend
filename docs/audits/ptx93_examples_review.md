# PTX 9.3 official Examples parser evidence

The reproducible bounded extraction selected **347 statement candidates from 192
instruction sections, spanning 117 opcode roots**: **340 parsed cleanly and 7
produced parser diagnostics**. The 7 diagnostics below involve malformed text in
the official examples; this batch establishes no confirmed frontend mismatch.
Clean parsing alone establishes neither valid PTX semantics nor target support.

Source: [NVIDIA PTX ISA 9.3, archived CUDA 13.3.0](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
The input `/tmp/ptx-isa-9.3-official.html` contains 3,577,575 bytes and has SHA-256
`40026f79b65fee3f8c8b3e717c6f80c6385e8d97215318d6e4b7f6106f58d5a4`.
The parser executable hash is recorded in
[ptx93_examples_results.json](ptx93_examples_results.json).

Reproduce from the repository root with the retained official HTML and the audit
parser executable built from revision `1b44ad8212aa254610b3afb447c9e774df03710d`:

```sh
python docs/audits/ptx93_example_probes.py /tmp/ptx-isa-9.3-official.html \
  --probe /tmp/ptx93-audit-build/ptx93_parse_probe \
  --output docs/audits/ptx93_examples_results.json
```

The stdlib-only utility derives opcode roots from chapter 9 instruction-heading
code tokens, then reads preformatted blocks owned by an instruction section and
introduced by its `Examples` rubric. It removes comments and splits on
semicolons. It keeps the first two eligible unique candidates per section, with a
400-candidate global cap. It skips declaration/block/label fragments, evident
placeholders, duplicate instruction text, and candidates beyond those limits.
The JSON retains all 1,152 candidates and their decisions, plus their complete
source blocks, section anchors, and HTML line numbers. No YAML descriptor or
frontend-generated inventory supplies the manual-side opcode names.

Each selected statement is copied without opcode or operand changes into:

```ptx
.version 9.3
.target sm_120a
.address_size 64
.entry audit(){
<statement>
}
```

The parser-only executable reports syntax/lowering diagnostics; no declarations
are invented to resolve the standalone operands. Each process has a three-second
timeout. All selected probes terminated normally. This wrapper deliberately does
not make family-specific matrix instructions semantically legal on sm_120a.

## Review of all seven diagnostics

| Official section and HTML block line | Exact problematic text | Assessment |
| --- | --- | --- |
| `logic-and-shift-instructions-or`, 14319 | `or.b32 mask mask,0x00010001` followed by `or.pred p,q,r;` | The first line lacks both the destination comma and terminating semicolon. Semicolon splitting consequently retains two lines in one candidate. The section's Syntax states `or.type d, a, b;`; this is malformed official example text, not a valid positive case. |
| `parallel-synchronization-and-communication-instructions-mbarrier-check-layout`, 25124 | `@!p bra exit` followed by `exit: ret;` | The branch lacks a terminating semicolon; the retained candidate includes the following label/return. This is an incomplete official fragment, not an isolated valid statement. |
| `parallel-synchronization-and-communication-instructions-tensormap-cp-fenceproxy`, 25179 | `@!p bra try_wait loop;` | The surrounding example declares `try_wait_loop:`. The branch instead has two whitespace-separated operand tokens without a comma. Treat as editorial inconsistency, without silently repairing it. |
| `parallel-synchronization-and-communication-instructions-clusterlaunchcontrol-query-cancel`, 25376 | `clusterlaunchcontrol.query_cancel.is_canceled pred.b128 p, handle;` | The opcode is split before `pred.b128`. The preceding `try_cancel` example at HTML 25312 writes `.is_canceled.pred.b128`, agreeing with the Syntax form. |
| Same section/block | `@p clusterlaunchcontrol.query_cancel.get_first_ctaid.v4.b32.b128 {xdim, ydim, zdim, ignr} handle;` | The vector result and handle lack a comma separator. The preceding `try_cancel` example at HTML 25318 has that comma. |
| `tcgen05-mma-instructions-mma-sp`, 40635 | `.collector::a:fill` | The example uses one colon before `fill`; the section's qualifier table at HTML 40566 specifies `.collector::a::fill`. Preserve as an editorial inconsistency. |
| `tcgen05-mma-instructions-mma-ws`, 40782 | `.collector::b2:use` | The example uses one colon before `use`; the section's Syntax at HTML 40678 specifies `.collector::buffer::op`. Preserve as an editorial inconsistency. |

These assessments compare the raw Examples text with nearby official Syntax,
qualifier tables, or surrounding labels/examples. They do not substitute repaired
inputs into the batch. A future corrected manual can be rerun directly.

## Limits

This is a finite source-based syntax sample, not a conformance percentage or a
new test contract. The 805 skipped candidates remain visible for review; some
valid instructions are excluded by conservative isolation or sampling. The
semicolon splitter intentionally preserves malformed source fragments, which
explains two diagnostic candidates containing multiple apparent statements.
Control flow, brace scopes, declarations, target selection, and operand bindings
from entire examples are not reconstructed. Semantic validation, complete form
coverage, intentionally invalid examples, and target-family availability need
separate evidence.

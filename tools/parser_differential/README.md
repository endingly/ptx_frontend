# Parser differential snapshots

This standalone test tool compares separate parser processes, using the fixed
pre-migration handwritten parser at
`2bd78d605f7fc545c11b77f42e9cd5bc7fe376a4` as the reference. It never links
the reference parser into production or supplies a production fallback.

From the repository root, reproduce the initial expression-backend comparison:

```sh
.venv/bin/python -m unittest discover -s tools/parser_differential -p test_run.py -v
.venv/bin/python tools/parser_differential/run.py \
  --candidate-revision df9140c92f6d53cb17869e6a918ab4e666be3fa0
```

The default compiler is `clang++-21`; `--compiler`, `--prefix`, and `--build-root`
override the local compiler, existing dependency prefix, and ignored output
directory. The prefix needs the project's `magic_enum` package; Flex, Bison 3.8+,
CMake 3.28+, Ninja, and C++23 are needed for the selected generated build.
Only common/base/lexer/CST/syntax targets are built. The production root CMake
configuration and heavy Resolved IR targets are not involved.

For the evolving full generated backend, omit `--candidate-revision` and add
`--generated`. This calls the private `detail::parseGenerated` entry, then
lowers that CST directly. The separately recorded public `PtxSyntaxParser`
facade continues to exercise whichever backend the public facade selects.
Without `--generated`, the candidate uses public `PtxCstParser` entries.
`--candidate-backend OFF` selects a handwritten candidate when that option
exists. `--candidate-source` selects another live source tree.
`--mode instruction` or `--mode module` explicitly narrows parser roots;
`--case-filter REGEX` selects case names. Both filters, the available count,
selected count and excluded count appear in the manifest/report. An empty
selection fails. The default compares every case.

Each commit archive is reconstructed below `out/parser-differential` and every
archived file is checked against its original bytes on reuse. Reference and
candidate builds have separate outputs and executable processes. Live candidate
source hashes include untracked driver/grammar sources; input changes during a
run cause failure. `manifest.json` records source/configuration/tool/corpus
identities, and `report.json` records counts, executed record types and failures.
Each mismatch saves both complete JSON snapshots for inspection. No EOF or other
canonicalization is currently applied.

Snapshots encode every explicitly listed public CST/AST field, variant index and
record type, pointer/optional presence, punctuation token ID, concrete token range
and its resolved source range, complete lexer and parsed token buffers, trivia,
source bytes, round-trip text, parse/lower/facade optional values, and ordered
diagnostics including `expected_kind`. Bytes outside printable ASCII use JSON
`\u00XX` escapes, retaining embedded NUL and invalid UTF-8. A startup schema check
rejects added/removed public fields until the explicit snapshot schema is updated.
It is a bounded check for the repository's current declarations, not a general
C++ parser. `_type` is reserved as the snapshot discriminator so public fields
named `type` remain independently observable.

`corpus.json` freezes source literals from CST/Syntax AST regressions at the
reference revision (names give their source file and line), expression and
malformed-expression regressions from the accepted expression prototype, and
explicit boundary/operand fixtures. Reference-version M9 files and lexer/CST
fuzz seeds are added from the fixed archive. Generated limit cases cover depth
128 expressions/initializers and depth 256 blocks, including over-limit prefixes.
Deterministic fuzz uses a recorded seed, 100 cases by default, arbitrary byte
inputs shorter than 96 bytes, and at most five mutations of bounded regression
seeds. `--seed` and `--fuzz-count` reproduce or extend that bounded sample.

Every snapshot runs in a fresh process with a 10-second timeout. Crashes,
timeouts, malformed JSON, and exact field differences are reported as failures
with a nonzero overall status. Passing cases establish equality for this corpus
and configuration, not universal parser equivalence. `observed_types` in the
report distinguishes executed records from serializer schema coverage.

The sensitivity tests invoke the actual CLI comparator and require status 1 for
nested token, trivia, recovery expectation, and source-range mutations. They also
prove schema omission detection and deterministic bounded fuzz. Compare saved
snapshots directly with:

```sh
.venv/bin/python tools/parser_differential/run.py \
  --compare-files reference.json candidate.json
```

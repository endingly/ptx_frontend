# Corpus tooling

`natural_emission.py` analyzes frozen PTX emitted by ordinary CUDA source.
`regenerate_nvcc.py` regenerates the frozen nvcc-backed fixtures, natural-emission
manifest, and provenance ledger while retaining their historical paths under
`corpus/`.

Run a non-mutating consistency check from any working directory with:

```sh
python /path/to/ptx_frontend/tools/corpus/regenerate_nvcc.py --check --nvcc /path/to/nvcc
```

The tool deliberately requires the recorded nvcc version before it writes
evidence, so newer or different compiler output cannot replace the fixtures by
accident.

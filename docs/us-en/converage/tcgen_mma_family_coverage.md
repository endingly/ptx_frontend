# TCGEN MMA frontend family coverage

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../../sdk_support.md#tcgen-mma-family-coverage). This page retains technical reference material and evidence.

[SDK support: TCGEN MMA frontend family coverage](../../sdk_support.md#tcgen-mma-family-coverage--tcgen-mma-frontend-family-coverage).

## Historical 101 source profiles and validation features

[SDK support: Historical 101 source profiles and validation features](../../sdk_support.md#tcgen-mma-family-coverage--historical-101-source-profiles-and-validation-features).


## Recorded implementation and tool evidence

These are historical observations for the stated configuration and fixtures.

The historical integration snapshot was checked with Clang 21 Debug, Ninja and the configured LLD driver path. Complete CTest passed **1,357/1,357** tests, including **64/64** focused `TcgenMma*` tests; the Python CI suite passed **518/518**. All **13/13** installed MMA consumers configured, built and ran; a Python 3.14 wheel build, install and resource smoke passed. The generated plan contains **101 entries, 4,530 forms, 5,663 operand layouts, 913 multi-layout forms and 390 outputs** (11 global and 379 across seven categories). `GeneratorJobsTests.test_parallel_artifacts_match_serial_bytes_and_stable_manifest` checks the project's parallel-write/manifest protocol with synthetic emitters; `TcgenDescriptorGenerationTests.test_emitted_tables_are_deterministic_and_cover_literal_rules` checks TCGEN descriptor rows. Neither is a claim of independent byte comparison of all 390 outputs.

The first resolved-IR build in the integration sequence took **543.24 s** wall time with **4,146,040 KiB** peak child RSS; after the final core repair, an incremental resolved-IR build took **197.11 s** and **2,755,800 KiB**. These are different cache states, not a clean-build speed comparison. The latter full target build then took **11.93 s**, and full CTest took **106.40 s**. The fixed `ptxas` **13.3.73** checkpoint matched **31/31 expected probe outcomes**: 19 new kind groups, eight collector/ashift/zero-column controls, two exact-103a sparse-MX cases and two selector-omission controls (30 accepts and one intentional reject). It was not rerun in the final build, did not exercise every target or register spelling, and did not run on a GPU. The in-repository [source tests](../../../submod/resolved_ir/test/test_tcgen_mma_sparse_mx.cpp), [known-row tests](../../../python/tests/spec/test_tcgen_mma_sparse_mx_contract.py) and [installed consumer](../../../examples/tcgen_mma_ws_sparse_consumer) provide independently rerunnable positive and negative frontend evidence.

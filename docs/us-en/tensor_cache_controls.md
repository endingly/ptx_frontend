# Tensor async cache controls

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#tensor-cache-controls). This page retains technical reference material and evidence.

[SDK support: Tensor async cache controls](../sdk_support.md#tensor-cache-controls--tensor-async-cache-controls).


## Recorded implementation and tool evidence

These are historical observations for the stated configuration and fixtures.

The direct-class Debug feasibility check used Clang 21, identical configuration
flags and one compiler worker for the largest changed Cp methods shard `_003`.
Current main versus this change measured source size 2,597,968 versus 4,077,717
bytes, object size 7,922,896 versus 10,386,304 bytes, wall time 6.5 versus
10.0 seconds, and peak compiler RSS 445,016 versus 521,224 KiB. Both completed
above a 1,572,864 KiB host MemAvailable guard. Existing dependencies and cache
state were reused, so these are bounded local measurements rather than clean
build timings. A separate patched-library continuation at four workers took
289.8 seconds, peaked at 5,370,380 KiB aggregate compiler RSS, and kept at
least 14,885,684 KiB host memory available without triggering the guard; it
is not a whole-build comparison with main. The historical private Cp partition
is unnecessary in the current direct-class model.

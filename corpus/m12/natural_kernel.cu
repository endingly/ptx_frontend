// SPDX-License-Identifier: MIT
// Ordinary CUDA source used as frozen compiler-emission evidence for M12.
// Keep this free of inline assembly and inline PTX.

extern "C" __global__ void natural_kernel(
    const int* input, int* output, int count) {
  const int index = static_cast<int>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (index < count) {
    output[index] = input[index] * 3 + index;
  }
}

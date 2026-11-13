// SPDX-License-Identifier: MIT
// Project-authored ordinary CUDA; no inline PTX.
extern "C" __global__ void warp_reduce(const float* input, float* output, int n) {
  unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
  float sum = 0;
  for (unsigned j = i; j < (unsigned)n; j += blockDim.x * gridDim.x)
    sum += input[j];
  for (int delta = 16; delta; delta >>= 1)
    sum += __shfl_down_sync(0xffffffff, sum, delta);
  if ((threadIdx.x & 31) == 0) atomicAdd(output, sum);
}

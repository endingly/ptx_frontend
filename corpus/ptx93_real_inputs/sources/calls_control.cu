// SPDX-License-Identifier: MIT
// Project-authored ordinary CUDA; no inline PTX.
__device__ __noinline__ float nonlinear(float x, int rounds) {
  for (int k = 0; k < rounds; ++k) {
    if (x > 1.0f) x = x / (1.0f + x);
    else if (x < -1.0f) x = -x * 0.5f;
    else x = x * x + 0.125f;
  }
  return x;
}
extern "C" __global__ void calls_control(const float* in, float* out,
                                         int n, int rounds) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = nonlinear(in[i], rounds);
}

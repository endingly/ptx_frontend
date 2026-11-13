// SPDX-License-Identifier: MIT
// Project-authored ordinary CUDA; no inline PTX.
extern "C" __global__ void histogram(const unsigned char* input,
                                     unsigned* output, unsigned n) {
  __shared__ unsigned bins[256];
  for (unsigned i = threadIdx.x; i < 256; i += blockDim.x) bins[i] = 0;
  __syncthreads();
  for (unsigned i = blockIdx.x * blockDim.x + threadIdx.x; i < n;
       i += blockDim.x * gridDim.x) atomicAdd(&bins[input[i]], 1u);
  __syncthreads();
  for (unsigned i = threadIdx.x; i < 256; i += blockDim.x)
    atomicAdd(&output[i], bins[i]);
}

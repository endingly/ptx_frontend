// SPDX-License-Identifier: MIT
// Project-authored ordinary CUDA; no inline PTX.
extern "C" __global__ void tiled_matmul(const float* a, const float* b,
                                        float* c, int n) {
  __shared__ float as[16][16], bs[16][16];
  int x = threadIdx.x, y = threadIdx.y;
  int row = blockIdx.y * 16 + y, col = blockIdx.x * 16 + x;
  float sum = 0;
  for (int k = 0; k < n; k += 16) {
    as[y][x] = row < n && k+x < n ? a[row*n+k+x] : 0;
    bs[y][x] = col < n && k+y < n ? b[(k+y)*n+col] : 0;
    __syncthreads();
    for (int j = 0; j < 16; ++j) sum += as[y][j] * bs[j][x];
    __syncthreads();
  }
  if (row < n && col < n) c[row*n+col] = sum;
}

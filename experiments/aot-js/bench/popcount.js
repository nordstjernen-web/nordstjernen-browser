function popcount(n) {
  let total = 0;
  for (let i = 0; i < n; i++) {
    let x = i;
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    x = (x + (x >> 4)) & 0x0f0f0f0f;
    total = (total + ((x * 0x01010101) >>> 24)) | 0;
  }
  return total | 0;
}

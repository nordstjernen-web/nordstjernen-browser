function prng(n) {
  let s = 123456789 | 0;
  let acc = 0;
  for (let i = 0; i < n; i++) {
    s ^= s << 13;
    s ^= s >>> 17;
    s ^= s << 5;
    acc = (acc + (s & 1023)) | 0;
  }
  return acc | 0;
}

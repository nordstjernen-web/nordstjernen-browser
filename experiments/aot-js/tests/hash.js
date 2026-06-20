function hash(n) {
  let h = 2166136261;
  for (let i = 0; i < n; i++) {
    h = h ^ i;
    h = (h * 16777619) | 0;
    h = h ^ (h >>> 13);
  }
  return h | 0;
}

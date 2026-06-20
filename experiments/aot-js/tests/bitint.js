function imulchain(x, y) {
  return Math.imul(x | 0, y | 0);
}
function clzof(x) {
  return Math.clz32(x >>> 0);
}
function hypot2(a, b) {
  return Math.hypot(a, b);
}
function murmur(x) {
  let h = x | 0;
  h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
  h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
  h = h ^ (h >>> 16);
  return (h >>> 0) % 1000;
}
function rotl(x, k) {
  let v = x | 0;
  return ((v << (k & 31)) | (v >>> (32 - (k & 31)))) | 0;
}

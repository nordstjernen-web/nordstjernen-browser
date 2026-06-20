function bitops(a, b) {
  let x = (a & b) | (a ^ b);
  let y = a << (b & 7);
  let z = a >> 2;
  let u = a >>> 1;
  let n = ~a;
  return x + y * 3 + z - u + n;
}

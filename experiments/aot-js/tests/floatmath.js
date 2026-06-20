function floatmath(n) {
  let x = 0.1;
  for (let i = 0; i < n; i++) { x = x + 0.1; x = x * 1.0000001; }
  let neg = -x;
  let m = (x > 3.0) ? x - 3.0 : 3.0 - x;
  return neg + m - (x % 2.5);
}

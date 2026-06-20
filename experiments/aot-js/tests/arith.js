function arith(a, b) {
  let x = a * b + a - b;
  let y = (a + 1) / (b + 1);
  let z = a % b;
  return x + y - z + (a ** 2);
}

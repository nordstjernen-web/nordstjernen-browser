function normarg(x) {
  x = x | 0;
  x = x + 5;
  return x * 2;
}
function comma(a, b) {
  let x = (a++, b++, a + b);
  return x;
}
function froundx(x) {
  return Math.fround(x * 1.1);
}
function froundacc(n) {
  let s = 0;
  for (let i = 0; i < n; i++) s = Math.fround(s + 0.1);
  return s;
}
function accumulate(a, n) {
  a = a | 0;
  for (let i = 0; i < n; i++) a = (a + i) | 0;
  return a;
}

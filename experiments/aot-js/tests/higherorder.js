function square(x) { return x * x; }
function applyloop(n) {
  let sum = 0;
  for (let i = 0; i < n; i++) sum = sum + square(i);
  return sum;
}
function timeit(func, n) {
  let sum = 0;
  for (let i = 0; i < n; i++) sum = sum + func(i);
  return sum;
}

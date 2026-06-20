function ternary(n) {
  let s = 0;
  for (let i = 0; i < n; i++) {
    s = s + (i % 2 === 0 ? i : -i);
  }
  return s > 0 ? s : -s;
}

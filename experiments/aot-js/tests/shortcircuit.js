function shortcircuit(a, b) {
  let r = 0;
  if (a > 0 && b > 0) r = r + 1;
  if (a > 100 || b > 100) r = r + 10;
  let t = (a > b) ? a - b : b - a;
  return r + t;
}

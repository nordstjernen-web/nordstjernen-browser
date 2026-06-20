function notop(a) {
  let r = !a;
  let s = !!(a - 1);
  return r * 2 + s;
}

function asum(a) {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i];
  return s;
}
function amax(a) {
  let m = -1e300;
  for (let i = 0; i < a.length; i++) if (a[i] > m) m = a[i];
  return m;
}
function dot(a, b) {
  let s = 0, n = a.length < b.length ? a.length : b.length;
  for (let i = 0; i < n; i++) s += a[i] * b[i];
  return s;
}
function norm(a) {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return Math.sqrt(s);
}
function blur(a) {
  let s = 0;
  for (let i = 1; i < a.length - 1; i++) s += (a[i-1] + a[i] + a[i+1]) / 3;
  return s;
}
function variance(a) {
  let n = a.length, mean = 0;
  for (let i = 0; i < n; i++) mean += a[i];
  mean = mean / n;
  let v = 0;
  for (let i = 0; i < n; i++) { let d = a[i] - mean; v += d * d; }
  return v / n;
}
function scale(a, k) {
  for (let i = 0; i < a.length; i++) a[i] = a[i] * k + 1;
  return a.length;
}
function axpy(a, b, k) {
  let n = a.length < b.length ? a.length : b.length;
  for (let i = 0; i < n; i++) a[i] = a[i] + b[i] * k;
  return 0;
}
function setall(a, v) {
  for (let i = 0; i < a.length; i++) a[i] = v;
  return a.length;
}
function clampall(a, lo, hi) {
  for (let i = 0; i < a.length; i++) {
    let x = a[i];
    a[i] = x < lo ? lo : (x > hi ? hi : x);
  }
  return 0;
}
function negate(a) {
  for (let i = 0; i < a.length; i++) a[i] = -a[i];
  return 0;
}
function cumsum(a) {
  let s = 0;
  for (let i = 0; i < a.length; i++) { s += a[i]; a[i] = s; }
  return s;
}

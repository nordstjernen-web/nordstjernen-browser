function mathfns(x) {
  let a = Math.sqrt(Math.abs(x));
  let b = Math.floor(x) + Math.ceil(x / 3) - Math.trunc(x / 2);
  let c = Math.sin(x) + Math.cos(x) + Math.tan(x / 10);
  let d = Math.min(x, 5, -2) + Math.max(x, 0);
  let e = Math.pow(x, 3) - Math.exp(x / 50) + Math.log2(Math.abs(x) + 1);
  let f = Math.sign(x) * Math.round(x / 4) + Math.atan2(x, 2);
  return a + b + c + d + e + f + Math.PI - Math.E;
}

function mathkernel(n) {
  let acc = 0.0;
  for (let i = 1; i < n; i++) {
    let x = i * 0.001;
    acc = acc + Math.sqrt(x) * Math.sin(x) + Math.cos(x / 2)
              - Math.abs(Math.tan(x / 7)) + Math.pow(x, 1.5)
              + Math.atan2(x, 2.0) - Math.log(x + 1);
  }
  return acc;
}

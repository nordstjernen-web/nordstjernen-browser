function mandel(n) {
  let count = 0;
  for (let py = 0; py < n; py++) {
    for (let px = 0; px < n; px++) {
      let x0 = px / n * 3.5 - 2.5;
      let y0 = py / n * 2.0 - 1.0;
      let x = 0.0;
      let y = 0.0;
      let iter = 0;
      while (x * x + y * y <= 4.0 && iter < 100) {
        let xt = x * x - y * y + x0;
        y = 2.0 * x * y + y0;
        x = xt;
        iter = iter + 1;
      }
      count = count + iter;
    }
  }
  return count;
}

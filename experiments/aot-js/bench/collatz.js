function collatz(n) {
  let count = 0;
  let total = 0;
  for (let i = 1; i < n; i++) {
    let x = i;
    while (x > 1) {
      if (x % 2 === 0) { x = x / 2; } else { x = 3 * x + 1; }
      count = count + 1;
    }
    total = total + count;
  }
  return total;
}

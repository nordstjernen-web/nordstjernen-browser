function dowhile(n) {
  let i = 0, acc = 0;
  do { acc = acc + i * i; i = i + 1; } while (i < n);
  return acc;
}

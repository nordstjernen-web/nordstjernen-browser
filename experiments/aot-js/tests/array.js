function array(n) {
  let a = [];
  for (let i = 0; i < n; i++) a.push(i * i);
  let s = 0;
  for (let i = 0; i < a.length; i++) s = s + a[i];
  return s;
}

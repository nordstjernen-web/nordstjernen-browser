function isEven(n) { if (n === 0) return 1; return isOdd(n - 1); }
function isOdd(n) { if (n === 0) return 0; return isEven(n - 1); }
function mutual(n) { return isEven(n) * 100 + isOdd(n); }

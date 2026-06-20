function cordic(runs) {
  let total = 0.0;
  for (let r = 0; r < runs; r++) {
    let X = 0.6072529350 * 65536.0;
    let Y = 0.0;
    let TargetAngle = 28.027 * 65536.0;
    let CurrAngle = 0.0;
    for (let step = 0; step < 12; step++) {
      let dx = X / 65536.0, dy = Y / 65536.0;
      if (TargetAngle > CurrAngle) {
        let NewX = X - (Y * (1.0 / (1 << step)));
        Y = (X * (1.0 / (1 << step))) + Y;
        X = NewX; CurrAngle = CurrAngle + 3000.0;
      } else {
        let NewX = X + (Y * (1.0 / (1 << step)));
        Y = -(X * (1.0 / (1 << step))) + Y;
        X = NewX; CurrAngle = CurrAngle - 3000.0;
      }
    }
    total = total + (X / 65536.0) * (Y / 65536.0);
  }
  return total;
}

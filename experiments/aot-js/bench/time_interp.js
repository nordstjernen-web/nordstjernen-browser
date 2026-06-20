import * as os from "qjs:os";
import * as std from "qjs:std";

const file = scriptArgs[1];
const arg = parseFloat(scriptArgs[2]);
const reps = parseInt(scriptArgs[3]);
const name = file.split("/").pop().replace(".js", "");

const src = std.loadFile(file);
const f = std.evalScript(src + "\n;" + name + ";");

f(arg);
const t0 = os.now();
let r = 0;
for (let k = 0; k < reps; k++) r = f(arg);
const t1 = os.now();
std.out.printf("%.17g\n", r);
std.err.printf("ms=%.3f\n", (t1 - t0) / 1000);

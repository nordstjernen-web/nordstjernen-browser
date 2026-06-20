// Real numeric kernels extracted verbatim from popular JS frameworks.
// The frameworks' object/DOM/string core is not AOT-eligible (it is declined
// and interpreted), but these compute-bound helpers are — and they run hot
// (easing per animation frame, lane math throughout React's scheduler).

// React — packages/react-reconciler/src/ReactFiberLane.js
function getHighestPriorityLane(lanes) { return lanes & -lanes; }
function pickArbitraryLaneIndex(lanes) { return 31 - Math.clz32(lanes); }
function includesSomeLane(a, b) { return (a & b) !== 0; }
function isSubsetOfLanes(set, subset) { return (set & subset) === subset; }
function mergeLanes(a, b) { return a | b; }
function removeLanes(set, subset) { return set & ~subset; }

// jQuery — jquery/src/effects/Tween.js (easing)
function swing(p) { return 0.5 - Math.cos(p * Math.PI) / 2; }

// Robert Penner / Framer Motion easing curves
function easeInOutQuad(t) { return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2; }
function easeOutCubic(t) { return 1 - Math.pow(1 - t, 3); }
function easeInOutCubic(t) { return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2; }

// interpolation (every animation/transition library)
function lerp(a, b, t) { return a + (b - a) * t; }
function clamp01(x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

// color-space math (style/theme computation, e.g. d3-color, polished)
function hue2rgb(p, q, t) {
  if (t < 0) t = t + 1;
  if (t > 1) t = t - 1;
  if (t < 1 / 6) return p + (q - p) * 6 * t;
  if (t < 1 / 2) return q;
  if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
  return p;
}

// Sebi's DAW mobile - shared canvas sizing and touch gestures (tap, long press, drag) for the arrange and piano-roll screens.
export function fitCanvas(cv) {
  const r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  const w = Math.max(1, Math.round(r.width * dpr)), hh = Math.max(1, Math.round(r.height * dpr));
  if (cv.width !== w || cv.height !== hh) { cv.width = w; cv.height = hh; }
  const g = cv.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { g, w: r.width, h: r.height };
}
// handlers: down(p) -> state | null, tap(p, state), longPress(p, state), drag(p, state, dx, dy), up(p, state), pinch(scale, cx)
export function gestures(cv, H) {
  const pts = new Map();
  let st = null, start = null, moved = false, lpTimer = 0, longDone = false, pinch = null;
  const pos = e => { const r = cv.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  cv.addEventListener('pointerdown', e => {
    cv.setPointerCapture(e.pointerId);
    pts.set(e.pointerId, pos(e));
    if (pts.size === 2) {   // two fingers: zoom
      clearTimeout(lpTimer); st = null;
      const [a, b] = [...pts.values()];
      pinch = { d: Math.hypot(a.x - b.x, a.y - b.y), cx: (a.x + b.x) / 2 };
      return;
    }
    if (pts.size > 2) return;
    start = pos(e); moved = false; longDone = false;
    st = H.down ? H.down(start) : {};
    clearTimeout(lpTimer);
    if (H.longPress) lpTimer = setTimeout(() => { if (!moved && st) { longDone = true; H.longPress(start, st); } }, 480);
  });
  cv.addEventListener('pointermove', e => {
    if (!pts.has(e.pointerId)) return;
    pts.set(e.pointerId, pos(e));
    if (pinch && pts.size === 2) {
      const [a, b] = [...pts.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y);
      if (H.pinch && pinch.d > 10) H.pinch(d / pinch.d, pinch.cx);
      pinch.d = d; return;
    }
    if (!st || longDone) return;
    const p = pos(e), dx = p.x - start.x, dy = p.y - start.y;
    if (!moved && Math.hypot(dx, dy) > 7) { moved = true; clearTimeout(lpTimer); }
    if (moved && H.drag) H.drag(p, st, dx, dy);
  });
  const end = e => {
    if (!pts.has(e.pointerId)) return;
    pts.delete(e.pointerId);
    if (pinch) { if (pts.size === 0) pinch = null; return; }
    clearTimeout(lpTimer);
    if (!st) return;
    const p = pos(e);
    if (!moved && !longDone && H.tap) H.tap(p, st);
    if (H.up) H.up(p, st, moved);
    st = null;
  };
  cv.addEventListener('pointerup', end);
  cv.addEventListener('pointercancel', end);
}

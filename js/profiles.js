// Выбор профилей для одной нарезки из profiles/a1.json (собирает profiles/build.mjs).
// Общий для фонового потока и проверочных скриптов bench/, чтобы оба резали одинаково.

export const LAYERS = ['0.08', '0.12', '0.16', '0.2', '0.24', '0.28'];
export const MATERIALS = ['pla', 'petg', 'abs', 'asa', 'flex', 'pa6gf'];

/**
 * Машина, процесс выбранной толщины с надстройками (поддержки всегда, стол
 * модели принтера, «strong» — заполнение и стенки) и пластик. Возвращает объекты профилей.
 */
export function pickProfiles(profiles, { layer = '0.2', material = 'pla', strong = false } = {}) {
  const key = normalizeLayer(layer);
  const proc = profiles.process[key];
  const fil = profiles.filament[material];
  if (!proc) throw new TypeError(`unknown layer "${layer}"`);
  if (!fil) throw new TypeError(`unknown material "${material}"`);
  const process = { ...proc, ...profiles.overrides.support, ...profiles.overrides.plate, ...(strong ? profiles.overrides.strong : {}) };
  return { machine: profiles.machine, process, filament: fil };
}

/** '0.20', 0.2 и '0.2' — одна толщина. */
export function normalizeLayer(layer) {
  const n = Number(layer);
  return LAYERS.find((l) => Math.abs(Number(l) - n) < 1e-9) ?? String(layer);
}

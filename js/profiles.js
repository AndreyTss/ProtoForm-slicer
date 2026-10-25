// Выбор профилей для одной нарезки из profiles/a1.json (собирает profiles/build.mjs).
// Общий для фонового потока и проверочных скриптов bench/, чтобы оба резали одинаково.

export const LAYERS = ['0.08', '0.12', '0.16', '0.2', '0.24', '0.28'];
export const MATERIALS = ['pla', 'petg', 'abs', 'asa', 'flex', 'pa6gf'];

/**
 * Машина, процесс выбранной толщины с надстройками (поддержки всегда, стол
 * модели принтера, «strong» — заполнение и стенки) и пластик. Возвращает объекты профилей.
 */
export function pickProfiles(profiles, { layer = '0.2', material = 'pla', strong = false, settings = null } = {}) {
  const key = normalizeLayer(layer);
  const proc = profiles.process[key];
  const fil = profiles.filament[material];
  if (!proc) throw new TypeError(`unknown layer "${layer}"`);
  if (!fil) throw new TypeError(`unknown material "${material}"`);
  const process = {
    ...proc,
    ...profiles.overrides.support,
    ...profiles.overrides.plate,
    ...(strong ? profiles.overrides.strong : {}),
    ...(settings ? settingsToProcess(settings) : {}),
  };
  return { machine: profiles.machine, process, filament: fil };
}

export const PATTERNS = ['grid', 'gyroid', 'cubic', 'triangles', 'honeycomb'];
const SUPPORTS = { tree: 'tree(auto)', normal: 'normal(auto)' };

/** Целое в границах; нечисло — ошибка, а не тихая подмена. */
function int(name, v, min, max) {
  const n = Number(v);
  if (!Number.isFinite(n)) throw new TypeError(`settings.${name}: not a number`);
  return String(Math.min(max, Math.max(min, Math.round(n))));
}

/**
 * Ручные настройки поверх профиля: ключи процесса Orca в её текстовом виде.
 * Толщина оболочки обнуляется, иначе Orca сама добавит слоёв до своей толщины
 * и заданное число слоёв сверху и снизу не сработает.
 */
export function settingsToProcess(s) {
  const out = {};
  if (s.walls != null) out.wall_loops = int('walls', s.walls, 1, 10);
  if (s.topLayers != null) {
    out.top_shell_layers = int('topLayers', s.topLayers, 0, 20);
    out.top_shell_thickness = '0';
  }
  if (s.bottomLayers != null) {
    out.bottom_shell_layers = int('bottomLayers', s.bottomLayers, 0, 20);
    out.bottom_shell_thickness = '0';
  }
  if (s.infill != null) out.sparse_infill_density = int('infill', s.infill, 0, 100) + '%';
  if (s.pattern != null) {
    if (!PATTERNS.includes(s.pattern)) throw new TypeError(`settings.pattern: unknown "${s.pattern}"`);
    out.sparse_infill_pattern = s.pattern;
  }
  if (s.supports != null) {
    if (s.supports === 'off') out.enable_support = '0';
    else if (SUPPORTS[s.supports]) {
      out.enable_support = '1';
      out.support_type = SUPPORTS[s.supports];
    } else throw new TypeError(`settings.supports: unknown "${s.supports}"`);
  }
  if (s.supportAngle != null) out.support_threshold_angle = int('supportAngle', s.supportAngle, 0, 90);
  if (s.plateOnly != null) out.support_on_build_plate_only = s.plateOnly ? '1' : '0';
  return out;
}

/** '0.20', 0.2 и '0.2' — одна толщина. */
export function normalizeLayer(layer) {
  const n = Number(layer);
  return LAYERS.find((l) => Math.abs(Number(l) - n) < 1e-9) ?? String(layer);
}

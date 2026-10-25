// Расход нити по ролям из G-code OrcaSlicer: сумма E по участкам «; FEATURE: …».
// Нужен для сверки: шапка G-code даёт только общий расход вместе с линией
// прочистки сопла, а движок считает без неё и делит деталь и поддержки.
//
// node gcode-roles.mjs plate_1.gcode [--density 1.24] [--diameter 1.75]
// Печатает JSON: граммы и мм по ролям, «деталь», «поддержки», «прочистка» (Custom).
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

// Роли, которые идут в поддержки: так же их делит мост (support_fills слоёв поддержки).
export const SUPPORT_ROLES = new Set(['Support', 'Support interface', 'Support transition']);
// Custom: стартовый и прочие G-code из профиля принтера, в том числе линия прочистки сопла.
export const CUSTOM_ROLES = new Set(['Custom']);

/**
 * Разбирает G-code и суммирует E по ролям так же, как GCodeProcessor в Orca
 * (process_G1, тип EMoveType::Extrude): в расход идут только ходы с X или Y
 * и положительным E. Откат и возврат нити без движения по XY и откаты при
 * протирке не считаются ни там, ни здесь. Учитывает M82/M83, G92 E и G2/G3.
 */
export function gcodeRoles(text) {
  let role = 'Custom';
  let relative = false; // M83 в стартовом коде A1, но не полагаемся на это
  let lastE = 0;
  const mm = new Map();
  let header = {};
  for (const raw of text.split('\n')) {
    const line = raw.trimEnd();
    if (line.startsWith(';')) {
      const f = /^; FEATURE: (.+)$/.exec(line) || /^;TYPE:(.+)$/.exec(line);
      if (f) { role = f[1].trim(); continue; }
      const h = /^; filament used \[(mm|g)\] = ([\d.]+)/.exec(line);
      if (h) header[h[1]] = +h[2];
      const t = /^; total filament used \[g\] = ([\d.]+)/.exec(line);
      if (t) header.totalG = +t[1];
      continue;
    }
    const code = line.split(';')[0].trim();
    if (!code) continue;
    const cmd = code.split(/\s+/)[0];
    if (cmd === 'M83') { relative = true; continue; }
    if (cmd === 'M82') { relative = false; continue; }
    if (cmd === 'G92') {
      const e = /\bE(-?[\d.]+)/.exec(code);
      if (e) lastE = +e[1];
      continue;
    }
    if (cmd !== 'G1' && cmd !== 'G0' && cmd !== 'G2' && cmd !== 'G3') continue;
    const e = /\bE(-?[\d.]+)/.exec(code);
    if (!e) continue;
    const v = +e[1];
    const d = relative ? v : v - lastE;
    if (!relative) lastE = v;
    if (d <= 0 || !/\b[XY]-?[\d.]/.test(code)) continue;
    mm.set(role, (mm.get(role) || 0) + d);
  }
  return { mm: Object.fromEntries(mm), header };
}

/** Миллиметры нити в граммы: сечение нити × плотность. */
export function mmToGrams(mm, density, diameter = 1.75) {
  return (mm * Math.PI * (diameter / 2) ** 2 * density) / 1000;
}

/** Итог по группам: деталь (всё, кроме поддержек и Custom), поддержки, прочистка. */
export function summarize(text, density, diameter = 1.75) {
  const { mm, header } = gcodeRoles(text);
  let part = 0, support = 0, custom = 0;
  for (const [r, v] of Object.entries(mm)) {
    if (SUPPORT_ROLES.has(r)) support += v;
    else if (CUSTOM_ROLES.has(r)) custom += v;
    else part += v;
  }
  const g = (x) => +mmToGrams(x, density, diameter).toFixed(3);
  return {
    partGrams: g(part), supportGrams: g(support), grams: g(part + support), customGrams: g(custom),
    totalMm: +(part + support + custom).toFixed(2), headerMm: header.mm, headerG: header.g,
    roles: Object.fromEntries(Object.entries(mm).map(([r, v]) => [r, g(v)])),
  };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const opt = (k, d) => { const i = args.indexOf(k); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
  const density = +opt('--density', '1.24');
  const diameter = +opt('--diameter', '1.75');
  for (const f of args) console.log(JSON.stringify({ file: f, ...summarize(readFileSync(f, 'utf8'), density, diameter) }));
}

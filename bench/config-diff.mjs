// Сравнивает блоки настроек (; CONFIG_BLOCK_START … END) двух G-code OrcaSlicer
// или G-code и выгрузки настроек движка (строки «key = value»).
// Нужен, чтобы убедиться: развёрнутые профили и слияние в мосте дают
// ровно ту конфигурацию, что и OrcaSlicer на компьютере.
//
// node config-diff.mjs <a.gcode|a.ini> <b.gcode|b.ini> [--ignore key1,key2]
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export function readConfig(text) {
  const start = text.indexOf('; CONFIG_BLOCK_START');
  const end = text.indexOf('; CONFIG_BLOCK_END');
  const block = start >= 0 && end > start ? text.slice(start, end) : text;
  const cfg = new Map();
  for (const line of block.split('\n')) {
    const m = /^;?\s*([a-z0-9_]+) = (.*)$/.exec(line.trimEnd());
    if (m) cfg.set(m[1], m[2]);
  }
  return cfg;
}

export function diffConfigs(a, b, ignore = new Set()) {
  const out = [];
  for (const k of new Set([...a.keys(), ...b.keys()])) {
    if (ignore.has(k)) continue;
    if (a.get(k) !== b.get(k)) out.push({ key: k, a: a.get(k), b: b.get(k) });
  }
  return out.sort((x, y) => x.key.localeCompare(y.key));
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const i = args.indexOf('--ignore');
  const ignore = new Set(i >= 0 ? args.splice(i, 2)[1].split(',') : []);
  const [fa, fb] = args;
  const d = diffConfigs(readConfig(readFileSync(fa, 'utf8')), readConfig(readFileSync(fb, 'utf8')), ignore);
  const cut = (s) => (s === undefined ? '(нет)' : s.length > 100 ? s.slice(0, 100) + '…' : s);
  for (const x of d) console.log(`${x.key}\n  a: ${cut(x.a)}\n  b: ${cut(x.b)}`);
  console.log(`различий: ${d.length}`);
  process.exitCode = d.length ? 1 : 0;
}

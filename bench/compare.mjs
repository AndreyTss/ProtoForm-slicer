// Сравнение движка с эталоном OrcaSlicer: граммы (всего, модель, поддержки) и время.
//
// node compare.mjs <engine.jsonl> <orca.jsonl> [--tol 1]
//
// Строки сопоставляются по имени модели без расширения, толщине, пластику и «strong».
// Порог --tol (процентов) проверяется по общим граммам; выход 1, если кто-то не прошёл.
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const i = args.indexOf('--tol');
const tol = i >= 0 ? Number(args.splice(i, 2)[1]) : 1;
const [engineFile, orcaFile] = args;
if (!engineFile || !orcaFile) {
  console.error('usage: node compare.mjs <engine.jsonl> <orca.jsonl> [--tol 1]');
  process.exit(2);
}

const read = (f) => readFileSync(f, 'utf8').split('\n').filter((l) => l.trim().startsWith('{')).map((l) => JSON.parse(l));
const key = (r) => `${r.file.replace(/\.[^.]+$/, '')}|${r.layer}|${r.material}|${Boolean(r.strong)}`;
const ref = new Map(read(orcaFile).map((r) => [key(r), r]));

const pct = (a, b) => (b ? ((a - b) / b) * 100 : a ? Infinity : 0);
const fmt = (x) => (Number.isFinite(x) ? (x >= 0 ? '+' : '') + x.toFixed(2) + '%' : '—');
const pad = (s, n) => String(s).padEnd(n);

let failed = 0, compared = 0, worst = 0;
console.log([pad('модель', 34), pad('движок г', 10), pad('Orca г', 10), pad('всего', 9), pad('модель', 9), pad('поддержки', 10), 'время'].join(''));
for (const e of read(engineFile)) {
  const r = ref.get(key(e));
  if (!r) { console.log(`${pad(e.file, 34)}нет эталона`); continue; }
  if (e.error || r.error) { console.log(`${pad(e.file, 34)}ошибка: ${e.error || r.error}`); failed++; continue; }
  compared++;
  const dTotal = pct(e.grams, r.grams);
  const dTime = e.seconds != null ? pct(e.seconds, r.seconds) : NaN;
  worst = Math.max(worst, Math.abs(dTotal));
  const ok = Math.abs(dTotal) <= tol;
  if (!ok) failed++;
  console.log([
    pad(e.file, 34), pad(e.grams.toFixed(2), 10), pad(r.grams.toFixed(2), 10), pad(fmt(dTotal), 9),
    pad(fmt(pct(e.partGrams, r.partGrams)), 9), pad(fmt(pct(e.supportGrams, r.supportGrams)), 10),
    fmt(dTime), ok ? '' : '  ← больше порога',
  ].join(''));
}
console.log(`сравнено ${compared}, не прошли ${failed}, худшее расхождение ${worst.toFixed(2)}% (порог ${tol}%)`);
process.exitCode = failed ? 1 : 0;

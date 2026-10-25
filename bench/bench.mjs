// Полная сверка: движок → подготовленные модели → OrcaSlicer → сравнение.
//
// node bench.mjs --engine <папка сборки> --orca <путь к orca-slicer> --out <папка>
//                [--layer 0.2] [--material pla] [--strong] [--tol 1] <модель>...
//
// В --out остаются engine.jsonl, orca.jsonl, prepared/ (модели после автоповорота)
// и G-code OrcaSlicer. Набор моделей передаётся параметрами (в репозитории их нет).
import { spawnSync } from 'node:child_process';
import { writeFileSync, mkdirSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
const flag = (k) => { const i = args.indexOf(k); if (i < 0) return false; args.splice(i, 1); return true; };

const engine = opt('--engine', null), orca = opt('--orca', null), out = opt('--out', null);
const layer = opt('--layer', '0.2'), material = opt('--material', 'pla'), tol = opt('--tol', '1');
const strong = flag('--strong');
const models = args;
if (!engine || !orca || !out || !models.length) {
  console.error('usage: node bench.mjs --engine <dir> --orca <exe> --out <dir> [--layer 0.2] [--material pla] [--strong] [--tol 1] <model>...');
  process.exit(2);
}
mkdirSync(out, { recursive: true });
const common = ['--layer', layer, '--material', material, ...(strong ? ['--strong'] : [])];
const prepared = path.join(out, 'prepared');

const node = (script, extra) => {
  const r = spawnSync(process.execPath, [path.join(here, script), ...extra], { encoding: 'utf8', maxBuffer: 256 << 20, stdio: ['ignore', 'pipe', 'inherit'] });
  if (r.status !== 0 && script !== 'compare.mjs') throw new Error(`${script} failed with ${r.status}`);
  return r;
};

const e = node('engine-run.mjs', ['--engine', engine, '--mode', 'both', '--prepared', prepared, ...common, ...models]);
writeFileSync(path.join(out, 'engine.jsonl'), e.stdout);
const preparedModels = readdirSync(prepared).filter((f) => f.endsWith('.stl')).map((f) => path.join(prepared, f));
const o = node('orca-ref.mjs', ['--orca', orca, '--out', out, '--arrange', '0', ...common, ...preparedModels]);
writeFileSync(path.join(out, 'orca.jsonl'), o.stdout);
const c = node('compare.mjs', [path.join(out, 'engine.jsonl'), path.join(out, 'orca.jsonl'), '--tol', tol]);
process.stdout.write(c.stdout);
process.exitCode = c.status;

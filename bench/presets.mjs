// Готовит три файла профилей для OrcaSlicer командной строки из profiles/a1.json:
// тот же набор настроек, который получает движок (поддержки включены,
// «прочная деталь» поверх профиля толщины).
//
// node presets.mjs <выходная папка> [--layer 0.2] [--material pla] [--strong] [--profiles ../profiles/a1.json]
// Печатает JSON с путями к machine.json, process.json, filament.json.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { pickProfiles } from '../js/profiles.js';

const here = path.dirname(fileURLToPath(import.meta.url));
export { pickProfiles };

export function writePresets(dir, set) {
  mkdirSync(dir, { recursive: true });
  const files = {};
  for (const kind of ['machine', 'process', 'filament']) {
    files[kind] = path.join(dir, `${kind}.json`);
    writeFileSync(files[kind], JSON.stringify(set[kind], null, 1));
  }
  return files;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const opt = (k, d) => { const i = args.indexOf(k); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
  const flag = (k) => { const i = args.indexOf(k); if (i < 0) return false; args.splice(i, 1); return true; };
  const profilesPath = opt('--profiles', path.join(here, '../profiles/a1.json'));
  const layer = opt('--layer', '0.2'), material = opt('--material', 'pla'), strong = flag('--strong');
  const [dir] = args;
  if (!dir) { console.error('укажите выходную папку'); process.exit(2); }
  const profiles = JSON.parse(readFileSync(profilesPath, 'utf8'));
  console.log(JSON.stringify(writePresets(dir, pickProfiles(profiles, { layer, material, strong }))));
}

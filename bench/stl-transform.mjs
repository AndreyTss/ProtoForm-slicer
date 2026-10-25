// Копии STL для проверочного набора: поворот вокруг X (детали, которым нужны
// поддержки) и масштаб (тяжёлые детали).
//
// node stl-transform.mjs rotate <in.stl> <out.stl> <градусы>
// node stl-transform.mjs scale  <in.stl> <out.stl> <множитель>
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

/** Треугольники STL (двоичного или текстового) как плоский массив координат. */
export function readStl(buf) {
  if (buf.length >= 84) {
    const n = buf.readUInt32LE(80);
    if (84 + n * 50 === buf.length) {
      const out = new Float32Array(n * 9);
      for (let i = 0; i < n; i++) for (let j = 0; j < 9; j++) out[i * 9 + j] = buf.readFloatLE(84 + i * 50 + 12 + j * 4);
      return out;
    }
  }
  const nums = [...buf.toString('latin1').matchAll(/vertex\s+(\S+)\s+(\S+)\s+(\S+)/g)].flatMap((m) => [+m[1], +m[2], +m[3]]);
  return new Float32Array(nums);
}

/** Двоичный STL из плоского массива координат; нормали нулевые — Orca их пересчитывает. */
export function writeStl(coords) {
  const n = coords.length / 9;
  const out = Buffer.alloc(84 + n * 50);
  out.writeUInt32LE(n, 80);
  for (let i = 0; i < n; i++) for (let j = 0; j < 9; j++) out.writeFloatLE(coords[i * 9 + j], 84 + i * 50 + 12 + j * 4);
  return out;
}

export function rotateX(coords, degrees) {
  const a = (degrees * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
  const out = new Float32Array(coords.length);
  for (let i = 0; i < coords.length; i += 3) {
    const y = coords[i + 1], z = coords[i + 2];
    out[i] = coords[i];
    out[i + 1] = y * c - z * s;
    out[i + 2] = y * s + z * c;
  }
  return out;
}

export function scale(coords, k) {
  return coords.map((v) => v * k);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const [op, src, dst, value] = process.argv.slice(2);
  if (!['rotate', 'scale'].includes(op) || !dst || value === undefined) {
    console.error('usage: node stl-transform.mjs rotate|scale <in.stl> <out.stl> <value>');
    process.exit(2);
  }
  const coords = readStl(readFileSync(src));
  writeFileSync(dst, writeStl(op === 'rotate' ? rotateX(coords, +value) : scale(coords, +value)));
}

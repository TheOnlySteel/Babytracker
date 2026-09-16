// Generates the PWA icons without any image dependency: a dark rounded square with a
// yellow circle and an ink "R". Run: node scripts/make-icons.mjs
import { deflateSync } from 'node:zlib';
import { writeFileSync, mkdirSync } from 'node:fs';

function crc32(buf) {
  let c, crc = 0xffffffff;
  for (let n = 0; n < buf.length; n++) {
    c = (crc ^ buf[n]) & 0xff;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    crc = (crc >>> 8) ^ c;
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function png(size, pixel) {
  const raw = Buffer.alloc((size * 3 + 1) * size);
  for (let y = 0; y < size; y++) {
    raw[y * (size * 3 + 1)] = 0;
    for (let x = 0; x < size; x++) {
      const [r, g, b] = pixel(x, y);
      const o = y * (size * 3 + 1) + 1 + x * 3;
      raw[o] = r; raw[o + 1] = g; raw[o + 2] = b;
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(size, 0); ihdr.writeUInt32BE(size, 4);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))
  ]);
}
const BG = [0x1b, 0x20, 0x30], YEL = [0xf5, 0xc8, 0x42], INK = [0x1b, 0x20, 0x30];
// Simple 5x7 "R" glyph
const R = ['1111.', '1...1', '1...1', '1111.', '1.1..', '1..1.', '1...1'];
function make(size, maskable) {
  const pad = maskable ? 0.2 : 0.08;
  const cx = size / 2, cy = size / 2, rad = size * (0.5 - pad);
  const gw = rad * 0.9, gh = rad * 1.1, cellW = gw / 5, cellH = gh / 7;
  const gx = cx - gw / 2, gy = cy - gh / 2;
  return png(size, (x, y) => {
    const d = Math.hypot(x + 0.5 - cx, y + 0.5 - cy);
    if (d > rad) return BG;
    const i = Math.floor((x - gx) / cellW), j = Math.floor((y - gy) / cellH);
    if (i >= 0 && i < 5 && j >= 0 && j < 7 && R[j][i] === '1') return INK;
    return YEL;
  });
}
mkdirSync('static/icons', { recursive: true });
writeFileSync('static/icons/icon-192.png', make(192, false));
writeFileSync('static/icons/icon-512.png', make(512, true));
writeFileSync('static/icons/apple-touch-icon.png', make(180, false));
console.log('icons written');

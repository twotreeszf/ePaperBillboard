/**
 * Write TTI1: uncompressed 1-bit image for LittleFS.
 * Header: 'TTI1' + uint16le width + uint16le height.
 * Pixels: row-major, MSB-left, 1=white, 0=black ink. Stride = (w+7)>>3.
 */

export function maskToI1(mask, width, height) {
  const stride = (width + 7) >> 3;
  const data = Buffer.alloc(8 + stride * height, 0xff);
  data.write("TTI1", 0, 4, "ascii");
  data.writeUInt16LE(width, 4);
  data.writeUInt16LE(height, 6);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      if (!mask[y * width + x]) {
        continue;
      }
      const o = 8 + y * stride + (x >> 3);
      data[o] &= ~(1 << (7 - (x & 7)));
    }
  }
  return data;
}

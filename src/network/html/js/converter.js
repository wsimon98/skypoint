/*
 * SkyPoint in-browser comic converter — image / CBZ / CBR -> XTC for the reader.
 *
 * The dithering, XTG/XTH page encoding, and XTC container layout are ported from
 * XTC.js (https://github.com/varo6/xtcjs) by varo6 & sodaFMR, itself derived
 * from cbz2xtc by tazua. MIT licensed; see CREDITS in the repo. The port is
 * vanilla JS (no build step) so it can be embedded in the firmware-served page
 * and run entirely in the connecting browser.
 *
 * Output modes:
 *   quality 'grayscale' -> XTCH container, 2-bit (4-level) XTH pages (default, best looking)
 *   quality 'bw'        -> XTC  container, 1-bit XTG pages (smaller / faster)
 * Readability is now handled on the device: each page is stored WHOLE at a higher
 * "detail" resolution (detail x device res). The reader shows the whole page in
 * portrait and pans sharp, zoomed horizontal bands in landscape (long-press to
 * rotate). detail=2 gives sharp landscape zoom; detail=1 = screen res (smaller files).
 *
 * Exposes window.SkyPointConvert with:
 *   imagesToXtc(files, opts) -> Promise<Blob>
 *   cbzToXtc(file, opts)     -> Promise<Blob>
 *   cbrToXtc(file, opts)     -> Promise<Blob>
 *   suggestName(srcName)     -> string
 * opts: { device:'X4'|'X3', quality:'grayscale'|'bw', detail:1|2,
 *         dither, contrast:0..1, grayscale:bool, splitWide:bool, mangaOrder:bool }
 */
(function () {
  'use strict';

  var DEVICE_DIMENSIONS = {
    X4: { width: 480, height: 800 },
    X3: { width: 528, height: 792 }
  };

  var DEFAULTS = {
    device: 'X4',
    quality: 'grayscale', // 'grayscale' (2-bit XTCH) | 'bw' (1-bit XTC)
    detail: 2,           // stored resolution multiplier (1 = screen res; 2 = 2x for sharp on-device landscape zoom)
    dither: 'floyd',     // 'floyd' | 'atkinson' | 'sierra-lite' | 'ordered' | 'none'
    contrast: 0.5,       // 0 disables; otherwise contrast stretch strength
    grayscale: true,     // convert to grey before dithering (preprocessing)
    splitWide: false,    // split a wide double-spread scan into two pages
    mangaOrder: false    // right-to-left reading order when splitting
  };

  var IMAGE_EXT = /\.(jpe?g|png|webp|gif|bmp|avif)$/i;

  // ---- N-level quantize + dithering (ported & generalized from xtcjs dithering.ts) ----
  // levels=2 -> {0,255} (1-bit); levels=4 -> {0,85,170,255} (2-bit grayscale)

  function quantizeToLevels(value, levels) {
    var step = 255 / (levels - 1);
    var q = Math.round(value / step) * step;
    return q < 0 ? 0 : (q > 255 ? 255 : q);
  }

  function applyThreshold(data, levels) {
    for (var i = 0; i < data.length; i += 4) {
      var v = quantizeToLevels(data[i], levels);
      data[i] = data[i + 1] = data[i + 2] = v;
    }
  }

  function diffuse(data, w, h, weights, divisor, levels) {
    var px = new Float32Array(w * h);
    var i;
    for (i = 0; i < px.length; i++) px[i] = data[i * 4];
    for (var y = 0; y < h; y++) {
      for (var x = 0; x < w; x++) {
        var idx = y * w + x;
        var oldP = px[idx];
        var newP = quantizeToLevels(oldP, levels);
        px[idx] = newP;
        var err = oldP - newP;
        for (var k = 0; k < weights.length; k++) {
          var dx = weights[k][0], dy = weights[k][1], wt = weights[k][2];
          var nx = x + dx, ny = y + dy;
          if (nx < 0 || nx >= w || ny >= h) continue;
          px[(ny * w + nx)] += err * wt / divisor;
        }
      }
    }
    for (i = 0; i < px.length; i++) {
      var val = px[i] < 0 ? 0 : (px[i] > 255 ? 255 : px[i]);
      data[i * 4] = data[i * 4 + 1] = data[i * 4 + 2] = val;
    }
  }

  // weight tuples: [dx, dy, weight]
  var FLOYD = [[1, 0, 7], [-1, 1, 3], [0, 1, 5], [1, 1, 1]];
  var SIERRA_LITE = [[1, 0, 2], [-1, 1, 1], [0, 1, 1]];
  var ATKINSON = [[1, 0, 1], [2, 0, 1], [-1, 1, 1], [0, 1, 1], [1, 1, 1], [0, 2, 1]];

  function applyOrdered(data, w, h, levels) {
    var bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]];
    var step = 255 / (levels - 1);
    for (var y = 0; y < h; y++) {
      for (var x = 0; x < w; x++) {
        var idx = (y * w + x) * 4;
        var bias = (bayer[y % 4][x % 4] / 16 - 0.5) * step;
        var val = quantizeToLevels(data[idx] + bias, levels);
        data[idx] = data[idx + 1] = data[idx + 2] = val;
      }
    }
  }

  function applyDithering(ctx, w, h, algorithm, levels) {
    var imageData = ctx.getImageData(0, 0, w, h);
    var data = imageData.data;
    switch (algorithm) {
      case 'none': applyThreshold(data, levels); break;
      case 'sierra-lite': diffuse(data, w, h, SIERRA_LITE, 4, levels); break;
      case 'atkinson': diffuse(data, w, h, ATKINSON, 8, levels); break;
      case 'ordered': applyOrdered(data, w, h, levels); break;
      case 'floyd':
      default: diffuse(data, w, h, FLOYD, 16, levels); break;
    }
    ctx.putImageData(imageData, 0, 0);
  }

  // ---- grayscale + contrast (ported from xtcjs processing/image.ts) ----

  function toGrayscale(ctx, w, h) {
    var imageData = ctx.getImageData(0, 0, w, h);
    var data = imageData.data;
    for (var i = 0; i < data.length; i += 4) {
      var g = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
      data[i] = data[i + 1] = data[i + 2] = g;
    }
    ctx.putImageData(imageData, 0, 0);
  }

  function applyContrast(ctx, w, h, level) {
    if (!level || level <= 0) return;
    var blackCutoff = 3 * level;
    var whiteCutoff = 3 + 9 * level;
    var imageData = ctx.getImageData(0, 0, w, h);
    var data = imageData.data;
    var hist = new Array(256).fill(0);
    var i;
    for (i = 0; i < data.length; i += 4) {
      var gray = Math.round(0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2]);
      hist[gray]++;
    }
    var total = w * h;
    var blackThr = total * blackCutoff / 100;
    var whiteThr = total * whiteCutoff / 100;
    var blackPoint = 0, whitePoint = 255, count = 0;
    for (i = 0; i < 256; i++) { count += hist[i]; if (count >= blackThr) { blackPoint = i; break; } }
    count = 0;
    for (i = 255; i >= 0; i--) { count += hist[i]; if (count >= whiteThr) { whitePoint = i; break; } }
    var range = whitePoint - blackPoint;
    if (range > 0) {
      for (i = 0; i < data.length; i += 4) {
        for (var c = 0; c < 3; c++) {
          var val = (data[i + c] - blackPoint) / range * 255;
          data[i + c] = val < 0 ? 0 : (val > 255 ? 255 : val);
        }
      }
    }
    ctx.putImageData(imageData, 0, 0);
  }

  // ---- canvas sizing (ported from xtcjs processing/canvas.ts) ----

  function newCanvas(w, h) {
    var c = document.createElement('canvas');
    c.width = w; c.height = h;
    return c;
  }

  function resizeWithPadding(src, targetW, targetH, padColor) {
    var result = newCanvas(targetW, targetH);
    var ctx = result.getContext('2d');
    ctx.fillStyle = 'rgb(' + padColor + ',' + padColor + ',' + padColor + ')';
    ctx.fillRect(0, 0, targetW, targetH);
    var scale = Math.min(targetW / src.width, targetH / src.height);
    var nw = Math.floor(src.width * scale);
    var nh = Math.floor(src.height * scale);
    var x = Math.floor((targetW - nw) / 2);
    var y = Math.floor((targetH - nh) / 2);
    ctx.drawImage(src, 0, 0, src.width, src.height, x, y, nw, nh);
    return result;
  }

  // ---- XTG page encoder (ported from xtcjs processing/xtg.ts, 1-bit path) ----

  function imageDataToXtg(imageData) {
    var w = imageData.width, h = imageData.height, data = imageData.data;
    var rowBytes = Math.ceil(w / 8);
    var pixelData = new Uint8Array(rowBytes * h);
    for (var y = 0; y < h; y++) {
      for (var x = 0; x < w; x++) {
        var idx = (y * w + x) * 4;
        if (data[idx] >= 128) {
          var byteIndex = y * rowBytes + (x >> 3);
          pixelData[byteIndex] |= 1 << (7 - (x % 8));
        }
      }
    }
    return buildPageBuffer('XTG', w, h, pixelData);
  }

  // ---- XTH page encoder (2-bit / 4-level grayscale) ----
  // Device decoder layout: two bit planes, column-major, columns RIGHT-TO-LEFT,
  // 8 vertical pixels per byte (MSB = topmost), planeSize = (w*h+7)/8.
  // pixelValue = (bit1<<1)|bit2; device levels: 0=White, 1=DarkGrey, 2=LightGrey, 3=Black.
  // Requires height % 8 == 0 (all our targets are: 800/480/792/528).

  // dithered gray {0,85,170,255} -> device pixel value.
  // gray/85 -> L: 0=black,1=dark,2=light,3=white ; pv: black=3,dark=1,light=2,white=0
  var PV_FROM_LEVEL = [3, 1, 2, 0];

  function imageDataToXth(imageData) {
    var w = imageData.width, h = imageData.height, data = imageData.data;
    var planeSize = (w * h + 7) >> 3;
    var colBytes = (h + 7) >> 3;
    var planes = new Uint8Array(planeSize * 2);
    for (var y = 0; y < h; y++) {
      var byteInCol = y >> 3;
      var bitInByte = 7 - (y & 7);
      var mask = 1 << bitInByte;
      for (var x = 0; x < w; x++) {
        var g = data[(y * w + x) * 4];
        var level = Math.round(g / 85);
        if (level < 0) level = 0; else if (level > 3) level = 3;
        var pv = PV_FROM_LEVEL[level];
        if (pv === 0) continue; // white: both bits 0
        var colIndex = w - 1 - x;
        var byteOffset = colIndex * colBytes + byteInCol;
        if (pv & 2) planes[byteOffset] |= mask;            // bit1 plane
        if (pv & 1) planes[planeSize + byteOffset] |= mask; // bit2 plane
      }
    }
    return buildPageBuffer('XTH', w, h, planes);
  }

  function buildPageBuffer(magic, width, height, pixelData) {
    var headerSize = 22;
    var buffer = new ArrayBuffer(headerSize + pixelData.length);
    var view = new DataView(buffer);
    var u8 = new Uint8Array(buffer);
    u8[0] = magic.charCodeAt(0);
    u8[1] = magic.charCodeAt(1);
    u8[2] = magic.charCodeAt(2);
    u8[3] = 0x00;
    view.setUint16(4, width, true);
    view.setUint16(6, height, true);
    view.setUint8(8, 0);
    view.setUint8(9, 0);
    view.setUint32(10, pixelData.length, true);
    // digest seed = first 8 bytes of pixel data
    for (var i = 0; i < Math.min(8, pixelData.length); i++) u8[14 + i] = pixelData[i];
    u8.set(pixelData, headerSize);
    return buffer;
  }

  // ---- XTC container (ported from xtcjs lib/xtc-format.ts, no-metadata path) ----

  function setBigUint64(view, offset, value) {
    var low = value >>> 0;
    var high = Math.floor(value / 4294967296) >>> 0;
    view.setUint32(offset, low, true);
    view.setUint32(offset + 4, high, true);
  }

  function buildXtcFromXtgPages(xtgBlobs, grayscale4) {
    var HEADER_BASE_SIZE = 48;
    var INDEX_ENTRY_SIZE = 16;
    var pageCount = xtgBlobs.length;
    var indexOffset = HEADER_BASE_SIZE;
    var dataOffset = indexOffset + pageCount * INDEX_ENTRY_SIZE;
    var totalSize = dataOffset;
    var b;
    for (b = 0; b < xtgBlobs.length; b++) totalSize += xtgBlobs[b].byteLength;

    var buffer = new ArrayBuffer(totalSize);
    var view = new DataView(buffer);
    var u8 = new Uint8Array(buffer);

    // 'XTC\0' (1-bit) or 'XTCH' (2-bit grayscale) — device derives bit depth from magic
    u8[0] = 0x58; u8[1] = 0x54; u8[2] = 0x43; u8[3] = grayscale4 ? 0x48 : 0x00;
    view.setUint16(4, 1, true);          // version
    view.setUint16(6, pageCount, true);
    view.setUint32(8, 0, true);          // flags low (no metadata)
    view.setUint32(12, 0, true);         // flags high
    setBigUint64(view, 16, 0);           // metadata offset
    setBigUint64(view, 24, indexOffset);
    setBigUint64(view, 32, dataOffset);
    setBigUint64(view, 40, 0);           // reserved

    var relOffset = dataOffset;
    for (b = 0; b < pageCount; b++) {
      var blob = xtgBlobs[b];
      var dv = new DataView(blob);
      var width = blob.byteLength >= 8 ? dv.getUint16(4, true) : 480;
      var height = blob.byteLength >= 8 ? dv.getUint16(6, true) : 800;
      var e = indexOffset + b * INDEX_ENTRY_SIZE;
      setBigUint64(view, e, relOffset);
      view.setUint32(e + 8, blob.byteLength, true);
      view.setUint16(e + 12, width, true);
      view.setUint16(e + 14, height, true);
      relOffset += blob.byteLength;
    }

    var writeOffset = dataOffset;
    for (b = 0; b < pageCount; b++) {
      u8.set(new Uint8Array(xtgBlobs[b]), writeOffset);
      writeOffset += xtgBlobs[b].byteLength;
    }
    return buffer;
  }

  // ---- decode source blob -> drawable bitmap ----

  function loadBitmap(blob) {
    if (typeof createImageBitmap === 'function') {
      return createImageBitmap(blob).catch(function () { return loadViaImg(blob); });
    }
    return loadViaImg(blob);
  }

  function loadViaImg(blob) {
    return new Promise(function (resolve, reject) {
      var url = URL.createObjectURL(blob);
      var img = new Image();
      img.onload = function () { URL.revokeObjectURL(url); resolve(img); };
      img.onerror = function () { URL.revokeObjectURL(url); reject(new Error('decode failed')); };
      img.src = url;
    });
  }

  // ---- per-page processing: bitmap -> one or more XTG pages ----

  function drawRegionToCanvas(bitmap, sx, sy, sw, sh) {
    var c = newCanvas(sw, sh);
    c.getContext('2d').drawImage(bitmap, sx, sy, sw, sh, 0, 0, sw, sh);
    return c;
  }

  function processCanvasToXtg(srcCanvas, opts, target) {
    var fitted = resizeWithPadding(srcCanvas, target.width, target.height, 255);
    var ctx = fitted.getContext('2d');
    var levels = opts.quality === 'bw' ? 2 : 4;
    if (opts.grayscale) toGrayscale(ctx, fitted.width, fitted.height);
    applyContrast(ctx, fitted.width, fitted.height, opts.contrast);
    applyDithering(ctx, fitted.width, fitted.height, opts.dither, levels);
    var imageData = ctx.getImageData(0, 0, fitted.width, fitted.height);
    return levels === 4 ? imageDataToXth(imageData) : imageDataToXtg(imageData);
  }

  function bitmapToPages(bitmap, opts, target) {
    var pages = [];
    var bw = bitmap.width, bh = bitmap.height;

    if (opts.splitWide && bw > bh) {
      var half = Math.floor(bw / 2);
      var left = drawRegionToCanvas(bitmap, 0, 0, half, bh);
      var right = drawRegionToCanvas(bitmap, half, 0, bw - half, bh);
      var first = opts.mangaOrder ? right : left;
      var second = opts.mangaOrder ? left : right;
      pages.push(processCanvasToXtg(first, opts, target));
      pages.push(processCanvasToXtg(second, opts, target));
    } else {
      pages.push(processCanvasToXtg(drawRegionToCanvas(bitmap, 0, 0, bw, bh), opts, target));
    }
    return pages;
  }

  // ---- natural sort for CBZ entry names ----

  function naturalCompare(a, b) {
    var ax = [], bx = [];
    a.replace(/(\d+)|(\D+)/g, function (_, n, s) { ax.push([n || Infinity, s || '']); });
    b.replace(/(\d+)|(\D+)/g, function (_, n, s) { bx.push([n || Infinity, s || '']); });
    while (ax.length && bx.length) {
      var an = ax.shift(), bn = bx.shift();
      var nn = (an[0] - bn[0]) || an[1].localeCompare(bn[1]);
      if (nn) return nn;
    }
    return ax.length - bx.length;
  }

  // ---- public API ----

  function resolveOpts(opts) {
    var o = {};
    for (var k in DEFAULTS) o[k] = DEFAULTS[k];
    if (opts) for (var j in opts) if (opts[j] !== undefined) o[j] = opts[j];
    var base = DEVICE_DIMENSIONS[o.device] || DEVICE_DIMENSIONS.X4;
    var d = o.detail | 0; if (d < 1) d = 1; if (d > 2) d = 2;
    o.detail = d;
    // store whole pages in portrait at detail x device res; height stays %8 (800/792 x2 still %8)
    o.target = { width: base.width * d, height: base.height * d };
    return o;
  }

  async function imagesToXtc(files, opts, onProgress) {
    var o = resolveOpts(opts);
    var pages = [];
    for (var i = 0; i < files.length; i++) {
      var bmp = await loadBitmap(files[i]);
      var p = bitmapToPages(bmp, o, o.target);
      for (var k = 0; k < p.length; k++) pages.push(p[k]);
      if (onProgress) onProgress(i + 1, files.length);
    }
    if (pages.length === 0) throw new Error('No images to convert.');
    return new Blob([buildXtcFromXtgPages(pages, o.quality !== 'bw')], { type: 'application/octet-stream' });
  }

  async function cbzToXtc(file, opts, onProgress) {
    if (typeof JSZip === 'undefined') throw new Error('JSZip not loaded.');
    var o = resolveOpts(opts);
    var zip = await JSZip.loadAsync(file);
    var names = [];
    zip.forEach(function (path, entry) {
      if (!entry.dir && IMAGE_EXT.test(path)) names.push(path);
    });
    names.sort(naturalCompare);
    if (names.length === 0) throw new Error('No images found in archive.');
    var pages = [];
    for (var i = 0; i < names.length; i++) {
      var blob = await zip.file(names[i]).async('blob');
      var bmp = await loadBitmap(blob);
      var p = bitmapToPages(bmp, o, o.target);
      for (var k = 0; k < p.length; k++) pages.push(p[k]);
      if (onProgress) onProgress(i + 1, names.length);
    }
    return new Blob([buildXtcFromXtgPages(pages, o.quality !== 'bw')], { type: 'application/octet-stream' });
  }

  async function cbrToXtc(file, opts, onProgress) {
    if (typeof SkyPointUnrar === 'undefined') throw new Error('RAR unpacker not loaded.');
    var o = resolveOpts(opts);
    var ab = await file.arrayBuffer();
    var entries = await SkyPointUnrar.extractImages(ab);
    if (!entries || entries.length === 0) throw new Error('No images found in archive.');
    entries.sort(function (a, b) { return naturalCompare(a.name, b.name); });
    var pages = [];
    for (var i = 0; i < entries.length; i++) {
      var blob = new Blob([entries[i].data]);
      var bmp = await loadBitmap(blob);
      var p = bitmapToPages(bmp, o, o.target);
      for (var k = 0; k < p.length; k++) pages.push(p[k]);
      if (onProgress) onProgress(i + 1, entries.length);
    }
    return new Blob([buildXtcFromXtgPages(pages, o.quality !== 'bw')], { type: 'application/octet-stream' });
  }

  function suggestName(srcName) {
    return String(srcName).replace(/\.[^.]+$/, '') + '.xtc';
  }

  window.SkyPointConvert = {
    imagesToXtc: imagesToXtc,
    cbzToXtc: cbzToXtc,
    cbrToXtc: cbrToXtc,
    suggestName: suggestName,
    IMAGE_EXT: IMAGE_EXT,
    DEVICE_DIMENSIONS: DEVICE_DIMENSIONS
  };
})();

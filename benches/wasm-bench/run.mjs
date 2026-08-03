// clay-codes vs @shelby-protocol/clay-codes, both as raw wasm under one Node
// runtime on one machine. Build the clay side first:
//
//   RUSTFLAGS="-C target-feature=+simd128" \
//     cargo build --release --target wasm32-unknown-unknown
//
// and put Shelby's package somewhere findable (npm pack @shelby-protocol/clay-codes):
//
//   node run.mjs --shelby /path/to/package/dist/clay.wasm

import { readFile } from 'fs/promises';
import { existsSync } from 'fs';

// config
const argv = process.argv.slice(2);
const argOf = (flag, dflt) => {
  const i = argv.indexOf(flag);
  return i >= 0 && argv[i + 1] ? argv[i + 1] : dflt;
};

const CLAY_WASM = argOf('--clay',
  new URL('./target/wasm32-unknown-unknown/release/clay_wasm_bench.wasm', import.meta.url).pathname);
const SHELBY_WASM = argOf('--shelby', './clay.wasm');

// Shelby's wasm only accepts n a power of two, q = d-k+1 a power of two, and a
// power-of-two chunk size.
const SHARED = [
  { k: 10, m: 6, d: 13, label: '(16,10,13) shelby prod' },
  { k: 8, m: 8, d: 11, label: '(16,8,11)' },
  { k: 8, m: 8, d: 15, label: '(16,8,15)' },
  { k: 4, m: 4, d: 7, label: '(8,4,7)' },
];

// clay-only shapes: tape production plus the Ceph-report configs.
const CLAY_ONLY = [
  { k: 7, m: 13, d: 16, label: '(20,7,16) tape prod' },
  { k: 4, m: 2, d: 5, label: '(6,4,5)' },
  { k: 6, m: 3, d: 8, label: '(9,6,8)' },
  { k: 10, m: 4, d: 13, label: '(14,10,13)' },
];

// Per-chunk sizes. Shelby's production chunk is 1 MiB (10 MiB chunkset / k=10).
const CHUNKS = [65536, 262144, 1048576, 4194304];

const REPS = 7;             // measured reps per cell; best is reported
const MIN_ITERS = 3;
const TARGET_MS = 120;      // per rep


// loaders
async function loadClay() {
  const mod = await WebAssembly.compile(await readFile(CLAY_WASM));
  const inst = await WebAssembly.instantiate(mod, {});
  return inst.exports;
}

const alignUp = (x, a) => (x + a - 1) & ~(a - 1);

async function loadShelby() {
  const mod = await WebAssembly.compile(await readFile(SHELBY_WASM));
  return async () => {
    const inst = await WebAssembly.instantiate(mod, {
      env: { abort: () => { throw new Error('shelby wasm abort'); }, js_msg: () => {} },
    });
    return inst.exports;
  };
}

/** Bump allocator over a wasm memory, growing as needed. */
function allocator(exp) {
  let heap = alignUp(exp.__heap_base.value, 64);
  return (size, align = 64) => {
    const ptr = alignUp(heap, align);
    heap = alignUp(ptr + size, 64);
    const need = heap - exp.memory.buffer.byteLength;
    if (need > 0) exp.memory.grow(Math.ceil(need / 65536) + 1);
    return ptr;
  };
}


// timing

/** Best-of-REPS mean nanoseconds per op, with the run's spread. */
function measure(fn) {
  // calibrate so one rep is ~TARGET_MS
  let iters = MIN_ITERS;
  for (;;) {
    const t0 = performance.now();
    for (let i = 0; i < iters; i++) fn();
    const ms = performance.now() - t0;
    if (ms >= TARGET_MS || iters > 1 << 22) break;
    iters = Math.max(iters + 1, Math.ceil(iters * Math.max(2, TARGET_MS / Math.max(ms, 0.05))));
  }
  for (let i = 0; i < Math.min(iters, 64); i++) fn(); // warm

  const perOp = [];
  for (let r = 0; r < REPS; r++) {
    const t0 = process.hrtime.bigint();
    for (let i = 0; i < iters; i++) fn();
    const t1 = process.hrtime.bigint();
    perOp.push(Number(t1 - t0) / iters);
  }
  perOp.sort((a, b) => a - b);
  return { ns: perOp[0], median: perOp[(REPS - 1) >> 1], worst: perOp[REPS - 1], iters };
}

const mbps = (bytes, ns) => (bytes / 1e6) / (ns / 1e9);

// arms
function clayArm(exp, { k, m, d }, chunk) {
  const rc = exp.setup(k, m, d, chunk);
  if (rc !== 0) return { ok: false, err: `setup rc=${rc}` };
  const v = exp.verify();
  if (v !== 0) return { ok: false, err: `verify rc=${v}` };
  return {
    ok: true,
    alpha: exp.info_alpha(),
    beta: exp.info_beta(),
    repairBytes: exp.info_repair_bytes(),
    encode: () => exp.encode_once(),
    decode: () => exp.decode_once(),
    repair: () => exp.repair_once(),
    noop: () => exp.noop(),
  };
}

/**
 * Shelby arm. `hash` picks the SHA-256 flag its own bindings hardcode to 1;
 * hash=0 isolates the coding work, hash=1 is what the shipped client does.
 */
async function shelbyArm(fresh, { k, m, d }, chunk, hash) {
  const n = k + m;
  const exp = await fresh();
  const alloc = allocator(exp);
  const paramsPtr = alloc(64, 64);
  try {
    exp.clay_params_init(paramsPtr, n, k, d, chunk, hash);
  } catch (e) {
    return { ok: false, err: 'params rejected' };
  }
  let encPtr, decPtr, active;
  try {
    encPtr = alloc(exp.clay_encoder_footprint(paramsPtr));
    exp.clay_encoder_init(encPtr, paramsPtr);
  } catch { return { ok: false, err: 'encoder init failed' }; }

  const src = alloc(chunk);
  const dst = alloc(chunk);
  const fill = (seed) => {
    const view = new Uint8Array(exp.memory.buffer, src, chunk);
    let x = seed >>> 0;
    for (let i = 0; i < chunk; i++) {
      x = (Math.imul(x, 1664525) + 1013904223) >>> 0;
      view[i] = x >>> 24;
    }
  };
  for (let i = 0; i < k; i++) { fill(0x9e3779b9 + i); exp.clay_encoder_set_data_chunk(encPtr, i, src); }
  exp.clay_encoder_run(encPtr);

  // keep the encoded chunks so the decoder can be staged from them
  const encoded = [];
  for (let i = 0; i < n; i++) {
    exp.clay_encoder_get_chunk(encPtr, i, dst);
    encoded.push(new Uint8Array(exp.memory.buffer, dst, chunk).slice());
  }

  // decoder with node 0 erased
  let decodeFn = null;
  try {
    decPtr = alloc(exp.clay_decoder_footprint(paramsPtr));
    const mask = 1 >>> 0;
    active = exp.clay_decoder_init_mds(decPtr, mask, paramsPtr) || decPtr;
    for (let i = 1; i < n; i++) {
      new Uint8Array(exp.memory.buffer, src, chunk).set(encoded[i]);
      exp.clay_decoder_set_slice(active, i, src, chunk);
    }
    exp.clay_decoder_run(active);
    exp.clay_decoder_get_chunk(active, 0, dst);
    const got = new Uint8Array(exp.memory.buffer, dst, chunk);
    const same = got.every((b, i) => b === encoded[0][i]);
    decodeFn = same ? () => exp.clay_decoder_run(active) : null;
    if (!same) process.stderr.write(`  [warn] shelby decode did not reproduce chunk 0 for k=${k} d=${d} chunk=${chunk}\n`);
  } catch (e) {
    decodeFn = null;
  }

  return {
    ok: true,
    encode: () => exp.clay_encoder_run(encPtr),
    decode: decodeFn,
    repair: null, // probed separately; set_slice takes a full chunk length
    encoded,
  };
}

/**
 * Does Shelby's decoder accept a short (beta sub-chunk) slice, i.e. is an MSR
 * repair reachable from the shipped client surface at all?
 */
async function probeShelbyRepair(fresh, { k, m, d }, chunk) {
  const n = k + m;
  const q = d - k + 1;
  const nu = n % q === 0 ? 0 : q - (n % q);
  const alpha = Math.pow(q, (n + nu) / q);
  const beta = alpha / q;
  const sub = chunk / alpha;
  const partial = beta * sub;

  const exp = await fresh();
  const alloc = allocator(exp);
  const p = alloc(64, 64);
  exp.clay_params_init(p, n, k, d, chunk, 1);
  const encPtr = alloc(exp.clay_encoder_footprint(p));
  exp.clay_encoder_init(encPtr, p);
  const src = alloc(chunk), dst = alloc(chunk);
  new Uint8Array(exp.memory.buffer, src, chunk).fill(7);
  for (let i = 0; i < k; i++) exp.clay_encoder_set_data_chunk(encPtr, i, src);
  exp.clay_encoder_run(encPtr);
  const enc0 = (() => { exp.clay_encoder_get_chunk(encPtr, 0, dst); return new Uint8Array(exp.memory.buffer, dst, chunk).slice(); })();

  const decPtr = alloc(exp.clay_decoder_footprint(p));
  const active = exp.clay_decoder_init_mds(decPtr, 1 >>> 0, p) || decPtr;
  try {
    // stage d helpers with only beta sub-chunks each
    for (let i = 1; i <= d; i++) exp.clay_decoder_set_slice(active, i, src, partial);
    exp.clay_decoder_run(active);
    exp.clay_decoder_get_chunk(active, 0, dst);
    const got = new Uint8Array(exp.memory.buffer, dst, chunk);
    const correct = got.every((b, i) => b === enc0[i]);
    return { accepted: true, correct, partial, full: chunk };
  } catch (e) {
    return { accepted: false, reason: String(e.message || e).slice(0, 60), partial, full: chunk };
  }
}

// report
const pad = (s, n) => String(s).padStart(n);
const fmt = (x, dp = 0) => (x == null ? '—' : x.toFixed(dp));
const KB = (b) => {
  if (b >= 1048576 && b % 1048576 === 0) return `${b / 1048576} MiB`;
  if (b % 1024 === 0) return `${b / 1024} KiB`;
  return `${(b / 1024).toFixed(1)} KiB`;
};

/** alpha for a shape, matching ClayCode::new. */
function alphaOf({ k, m, d }) {
  const n = k + m, q = d - k + 1;
  const nu = n % q === 0 ? 0 : q - (n % q);
  return Math.pow(q, (n + nu) / q);
}

/**
 * clay pads the object to a multiple of k*alpha*2, so a chunk is always a
 * multiple of alpha*2. Round the requested chunk up to the nearest legal one;
 * for every shared shape alpha is a power of two and this is the identity, so
 * the two arms stay on exactly the same geometry.
 */
function legalChunk(shape, target) {
  const g = alphaOf(shape) * 2;
  return Math.ceil(target / g) * g;
}

async function main() {
  if (!existsSync(CLAY_WASM)) {
    console.error(`clay wasm not found at ${CLAY_WASM} — build it first (see header).`);
    process.exit(1);
  }
  const haveShelby = existsSync(SHELBY_WASM);
  if (!haveShelby) console.error(`[note] shelby wasm not at ${SHELBY_WASM}; running the clay arm only.\n`);

  const clay = await loadClay();
  const fresh = haveShelby ? await loadShelby() : null;

  console.log(`node ${process.version}   clay=${CLAY_WASM.split('/').slice(-1)[0]}` +
    (haveShelby ? `   shelby=${SHELBY_WASM}` : ''));
  const nb = measure(() => clay.noop());
  console.log(`JS -> wasm call boundary (noop): ${nb.ns.toFixed(1)} ns\n`);

  if (haveShelby) {
    console.log('== Shelby repair-path probe (can a client ask for an MSR repair?) ==');
    for (const shape of SHARED.slice(0, 1)) {
      for (const chunk of [1048576]) {
        const r = await probeShelbyRepair(fresh, shape, chunk);
        console.log(`  ${shape.label} chunk=${KB(chunk)}: staging ${r.partial} B of ${r.full} B per helper -> ` +
          (r.accepted ? (r.correct ? 'accepted AND correct' : 'accepted but WRONG output') : `rejected (${r.reason})`));
      }
    }
    console.log();
  }

  for (const op of ['encode', 'decode', 'repair']) {
    // encode/decode move the whole object; a repair produces one chunk, so it is
    // rated on repaired-chunk bytes — the same convention as the Ceph report.
    const basis = op === 'repair' ? 'repaired-chunk bytes' : 'object bytes (k x chunk)';
    console.log(`== ${op} — MB/s of ${basis}, best of ${REPS} ==`);
    const head = `${pad('shape', 22)} ${pad('chunk', 9)} ${pad('object', 9)} ${pad('alpha', 6)} ` +
      `${pad('clay', 9)} ${pad('shelby h0', 10)} ${pad('shelby h1', 10)} ${pad('clay/h0', 8)}`;
    console.log(head);
    console.log('-'.repeat(head.length));

    for (const shape of [...SHARED, ...CLAY_ONLY]) {
      const shared = SHARED.includes(shape);
      for (const target of CHUNKS) {
        const chunk = legalChunk(shape, target);
        const object = shape.k * chunk;
        const a = clayArm(clay, shape, chunk);
        if (!a.ok) { console.log(`${pad(shape.label, 22)} ${pad(KB(chunk), 9)} clay: ${a.err}`); continue; }
        if (op === 'repair' && !a.repair) continue;

        const bytes = op === 'repair' ? chunk : object;
        const cm = measure(a[op]);
        const clayV = mbps(bytes, cm.ns);

        let h0 = null, h1 = null;
        if (haveShelby && shared) {
          for (const [hash, slot] of [[0, 'h0'], [1, 'h1']]) {
            const s = await shelbyArm(fresh, shape, chunk, hash);
            if (!s.ok || !s[op]) continue;
            const sm = measure(s[op]);
            const v = mbps(bytes, sm.ns);
            if (slot === 'h0') h0 = v; else h1 = v;
          }
        }
        console.log(
          `${pad(shape.label, 22)} ${pad(KB(chunk), 9)} ${pad(KB(object), 9)} ${pad(a.alpha, 6)} ` +
          `${pad(fmt(clayV), 9)} ${pad(fmt(h0), 10)} ${pad(fmt(h1), 10)} ` +
          `${pad(h0 ? (clayV / h0).toFixed(2) + 'x' : '—', 8)}`);
      }
    }
    console.log();
  }

  console.log('== repair read volume (clay MSR; Shelby\'s client wasm exposes no repair entry) ==');
  console.log(`${pad('shape', 22)} ${pad('chunk', 9)} ${pad('alpha', 6)} ${pad('beta', 6)} ` +
    `${pad('repair reads', 13)} ${pad('full decode', 12)} ${pad('ratio', 7)}`);
  for (const shape of [...SHARED, ...CLAY_ONLY]) {
    const chunk = legalChunk(shape, 1048576);
    const a = clayArm(clay, shape, chunk);
    if (!a.ok) continue;
    const full = shape.k * chunk;
    console.log(`${pad(shape.label, 22)} ${pad(KB(chunk), 9)} ${pad(a.alpha, 6)} ${pad(a.beta, 6)} ` +
      `${pad(a.repairBytes, 13)} ${pad(full, 12)} ${pad((a.repairBytes / full).toFixed(3), 7)}`);
  }
}

await main();

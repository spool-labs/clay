// SPDX-License-Identifier: Apache-2.0
//! wasm32 simd128 bench module for clay-codes, shaped to sit beside
//! `@shelby-protocol/clay-codes`' `clay.wasm` under one Node runtime.
//!
//! Both modules are raw wasm with a C ABI and no bindgen glue, so the JS side
//! loads and calls them identically and the boundary cost is the same on each.
//! `setup` builds the code and stages every input; the `*_once` entries each
//! perform exactly one operation on that state, matching Shelby's
//! `clay_encoder_run` / `clay_decoder_run`, which likewise run with their
//! inputs already staged. `noop` measures the call boundary itself so it can
//! be reported alongside the small-chunk cells where it is not negligible.

use clay_codes::ClayCode;
use std::cell::RefCell;

struct State {
    clay: ClayCode,
    /// k * chunk_size bytes of object payload
    object: Vec<u8>,
    /// the n encoded chunks, produced once during setup
    chunks: Vec<Vec<u8>>,
    /// per-helper repair partials for losing node 0, in minimum_to_repair order
    helpers: Vec<Option<Vec<u8>>>,
    chunk_size: usize,
    repair_bytes: usize,
}

thread_local! {
    static STATE: RefCell<Option<State>> = const { RefCell::new(None) };
}

/// Deterministic filler so encode has real data and runs are reproducible.
fn fill(len: usize) -> Vec<u8> {
    let mut v = vec![0u8; len];
    let mut x: u32 = 0x9e37_79b9;
    for b in v.iter_mut() {
        x = x.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        *b = (x >> 24) as u8;
    }
    v
}

fn checksum(bytes: &[u8]) -> u32 {
    // cheap, order-sensitive, and enough to keep the result alive
    let mut h: u32 = 2_166_136_261;
    for &b in bytes.iter().step_by(64) {
        h ^= b as u32;
        h = h.wrapping_mul(16_777_619);
    }
    h
}

/// Build the code and stage every input. `chunk_size` is bytes per chunk, so
/// the object is `k * chunk_size` — the same accounting Shelby's
/// `chunkSizeBytes` uses. Returns 0 on success, negative on error.
#[no_mangle]
pub extern "C" fn setup(k: u32, m: u32, d: u32, chunk_size: u32) -> i32 {
    let (k, m, d, chunk_size) = (k as usize, m as usize, d as usize, chunk_size as usize);

    let clay = match ClayCode::new(k, m, d) {
        Ok(c) => c,
        Err(_) => return -1,
    };

    // clay pads the object up to a multiple of k * alpha * 2; require the
    // caller to pick a chunk_size that lands exactly so both sides encode the
    // same per-chunk byte count with no hidden padding.
    if chunk_size % (clay.sub_chunk_no * 2) != 0 {
        return -2;
    }

    let object = fill(k * chunk_size);
    let chunks = clay.encode(&object);
    if chunks.len() != clay.n || chunks[0].len() != chunk_size {
        return -3;
    }

    // MSR repair inputs for losing node 0: each helper contributes beta
    // sub-chunks, concatenated in the order minimum_to_repair returns them.
    let available: Vec<usize> = (1..clay.n).collect();
    let helper_info = match clay.minimum_to_repair(0, &available) {
        Ok(h) => h,
        Err(_) => return -4,
    };
    let sub_chunk_size = chunk_size / clay.sub_chunk_no;
    let mut helpers: Vec<Option<Vec<u8>>> = vec![None; clay.n];
    let mut repair_bytes = 0usize;
    for (helper_idx, indices) in &helper_info {
        let mut partial = Vec::with_capacity(indices.len() * sub_chunk_size);
        for &sc in indices {
            partial.extend_from_slice(&chunks[*helper_idx][sc * sub_chunk_size..(sc + 1) * sub_chunk_size]);
        }
        repair_bytes += partial.len();
        helpers[*helper_idx] = Some(partial);
    }

    STATE.with(|s| {
        *s.borrow_mut() = Some(State { clay, object, chunks, helpers, chunk_size, repair_bytes });
    });
    0
}

/// One full encode of the staged object into n chunks.
#[no_mangle]
pub extern "C" fn encode_once() -> u32 {
    STATE.with(|s| {
        let b = s.borrow();
        let st = match b.as_ref() { Some(v) => v, None => return 0 };
        let out = st.clay.encode(&st.object);
        checksum(&out[st.clay.n - 1])
    })
}

/// One full reconstruction of the object with node 0 erased.
#[no_mangle]
pub extern "C" fn decode_once() -> u32 {
    STATE.with(|s| {
        let b = s.borrow();
        let st = match b.as_ref() { Some(v) => v, None => return 0 };
        let mut rows: Vec<Option<&[u8]>> = Vec::with_capacity(st.clay.n);
        rows.push(None);
        for c in st.chunks.iter().skip(1) {
            rows.push(Some(c.as_slice()));
        }
        match st.clay.decode_rows(&rows) {
            Ok(obj) => checksum(&obj),
            Err(_) => 0,
        }
    })
}

/// One MSR repair of node 0 from the staged helper partials.
#[no_mangle]
pub extern "C" fn repair_once() -> u32 {
    STATE.with(|s| {
        let b = s.borrow();
        let st = match b.as_ref() { Some(v) => v, None => return 0 };
        let rows: Vec<Option<&[u8]>> = st.helpers.iter().map(|h| h.as_deref()).collect();
        match st.clay.repair_rows(0, &rows, st.chunk_size) {
            Ok(c) => checksum(&c),
            Err(_) => 0,
        }
    })
}

/// Empty call: measures the JS -> wasm boundary cost on this side.
#[no_mangle]
pub extern "C" fn noop() -> u32 {
    0
}

/// Round-trip gate: repair(node 0) and decode(node 0 erased) must both
/// reproduce what encode produced. Returns 0 when both hold.
#[no_mangle]
pub extern "C" fn verify() -> u32 {
    STATE.with(|s| {
        let b = s.borrow();
        let st = match b.as_ref() { Some(v) => v, None => return 1 };

        let rows: Vec<Option<&[u8]>> = st.helpers.iter().map(|h| h.as_deref()).collect();
        let repaired = match st.clay.repair_rows(0, &rows, st.chunk_size) {
            Ok(c) => c,
            Err(_) => return 2,
        };
        if repaired != st.chunks[0] {
            return 3;
        }

        let mut drows: Vec<Option<&[u8]>> = Vec::with_capacity(st.clay.n);
        drows.push(None);
        for c in st.chunks.iter().skip(1) {
            drows.push(Some(c.as_slice()));
        }
        let decoded = match st.clay.decode_rows(&drows) {
            Ok(o) => o,
            Err(_) => return 4,
        };
        if decoded[..st.object.len()] != st.object[..] {
            return 5;
        }
        0
    })
}

#[no_mangle]
pub extern "C" fn info_alpha() -> u32 {
    STATE.with(|s| s.borrow().as_ref().map_or(0, |st| st.clay.sub_chunk_no as u32))
}

#[no_mangle]
pub extern "C" fn info_beta() -> u32 {
    STATE.with(|s| s.borrow().as_ref().map_or(0, |st| st.clay.beta as u32))
}

#[no_mangle]
pub extern "C" fn info_chunk_size() -> u32 {
    STATE.with(|s| s.borrow().as_ref().map_or(0, |st| st.chunk_size as u32))
}

/// Bytes read from helpers for one MSR repair — the network-traffic figure.
#[no_mangle]
pub extern "C" fn info_repair_bytes() -> u32 {
    STATE.with(|s| s.borrow().as_ref().map_or(0, |st| st.repair_bytes as u32))
}

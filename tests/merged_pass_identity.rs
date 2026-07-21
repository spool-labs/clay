//! Decode and repair batch each pass's Reed-Solomon calls into one call per
//! contiguous run of planes. That is only sound because the planes in a pass
//! are independent, share one erasure set, and RS over GF(2^8) is byte-column
//! independent. These tests pin the observable result: decode must return the
//! original data and repair must return the encoded chunk, byte for byte.
//!
//! Run under `cargo test` (debug), so the `debug_assert!` guarding the
//! merge precondition is live.
use std::collections::HashMap;

use clay_codes::ClayCode;

/// (k, m, d) covering alpha from 8 to 256, plus the production shape
const CONFIGS: &[(usize, usize, usize)] = &[(4, 2, 5), (6, 3, 8), (10, 4, 13), (7, 13, 16)];
const SIZES: &[usize] = &[1024, 10 * 1024];

fn payload(size: usize) -> Vec<u8> {
    (0..size).map(|i| ((i * 31 + 7) % 251) as u8).collect()
}

#[test]
fn decode_recovers_payload_for_every_erasure_pattern() {
    for &(k, m, d) in CONFIGS {
        let clay = ClayCode::new(k, m, d).unwrap();
        for &size in SIZES {
            let data = payload(size);
            let chunks = clay.encode(&data);

            for n_lost in 1..=m {
                for start in 0..clay.n {
                    let lost: Vec<usize> = (0..n_lost).map(|i| (start + i) % clay.n).collect();
                    let available: HashMap<usize, Vec<u8>> = chunks
                        .iter()
                        .enumerate()
                        .filter(|(i, _)| !lost.contains(i))
                        .map(|(i, c)| (i, c.clone()))
                        .collect();

                    let got = clay.decode(&available, &lost).unwrap();
                    assert_eq!(
                        &got[..size],
                        &data[..],
                        "decode diverged: ({},{},{}) size={size} lost={lost:?}",
                        clay.n,
                        clay.k,
                        clay.d
                    );

                    // the slice entry point must agree with the map one on
                    // every pattern, not just compile
                    let mut rows: Vec<Option<&[u8]>> = vec![None; clay.n];
                    for (i, chunk) in chunks.iter().enumerate() {
                        if !lost.contains(&i) {
                            rows[i] = Some(chunk.as_slice());
                        }
                    }
                    let got_rows = clay.decode_rows(&rows).unwrap();
                    assert_eq!(
                        got_rows, got,
                        "decode_rows diverged from decode: ({},{},{}) size={size} lost={lost:?}",
                        clay.n, clay.k, clay.d
                    );
                }
            }
        }
    }
}

#[test]
fn repair_reproduces_the_encoded_chunk_for_every_lost_node() {
    for &(k, m, d) in CONFIGS {
        let clay = ClayCode::new(k, m, d).unwrap();
        for &size in SIZES {
            let data = payload(size);
            let chunks = clay.encode(&data);
            let chunk_size = chunks[0].len();
            let sub_chunk_size = chunk_size / clay.sub_chunk_no;

            for lost in 0..clay.n {
                let available: Vec<usize> = (0..clay.n).filter(|&i| i != lost).collect();
                let helper_info = clay.minimum_to_repair(lost, &available).unwrap();

                let mut partial: HashMap<usize, Vec<u8>> = HashMap::new();
                for (helper, indices) in &helper_info {
                    let mut buf = Vec::with_capacity(indices.len() * sub_chunk_size);
                    for &sc in indices {
                        buf.extend_from_slice(
                            &chunks[*helper][sc * sub_chunk_size..(sc + 1) * sub_chunk_size],
                        );
                    }
                    partial.insert(*helper, buf);
                }

                let repaired = clay.repair(lost, &partial, chunk_size).unwrap();
                assert_eq!(
                    repaired, chunks[lost],
                    "repair diverged: ({},{},{}) size={size} lost={lost}",
                    clay.n, clay.k, clay.d
                );

                // the slice entry point must agree with the map one
                let mut rows: Vec<Option<&[u8]>> = vec![None; clay.n];
                for (helper, buf) in &partial {
                    rows[*helper] = Some(buf.as_slice());
                }
                let repaired_rows = clay.repair_rows(lost, &rows, chunk_size).unwrap();
                assert_eq!(
                    repaired_rows, repaired,
                    "repair_rows diverged from repair: ({},{},{}) size={size} lost={lost}",
                    clay.n, clay.k, clay.d
                );
            }
        }
    }
}

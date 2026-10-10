//! Encoding logic for Clay codes
//!
//! This module handles encoding data into Clay code chunks.

use crate::decode::{decode_layered, RsCodec};
use crate::error::ClayError;

/// Parameters needed for encoding
pub struct EncodeParams {
    pub k: usize,
    pub m: usize,
    pub n: usize,
    pub q: usize,
    pub t: usize,
    pub nu: usize,
    pub sub_chunk_no: usize,
    pub original_count: usize,
}

/// Bytes in each chunk an encode of `data_len` bytes produces
///
/// The 2-byte sub-chunk floor sets the padded length and therefore the
/// encoded layout, so it is part of the wire format and must not change.
pub fn chunk_size(params: &EncodeParams, data_len: usize) -> usize {
    let min_sub_chunk_size = 2;
    let min_size = params.k * params.sub_chunk_no * min_sub_chunk_size;
    let padded_len = if data_len == 0 {
        min_size
    } else {
        data_len.div_ceil(min_size) * min_size
    };
    padded_len / params.k
}

/// Encode data into n chunks
///
/// # Parameters
/// - `params`: Encoding parameters from ClayCode
/// - `rs`: Reed-Solomon codec built for these parameters
/// - `data`: Raw data bytes to encode
///
/// # Returns
/// Vector of n chunks, each containing alpha sub-chunks
pub fn encode(params: &EncodeParams, rs: &RsCodec, data: &[u8]) -> Vec<Vec<u8>> {
    let size = chunk_size(params, data.len());
    let mut chunks: Vec<Vec<u8>> = vec![vec![0u8; size]; params.n];
    let mut rows: Vec<&mut [u8]> = chunks.iter_mut().map(|chunk| chunk.as_mut_slice()).collect();
    // The rows are n buffers of the size encode_into asks for, so it cannot refuse them
    encode_into(params, rs, data, &mut rows).expect("Encode failed: this indicates a bug in ClayCode");
    chunks
}

/// Encode data into n caller-owned chunks of `chunk_size(params, data.len())` bytes each
///
/// The first k chunks take the data with its zero padding, the last m take the
/// parity. Every byte of every chunk is written, so the buffers may hold
/// anything on the way in.
pub fn encode_into(
    params: &EncodeParams,
    rs: &RsCodec,
    data: &[u8],
    chunks: &mut [&mut [u8]],
) -> Result<(), ClayError> {
    let size = chunk_size(params, data.len());
    if chunks.len() != params.n {
        return Err(ClayError::InvalidParameters(format!(
            "encode needs {} chunks, got {}",
            params.n,
            chunks.len()
        )));
    }
    for chunk in chunks.iter() {
        if chunk.len() != size {
            return Err(ClayError::InvalidChunkSize {
                expected: size,
                actual: chunk.len(),
            });
        }
    }
    let sub_chunk_size = size / params.sub_chunk_no;
    let total_nodes = params.q * params.t; // k + m + nu
    let parity_start = params.k + params.nu;

    // The data goes straight into the first k chunks; the zeroed tail is the padding
    let (data_chunks, parity_chunks) = chunks.split_at_mut(params.k);
    for (index, row) in data_chunks.iter_mut().enumerate() {
        let start = (index * size).min(data.len());
        let end = (start + size).min(data.len());
        row[..end - start].copy_from_slice(&data[start..end]);
        row[end - start..].fill(0);
    }

    // Data rows are available, shortened nodes share one known-zero row, and
    // parity rows are left as erasures for the layered decoder to fill in
    let zero_row = match params.nu {
        0 => Vec::new(),
        _ => vec![0u8; size],
    };
    let mut available_rows: Vec<Option<&[u8]>> = vec![None; total_nodes];
    for (index, row) in data_chunks.iter().enumerate() {
        available_rows[index] = Some(&**row);
    }
    for row in available_rows.iter_mut().take(parity_start).skip(params.k) {
        *row = Some(&zero_row);
    }

    let mut erased_rows: Vec<&mut [u8]> = Vec::with_capacity(total_nodes);
    for _ in 0..parity_start {
        erased_rows.push(&mut []);
    }
    for row in parity_chunks.iter_mut() {
        row.fill(0);
        erased_rows.push(&mut **row);
    }

    // Encode by treating parity computation as recovery
    decode_layered(params, rs, &available_rows, &mut erased_rows, sub_chunk_size)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_params() -> EncodeParams {
        // (4, 2, 5) configuration
        EncodeParams {
            k: 4,
            m: 2,
            n: 6,
            q: 2,
            t: 3,
            nu: 0,
            sub_chunk_no: 8,
            original_count: 4,
        }
    }

    fn test_rs(params: &EncodeParams) -> RsCodec {
        RsCodec::new(params.original_count, params.m).expect("test params should build a codec")
    }

    // encoding yields exactly n chunks
    #[test]
    fn chunk_count() {
        let params = test_params();
        let rs = test_rs(&params);

        let chunks = encode(&params, &rs, b"Test data for encoding");

        assert_eq!(chunks.len(), params.n);
    }

    // empty input still produces n equally sized chunks
    #[test]
    fn empty_data() {
        let params = test_params();
        let rs = test_rs(&params);

        let chunks = encode(&params, &rs, &[]);

        assert_eq!(chunks.len(), params.n);
        let chunk_size = chunks[0].len();
        for chunk in &chunks {
            assert_eq!(chunk.len(), chunk_size);
        }
    }

    // chunk sizes divide evenly into sub-chunks
    #[test]
    fn chunk_alignment() {
        let params = test_params();
        let rs = test_rs(&params);

        let chunks = encode(&params, &rs, &vec![0xABu8; 100]);

        for chunk in &chunks {
            assert_eq!(chunk.len() % params.sub_chunk_no, 0);
        }
    }
}

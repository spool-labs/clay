//! Encoding logic for Clay codes
//!
//! This module handles encoding data into Clay code chunks.

use crate::decode::{decode_layered, RsCodec};

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
    // Calculate chunk size: must be divisible by (k * sub_chunk_no)
    // Also ensure sub_chunk_size >= 2 bytes (reed-solomon-erasure requirement)
    let min_sub_chunk_size = 2;
    let min_size = params.k * params.sub_chunk_no * min_sub_chunk_size;
    let padded_len = if data.is_empty() {
        min_size
    } else {
        let aligned = ((data.len() + min_size - 1) / min_size) * min_size;
        aligned.max(min_size)
    };
    let chunk_size = padded_len / params.k;
    let sub_chunk_size = chunk_size / params.sub_chunk_no;

    let total_nodes = params.q * params.t; // k + m + nu
    let parity_start = params.k + params.nu;

    // Owned data rows for the first k nodes; the zeroed tail is the padding
    let mut data_rows: Vec<Vec<u8>> = vec![vec![0u8; chunk_size]; params.k];
    for i in 0..params.k {
        let start = i * chunk_size;
        if start >= data.len() {
            break;
        }
        let end = data.len().min(start + chunk_size);
        data_rows[i][..end - start].copy_from_slice(&data[start..end]);
    }

    // Data rows are available, shortened nodes share one known-zero row, and
    // parity rows are left as erasures for the layered decoder to fill in
    let zero_row = vec![0u8; chunk_size];
    let mut available_rows: Vec<Option<&[u8]>> = vec![None; total_nodes];
    for i in 0..params.k {
        available_rows[i] = Some(&data_rows[i]);
    }
    for i in params.k..parity_start {
        available_rows[i] = Some(&zero_row);
    }

    let mut erased_rows: Vec<Vec<u8>> = Vec::with_capacity(total_nodes);
    for node in 0..total_nodes {
        if node < parity_start {
            erased_rows.push(Vec::new());
        } else {
            erased_rows.push(vec![0u8; chunk_size]);
        }
    }

    // Encode by treating parity computation as recovery
    // This should never fail for valid parameters (parity count = m <= m)
    decode_layered(params, rs, &available_rows, &mut erased_rows, sub_chunk_size)
        .expect("Encode failed: this indicates a bug in ClayCode");

    // Return the k data + m parity chunks (shortened nodes stay internal)
    let mut result = Vec::with_capacity(params.n);
    for row in data_rows {
        result.push(row);
    }
    for node in parity_start..total_nodes {
        result.push(std::mem::take(&mut erased_rows[node]));
    }

    result
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

//! Encoding logic for Clay codes
//!
//! This module handles encoding data into Clay code chunks.

use std::collections::BTreeSet;

use crate::decode::decode_layered;

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
    pub recovery_count: usize,
}

/// Encode data into n chunks
///
/// # Parameters
/// - `params`: Encoding parameters from ClayCode
/// - `data`: Raw data bytes to encode
///
/// # Returns
/// Vector of n chunks, each containing α sub-chunks
pub fn encode(params: &EncodeParams, data: &[u8]) -> Vec<Vec<u8>> {
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

    // Create padded data
    let mut padded_data = data.to_vec();
    padded_data.resize(padded_len, 0);

    // Initialize all chunks (k data + nu shortened + m parity)
    let total_nodes = params.q * params.t; // k + m + nu
    let mut chunks: Vec<Vec<u8>> = vec![vec![0u8; chunk_size]; total_nodes];

    // Load data into first k nodes
    for i in 0..params.k {
        chunks[i].copy_from_slice(&padded_data[i * chunk_size..(i + 1) * chunk_size]);
    }

    // Shortened nodes (k to k+nu-1) are already zeros - they are KNOWN zeros,
    // not erasures. We mark only parity nodes as needing computation.
    let parity_start = params.k + params.nu;
    let mut nodes_to_compute: BTreeSet<usize> = BTreeSet::new();
    for i in parity_start..total_nodes {
        nodes_to_compute.insert(i);
    }

    // Encode by treating parity computation as recovery
    // This should never fail for valid parameters (parity count = m <= m)
    decode_layered(params, &nodes_to_compute, &mut chunks, sub_chunk_size)
        .expect("Encode failed: this indicates a bug in ClayCode");

    // Return only the k data + m parity chunks (exclude shortened nodes)
    let mut result = Vec::with_capacity(params.n);
    for i in 0..params.k {
        result.push(chunks[i].clone());
    }
    for i in (params.k + params.nu)..total_nodes {
        result.push(chunks[i].clone());
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
            recovery_count: 2,
        }
    }

    #[test]
    fn test_encode_produces_correct_chunk_count() {
        let params = test_params();
        let data = b"Test data for encoding";
        let chunks = encode(&params, data);
        assert_eq!(chunks.len(), params.n);
    }

    #[test]
    fn test_encode_empty_data() {
        let params = test_params();
        let chunks = encode(&params, &[]);
        assert_eq!(chunks.len(), params.n);
        // All chunks should have same size
        let chunk_size = chunks[0].len();
        for chunk in &chunks {
            assert_eq!(chunk.len(), chunk_size);
        }
    }

    #[test]
    fn test_encode_chunk_alignment() {
        let params = test_params();
        let data = vec![0xABu8; 100];
        let chunks = encode(&params, &data);

        // Chunk size should be divisible by sub_chunk_no
        for chunk in &chunks {
            assert_eq!(chunk.len() % params.sub_chunk_no, 0);
        }
    }
}

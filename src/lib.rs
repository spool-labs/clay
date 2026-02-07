//! Clay (Coupled-Layer) Erasure Codes
//!
//! Implementation of Clay codes based on the FAST'18 paper:
//! "Clay Codes: Moulding MDS Codes to Yield an MSR Code"
//!
//! Clay codes are MSR (Minimum Storage Regenerating) codes that provide
//! optimal repair bandwidth - recovering a lost node using only β sub-chunks
//! from each of d helper nodes, rather than downloading k full chunks.
//!
//! # Example
//!
//! ```
//! use clay_codes::ClayCode;
//! use std::collections::HashMap;
//!
//! // Create a (6, 4, 5) Clay code: 4 data + 2 parity, repair with 5 helpers
//! let clay = ClayCode::new(4, 2, 5).unwrap();
//!
//! // Encode data
//! let data = b"Hello, Clay codes!";
//! let chunks = clay.encode(data);
//!
//! // Decode with all chunks
//! let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
//! for (i, chunk) in chunks.iter().enumerate() {
//!     available.insert(i, chunk.clone());
//! }
//! let decoded = clay.decode(&available, &[]).unwrap();
//! assert_eq!(&decoded[..data.len()], &data[..]);
//! ```
//!
//! # Modules
//!
//! - `error`: Error types for Clay code operations
//! - `transforms`: Pairwise coupling transforms (PRT/PFT)
//! - `encode`: Encoding implementation
//! - `decode`: Decoding and erasure recovery
//! - `repair`: Single-node optimal repair

use std::collections::HashMap;

mod coords;
mod decode;
mod encode;
mod error;
mod repair;
mod transforms;

pub use error::ClayError;

const MAX_RS_SHARDS: usize = 32768;

use decode::decode as decode_chunks;
use encode::encode as encode_chunks;
use repair::{minimum_to_repair as min_repair, repair as repair_chunk};

/// Clay (Coupled-Layer) erasure code
#[derive(Clone, Debug)]
pub struct ClayCode {
    /// Number of data chunks
    pub k: usize,
    /// Number of parity chunks
    pub m: usize,
    /// Total nodes (k + m)
    pub n: usize,
    /// Number of helper nodes for repair (k <= d <= n-1)
    pub d: usize,
    /// Coupling factor: q = d - k + 1
    pub q: usize,
    /// Number of y-sections: t = (n + nu) / q
    pub t: usize,
    /// Shortening parameter: makes (k + m + nu) divisible by q
    pub nu: usize,
    /// Sub-packetization level: α = q^t (sub-chunks per chunk)
    pub sub_chunk_no: usize,
    /// Sub-chunks needed from each helper during repair: β = α / q
    pub beta: usize,
    /// Number of original shards for RS (k + nu)
    original_count: usize,
    /// Number of recovery shards for RS (m)
    recovery_count: usize,
}

impl ClayCode {
    /// Create a new Clay code with parameters (k, m, d)
    ///
    /// # Parameters
    /// - `k`: Number of data chunks (systematic nodes)
    /// - `m`: Number of parity chunks
    /// - `d`: Number of helper nodes for repair
    ///
    /// # Returns
    /// Result with ClayCode or error if parameters are invalid
    pub fn new(k: usize, m: usize, d: usize) -> Result<Self, ClayError> {
        if k < 1 {
            return Err(ClayError::InvalidParameters("k must be at least 1".into()));
        }
        if m < 1 {
            return Err(ClayError::InvalidParameters("m must be at least 1".into()));
        }
        if d < k + 1 || d > k + m - 1 {
            return Err(ClayError::InvalidParameters(format!(
                "d must be in range [{}, {}], got {}",
                k + 1,
                k + m - 1,
                d
            )));
        }

        let q = d - k + 1;
        let n = k + m;

        // Calculate nu for shortening (so that n + nu is divisible by q)
        let nu = if n % q == 0 { 0 } else { q - (n % q) };

        let t = (n + nu) / q;

        // Use checked arithmetic for sub_chunk_no = q^t
        let sub_chunk_no = checked_pow(q, t).ok_or_else(|| {
            ClayError::Overflow(format!("q^t = {}^{} overflows", q, t))
        })?;

        let beta = sub_chunk_no / q; // β = α / q

        // Validate that k+nu+m fits in reed-solomon limits (up to 32768 shards)
        let original_count = k + nu;
        let recovery_count = m;
        if original_count > MAX_RS_SHARDS || recovery_count > MAX_RS_SHARDS {
            return Err(ClayError::InvalidParameters(
                "Total nodes exceeds reed-solomon limit of 32768".into(),
            ));
        }

        Ok(ClayCode {
            k,
            m,
            n,
            d,
            q,
            t,
            nu,
            sub_chunk_no,
            beta,
            original_count,
            recovery_count,
        })
    }

    /// Create with default d = k + m - 1 (maximum helpers)
    pub fn new_default(k: usize, m: usize) -> Result<Self, ClayError> {
        Self::new(k, m, k + m - 1)
    }

    /// Get encoding parameters for internal use
    fn encode_params(&self) -> encode::EncodeParams {
        encode::EncodeParams {
            k: self.k,
            m: self.m,
            n: self.n,
            q: self.q,
            t: self.t,
            nu: self.nu,
            sub_chunk_no: self.sub_chunk_no,
            original_count: self.original_count,
            recovery_count: self.recovery_count,
        }
    }

    /// Encode data into n chunks
    ///
    /// # Parameters
    /// - `data`: Raw data bytes to encode
    ///
    /// # Returns
    /// Vector of n chunks, each containing α sub-chunks
    pub fn encode(&self, data: &[u8]) -> Vec<Vec<u8>> {
        encode_chunks(&self.encode_params(), data)
    }

    /// Decode data from available chunks
    ///
    /// # Parameters
    /// - `available`: Map from chunk index to chunk data
    /// - `erasures`: Set of erased chunk indices
    ///
    /// # Returns
    /// Recovered original data, or error if decoding fails
    pub fn decode(
        &self,
        available: &HashMap<usize, Vec<u8>>,
        erasures: &[usize],
    ) -> Result<Vec<u8>, ClayError> {
        decode_chunks(&self.encode_params(), available, erasures)
    }

    /// Determine minimum sub-chunks needed to repair a lost node
    ///
    /// # Parameters
    /// - `lost_node`: Index of the lost node (0 to n-1)
    /// - `available`: Available node indices
    ///
    /// # Returns
    /// Vector of (helper_node_idx, sub_chunk_indices) where sub_chunk_indices
    /// is a vector of the specific sub-chunk indices needed from that helper.
    /// The repair() function expects helper data to contain these sub-chunks
    /// concatenated in the ORDER they appear in sub_chunk_indices.
    pub fn minimum_to_repair(
        &self,
        lost_node: usize,
        available: &[usize],
    ) -> Result<Vec<(usize, Vec<usize>)>, ClayError> {
        min_repair(&self.encode_params(), lost_node, available)
    }

    /// Repair a lost chunk using partial data from helper nodes
    ///
    /// # Parameters
    /// - `lost_node`: Index of the lost node (0 to n-1)
    /// - `helper_data`: Map from helper node index to partial chunk data.
    ///   Each helper's data must be the concatenation of sub-chunks at the
    ///   indices returned by minimum_to_repair(), in that exact order.
    /// - `chunk_size`: Full chunk size
    ///
    /// # Returns
    /// The recovered full chunk, or error if repair fails
    pub fn repair(
        &self,
        lost_node: usize,
        helper_data: &HashMap<usize, Vec<u8>>,
        chunk_size: usize,
    ) -> Result<Vec<u8>, ClayError> {
        repair_chunk(&self.encode_params(), lost_node, helper_data, chunk_size)
    }

    /// Calculate normalized repair bandwidth
    ///
    /// This is the ratio of data downloaded for repair to the size of the
    /// repaired chunk. For Clay codes, this is d / (k * q).
    pub fn normalized_repair_bandwidth(&self) -> f64 {
        (self.d as f64) / ((self.k as f64) * (self.d - self.k + 1) as f64)
    }
}

/// Integer power function with overflow checking
fn checked_pow(base: usize, exp: usize) -> Option<usize> {
    let mut result: usize = 1;
    let mut b = base;
    let mut e = exp;
    while e > 0 {
        if e & 1 == 1 {
            result = result.checked_mul(b)?;
        }
        e >>= 1;
        if e > 0 {
            b = b.checked_mul(b)?;
        }
    }
    Some(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_encode_decode() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data = b"Test data for Clay codes - not empty!";
        let chunks = clay.encode(data);
        assert_eq!(chunks.len(), 6); // k + m = 6

        // Decode with all chunks
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            available.insert(i, chunk.clone());
        }
        let decoded = clay.decode(&available, &[]).unwrap();

        // Check prefix matches (may have padding)
        assert_eq!(&decoded[..data.len()], &data[..]);
    }

    #[test]
    fn test_decode_with_erasures() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data = b"Test data for Clay codes - testing erasure recovery!";
        let chunks = clay.encode(data);

        // Lose node 0
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if i != 0 {
                available.insert(i, chunk.clone());
            }
        }
        let decoded = clay.decode(&available, &[0]).unwrap();
        assert_eq!(&decoded[..data.len()], &data[..]);

        // Lose node 5 (parity)
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if i != 5 {
                available.insert(i, chunk.clone());
            }
        }
        let decoded = clay.decode(&available, &[5]).unwrap();
        assert_eq!(&decoded[..data.len()], &data[..]);

        // Lose two nodes
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if i != 0 && i != 5 {
                available.insert(i, chunk.clone());
            }
        }
        let decoded = clay.decode(&available, &[0, 5]).unwrap();
        assert_eq!(&decoded[..data.len()], &data[..]);
    }

    #[test]
    fn test_parameters() {
        // Test (6, 4, 5) - from paper
        let clay = ClayCode::new(4, 2, 5).unwrap();
        assert_eq!(clay.q, 2);
        assert_eq!(clay.t, 3);
        assert_eq!(clay.sub_chunk_no, 8); // 2^3 = 8
        assert_eq!(clay.beta, 4); // 8 / 2 = 4

        // Test (14, 10, 13)
        let clay2 = ClayCode::new(10, 4, 13).unwrap();
        assert_eq!(clay2.q, 4);
        assert_eq!(clay2.t, 4);
        assert_eq!(clay2.sub_chunk_no, 256); // 4^4 = 256
        assert_eq!(clay2.beta, 64); // 256 / 4 = 64
    }

    #[test]
    fn test_minimum_to_repair() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let available: Vec<usize> = vec![1, 2, 3, 4, 5];
        let helper_info = clay.minimum_to_repair(0, &available).unwrap();

        // Should return d = 5 helpers
        assert_eq!(helper_info.len(), 5);

        // Each helper should provide β = 4 sub-chunks
        for (_, indices) in &helper_info {
            assert_eq!(indices.len(), 4);
        }
    }

    #[test]
    fn test_repair_bandwidth_verification() {
        // This test verifies we're actually using Clay's repair advantage
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data = b"Test data for bandwidth verification of Clay codes repair!";
        let chunks = clay.encode(data);
        let chunk_size = chunks[0].len();

        // Get minimum data needed to repair node 0
        let available: Vec<usize> = vec![1, 2, 3, 4, 5];
        let helper_info = clay.minimum_to_repair(0, &available).unwrap();

        // Calculate total sub-chunks requested
        let sub_chunk_size = chunk_size / clay.sub_chunk_no;
        let total_repair_subchunks: usize = helper_info
            .iter()
            .map(|(_, indices)| indices.len())
            .sum();
        let total_repair_bytes = total_repair_subchunks * sub_chunk_size;

        let full_decode_bytes = clay.k * chunk_size;

        // Clay repair should use significantly less data
        let ratio = total_repair_bytes as f64 / full_decode_bytes as f64;
        println!(
            "Repair bandwidth: {} bytes, Full decode: {} bytes, Ratio: {:.3}",
            total_repair_bytes, full_decode_bytes, ratio
        );

        assert!(
            total_repair_bytes < full_decode_bytes * 7 / 10,
            "Repair bandwidth {} should be < 70% of full decode {}",
            total_repair_bytes,
            full_decode_bytes
        );
    }

    #[test]
    fn test_repair_correctness() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data = b"Test data for repair correctness verification!!!!";
        let chunks = clay.encode(data);
        let chunk_size = chunks[0].len();
        let sub_chunk_size = chunk_size / clay.sub_chunk_no;

        // Test repairing each node
        for lost_node in 0..clay.n {
            let available: Vec<usize> = (0..clay.n).filter(|&i| i != lost_node).collect();
            let helper_info = clay.minimum_to_repair(lost_node, &available).unwrap();

            // Extract only the required sub-chunks from each helper
            let mut partial_data: HashMap<usize, Vec<u8>> = HashMap::new();
            for (helper_idx, indices) in &helper_info {
                let mut helper_partial = Vec::new();
                for &sc_idx in indices {
                    let start_byte = sc_idx * sub_chunk_size;
                    let end_byte = (sc_idx + 1) * sub_chunk_size;
                    helper_partial.extend_from_slice(&chunks[*helper_idx][start_byte..end_byte]);
                }
                partial_data.insert(*helper_idx, helper_partial);
            }

            // Repair using ONLY partial data
            let recovered = clay.repair(lost_node, &partial_data, chunk_size).unwrap();

            // Verify recovered chunk matches original
            assert_eq!(
                recovered, chunks[lost_node],
                "Repair failed for node {}",
                lost_node
            );
        }
    }

    #[test]
    fn test_various_parameters() {
        // Test different parameter combinations from the paper
        let params = vec![
            (4, 2, 5),   // (6, 4, 5) - α=8, β=4
            (9, 3, 11),  // (12, 9, 11) - α=81, β=27
            (10, 4, 13), // (14, 10, 13) - α=256, β=64
        ];

        for (k, m, d) in params {
            let clay = ClayCode::new(k, m, d).unwrap();
            let data_size = k * clay.sub_chunk_no * 2;
            let data: Vec<u8> = (0..data_size).map(|i| (i % 256) as u8).collect();
            let chunks = clay.encode(&data);

            // Test decode with one erasure
            let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
            for (i, chunk) in chunks.iter().enumerate() {
                if i != 0 {
                    available.insert(i, chunk.clone());
                }
            }
            let decoded = clay.decode(&available, &[0]).unwrap();
            assert_eq!(
                &decoded[..data.len()],
                &data[..],
                "Failed for params ({}, {}, {})",
                k,
                m,
                d
            );
        }
    }

    #[test]
    fn test_repair_all_nodes_various_params() {
        let params = vec![(4, 2, 5), (9, 3, 11)];

        for (k, m, d) in params {
            let clay = ClayCode::new(k, m, d).unwrap();
            let data_size = k * clay.sub_chunk_no;
            let data: Vec<u8> = (0..data_size).map(|i| ((i * 7 + 13) % 256) as u8).collect();
            let chunks = clay.encode(&data);
            let chunk_size = chunks[0].len();
            let sub_chunk_size = chunk_size / clay.sub_chunk_no;

            for lost_node in 0..clay.n {
                let available: Vec<usize> = (0..clay.n).filter(|&i| i != lost_node).collect();
                let helper_info = clay.minimum_to_repair(lost_node, &available).unwrap();

                let mut partial_data: HashMap<usize, Vec<u8>> = HashMap::new();
                for (helper_idx, indices) in &helper_info {
                    let mut helper_partial = Vec::new();
                    for &sc_idx in indices {
                        let start_byte = sc_idx * sub_chunk_size;
                        let end_byte = (sc_idx + 1) * sub_chunk_size;
                        helper_partial.extend_from_slice(&chunks[*helper_idx][start_byte..end_byte]);
                    }
                    partial_data.insert(*helper_idx, helper_partial);
                }

                let recovered = clay.repair(lost_node, &partial_data, chunk_size).unwrap();
                assert_eq!(
                    recovered, chunks[lost_node],
                    "Repair failed for node {} with params ({}, {}, {})",
                    lost_node, k, m, d
                );
            }
        }
    }

    #[test]
    fn test_decode_max_erasures() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data: Vec<u8> = (0..256).map(|i| (i % 256) as u8).collect();
        let chunks = clay.encode(&data);

        // Lose exactly m = 2 nodes in different patterns
        let patterns = vec![vec![0, 5], vec![0, 1], vec![4, 5], vec![1, 3]];

        for erasures in patterns {
            let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
            for (i, chunk) in chunks.iter().enumerate() {
                if !erasures.contains(&i) {
                    available.insert(i, chunk.clone());
                }
            }
            let decoded = clay.decode(&available, &erasures).unwrap();
            assert_eq!(
                &decoded[..data.len()],
                &data[..],
                "Failed for erasures {:?}",
                erasures
            );
        }
    }

    #[test]
    fn test_normalized_repair_bandwidth() {
        let test_cases = vec![
            ((4, 2, 5), 0.625),
            ((9, 3, 11), 0.407),
            ((10, 4, 13), 0.325),
        ];

        for ((k, m, d), expected) in test_cases {
            let clay = ClayCode::new(k, m, d).unwrap();
            let actual = clay.normalized_repair_bandwidth();
            assert!(
                (actual - expected).abs() < 0.01,
                "Expected {}, got {} for ({}, {}, {})",
                expected,
                actual,
                k,
                m,
                d
            );
        }
    }

    #[test]
    fn test_random_data() {
        use rand::Rng;
        let mut rng = rand::thread_rng();

        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data_size = clay.k * clay.sub_chunk_no * 4;
        let data: Vec<u8> = (0..data_size).map(|_| rng.gen()).collect();
        let chunks = clay.encode(&data);

        // Test full decode
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            available.insert(i, chunk.clone());
        }
        let decoded = clay.decode(&available, &[]).unwrap();
        assert_eq!(&decoded[..data.len()], &data[..]);

        // Test decode with erasure
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if i != 2 {
                available.insert(i, chunk.clone());
            }
        }
        let decoded = clay.decode(&available, &[2]).unwrap();
        assert_eq!(&decoded[..data.len()], &data[..]);
    }

    #[test]
    fn test_checked_pow_overflow() {
        // Test that checked_pow handles overflow gracefully
        assert!(checked_pow(2, 63).is_some());
        assert!(checked_pow(2, 64).is_none()); // Would overflow
        assert!(checked_pow(10, 20).is_none()); // Would overflow
    }

    #[test]
    fn test_invalid_parameters() {
        // k must be >= 1
        assert!(ClayCode::new(0, 2, 1).is_err());

        // m must be >= 1
        assert!(ClayCode::new(4, 0, 3).is_err());

        // d must be in range
        assert!(ClayCode::new(4, 2, 4).is_err()); // d < k+1
        assert!(ClayCode::new(4, 2, 6).is_err()); // d > k+m-1
    }

    #[test]
    fn test_clone_and_debug() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let clay2 = clay.clone();
        assert_eq!(clay2.k, clay.k);
        assert_eq!(clay2.m, clay.m);
        assert_eq!(clay2.d, clay.d);
        // Verify Debug is implemented
        let debug_str = format!("{:?}", clay);
        assert!(debug_str.contains("ClayCode"));
    }

    #[test]
    fn test_new_default() {
        let clay_default = ClayCode::new_default(4, 2).unwrap();
        let clay_explicit = ClayCode::new(4, 2, 4 + 2 - 1).unwrap();
        assert_eq!(clay_default.k, clay_explicit.k);
        assert_eq!(clay_default.m, clay_explicit.m);
        assert_eq!(clay_default.d, clay_explicit.d);
        assert_eq!(clay_default.q, clay_explicit.q);
        assert_eq!(clay_default.t, clay_explicit.t);
        assert_eq!(clay_default.sub_chunk_no, clay_explicit.sub_chunk_no);
        assert_eq!(clay_default.beta, clay_explicit.beta);

        // Also test with different params
        let clay_default2 = ClayCode::new_default(10, 4).unwrap();
        let clay_explicit2 = ClayCode::new(10, 4, 13).unwrap();
        assert_eq!(clay_default2.d, clay_explicit2.d);
        assert_eq!(clay_default2.sub_chunk_no, clay_explicit2.sub_chunk_no);
    }

    #[test]
    fn test_decode_empty_available_with_erasures() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let available: HashMap<usize, Vec<u8>> = HashMap::new();
        let result = clay.decode(&available, &[0]);
        assert!(
            matches!(result, Err(ClayError::InvalidParameters(_))),
            "Expected InvalidParameters error when available is empty but erasures is non-empty, got {:?}",
            result
        );
    }

    // ============ Adversarial Tests ============

    #[test]
    fn test_decode_too_many_erasures() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data: Vec<u8> = (0..128).map(|i| (i % 256) as u8).collect();
        let chunks = clay.encode(&data);

        // Try to decode with 3 erasures (more than m=2)
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if i > 2 {
                available.insert(i, chunk.clone());
            }
        }

        let result = clay.decode(&available, &[0, 1, 2]);
        assert!(
            matches!(result, Err(ClayError::TooManyErasures { max: 2, actual: 3 })),
            "Expected TooManyErasures error, got {:?}",
            result
        );
    }

    #[test]
    fn test_decode_inconsistent_chunk_sizes() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data: Vec<u8> = (0..128).map(|i| (i % 256) as u8).collect();
        let chunks = clay.encode(&data);

        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if i != 0 {
                if i == 5 {
                    // Deliberately corrupt chunk 5 with wrong size
                    let mut bad_chunk = chunk.clone();
                    bad_chunk.push(0); // Add extra byte
                    available.insert(i, bad_chunk);
                } else {
                    available.insert(i, chunk.clone());
                }
            }
        }

        let result = clay.decode(&available, &[0]);
        // Either InconsistentChunkSizes or InvalidChunkSize depending on iteration order
        assert!(
            matches!(result, Err(ClayError::InconsistentChunkSizes { .. }))
                || matches!(result, Err(ClayError::InvalidChunkSize { .. })),
            "Expected InconsistentChunkSizes or InvalidChunkSize error, got {:?}",
            result
        );
    }

    #[test]
    fn test_decode_invalid_chunk_index() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data: Vec<u8> = (0..128).collect();
        let chunks = clay.encode(&data);

        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            available.insert(i, chunk.clone());
        }
        // Add a chunk with invalid index
        available.insert(100, vec![0u8; chunks[0].len()]);

        let result = clay.decode(&available, &[]);
        assert!(
            matches!(result, Err(ClayError::InvalidParameters(_))),
            "Expected InvalidParameters error for out-of-range index, got {:?}",
            result
        );
    }

    #[test]
    fn test_decode_invalid_erasure_index() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data: Vec<u8> = (0..128).collect();
        let chunks = clay.encode(&data);

        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if i != 0 {
                available.insert(i, chunk.clone());
            }
        }

        // Declare an out-of-range erasure
        let result = clay.decode(&available, &[100]);
        assert!(
            matches!(result, Err(ClayError::InvalidParameters(_))),
            "Expected InvalidParameters error for out-of-range erasure, got {:?}",
            result
        );
    }

    #[test]
    fn test_decode_available_erasure_overlap() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data: Vec<u8> = (0..128).collect();
        let chunks = clay.encode(&data);

        // Include node 0 in both available AND erasures - should be an error
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            available.insert(i, chunk.clone());
        }

        let result = clay.decode(&available, &[0]);
        assert!(
            matches!(result, Err(ClayError::InvalidParameters(ref msg)) if msg.contains("both")),
            "Expected InvalidParameters error for overlap, got {:?}",
            result
        );
    }

    #[test]
    fn test_decode_wrong_available_count() {
        let clay = ClayCode::new(4, 2, 5).unwrap();
        let data: Vec<u8> = (0..128).collect();
        let chunks = clay.encode(&data);

        // Provide too few chunks for the declared erasures
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if i > 1 {
                available.insert(i, chunk.clone());
            }
        }

        // Say only node 0 is erased, but we only have 4 chunks (should have 5)
        let result = clay.decode(&available, &[0]);
        assert!(
            matches!(result, Err(ClayError::InvalidParameters(ref msg)) if msg.contains("Expected")),
            "Expected InvalidParameters error for wrong count, got {:?}",
            result
        );
    }
}

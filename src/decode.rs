//! Decoding and erasure recovery for Clay codes
//!
//! This module implements the layered decoding algorithm from the FAST'18 paper.
//! It handles both full decoding (all chunks available) and erasure recovery
//! (up to m chunks missing).

use std::collections::HashMap;

use reed_solomon_erasure::galois_8;
use reed_solomon_erasure::ReedSolomon;

use crate::coords::LayerGeometry;
use crate::encode::EncodeParams;
use crate::error::ClayError;
use crate::transforms::{compute_c_into, compute_u_into, pft_into, prt_into};

/// Parameters needed for decoding (same as encode for now)
pub type DecodeParams = EncodeParams;

/// The GF(2^8) Reed-Solomon codec shared across all layers
pub type RsCodec = ReedSolomon<galois_8::Field>;

/// Scratch buffers shared by every layer of one decode call
struct DecodeScratch {
    u_buf: Vec<Vec<u8>>,
    u_computed: Vec<Vec<bool>>,
    needs_mds: Vec<bool>,
}

/// Recover original data from available chunks
///
/// # Parameters
/// - `params`: Code parameters
/// - `rs`: Reed-Solomon codec built for these parameters
/// - `available`: Map from chunk index to chunk data
/// - `erasures`: Set of erased chunk indices
///
/// # Returns
/// Recovered original data, or error if decoding fails
pub fn decode(
    params: &DecodeParams,
    rs: &RsCodec,
    available: &HashMap<usize, Vec<u8>>,
    erasures: &[usize],
) -> Result<Vec<u8>, ClayError> {
    if available.is_empty() && erasures.is_empty() {
        return Ok(Vec::new());
    }
    if available.is_empty() {
        return Err(ClayError::InvalidParameters(
            "No available chunks provided but erasures are non-empty".into(),
        ));
    }

    // Validate erasure count
    if erasures.len() > params.m {
        return Err(ClayError::TooManyErasures {
            max: params.m,
            actual: erasures.len(),
        });
    }

    // Get chunk size from first available chunk and validate all chunks match
    let mut iter = available.iter();
    let (_, first_chunk) = iter.next().unwrap();
    let chunk_size = first_chunk.len();

    // Validate chunk_size is divisible by sub_chunk_no
    if chunk_size == 0 || chunk_size % params.sub_chunk_no != 0 {
        return Err(ClayError::InvalidChunkSize {
            expected: params.sub_chunk_no,
            actual: chunk_size,
        });
    }

    // Validate all chunks have same size
    for (&idx, chunk) in iter {
        if chunk.len() != chunk_size {
            return Err(ClayError::InconsistentChunkSizes {
                first_size: chunk_size,
                mismatched_idx: idx,
                mismatched_size: chunk.len(),
            });
        }
    }

    // Validate chunk indices are in valid range
    for &idx in available.keys() {
        if idx >= params.n {
            return Err(ClayError::InvalidParameters(format!(
                "Chunk index {} out of range [0, {})",
                idx, params.n
            )));
        }
    }
    for &e in erasures {
        if e >= params.n {
            return Err(ClayError::InvalidParameters(format!(
                "Erasure index {} out of range [0, {})",
                e, params.n
            )));
        }
    }

    // Validate consistency between available and erasures
    // 1. Erasures and available keys must be disjoint
    for &e in erasures {
        if available.contains_key(&e) {
            return Err(ClayError::InvalidParameters(format!(
                "Node {} is both in available chunks and marked as erased",
                e
            )));
        }
    }

    // 2. We need exactly n - erasures.len() available chunks
    let expected_available = params.n - erasures.len();
    if available.len() != expected_available {
        return Err(ClayError::InvalidParameters(format!(
            "Expected {} available chunks (n={} - erasures={}), but got {}",
            expected_available,
            params.n,
            erasures.len(),
            available.len()
        )));
    }

    // 3. All non-erased nodes must be present in available
    for node in 0..params.n {
        if !erasures.contains(&node) && !available.contains_key(&node) {
            return Err(ClayError::InvalidParameters(format!(
                "Node {} is neither erased nor provided in available chunks",
                node
            )));
        }
    }

    let sub_chunk_size = chunk_size / params.sub_chunk_no;
    let total_nodes = params.q * params.t;

    // Stage chunks by internal index; only erased and shortened slots need zero buffers
    let mut chunks: Vec<Vec<u8>> = Vec::with_capacity(total_nodes);
    for internal_idx in 0..total_nodes {
        if internal_idx >= params.k && internal_idx < params.k + params.nu {
            chunks.push(vec![0u8; chunk_size]);
            continue;
        }

        let external_idx = if internal_idx < params.k {
            internal_idx
        } else {
            internal_idx - params.nu
        };
        if let Some(data) = available.get(&external_idx) {
            chunks.push(data.clone());
        } else {
            chunks.push(vec![0u8; chunk_size]);
        }
    }

    // Mark erasures by internal index; shortened nodes are known zeros, not erasures
    let mut is_erased: Vec<bool> = vec![false; total_nodes];
    for &e in erasures {
        let internal_idx = if e < params.k { e } else { e + params.nu };
        is_erased[internal_idx] = true;
    }

    decode_layered(params, rs, &is_erased, &mut chunks, sub_chunk_size)?;

    // Extract original data from first k chunks
    let mut result = Vec::with_capacity(params.k * chunk_size);
    for i in 0..params.k {
        result.extend_from_slice(&chunks[i]);
    }

    Ok(result)
}

/// Main layered decoding algorithm
///
/// Processes layers in order of increasing intersection score, applying
/// PRT/PFT transforms and RS decoding as needed.
pub fn decode_layered(
    params: &DecodeParams,
    rs: &RsCodec,
    is_erased: &[bool],
    chunks: &mut [Vec<u8>],
    sub_chunk_size: usize,
) -> Result<(), ClayError> {
    let total_nodes = params.q * params.t;
    let geometry = LayerGeometry::new(params.q, params.t, params.sub_chunk_no);

    let mut erased_nodes: Vec<usize> = Vec::new();
    for (node, &erased) in is_erased.iter().enumerate() {
        if erased {
            erased_nodes.push(node);
        }
    }

    let chunk_size = chunks[0].len();
    let mut scratch = DecodeScratch {
        u_buf: vec![vec![0u8; chunk_size]; total_nodes],
        u_computed: vec![vec![false; params.sub_chunk_no]; total_nodes],
        needs_mds: vec![false; total_nodes],
    };

    // Bucket layers by intersection score so each pass visits only its own layers
    let max_iscore = get_max_iscore(params, &erased_nodes);
    let mut layers_by_iscore: Vec<Vec<usize>> = vec![Vec::new(); max_iscore + 1];
    for z in 0..params.sub_chunk_no {
        let digits = geometry.plane_digits(z);
        let mut iscore = 0;
        for &node in &erased_nodes {
            if node % params.q == digits[node / params.q] {
                iscore += 1;
            }
        }
        layers_by_iscore[iscore].push(z);
    }

    // Process layers in order of increasing intersection score
    for layers in &layers_by_iscore {
        // First pass: decode erasures for layers with this iscore
        for &z in layers {
            decode_layered_with_tracking(
                params,
                rs,
                &geometry,
                is_erased,
                z,
                chunks,
                &mut scratch,
                sub_chunk_size,
            )?;
        }

        // Second pass: recover C values from U values
        for &z in layers {
            let digits = geometry.plane_digits(z);

            for &node_xy in &erased_nodes {
                let x = node_xy % params.q;
                let y = node_xy / params.q;
                let z_y = digits[y];
                let node_sw = y * params.q + z_y;
                let z_sw = geometry.companion_layer(z, x, y, z_y);

                if z_y != x {
                    if !is_erased[node_sw] {
                        // Type 1: companion is not erased
                        recover_type1_erasure(
                            params,
                            chunks,
                            &scratch.u_buf,
                            x,
                            y,
                            z,
                            z_y,
                            z_sw,
                            sub_chunk_size,
                        );
                    } else if z_y < x {
                        // Both erased, process once (when z_y < x)
                        get_coupled_from_uncoupled(
                            params,
                            chunks,
                            &scratch.u_buf,
                            x,
                            y,
                            z,
                            z_y,
                            z_sw,
                            sub_chunk_size,
                        );
                    }
                } else {
                    // Red vertex: C = U
                    let offset = z * sub_chunk_size;
                    chunks[node_xy][offset..offset + sub_chunk_size].copy_from_slice(
                        &scratch.u_buf[node_xy][offset..offset + sub_chunk_size],
                    );
                }
            }
        }
    }

    Ok(())
}

/// Decode erasures for a single layer with U tracking
fn decode_layered_with_tracking(
    params: &DecodeParams,
    rs: &RsCodec,
    geometry: &LayerGeometry,
    is_erased: &[bool],
    z: usize,
    chunks: &[Vec<u8>],
    scratch: &mut DecodeScratch,
    sub_chunk_size: usize,
) -> Result<(), ClayError> {
    let digits = geometry.plane_digits(z);
    let u_buf = &mut scratch.u_buf;
    let u_computed = &mut scratch.u_computed;

    // Nodes needing MDS recovery this layer: the erasures plus any node whose
    // companion U is not known yet
    scratch.needs_mds.copy_from_slice(is_erased);
    let mut mds_count = 0;
    for &erased in is_erased {
        if erased {
            mds_count += 1;
        }
    }

    // Compute U values for non-erased nodes
    for x in 0..params.q {
        for y in 0..params.t {
            let node_xy = params.q * y + x;
            let z_y = digits[y];
            let node_sw = params.q * y + z_y;
            let z_sw = geometry.companion_layer(z, x, y, z_y);

            if !is_erased[node_xy] {
                if z_y == x {
                    // Red vertex: U = C (no companion needed)
                    let offset = z * sub_chunk_size;
                    u_buf[node_xy][offset..offset + sub_chunk_size]
                        .copy_from_slice(&chunks[node_xy][offset..offset + sub_chunk_size]);
                    u_computed[node_xy][z] = true;
                } else if !is_erased[node_sw] {
                    // Both nodes available - apply PRT (only process once when z_y < x)
                    if z_y < x {
                        get_uncoupled_from_coupled(
                            params, chunks, u_buf, x, y, z, z_y, z_sw, sub_chunk_size,
                        );
                        u_computed[node_xy][z] = true;
                        u_computed[node_sw][z_sw] = true;
                    }
                } else {
                    // Companion is erased - check if companion's U* is available
                    // from a previous iteration (lower intersection score layer)
                    if u_computed[node_sw][z_sw] {
                        // U = det*C + gamma*U* recovers U from C and the known U*
                        let offset_z = z * sub_chunk_size;
                        let offset_zsw = z_sw * sub_chunk_size;
                        let (u_xy_buf, u_sw_buf) = pair_mut(u_buf, node_xy, node_sw);
                        compute_u_into(
                            &chunks[node_xy][offset_z..offset_z + sub_chunk_size],
                            &u_sw_buf[offset_zsw..offset_zsw + sub_chunk_size],
                            &mut u_xy_buf[offset_z..offset_z + sub_chunk_size],
                        );
                        u_computed[node_xy][z] = true;
                    } else {
                        // Companion's U not available yet - mark for MDS
                        scratch.needs_mds[node_xy] = true;
                        mds_count += 1;
                    }
                }
            }
        }
    }

    // Decode uncoupled layer using MDS
    decode_uncoupled_layer(params, rs, &scratch.needs_mds, mds_count, z, sub_chunk_size, u_buf)?;

    // Mark reconstructed nodes as computed
    for (node, &needed) in scratch.needs_mds.iter().enumerate() {
        if needed {
            u_computed[node][z] = true;
        }
    }

    Ok(())
}

/// Decode uncoupled layer using RS MDS code
pub fn decode_uncoupled_layer(
    params: &DecodeParams,
    rs: &RsCodec,
    is_erased: &[bool],
    erased_count: usize,
    z: usize,
    sub_chunk_size: usize,
    u_buf: &mut [Vec<u8>],
) -> Result<(), ClayError> {
    let offset = z * sub_chunk_size;
    let parity_start = params.original_count; // k + nu

    if erased_count > params.m {
        return Err(ClayError::TooManyErasures {
            max: params.m,
            actual: erased_count,
        });
    }

    if erased_count == 0 {
        return Ok(());
    }

    let mut has_erased_originals = false;
    for &erased in &is_erased[..parity_start] {
        if erased {
            has_erased_originals = true;
            break;
        }
    }

    // The RS codec works directly on this layer's slice of each node buffer,
    // so nothing is copied in or out
    let mut shards: Vec<(&mut [u8], bool)> = Vec::with_capacity(u_buf.len());
    for (i, node_buf) in u_buf.iter_mut().enumerate() {
        shards.push((&mut node_buf[offset..offset + sub_chunk_size], !is_erased[i]));
    }

    if has_erased_originals {
        rs.reconstruct(&mut shards).map_err(|e| {
            ClayError::ReconstructionFailed(format!("Layer {} RS reconstruct failed: {:?}", z, e))
        })?;
    } else {
        // Only parities are missing, so re-encoding from the originals is enough
        let mut slices: Vec<&mut [u8]> = Vec::with_capacity(shards.len());
        for (slice, _) in shards {
            slices.push(slice);
        }
        rs.encode(&mut slices).map_err(|e| {
            ClayError::ReconstructionFailed(format!("Layer {} RS encode failed: {:?}", z, e))
        })?;
    }

    Ok(())
}

/// Split two mutable node buffers out of the same slice
///
/// The coupled pair always names two distinct nodes, so the split is safe.
pub fn pair_mut(
    buffers: &mut [Vec<u8>],
    first: usize,
    second: usize,
) -> (&mut Vec<u8>, &mut Vec<u8>) {
    debug_assert_ne!(first, second);
    if first < second {
        let (left, right) = buffers.split_at_mut(second);
        (&mut left[first], &mut right[0])
    } else {
        let (left, right) = buffers.split_at_mut(first);
        (&mut right[0], &mut left[second])
    }
}

/// Get uncoupled values from coupled values using PRT
fn get_uncoupled_from_coupled(
    params: &DecodeParams,
    chunks: &[Vec<u8>],
    u_buf: &mut [Vec<u8>],
    x: usize,
    y: usize,
    z: usize,
    z_y: usize,
    z_sw: usize,
    sub_chunk_size: usize,
) {
    let node_xy = y * params.q + x;
    let node_sw = y * params.q + z_y;

    let offset_z = z * sub_chunk_size;
    let offset_zsw = z_sw * sub_chunk_size;

    let c_xy = &chunks[node_xy][offset_z..offset_z + sub_chunk_size];
    let c_sw = &chunks[node_sw][offset_zsw..offset_zsw + sub_chunk_size];

    let (u_xy_buf, u_sw_buf) = pair_mut(u_buf, node_xy, node_sw);
    let u_xy = &mut u_xy_buf[offset_z..offset_z + sub_chunk_size];
    let u_sw = &mut u_sw_buf[offset_zsw..offset_zsw + sub_chunk_size];

    // The lower x coordinate of the pair holds C, the other C*
    if x < z_y {
        prt_into(c_xy, c_sw, u_xy, u_sw);
    } else {
        prt_into(c_sw, c_xy, u_sw, u_xy);
    }
}

/// Recover type 1 erasure (companion not erased)
fn recover_type1_erasure(
    params: &DecodeParams,
    chunks: &mut [Vec<u8>],
    u_buf: &[Vec<u8>],
    x: usize,
    y: usize,
    z: usize,
    z_y: usize,
    z_sw: usize,
    sub_chunk_size: usize,
) {
    let node_xy = y * params.q + x;
    let node_sw = y * params.q + z_y;

    let offset_z = z * sub_chunk_size;
    let offset_zsw = z_sw * sub_chunk_size;

    let u_xy = &u_buf[node_xy][offset_z..offset_z + sub_chunk_size];

    let (c_xy_buf, c_sw_buf) = pair_mut(chunks, node_xy, node_sw);
    compute_c_into(
        u_xy,
        &c_sw_buf[offset_zsw..offset_zsw + sub_chunk_size],
        &mut c_xy_buf[offset_z..offset_z + sub_chunk_size],
    );
}

/// Get coupled values from uncoupled values using PFT
fn get_coupled_from_uncoupled(
    params: &DecodeParams,
    chunks: &mut [Vec<u8>],
    u_buf: &[Vec<u8>],
    x: usize,
    y: usize,
    z: usize,
    z_y: usize,
    z_sw: usize,
    sub_chunk_size: usize,
) {
    let node_xy = y * params.q + x;
    let node_sw = y * params.q + z_y;

    let offset_z = z * sub_chunk_size;
    let offset_zsw = z_sw * sub_chunk_size;

    let u_xy = &u_buf[node_xy][offset_z..offset_z + sub_chunk_size];
    let u_sw = &u_buf[node_sw][offset_zsw..offset_zsw + sub_chunk_size];

    let (c_xy_buf, c_sw_buf) = pair_mut(chunks, node_xy, node_sw);
    let c_xy = &mut c_xy_buf[offset_z..offset_z + sub_chunk_size];
    let c_sw = &mut c_sw_buf[offset_zsw..offset_zsw + sub_chunk_size];

    // The lower x coordinate of the pair holds U, the other U*
    if x < z_y {
        pft_into(u_xy, u_sw, c_xy, c_sw);
    } else {
        pft_into(u_sw, u_xy, c_sw, c_xy);
    }
}

/// Maximum intersection score: the number of distinct erased y-sections
fn get_max_iscore(params: &DecodeParams, erased_nodes: &[usize]) -> usize {
    let mut y_seen = vec![false; params.t];
    let mut iscore = 0;

    for &node in erased_nodes {
        let y = node / params.q;
        if !y_seen[y] {
            y_seen[y] = true;
            iscore += 1;
        }
    }

    iscore
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_params() -> DecodeParams {
        DecodeParams {
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

    fn test_rs(params: &DecodeParams) -> RsCodec {
        RsCodec::new(params.original_count, params.m).expect("test params should build a codec")
    }

    // decoding nothing yields nothing
    #[test]
    fn decode_empty() {
        let params = test_params();
        let rs = test_rs(&params);

        let available: HashMap<usize, Vec<u8>> = HashMap::new();
        let result = decode(&params, &rs, &available, &[]);

        assert!(result.is_ok());
        assert!(result.unwrap().is_empty());
    }

    // the score counts distinct erased y-sections
    #[test]
    fn max_iscore() {
        let params = test_params();

        assert_eq!(get_max_iscore(&params, &[]), 0);
        assert_eq!(get_max_iscore(&params, &[0]), 1);
        assert_eq!(get_max_iscore(&params, &[0, 1]), 1);
        assert_eq!(get_max_iscore(&params, &[0, 2]), 2);
    }
}

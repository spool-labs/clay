//! Decoding and erasure recovery for Clay codes
//!
//! This module implements the layered decoding algorithm from the FAST'18 paper.
//! It handles both full decoding (all chunks available) and erasure recovery
//! (up to m chunks missing).

use std::collections::{BTreeSet, HashMap};

use reed_solomon_erasure::galois_8::{self, add as gf_add, mul as gf_mul};

use crate::coords::get_plane_vector;
use crate::encode::EncodeParams;
use crate::error::ClayError;
use crate::transforms::{
    compute_c_from_u_and_cstar, compute_u_from_c_and_ustar, pft_compute_both, prt_compute_both,
    GAMMA,
};

/// Parameters needed for decoding (same as encode for now)
pub type DecodeParams = EncodeParams;

/// Recover original data from available chunks
///
/// # Parameters
/// - `params`: Code parameters
/// - `available`: Map from chunk index to chunk data
/// - `erasures`: Set of erased chunk indices
///
/// # Returns
/// Recovered original data, or error if decoding fails
pub fn decode(
    params: &DecodeParams,
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

    // Build full chunks array with proper node indices
    let mut chunks: Vec<Vec<u8>> = vec![vec![0u8; chunk_size]; total_nodes];

    // Copy available chunks, mapping from external (k data + m parity) to internal indices
    for (&idx, data) in available.iter() {
        let internal_idx = if idx < params.k { idx } else { idx + params.nu };
        chunks[internal_idx] = data.clone();
    }

    // Build erasure set with internal indices
    // Note: shortened nodes are NOT erasures - they are known zeros
    let mut erased_set: BTreeSet<usize> = BTreeSet::new();
    for &e in erasures {
        let internal_idx = if e < params.k { e } else { e + params.nu };
        erased_set.insert(internal_idx);
    }

    // Shortened nodes are KNOWN zeros, already set in chunks array
    // They should NOT be added to erased_set

    // Decode
    decode_layered(params, &erased_set, &mut chunks, sub_chunk_size)?;

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
pub(crate) fn decode_layered(
    params: &DecodeParams,
    erased_chunks: &BTreeSet<usize>,
    chunks: &mut Vec<Vec<u8>>,
    sub_chunk_size: usize,
) -> Result<(), ClayError> {
    let total_nodes = params.q * params.t;

    // Create RS codec once for all layers
    let rs = reed_solomon_erasure::ReedSolomon::<galois_8::Field>::new(
        params.original_count,
        params.recovery_count,
    )
    .map_err(|e| ClayError::ReconstructionFailed(format!("RS init failed: {:?}", e)))?;

    // Initialize U buffers
    let chunk_size = chunks[0].len();
    let mut u_buf: Vec<Vec<u8>> = vec![vec![0u8; chunk_size]; total_nodes];

    // Track which U values have been computed (for using across iterations)
    let mut u_computed: Vec<Vec<bool>> = vec![vec![false; params.sub_chunk_no]; total_nodes];

    // Compute layer order by intersection score
    let mut order: Vec<usize> = vec![0; params.sub_chunk_no];
    set_planes_sequential_decoding_order(params, &mut order, erased_chunks);

    let max_iscore = get_max_iscore(params, erased_chunks);

    // Process layers in order of increasing intersection score
    for iscore in 0..=max_iscore {
        // First pass: decode erasures for layers with this iscore
        for z in 0..params.sub_chunk_no {
            if order[z] == iscore {
                decode_layered_with_tracking(
                    params,
                    erased_chunks,
                    z,
                    chunks,
                    &mut u_buf,
                    &mut u_computed,
                    sub_chunk_size,
                    &rs,
                )?;
            }
        }

        // Second pass: recover C values from U values
        for z in 0..params.sub_chunk_no {
            if order[z] == iscore {
                let z_vec = get_plane_vector(z, params.t, params.q);

                for &node_xy in erased_chunks {
                    let x = node_xy % params.q;
                    let y = node_xy / params.q;
                    let z_y = z_vec[y];
                    let node_sw = y * params.q + z_y;
                    let z_sw = get_companion_layer(params, z, x, y, z_y);

                    if z_y != x {
                        if !erased_chunks.contains(&node_sw) {
                            // Type 1: companion is not erased
                            recover_type1_erasure(
                                params,
                                chunks,
                                &u_buf,
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
                                params, chunks, &u_buf, x, y, z, z_y, z_sw, sub_chunk_size,
                            );
                        }
                    } else {
                        // Red vertex: C = U
                        let offset = z * sub_chunk_size;
                        chunks[node_xy][offset..offset + sub_chunk_size]
                            .copy_from_slice(&u_buf[node_xy][offset..offset + sub_chunk_size]);
                    }
                }
            }
        }
    }

    Ok(())
}

/// Decode erasures for a single layer with U tracking
fn decode_layered_with_tracking(
    params: &DecodeParams,
    erased_chunks: &BTreeSet<usize>,
    z: usize,
    chunks: &[Vec<u8>],
    u_buf: &mut [Vec<u8>],
    u_computed: &mut [Vec<bool>],
    sub_chunk_size: usize,
    rs: &reed_solomon_erasure::ReedSolomon<galois_8::Field>,
) -> Result<(), ClayError> {
    let z_vec = get_plane_vector(z, params.t, params.q);

    // Track nodes that need MDS recovery for this layer
    let mut needs_mds: BTreeSet<usize> = erased_chunks.clone();

    // Compute U values for non-erased nodes
    for x in 0..params.q {
        for y in 0..params.t {
            let node_xy = params.q * y + x;
            let z_y = z_vec[y];
            let node_sw = params.q * y + z_y;
            let z_sw = get_companion_layer(params, z, x, y, z_y);

            if !erased_chunks.contains(&node_xy) {
                if z_y == x {
                    // Red vertex: U = C (no companion needed)
                    let offset = z * sub_chunk_size;
                    u_buf[node_xy][offset..offset + sub_chunk_size]
                        .copy_from_slice(&chunks[node_xy][offset..offset + sub_chunk_size]);
                    u_computed[node_xy][z] = true;
                } else if !erased_chunks.contains(&node_sw) {
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
                        // Use U = det*C + γ*U* to compute U from C and known U*
                        let offset_z = z * sub_chunk_size;
                        let offset_zsw = z_sw * sub_chunk_size;
                        let c_xy = &chunks[node_xy][offset_z..offset_z + sub_chunk_size];
                        let u_sw = &u_buf[node_sw][offset_zsw..offset_zsw + sub_chunk_size];
                        let u_xy = compute_u_from_c_and_ustar(c_xy, u_sw);
                        u_buf[node_xy][offset_z..offset_z + sub_chunk_size].copy_from_slice(&u_xy);
                        u_computed[node_xy][z] = true;
                    } else {
                        // Companion's U not available yet - mark for MDS
                        needs_mds.insert(node_xy);
                    }
                }
            }
        }
    }

    // Decode uncoupled layer using MDS
    decode_uncoupled_layer(params, &needs_mds, z, sub_chunk_size, u_buf, rs)?;

    // Mark reconstructed nodes as computed
    for &node in &needs_mds {
        u_computed[node][z] = true;
    }

    Ok(())
}

/// Decode uncoupled layer using RS MDS code
pub(crate) fn decode_uncoupled_layer(
    params: &DecodeParams,
    erased_chunks: &BTreeSet<usize>,
    z: usize,
    sub_chunk_size: usize,
    u_buf: &mut [Vec<u8>],
    rs: &reed_solomon_erasure::ReedSolomon<galois_8::Field>,
) -> Result<(), ClayError> {
    let total_nodes = params.q * params.t;
    let offset = z * sub_chunk_size;
    let parity_start = params.original_count; // k + nu

    // Check if we have too many erasures for this layer
    if erased_chunks.len() > params.m {
        return Err(ClayError::TooManyErasures {
            max: params.m,
            actual: erased_chunks.len(),
        });
    }

    // If no erasures, nothing to do
    if erased_chunks.is_empty() {
        return Ok(());
    }

    // Check if we have erased originals or parities
    let has_erased_originals = erased_chunks.iter().any(|&i| i < parity_start);
    let has_erased_parities = erased_chunks.iter().any(|&i| i >= parity_start);

    if has_erased_originals {
        // Build shards as Option<Vec<u8>> for reconstruction
        let mut shards: Vec<Option<Vec<u8>>> = Vec::with_capacity(total_nodes);

        for i in 0..total_nodes {
            if erased_chunks.contains(&i) {
                shards.push(None);
            } else {
                shards.push(Some(u_buf[i][offset..offset + sub_chunk_size].to_vec()));
            }
        }

        // Reconstruct missing shards
        rs.reconstruct(&mut shards).map_err(|e| {
            ClayError::ReconstructionFailed(format!("Layer {} RS reconstruct failed: {:?}", z, e))
        })?;

        // Copy restored shards back
        for i in 0..total_nodes {
            if erased_chunks.contains(&i) {
                if let Some(ref data) = shards[i] {
                    u_buf[i][offset..offset + sub_chunk_size].copy_from_slice(data);
                }
            }
        }
    } else if has_erased_parities {
        // Only parity shards erased - just re-encode
        let mut shards: Vec<Vec<u8>> = Vec::with_capacity(total_nodes);

        for i in 0..total_nodes {
            shards.push(u_buf[i][offset..offset + sub_chunk_size].to_vec());
        }

        // Encode to regenerate parity shards
        rs.encode(&mut shards).map_err(|e| {
            ClayError::ReconstructionFailed(format!("Layer {} RS encode failed: {:?}", z, e))
        })?;

        // Copy regenerated parity shards back
        for i in parity_start..total_nodes {
            if erased_chunks.contains(&i) {
                u_buf[i][offset..offset + sub_chunk_size].copy_from_slice(&shards[i]);
            }
        }
    }

    Ok(())
}

/// Get companion layer index with proper modular arithmetic
///
/// z_sw = (z + (x - z_y) * q^(t-1-y)) mod α
pub(crate) fn get_companion_layer(params: &DecodeParams, z: usize, x: usize, y: usize, z_y: usize) -> usize {
    debug_assert!(y < params.t, "y={} must be < t={}", y, params.t);
    debug_assert!(x < params.q, "x={} must be < q={}", x, params.q);
    debug_assert!(z_y < params.q, "z_y={} must be < q={}", z_y, params.q);
    debug_assert!(
        z < params.sub_chunk_no,
        "z={} must be < α={}",
        z,
        params.sub_chunk_no
    );

    let alpha = params.sub_chunk_no as isize;
    let multiplier = params.q.pow((params.t - 1 - y) as u32) as isize;
    let diff = x as isize - z_y as isize;
    let z_sw = ((z as isize) + diff * multiplier).rem_euclid(alpha) as usize;
    debug_assert!(
        z_sw < params.sub_chunk_no,
        "z_sw out of bounds: {} >= {}",
        z_sw,
        params.sub_chunk_no
    );
    z_sw
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

    // Determine which is C and which is C* based on x vs z_y
    let (u_xy, u_sw) = if x < z_y {
        prt_compute_both(c_xy, c_sw)
    } else {
        let (u_sw, u_xy) = prt_compute_both(c_sw, c_xy);
        (u_xy, u_sw)
    };

    u_buf[node_xy][offset_z..offset_z + sub_chunk_size].copy_from_slice(&u_xy);
    u_buf[node_sw][offset_zsw..offset_zsw + sub_chunk_size].copy_from_slice(&u_sw);
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

    let c_sw = &chunks[node_sw][offset_zsw..offset_zsw + sub_chunk_size];
    let u_xy = &u_buf[node_xy][offset_z..offset_z + sub_chunk_size];

    // Compute C from U and C*
    let c_xy = compute_c_from_u_and_cstar(u_xy, c_sw);

    chunks[node_xy][offset_z..offset_z + sub_chunk_size].copy_from_slice(&c_xy);
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

    // PFT: compute C from U pair
    let (c_xy, c_sw) = if x < z_y {
        pft_compute_both(u_xy, u_sw)
    } else {
        let (c_sw, c_xy) = pft_compute_both(u_sw, u_xy);
        (c_xy, c_sw)
    };

    chunks[node_xy][offset_z..offset_z + sub_chunk_size].copy_from_slice(&c_xy);
    chunks[node_sw][offset_zsw..offset_zsw + sub_chunk_size].copy_from_slice(&c_sw);
}

/// Set decoding order based on intersection scores
fn set_planes_sequential_decoding_order(
    params: &DecodeParams,
    order: &mut [usize],
    erasures: &BTreeSet<usize>,
) {
    for z in 0..params.sub_chunk_no {
        let z_vec = get_plane_vector(z, params.t, params.q);
        order[z] = 0;
        for &i in erasures {
            if i % params.q == z_vec[i / params.q] {
                order[z] += 1;
            }
        }
    }
}

/// Get maximum intersection score
fn get_max_iscore(params: &DecodeParams, erased_chunks: &BTreeSet<usize>) -> usize {
    let mut weight_vec = vec![false; params.t];
    let mut iscore = 0;

    for &i in erased_chunks {
        let y = i / params.q;
        if !weight_vec[y] {
            weight_vec[y] = true;
            iscore += 1;
        }
    }

    iscore
}

/// Compute C* from C and U (for repair)
///
/// companion_value = (U + C) / γ
pub(crate) fn compute_cstar_from_c_and_u(c_helper: &[u8], u_helper: &[u8]) -> Vec<u8> {
    let len = c_helper.len();
    let mut companion_c = vec![0u8; len];
    let gamma_inv = crate::transforms::gf_inv(GAMMA);

    for i in 0..len {
        companion_c[i] = gf_mul(gf_add(u_helper[i], c_helper[i]), gamma_inv);
    }

    companion_c
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
            recovery_count: 2,
        }
    }

    #[test]
    fn test_companion_layer_valid_range() {
        let params = test_params();

        for z in 0..params.sub_chunk_no {
            let z_vec = get_plane_vector(z, params.t, params.q);
            for y in 0..params.t {
                for x in 0..params.q {
                    let z_sw = get_companion_layer(&params, z, x, y, z_vec[y]);
                    assert!(
                        z_sw < params.sub_chunk_no,
                        "z_sw {} out of range for z={}, x={}, y={}",
                        z_sw,
                        z,
                        x,
                        y
                    );
                }
            }
        }
    }

    #[test]
    fn test_decode_empty_both() {
        let params = test_params();
        let available: HashMap<usize, Vec<u8>> = HashMap::new();
        let result = decode(&params, &available, &[]);
        assert!(result.is_ok());
        assert!(result.unwrap().is_empty());
    }

    #[test]
    fn test_get_max_iscore() {
        let params = test_params();

        // No erasures
        let empty: BTreeSet<usize> = BTreeSet::new();
        assert_eq!(get_max_iscore(&params, &empty), 0);

        // One erasure
        let mut one: BTreeSet<usize> = BTreeSet::new();
        one.insert(0);
        assert_eq!(get_max_iscore(&params, &one), 1);

        // Two erasures in same y-section
        let mut two_same: BTreeSet<usize> = BTreeSet::new();
        two_same.insert(0);
        two_same.insert(1);
        assert_eq!(get_max_iscore(&params, &two_same), 1);

        // Two erasures in different y-sections
        let mut two_diff: BTreeSet<usize> = BTreeSet::new();
        two_diff.insert(0);
        two_diff.insert(2);
        assert_eq!(get_max_iscore(&params, &two_diff), 2);
    }
}

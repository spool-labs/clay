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
    u_computed: Vec<bool>,
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

    // Borrow available chunks in internal order instead of cloning them; only
    // erased slots get owned buffers, and shortened nodes share one zero row
    let zero_row = vec![0u8; chunk_size];
    let mut available_rows: Vec<Option<&[u8]>> = vec![None; total_nodes];
    let mut erased_rows: Vec<Vec<u8>> = Vec::with_capacity(total_nodes);
    for internal_idx in 0..total_nodes {
        if internal_idx >= params.k && internal_idx < params.k + params.nu {
            available_rows[internal_idx] = Some(&zero_row);
            erased_rows.push(Vec::new());
            continue;
        }

        let external_idx = if internal_idx < params.k {
            internal_idx
        } else {
            internal_idx - params.nu
        };
        match available.get(&external_idx) {
            Some(data) => {
                available_rows[internal_idx] = Some(data.as_slice());
                erased_rows.push(Vec::new());
            }
            None => erased_rows.push(vec![0u8; chunk_size]),
        }
    }

    decode_layered(params, rs, &available_rows, &mut erased_rows, sub_chunk_size)?;

    // Assemble the original data from the first k rows
    let mut result = Vec::with_capacity(params.k * chunk_size);
    for i in 0..params.k {
        match available_rows[i] {
            Some(row) => result.extend_from_slice(row),
            None => result.extend_from_slice(&erased_rows[i]),
        }
    }

    Ok(result)
}

/// Main layered decoding algorithm
///
/// Processes layers in order of increasing intersection score, applying
/// PRT/PFT transforms and RS decoding as needed. A node is erased exactly
/// when its slot in `available_rows` is `None`; recovered chunks are written
/// into the matching slot of `erased_rows`, which must hold a chunk-sized
/// buffer for every erased node.
pub fn decode_layered(
    params: &DecodeParams,
    rs: &RsCodec,
    available_rows: &[Option<&[u8]>],
    erased_rows: &mut [Vec<u8>],
    sub_chunk_size: usize,
) -> Result<(), ClayError> {
    let total_nodes = params.q * params.t;
    let alpha = params.sub_chunk_no;
    let geometry = LayerGeometry::new(params.q, params.t, alpha);

    let mut is_erased: Vec<bool> = vec![false; total_nodes];
    let mut erased_nodes: Vec<usize> = Vec::new();
    for (node, row) in available_rows.iter().enumerate() {
        if row.is_none() {
            is_erased[node] = true;
            erased_nodes.push(node);
        }
    }

    let chunk_size = sub_chunk_size * alpha;
    let mut scratch = DecodeScratch {
        u_buf: vec![vec![0u8; chunk_size]; total_nodes],
        u_computed: vec![false; total_nodes * alpha],
        needs_mds: vec![false; total_nodes],
    };

    // Bucket layers by intersection score so each pass visits only its own layers
    let max_iscore = get_max_iscore(params, &erased_nodes);
    let mut layers_by_iscore: Vec<Vec<usize>> = vec![Vec::new(); max_iscore + 1];
    for z in 0..alpha {
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
                &is_erased,
                z,
                available_rows,
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

                let offset_z = z * sub_chunk_size;
                let offset_zsw = z_sw * sub_chunk_size;

                if z_y != x {
                    if let Some(c_sw_row) = available_rows[node_sw] {
                        // Type 1: companion is not erased, its C pins down ours
                        compute_c_into(
                            &scratch.u_buf[node_xy][offset_z..offset_z + sub_chunk_size],
                            &c_sw_row[offset_zsw..offset_zsw + sub_chunk_size],
                            &mut erased_rows[node_xy][offset_z..offset_z + sub_chunk_size],
                        );
                    } else if z_y < x {
                        // Both erased, run the forward transform once per pair
                        let u_xy = &scratch.u_buf[node_xy][offset_z..offset_z + sub_chunk_size];
                        let u_sw = &scratch.u_buf[node_sw][offset_zsw..offset_zsw + sub_chunk_size];

                        let (c_xy_row, c_sw_row) = pair_mut(erased_rows, node_xy, node_sw);
                        pft_into(
                            u_xy,
                            u_sw,
                            &mut c_xy_row[offset_z..offset_z + sub_chunk_size],
                            &mut c_sw_row[offset_zsw..offset_zsw + sub_chunk_size],
                        );
                    }
                } else {
                    // Red vertex: C = U
                    erased_rows[node_xy][offset_z..offset_z + sub_chunk_size].copy_from_slice(
                        &scratch.u_buf[node_xy][offset_z..offset_z + sub_chunk_size],
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
    available_rows: &[Option<&[u8]>],
    scratch: &mut DecodeScratch,
    sub_chunk_size: usize,
) -> Result<(), ClayError> {
    let digits = geometry.plane_digits(z);
    let alpha = params.sub_chunk_no;
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

    // Compute U values for available nodes
    for x in 0..params.q {
        for y in 0..params.t {
            let node_xy = params.q * y + x;
            let c_row = match available_rows[node_xy] {
                Some(c_row) => c_row,
                None => continue,
            };

            let z_y = digits[y];
            let node_sw = params.q * y + z_y;
            let z_sw = geometry.companion_layer(z, x, y, z_y);

            let offset_z = z * sub_chunk_size;
            let offset_zsw = z_sw * sub_chunk_size;

            if z_y == x {
                // Red vertex: U = C (no companion needed)
                u_buf[node_xy][offset_z..offset_z + sub_chunk_size]
                    .copy_from_slice(&c_row[offset_z..offset_z + sub_chunk_size]);
                u_computed[node_xy * alpha + z] = true;
            } else if let Some(c_sw_row) = available_rows[node_sw] {
                // Both nodes available - apply PRT (only process once when z_y < x)
                if z_y < x {
                    let c_xy = &c_row[offset_z..offset_z + sub_chunk_size];
                    let c_sw = &c_sw_row[offset_zsw..offset_zsw + sub_chunk_size];

                    let (u_xy_buf, u_sw_buf) = pair_mut(u_buf, node_xy, node_sw);
                    prt_into(
                        c_xy,
                        c_sw,
                        &mut u_xy_buf[offset_z..offset_z + sub_chunk_size],
                        &mut u_sw_buf[offset_zsw..offset_zsw + sub_chunk_size],
                    );
                    u_computed[node_xy * alpha + z] = true;
                    u_computed[node_sw * alpha + z_sw] = true;
                }
            } else if u_computed[node_sw * alpha + z_sw] {
                // Companion is erased but its U* is known from an earlier
                // layer: U = det*C + gamma*U*
                let (u_xy_buf, u_sw_buf) = pair_mut(u_buf, node_xy, node_sw);
                compute_u_into(
                    &c_row[offset_z..offset_z + sub_chunk_size],
                    &u_sw_buf[offset_zsw..offset_zsw + sub_chunk_size],
                    &mut u_xy_buf[offset_z..offset_z + sub_chunk_size],
                );
                u_computed[node_xy * alpha + z] = true;
            } else {
                // Companion's U not available yet - mark for MDS
                scratch.needs_mds[node_xy] = true;
                mds_count += 1;
            }
        }
    }

    // Decode uncoupled layer using MDS
    decode_uncoupled_layer(
        params,
        rs,
        &scratch.needs_mds,
        mds_count,
        z,
        z * sub_chunk_size,
        sub_chunk_size,
        u_buf,
    )?;

    // Mark reconstructed nodes as computed
    for (node, &needed) in scratch.needs_mds.iter().enumerate() {
        if needed {
            u_computed[node * alpha + z] = true;
        }
    }

    Ok(())
}

/// Decode uncoupled layer using RS MDS code
///
/// `offset` locates layer `z` inside each node's U buffer; repair packs its
/// buffers densely, so the offset is not always `z * sub_chunk_size`.
pub fn decode_uncoupled_layer(
    params: &DecodeParams,
    rs: &RsCodec,
    is_erased: &[bool],
    erased_count: usize,
    z: usize,
    offset: usize,
    sub_chunk_size: usize,
    u_buf: &mut [Vec<u8>],
) -> Result<(), ClayError> {
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

//! Single-node repair for Clay codes
//!
//! This module implements the optimal repair algorithm from the FAST'18 paper.
//! Clay codes achieve MSR (Minimum Storage Regenerating) repair bandwidth by
//! downloading only β = α/q sub-chunks from each of d helper nodes, rather
//! than k full chunks.

use std::collections::HashMap;

use crate::checked_pow;
use crate::coords::LayerGeometry;
use crate::decode::{decode_uncoupled_layer, DecodeParams, NodeRows, RsCodec};
use crate::error::ClayError;
use crate::transforms::{compute_cstar_into, compute_u_into, prt_into};

/// Parameters needed for repair (alias to DecodeParams)
pub type RepairParams = DecodeParams;

/// Get the list of sub-chunk indices needed for repair
///
/// These are the layers where the lost node is "red" (unpaired).
pub fn get_repair_subchunk_indices(
    params: &RepairParams,
    lost_node: usize,
) -> Result<Vec<usize>, ClayError> {
    let y_lost = lost_node / params.q;
    let x_lost = lost_node % params.q;

    let seq_sc_count = checked_pow(params.q, params.t - 1 - y_lost).ok_or_else(|| {
        ClayError::Overflow(format!(
            "q^(t-1-y) = {}^{} overflows",
            params.q,
            params.t - 1 - y_lost
        ))
    })?;
    let num_seq = checked_pow(params.q, y_lost).ok_or_else(|| {
        ClayError::Overflow(format!("q^y = {}^{} overflows", params.q, y_lost))
    })?;

    let beta = params.sub_chunk_no / params.q;
    let mut result = Vec::with_capacity(beta);
    for seq in 0..num_seq {
        let base = x_lost * seq_sc_count + seq * params.q * seq_sc_count;
        for offset in 0..seq_sc_count {
            result.push(base + offset);
        }
    }
    Ok(result)
}

/// Determine minimum sub-chunks needed to repair a lost node
///
/// # Parameters
/// - `params`: Code parameters
/// - `lost_node`: Index of the lost node (0 to n-1)
/// - `available`: Available node indices
///
/// # Returns
/// Vector of (helper_node_idx, sub_chunk_indices) where sub_chunk_indices
/// is a vector of the specific sub-chunk indices needed from that helper.
pub fn minimum_to_repair(
    params: &RepairParams,
    lost_node: usize,
    available: &[usize],
) -> Result<Vec<(usize, Vec<usize>)>, ClayError> {
    if lost_node >= params.n {
        return Err(ClayError::InvalidParameters(format!(
            "Invalid lost node index: {} >= {}",
            lost_node, params.n
        )));
    }

    // Convert to internal index
    let lost_internal = if lost_node < params.k {
        lost_node
    } else {
        lost_node + params.nu
    };

    // Get repair sub-chunk indices (the layers where lost node is "red")
    let repair_sub_chunk_indices = get_repair_subchunk_indices(params, lost_internal)?;

    let d = params.k + params.q - 1; // d = k + q - 1 for Clay codes
    let mut result = Vec::new();

    // First, add all nodes in the lost node's y-section (except the lost node itself)
    // These MUST be included for the repair algorithm to work
    let y_section = lost_internal / params.q;
    for x in 0..params.q {
        let node = y_section * params.q + x;
        if node != lost_internal {
            // Convert internal index to external
            let external_idx = if node < params.k {
                node
            } else if node >= params.k + params.nu {
                node - params.nu
            } else {
                continue; // Skip shortened nodes
            };

            if available.contains(&external_idx) {
                result.push((external_idx, repair_sub_chunk_indices.clone()));
            }
        }
    }

    // Add more helpers until we have d total
    for &node in available {
        if result.len() >= d {
            break;
        }
        if !result.iter().any(|(n, _)| *n == node) && node != lost_node {
            result.push((node, repair_sub_chunk_indices.clone()));
        }
    }

    if result.len() < d {
        return Err(ClayError::InsufficientHelpers {
            needed: d,
            provided: result.len(),
        });
    }

    result.truncate(d);
    Ok(result)
}

/// Repair a lost chunk using partial data from helper nodes
///
/// # Parameters
/// - `params`: Code parameters
/// - `rs`: Reed-Solomon codec built for these parameters
/// - `lost_node`: Index of the lost node (0 to n-1)
/// - `helper_data`: Map from helper node index to partial chunk data.
///   Each helper's data must be the concatenation of sub-chunks at the
///   indices returned by minimum_to_repair(), in that exact order.
/// - `chunk_size`: Full chunk size
///
/// # Returns
/// The recovered full chunk, or error if repair fails
pub fn repair(
    params: &RepairParams,
    rs: &RsCodec,
    lost_node: usize,
    helper_data: &HashMap<usize, Vec<u8>>,
    chunk_size: usize,
) -> Result<Vec<u8>, ClayError> {
    let mut helpers: Vec<Option<&[u8]>> = vec![None; params.n];
    for (&ext_idx, data) in helper_data.iter() {
        if ext_idx >= params.n {
            return Err(ClayError::InvalidParameters(format!(
                "Helper index {} out of range [0, {})",
                ext_idx, params.n
            )));
        }
        helpers[ext_idx] = Some(data.as_slice());
    }
    repair_rows(params, rs, lost_node, &helpers, chunk_size)
}

/// Repair a lost chunk from helper partials indexed by node
///
/// The zero-copy form of repair. One slot per node, `None` where a node is not
/// contributing, and each entry holds that helper's sub-chunks in the order
/// minimum_to_repair returned them.
pub fn repair_rows(
    params: &RepairParams,
    rs: &RsCodec,
    lost_node: usize,
    helpers: &[Option<&[u8]>],
    chunk_size: usize,
) -> Result<Vec<u8>, ClayError> {
    if helpers.len() != params.n {
        return Err(ClayError::InvalidParameters(format!(
            "Expected {} helper slots (n), got {}",
            params.n,
            helpers.len()
        )));
    }
    let d = params.k + params.q - 1;

    if lost_node >= params.n {
        return Err(ClayError::InvalidParameters(format!(
            "Invalid lost node index: {} >= {}",
            lost_node, params.n
        )));
    }

    let helper_count = helpers.iter().filter(|slot| slot.is_some()).count();
    if helper_count < d {
        return Err(ClayError::InsufficientHelpers {
            needed: d,
            provided: helper_count,
        });
    }

    if chunk_size == 0 || chunk_size % params.sub_chunk_no != 0 {
        return Err(ClayError::InvalidChunkSize {
            expected: params.sub_chunk_no,
            actual: chunk_size,
        });
    }

    let lost_internal = if lost_node < params.k {
        lost_node
    } else {
        lost_node + params.nu
    };

    let repair_sub_chunk_indices = get_repair_subchunk_indices(params, lost_internal)?;
    let sub_chunk_size = chunk_size / params.sub_chunk_no;
    let expected_helper_bytes = repair_sub_chunk_indices.len() * sub_chunk_size;

    let total_nodes = params.q * params.t;

    // Validate that all required y-section helpers are present
    let lost_y = lost_internal / params.q;
    for x in 0..params.q {
        let node = lost_y * params.q + x;
        if node == lost_internal {
            continue; // This is the lost node itself
        }
        // Skip shortened nodes
        if node >= params.k && node < params.k + params.nu {
            continue;
        }
        // Convert internal to external
        let external_idx = if node < params.k {
            node
        } else {
            node - params.nu
        };
        if helpers[external_idx].is_none() {
            return Err(ClayError::MissingYSectionHelper {
                lost_node,
                missing_helper: external_idx,
            });
        }
    }

    let geometry = LayerGeometry::new(params.q, params.t, params.sub_chunk_no);

    // Repair only ever touches the beta repair planes (their companions are
    // repair planes too), so the U scratch is packed densely by plane position
    // instead of being allocated for all alpha layers
    let beta = repair_sub_chunk_indices.len();
    let mut u_buf = NodeRows::new(total_nodes, beta * sub_chunk_size);

    // Track which U values have been computed (for dependency checking)
    let mut u_computed: Vec<bool> = vec![false; total_nodes * beta];

    // Create recovered data buffer
    let mut recovered = vec![0u8; chunk_size];

    // Stage helper slices by internal node index and validate sizes
    let zero_data = vec![0u8; expected_helper_bytes];
    let mut helper_slices: Vec<Option<&[u8]>> = vec![None; total_nodes];
    for (ext_idx, slot) in helpers.iter().enumerate() {
        let Some(data) = slot else { continue };
        let internal = if ext_idx < params.k {
            ext_idx
        } else {
            ext_idx + params.nu
        };
        if data.len() != expected_helper_bytes {
            return Err(ClayError::InsufficientHelperData {
                helper: ext_idx,
                expected: expected_helper_bytes,
                actual: data.len(),
            });
        }
        helper_slices[internal] = Some(data);
    }

    // Aloof nodes are neither helpers, the lost node, nor shortened
    let mut is_aloof: Vec<bool> = vec![false; total_nodes];
    let mut aloof_nodes: Vec<usize> = Vec::new();
    for (node, slice) in helper_slices.iter().enumerate() {
        if node != lost_internal
            && slice.is_none()
            && (node < params.k || node >= params.k + params.nu)
        {
            is_aloof[node] = true;
            aloof_nodes.push(node);
        }
    }

    // Shortened nodes act as helpers with known-zero data
    for node in params.k..(params.k + params.nu) {
        helper_slices[node] = Some(&zero_data);
    }

    // Position of each repair plane inside the helper payloads
    let mut plane_position: Vec<Option<usize>> = vec![None; params.sub_chunk_no];
    for (position, &z) in repair_sub_chunk_indices.iter().enumerate() {
        plane_position[z] = Some(position);
    }

    // Bucket repair planes by intersection score, keeping each plane's payload position
    let mut planes_by_iscore: Vec<Vec<(usize, usize)>> = vec![Vec::new(); aloof_nodes.len() + 2];
    for (position, &z) in repair_sub_chunk_indices.iter().enumerate() {
        let digits = geometry.plane_digits(z);
        let mut iscore = 0;

        if lost_internal % params.q == digits[lost_internal / params.q] {
            iscore += 1;
        }
        for &node in &aloof_nodes {
            if node % params.q == digits[node / params.q] {
                iscore += 1;
            }
        }

        planes_by_iscore[iscore].push((z, position));
    }

    // Base erasure set: lost node's y-section + aloof nodes
    let mut base_erased: Vec<bool> = vec![false; total_nodes];
    let mut base_count = 0;
    for x in 0..params.q {
        base_erased[lost_y * params.q + x] = true;
        base_count += 1;
    }
    for &node in &aloof_nodes {
        if !base_erased[node] {
            base_erased[node] = true;
            base_count += 1;
        }
    }
    let mut base_erasure_nodes: Vec<usize> = Vec::with_capacity(base_count);
    for (node, &erased) in base_erased.iter().enumerate() {
        if erased {
            base_erasure_nodes.push(node);
        }
    }

    // Per-plane erasure scratch, reset from the base set for every plane
    let mut layer_erased: Vec<bool> = vec![false; total_nodes];

    // One entry per plane in the current pass: (z, position, erasure set, count).
    // Phase 1 fills this for the whole pass so phase 2 can merge planes that
    // agree on the erasure set into a single RS call.
    let mut plane_patterns: Vec<(usize, usize, Vec<bool>, usize)> = Vec::with_capacity(beta);

    // Process planes in order of increasing intersection score
    for planes in &planes_by_iscore {
        if planes.is_empty() {
            continue;
        }

        // Phase 1 runs for every plane in the pass before any RS call, so the
        // pass can then issue one merged call per contiguous run of planes
        plane_patterns.clear();
        for &(z, position) in planes {
            let digits = geometry.plane_digits(z);
            let plane_offset = position * sub_chunk_size;

            // Per-plane erasure set: base erasures plus any node whose U we
            // cannot compute this pass
            layer_erased.copy_from_slice(&base_erased);
            let mut layer_count = base_count;

            // Phase 1: Compute U values from C values for non-erased nodes
            for y in 0..params.t {
                for x in 0..params.q {
                    let node_xy = y * params.q + x;

                    if base_erased[node_xy] {
                        continue;
                    }
                    let helper_chunk = match helper_slices[node_xy] {
                        Some(helper_chunk) => helper_chunk,
                        None => {
                            // No helper data for this node - mark for MDS
                            layer_erased[node_xy] = true;
                            layer_count += 1;
                            continue;
                        }
                    };

                    let z_y = digits[y];
                    let z_sw = geometry.companion_layer(z, x, y, z_y);
                    let node_sw = y * params.q + z_y;

                    if z_y == x {
                        // Red vertex: U = C
                        u_buf.row_mut(node_xy)[plane_offset..plane_offset + sub_chunk_size]
                            .copy_from_slice(
                                &helper_chunk[plane_offset..plane_offset + sub_chunk_size],
                            );
                        u_computed[node_xy * beta + position] = true;
                    } else if is_aloof[node_sw] {
                        // Companion is aloof - its U* must come from an earlier plane
                        match plane_position[z_sw] {
                            Some(sw_position) if u_computed[node_sw * beta + sw_position] => {
                                let c_xy =
                                    &helper_chunk[plane_offset..plane_offset + sub_chunk_size];
                                let sw_offset = sw_position * sub_chunk_size;

                                let (u_xy_buf, u_sw_buf) = u_buf.pair_mut(node_xy, node_sw);
                                compute_u_into(
                                    c_xy,
                                    &u_sw_buf[sw_offset..sw_offset + sub_chunk_size],
                                    &mut u_xy_buf[plane_offset..plane_offset + sub_chunk_size],
                                );
                                u_computed[node_xy * beta + position] = true;
                            }
                            Some(_) | None => {
                                // Companion's U not available - mark this node as needing MDS
                                layer_erased[node_xy] = true;
                                layer_count += 1;
                            }
                        }
                    } else if let (Some(helper_sw), Some(sw_position)) =
                        (helper_slices[node_sw], plane_position[z_sw])
                    {
                        // Both nodes are helpers with the companion plane on hand - use PRT
                        let sw_offset = sw_position * sub_chunk_size;
                        let c_xy = &helper_chunk[plane_offset..plane_offset + sub_chunk_size];
                        let c_sw = &helper_sw[sw_offset..sw_offset + sub_chunk_size];

                        let (u_xy_buf, u_sw_buf) = u_buf.pair_mut(node_xy, node_sw);
                        let u_xy = &mut u_xy_buf[plane_offset..plane_offset + sub_chunk_size];
                        let u_sw = &mut u_sw_buf[sw_offset..sw_offset + sub_chunk_size];

                        // The lower x coordinate of the pair holds C, the other C*
                        if x < z_y {
                            prt_into(c_xy, c_sw, u_xy, u_sw);
                        } else {
                            prt_into(c_sw, c_xy, u_sw, u_xy);
                        }
                        u_computed[node_xy * beta + position] = true;
                        u_computed[node_sw * beta + sw_position] = true;
                    } else if helper_slices[node_sw].is_none() {
                        // No way to compute U - mark for MDS
                        layer_erased[node_xy] = true;
                        layer_count += 1;
                    }
                }
            }

            plane_patterns.push((z, position, layer_erased.clone(), layer_count));
        }

        // Phase 2: recover U for the nodes we could not compute. Every plane in
        // a pass is independent, and the pattern census shows they share one
        // erasure set; since RS over GF(2^8) is byte-column independent, a
        // contiguous run of planes decodes exactly as those planes would one at
        // a time. Merging turns beta calls on tiny shards into one per run.
        let is_uniform = plane_patterns.windows(2).all(|pair| pair[0].2 == pair[1].2);
        debug_assert!(is_uniform, "planes in one pass disagreed on the erasure set");

        if is_uniform {
            let pattern = &plane_patterns[0].2;
            let count = plane_patterns[0].3;
            let z_first = plane_patterns[0].0;
            let mut positions: Vec<usize> = plane_patterns.iter().map(|entry| entry.1).collect();
            positions.sort_unstable();

            let mut i = 0;
            while i < positions.len() {
                let start = positions[i];
                let mut end = start;
                while i + 1 < positions.len() && positions[i + 1] == end + 1 {
                    i += 1;
                    end = positions[i];
                }
                decode_uncoupled_layer(
                    params,
                    rs,
                    pattern,
                    count,
                    z_first,
                    start * sub_chunk_size,
                    (end - start + 1) * sub_chunk_size,
                    &mut u_buf,
                )?;
                i += 1;
            }
        } else {
            // Safety net: a pass that disagrees decodes plane by plane
            for (z, position, pattern, count) in &plane_patterns {
                decode_uncoupled_layer(
                    params,
                    rs,
                    pattern,
                    *count,
                    *z,
                    position * sub_chunk_size,
                    sub_chunk_size,
                    &mut u_buf,
                )?;
            }
        }

        for (_, position, pattern, _) in &plane_patterns {
            for (node, &erased) in pattern.iter().enumerate() {
                if erased {
                    u_computed[node * beta + position] = true;
                }
            }
        }

        // Phase 3: Compute C values for the lost node
        for &(z, position) in planes {
            let digits = geometry.plane_digits(z);
            let plane_offset = position * sub_chunk_size;
            for &node in &base_erasure_nodes {
                if is_aloof[node] {
                    continue;
                }

                let x = node % params.q;
                let y = node / params.q;
                let z_y = digits[y];
                let node_sw = y * params.q + z_y;
                let z_sw = geometry.companion_layer(z, x, y, z_y);

                if x == z_y {
                    // Red vertex: C = U
                    if node == lost_internal {
                        recovered[z * sub_chunk_size..(z + 1) * sub_chunk_size].copy_from_slice(
                            &u_buf.row(node)[plane_offset..plane_offset + sub_chunk_size],
                        );
                    }
                } else if node_sw == lost_internal {
                    // node is a helper in y-section, its companion is the lost node
                    if let Some(helper_chunk) = helper_slices[node] {
                        let c_node = &helper_chunk[plane_offset..plane_offset + sub_chunk_size];
                        let u_node = &u_buf.row(node)[plane_offset..plane_offset + sub_chunk_size];

                        // The helper's C and U pin down the lost node's C at z_sw
                        compute_cstar_into(
                            c_node,
                            u_node,
                            &mut recovered[z_sw * sub_chunk_size..(z_sw + 1) * sub_chunk_size],
                        );
                    }
                }
            }
        }
    }

    Ok(recovered)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_params() -> RepairParams {
        RepairParams {
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

    #[test]
    fn test_repair_subchunk_indices_count() {
        let params = test_params();
        let beta = params.sub_chunk_no / params.q; // 8 / 2 = 4

        for lost_node in 0..params.n {
            let internal = if lost_node < params.k {
                lost_node
            } else {
                lost_node + params.nu
            };
            let indices = get_repair_subchunk_indices(&params, internal).unwrap();
            assert_eq!(
                indices.len(),
                beta,
                "Expected {} sub-chunks for node {}",
                beta,
                lost_node
            );
        }
    }

    #[test]
    fn test_minimum_to_repair_helpers_count() {
        let params = test_params();
        let d = params.k + params.q - 1; // 4 + 2 - 1 = 5

        let available: Vec<usize> = (1..params.n).collect();
        let helper_info = minimum_to_repair(&params, 0, &available).unwrap();

        assert_eq!(helper_info.len(), d);
    }

    #[test]
    fn test_minimum_to_repair_includes_y_section() {
        let params = test_params();

        // For node 0, y-section contains node 1 (both at y=0)
        let available: Vec<usize> = (1..params.n).collect();
        let helper_info = minimum_to_repair(&params, 0, &available).unwrap();

        let helpers: Vec<usize> = helper_info.iter().map(|(h, _)| *h).collect();
        assert!(
            helpers.contains(&1),
            "Y-section partner (node 1) should be included for repairing node 0"
        );
    }

    #[test]
    fn test_minimum_to_repair_insufficient_helpers() {
        let params = test_params();
        let d = params.k + params.q - 1;

        // Only provide d-1 helpers
        let available: Vec<usize> = (1..d).collect();
        let result = minimum_to_repair(&params, 0, &available);

        assert!(matches!(
            result,
            Err(ClayError::InsufficientHelpers { .. })
        ));
    }
}

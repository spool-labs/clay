//! Single-node repair for Clay codes
//!
//! This module implements the optimal repair algorithm from the FAST'18 paper.
//! Clay codes achieve MSR (Minimum Storage Regenerating) repair bandwidth by
//! downloading only β = α/q sub-chunks from each of d helper nodes, rather
//! than k full chunks.

use std::collections::{BTreeMap, BTreeSet, HashMap};

use crate::coords::get_plane_vector;
use crate::decode::{compute_cstar_from_c_and_u, decode_uncoupled_layer, get_companion_layer, DecodeParams};
use crate::error::ClayError;
use crate::transforms::{compute_u_from_c_and_ustar, prt_compute_both_oriented};

/// Parameters needed for repair (alias to DecodeParams)
pub type RepairParams = DecodeParams;

/// Checked integer power function
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
    lost_node: usize,
    helper_data: &HashMap<usize, Vec<u8>>,
    chunk_size: usize,
) -> Result<Vec<u8>, ClayError> {
    let d = params.k + params.q - 1;

    if lost_node >= params.n {
        return Err(ClayError::InvalidParameters(format!(
            "Invalid lost node index: {} >= {}",
            lost_node, params.n
        )));
    }

    if helper_data.len() < d {
        return Err(ClayError::InsufficientHelpers {
            needed: d,
            provided: helper_data.len(),
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
        if !helper_data.contains_key(&external_idx) {
            return Err(ClayError::MissingYSectionHelper {
                lost_node,
                missing_helper: external_idx,
            });
        }
    }

    // Initialize U buffers for all nodes
    let mut u_buf: Vec<Vec<u8>> = vec![vec![0u8; chunk_size]; total_nodes];

    // Track which U values have been computed (for dependency checking)
    let mut u_computed: Vec<Vec<bool>> = vec![vec![false; params.sub_chunk_no]; total_nodes];

    // Create recovered data buffer
    let mut recovered = vec![0u8; chunk_size];

    // Build helper data map with internal indices and validate sizes
    let mut helper_internal: HashMap<usize, Vec<u8>> = HashMap::new();
    for (&ext_idx, data) in helper_data.iter() {
        if ext_idx >= params.n {
            return Err(ClayError::InvalidParameters(format!(
                "Helper index {} out of range [0, {})",
                ext_idx, params.n
            )));
        }
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
        helper_internal.insert(internal, data.clone());
    }

    // Build set of aloof nodes (not helpers and not the lost node)
    let mut aloof_nodes: BTreeSet<usize> = BTreeSet::new();
    for i in 0..total_nodes {
        if i != lost_internal && !helper_internal.contains_key(&i) {
            if i < params.k || i >= params.k + params.nu {
                aloof_nodes.insert(i);
            }
        }
    }

    // Add shortened nodes as helpers with zero data
    let zero_data = vec![0u8; expected_helper_bytes];
    for i in params.k..(params.k + params.nu) {
        helper_internal.insert(i, zero_data.clone());
    }

    // Build mapping from layer z to position in helper data
    let mut repair_plane_to_ind: HashMap<usize, usize> = HashMap::new();
    for (idx, &z) in repair_sub_chunk_indices.iter().enumerate() {
        repair_plane_to_ind.insert(z, idx);
    }

    // Build ordered planes by intersection score
    let mut ordered_planes: BTreeMap<usize, Vec<usize>> = BTreeMap::new();
    for &z in &repair_sub_chunk_indices {
        let z_vec = get_plane_vector(z, params.t, params.q);
        let mut order = 0;

        // Check if lost node is "red" in this layer
        if lost_internal % params.q == z_vec[lost_internal / params.q] {
            order += 1;
        }

        // Check aloof nodes
        for &node in &aloof_nodes {
            if node % params.q == z_vec[node / params.q] {
                order += 1;
            }
        }

        ordered_planes.entry(order).or_default().push(z);
    }

    // Base erasure set: lost node's y-section + aloof nodes
    let mut base_erasures: BTreeSet<usize> = BTreeSet::new();
    for x in 0..params.q {
        base_erasures.insert(lost_y * params.q + x);
    }
    for &node in &aloof_nodes {
        base_erasures.insert(node);
    }

    // Process planes in order of increasing intersection score
    for (&_order, planes) in &ordered_planes {
        for &z in planes {
            let z_vec = get_plane_vector(z, params.t, params.q);

            // Per-layer erasure set: starts with base erasures
            // Add any node whose U we couldn't compute
            let mut layer_erasures = base_erasures.clone();

            // Phase 1: Compute U values from C values for non-erased nodes
            for y in 0..params.t {
                for x in 0..params.q {
                    let node_xy = y * params.q + x;

                    if !base_erasures.contains(&node_xy) {
                        if let Some(helper_chunk) = helper_internal.get(&node_xy) {
                            let z_y = z_vec[y];
                            let z_sw = get_companion_layer(params, z, x, y, z_y);
                            let node_sw = y * params.q + z_y;

                            if z_y == x {
                                // Red vertex: U = C
                                let c_offset = repair_plane_to_ind[&z] * sub_chunk_size;
                                u_buf[node_xy][z * sub_chunk_size..(z + 1) * sub_chunk_size]
                                    .copy_from_slice(
                                        &helper_chunk[c_offset..c_offset + sub_chunk_size],
                                    );
                                u_computed[node_xy][z] = true;
                            } else if aloof_nodes.contains(&node_sw) {
                                // Companion is aloof - need U* from previous iteration
                                if u_computed[node_sw][z_sw] {
                                    let c_offset = repair_plane_to_ind[&z] * sub_chunk_size;
                                    let c_xy =
                                        &helper_chunk[c_offset..c_offset + sub_chunk_size];
                                    let u_sw = &u_buf[node_sw]
                                        [z_sw * sub_chunk_size..(z_sw + 1) * sub_chunk_size];

                                    // Compute U from C and U* using PFT relationship
                                    let u_xy = compute_u_from_c_and_ustar(c_xy, u_sw);
                                    u_buf[node_xy][z * sub_chunk_size..(z + 1) * sub_chunk_size]
                                        .copy_from_slice(&u_xy);
                                    u_computed[node_xy][z] = true;
                                } else {
                                    // Companion's U not available - mark this node as needing MDS
                                    layer_erasures.insert(node_xy);
                                }
                            } else if let Some(helper_sw) = helper_internal.get(&node_sw) {
                                // Both nodes are helpers - use PRT
                                if let Some(&sw_idx) = repair_plane_to_ind.get(&z_sw) {
                                    let c_xy_offset = repair_plane_to_ind[&z] * sub_chunk_size;
                                    let c_sw_offset = sw_idx * sub_chunk_size;
                                    let c_xy =
                                        &helper_chunk[c_xy_offset..c_xy_offset + sub_chunk_size];
                                    let c_sw =
                                        &helper_sw[c_sw_offset..c_sw_offset + sub_chunk_size];

                                    // PRT: compute U from C pair using correct orientation
                                    let (u_xy, u_sw_val) =
                                        prt_compute_both_oriented(c_xy, c_sw, x < z_y);
                                    u_buf[node_xy][z * sub_chunk_size..(z + 1) * sub_chunk_size]
                                        .copy_from_slice(&u_xy);
                                    u_buf[node_sw]
                                        [z_sw * sub_chunk_size..(z_sw + 1) * sub_chunk_size]
                                        .copy_from_slice(&u_sw_val);
                                    u_computed[node_xy][z] = true;
                                    u_computed[node_sw][z_sw] = true;
                                }
                            } else {
                                // No way to compute U - mark for MDS
                                layer_erasures.insert(node_xy);
                            }
                        } else {
                            // No helper data for this node - mark for MDS
                            layer_erasures.insert(node_xy);
                        }
                    }
                }
            }

            // Phase 2: Decode uncoupled code to recover U for nodes we couldn't compute
            decode_uncoupled_layer(params, &layer_erasures, z, sub_chunk_size, &mut u_buf)?;
            for &node in &layer_erasures {
                u_computed[node][z] = true;
            }

            // Phase 3: Compute C values for the lost node
            for &node in &base_erasures {
                if aloof_nodes.contains(&node) {
                    continue;
                }

                let x = node % params.q;
                let y = node / params.q;
                let z_y = z_vec[y];
                let node_sw = y * params.q + z_y;
                let z_sw = get_companion_layer(params, z, x, y, z_y);

                if x == z_y {
                    // Red vertex: C = U
                    if node == lost_internal {
                        recovered[z * sub_chunk_size..(z + 1) * sub_chunk_size].copy_from_slice(
                            &u_buf[node][z * sub_chunk_size..(z + 1) * sub_chunk_size],
                        );
                    }
                } else if node_sw == lost_internal {
                    // node is a helper in y-section, its companion is the lost node
                    if let Some(helper_chunk) = helper_internal.get(&node) {
                        let c_offset = repair_plane_to_ind[&z] * sub_chunk_size;
                        let c_node = &helper_chunk[c_offset..c_offset + sub_chunk_size];
                        let u_node = &u_buf[node][z * sub_chunk_size..(z + 1) * sub_chunk_size];

                        // Compute C* (lost node's C at z_sw) from C and U
                        let c_lost = compute_cstar_from_c_and_u(c_node, u_node);
                        recovered[z_sw * sub_chunk_size..(z_sw + 1) * sub_chunk_size]
                            .copy_from_slice(&c_lost);
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
            recovery_count: 2,
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

//! Coordinate system helpers for Clay codes
//!
//! Clay codes use a 3D coordinate system (x, y, z) where:
//! - x: position within y-section (0 to q-1)
//! - y: y-section index (0 to t-1)
//! - z: layer/plane index (0 to α-1)
//!
//! Key concepts:
//! - **y-section**: Nodes with the same y-coordinate
//! - **Companion layer**: For vertex (x, y, z), its companion is at layer z_sw
//! - **Intersection Score (IS)**: Count of erased "red" vertices in a layer
//! - **Plane vector**: The z-coordinates that make up layer z (base-q representation)

use std::collections::BTreeSet;

/// Get the companion (swap) layer for a given vertex
///
/// For a vertex at (x, y, z), the companion layer z_sw is computed by
/// swapping x with z_y (the y-th digit of z in base q).
///
/// # Arguments
/// * `z` - Current layer index
/// * `x` - x-coordinate within y-section
/// * `y` - y-section index
/// * `z_y` - The y-th digit of z in base q (from plane_vector)
/// * `q` - Coupling factor (d - k + 1)
///
/// # Returns
/// The companion layer index z_sw
pub fn get_companion_layer(z: usize, x: usize, y: usize, z_y: usize, q: usize) -> usize {
    if x == z_y {
        // Unpaired (red) vertex - companion is itself
        return z;
    }

    // Compute z_sw by swapping x with z_y at position y
    // z_sw = z - z_y * q^y + x * q^y
    let q_pow_y = q.pow(y as u32);
    z - z_y * q_pow_y + x * q_pow_y
}

/// Get the plane (layer) vector for a given z
///
/// Converts z to base-q representation, giving the z_y value for each y-section.
/// This is used to identify "red" (unpaired) vertices in each layer.
///
/// The representation stores most significant digit at index 0:
/// - z_vec[0] = coefficient for q^(t-1) (MSB)
/// - z_vec[t-1] = coefficient for q^0 (LSB)
///
/// # Arguments
/// * `z` - Layer index
/// * `t` - Number of y-sections
/// * `q` - Base (coupling factor)
///
/// # Returns
/// Vector where element y contains the coefficient for q^(t-1-y)
pub fn get_plane_vector(z: usize, t: usize, q: usize) -> Vec<usize> {
    let mut result = vec![0usize; t];
    let mut remaining = z;

    for i in 0..t {
        result[t - 1 - i] = remaining % q;
        remaining /= q;
    }

    result
}

/// Get the maximum intersection score for a set of erased chunks
///
/// The intersection score (IS) of a layer is the count of erased vertices
/// that are "red" (unpaired) in that layer.
///
/// # Arguments
/// * `erased_chunks` - Set of erased chunk indices
/// * `t` - Number of y-sections
/// * `q` - Coupling factor
/// * `sub_chunk_no` - Total number of sub-chunks (α = q^t)
///
/// # Returns
/// Maximum IS across all layers
pub fn get_max_iscore(erased_chunks: &BTreeSet<usize>, t: usize, q: usize, sub_chunk_no: usize) -> usize {
    let mut max_iscore = 0;

    for z in 0..sub_chunk_no {
        let plane_vec = get_plane_vector(z, t, q);
        let mut iscore = 0;

        for &erased in erased_chunks {
            let y = erased / q;
            let x = erased % q;
            if y < t && plane_vec[y] == x {
                iscore += 1;
            }
        }

        max_iscore = max_iscore.max(iscore);
    }

    max_iscore
}

/// Set the decoding order for planes based on intersection score
///
/// Planes (layers) are processed in order of increasing intersection score.
/// This ensures that we can always recover the necessary U values for each layer.
///
/// # Arguments
/// * `order` - Output array to fill with layer indices in processing order
/// * `erasures` - Set of erased chunk indices
/// * `t` - Number of y-sections
/// * `q` - Coupling factor
/// * `sub_chunk_no` - Total number of sub-chunks (α)
pub fn set_planes_sequential_decoding_order(
    order: &mut [usize],
    erasures: &BTreeSet<usize>,
    t: usize,
    q: usize,
    sub_chunk_no: usize,
) {
    // Calculate IS for each plane and sort by IS
    let mut planes_with_is: Vec<(usize, usize)> = (0..sub_chunk_no)
        .map(|z| {
            let plane_vec = get_plane_vector(z, t, q);
            let is = erasures.iter().filter(|&&e| {
                let y = e / q;
                let x = e % q;
                y < t && plane_vec[y] == x
            }).count();
            (z, is)
        })
        .collect();

    // Sort by IS (ascending), then by z for stability
    planes_with_is.sort_by_key(|&(z, is)| (is, z));

    // Fill order array
    for (i, (z, _)) in planes_with_is.iter().enumerate() {
        if i < order.len() {
            order[i] = *z;
        }
    }
}

/// Check if a vertex is unpaired (red) in its layer
///
/// A vertex (x, y, z) is unpaired if x == z_y, where z_y is the y-th digit of z in base q.
///
/// # Arguments
/// * `x` - x-coordinate
/// * `y` - y-section
/// * `z` - layer index
/// * `q` - coupling factor
///
/// # Returns
/// true if the vertex is unpaired (red)
#[inline]
pub fn is_unpaired(x: usize, y: usize, z: usize, q: usize) -> bool {
    let z_y = (z / q.pow(y as u32)) % q;
    x == z_y
}

/// Get the z_y value (y-th digit of z in base q)
#[inline]
pub fn get_z_y(z: usize, y: usize, q: usize) -> usize {
    (z / q.pow(y as u32)) % q
}

/// Convert node index to (x, y) coordinates
#[inline]
pub fn node_to_xy(node: usize, q: usize) -> (usize, usize) {
    (node % q, node / q)
}

/// Convert (x, y) coordinates to node index
#[inline]
pub fn xy_to_node(x: usize, y: usize, q: usize) -> usize {
    y * q + x
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_plane_vector() {
        // For q=2, t=2 (MSB at index 0, LSB at index t-1):
        // z=0 (binary 00) -> [0,0]
        // z=1 (binary 01) -> [0,1]
        // z=2 (binary 10) -> [1,0]
        // z=3 (binary 11) -> [1,1]
        assert_eq!(get_plane_vector(0, 2, 2), vec![0, 0]);
        assert_eq!(get_plane_vector(1, 2, 2), vec![0, 1]);
        assert_eq!(get_plane_vector(2, 2, 2), vec![1, 0]);
        assert_eq!(get_plane_vector(3, 2, 2), vec![1, 1]);

        // For q=3, t=2: z=5 = 1*3 + 2 -> [1,2] (MSB=1, LSB=2)
        assert_eq!(get_plane_vector(5, 2, 3), vec![1, 2]);
    }

    #[test]
    fn test_companion_layer() {
        let q = 2;

        // Test case from paper: z=2 (plane_vec=[1,0]), x=1, y=0
        // z_y = 0, x != z_y, so companion exists
        // z_sw = 2 - 0*1 + 1*1 = 3
        let z_y = get_z_y(2, 0, q);
        assert_eq!(z_y, 0);
        assert_eq!(get_companion_layer(2, 1, 0, z_y, q), 3);

        // Unpaired case: z=3, x=1, y=0, z_y=1, x==z_y
        let z_y = get_z_y(3, 0, q);
        assert_eq!(z_y, 1);
        assert_eq!(get_companion_layer(3, 1, 0, z_y, q), 3); // Returns self
    }

    #[test]
    fn test_is_unpaired() {
        let q = 2;
        // z=0: plane_vec=[0,0], unpaired at x=0 for y=0 and y=1
        assert!(is_unpaired(0, 0, 0, q));
        assert!(is_unpaired(0, 1, 0, q));
        assert!(!is_unpaired(1, 0, 0, q));

        // z=3: plane_vec=[1,1], unpaired at x=1 for y=0 and y=1
        assert!(is_unpaired(1, 0, 3, q));
        assert!(is_unpaired(1, 1, 3, q));
        assert!(!is_unpaired(0, 0, 3, q));
    }

    #[test]
    fn test_node_conversion() {
        let q = 3;
        assert_eq!(node_to_xy(0, q), (0, 0));
        assert_eq!(node_to_xy(5, q), (2, 1));
        assert_eq!(xy_to_node(2, 1, q), 5);
    }
}

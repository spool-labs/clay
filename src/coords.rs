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
#[allow(dead_code)]
pub fn is_unpaired(x: usize, y: usize, z: usize, q: usize) -> bool {
    let z_y = (z / q.pow(y as u32)) % q;
    x == z_y
}

/// Get the z_y value (y-th digit of z in base q)
#[inline]
#[allow(dead_code)]
pub fn get_z_y(z: usize, y: usize, q: usize) -> usize {
    (z / q.pow(y as u32)) % q
}

/// Convert node index to (x, y) coordinates
#[inline]
#[allow(dead_code)]
pub fn node_to_xy(node: usize, q: usize) -> (usize, usize) {
    (node % q, node / q)
}

/// Convert (x, y) coordinates to node index
#[inline]
#[allow(dead_code)]
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

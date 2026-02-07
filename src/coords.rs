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
}

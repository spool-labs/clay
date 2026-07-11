//! Coordinate system helpers for Clay codes
//!
//! Clay codes use a 3D coordinate system (x, y, z) where:
//! - x: position within y-section (0 to q-1)
//! - y: y-section index (0 to t-1)
//! - z: layer/plane index (0 to alpha-1)
//!
//! Key concepts:
//! - **y-section**: Nodes with the same y-coordinate
//! - **Companion layer**: For vertex (x, y, z), its companion is at layer z_sw
//! - **Plane digits**: The base-q representation of a layer index; digit y is
//!   the z-coordinate that makes vertex (z_y, y) "red" (unpaired) in that layer

/// Precomputed plane geometry shared by the decode and repair loops
///
/// Holds every layer's base-q digits in one flat table plus the powers of q,
/// so the hot loops index instead of dividing.
pub struct LayerGeometry {
    pub t: usize,

    /// Digits of all layers, t per layer, most significant first
    pub digits: Vec<usize>,
    /// q^0 through q^(t-1)
    pub q_powers: Vec<usize>,
}

impl LayerGeometry {
    /// Build the digit table for all sub_chunk_no layers
    pub fn new(q: usize, t: usize, sub_chunk_no: usize) -> Self {
        let mut q_powers = vec![1usize; t];
        for i in 1..t {
            q_powers[i] = q_powers[i - 1] * q;
        }

        let mut digits = vec![0usize; sub_chunk_no * t];
        for z in 0..sub_chunk_no {
            let mut remaining = z;
            for i in 0..t {
                digits[z * t + t - 1 - i] = remaining % q;
                remaining /= q;
            }
        }

        LayerGeometry {
            t,
            digits,
            q_powers,
        }
    }

    /// Base-q digits of layer z, one per y-section, most significant first
    #[inline]
    pub fn plane_digits(&self, z: usize) -> &[usize] {
        &self.digits[z * self.t..(z + 1) * self.t]
    }

    /// Layer of the companion vertex: digit y of z moves from z_y to x
    ///
    /// z carries digit z_y at position y, so the subtraction cannot underflow
    /// and the result stays inside 0..alpha.
    #[inline]
    pub fn companion_layer(&self, z: usize, x: usize, y: usize, z_y: usize) -> usize {
        let place = self.q_powers[self.t - 1 - y];
        z + x * place - z_y * place
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // digit rows match the base-q expansion of each layer index
    #[test]
    fn plane_digits() {
        // q=2, t=2: z=0..3 -> 00, 01, 10, 11 (most significant first)
        let geometry = LayerGeometry::new(2, 2, 4);
        assert_eq!(geometry.plane_digits(0), &[0, 0]);
        assert_eq!(geometry.plane_digits(1), &[0, 1]);
        assert_eq!(geometry.plane_digits(2), &[1, 0]);
        assert_eq!(geometry.plane_digits(3), &[1, 1]);

        // q=3, t=2: z=5 is 1*3 + 2
        let geometry = LayerGeometry::new(3, 2, 9);
        assert_eq!(geometry.plane_digits(5), &[1, 2]);
    }

    // companion layer swaps exactly one digit and stays in range
    #[test]
    fn companion_layer() {
        let geometry = LayerGeometry::new(3, 3, 27);

        for z in 0..27 {
            let digits = geometry.plane_digits(z).to_vec();
            for y in 0..3 {
                for x in 0..3 {
                    let z_sw = geometry.companion_layer(z, x, y, digits[y]);
                    assert!(z_sw < 27);

                    let sw_digits = geometry.plane_digits(z_sw);
                    assert_eq!(sw_digits[y], x);
                    for other in 0..3 {
                        if other != y {
                            assert_eq!(sw_digits[other], digits[other]);
                        }
                    }
                }
            }
        }
    }
}

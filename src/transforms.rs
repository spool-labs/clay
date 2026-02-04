//! Pairwise transforms for Clay codes
//!
//! This module implements the core coupling/decoupling transforms from the FAST'18 paper:
//! - **PRT (Pairwise Reverse Transform)**: C-plane → U-plane
//! - **PFT (Pairwise Forward Transform)**: U-plane → C-plane
//!
//! The transforms use a 2x2 matrix with parameter γ (gamma):
//! ```text
//! PRT: [U, U*] = [1, γ; γ, 1] × [C, C*]
//! PFT: [C, C*] = [1, γ; γ, 1]⁻¹ × [U, U*]
//! ```
//!
//! γ must satisfy: γ ≠ 0 and γ² ≠ 1

use reed_solomon_erasure::galois_8::{add as gf_add, mul as gf_mul, div as gf_div};

/// Gamma value for pairwise transforms.
/// Must satisfy: γ ≠ 0, γ² ≠ 1
/// In GF(2^8), 2 works well since 2² = 4 ≠ 1
pub const GAMMA: u8 = 2;

/// GF(2^8) multiplicative inverse: a^(-1) = 1/a
#[inline]
pub fn gf_inv(a: u8) -> u8 {
    gf_div(1, a)
}

/// PRT: Pairwise Reverse Transform (C-plane → U-plane)
///
/// Computes both U and U* from C and C*:
/// ```text
/// [U ]   [1  γ] [C ]
/// [U*] = [γ  1] [C*]
/// ```
///
/// # Arguments
/// * `c` - C values (primary)
/// * `c_star` - C* values (companion)
///
/// # Returns
/// Tuple of (U, U*) vectors
pub fn prt_compute_both(c: &[u8], c_star: &[u8]) -> (Vec<u8>, Vec<u8>) {
    let len = c.len();
    let mut u = vec![0u8; len];
    let mut u_star = vec![0u8; len];

    for i in 0..len {
        // U = C + γ*C*
        u[i] = gf_add(c[i], gf_mul(GAMMA, c_star[i]));
        // U* = γ*C + C*
        u_star[i] = gf_add(gf_mul(GAMMA, c[i]), c_star[i]);
    }

    (u, u_star)
}

/// PRT with orientation handling
///
/// Given c_xy at node (x,y) layer z, and c_sw at node (z_y,y) layer z_sw:
/// - If xy_is_primary (x < z_y): c_xy is C, c_sw is C*
/// - Otherwise (x > z_y): c_xy is C*, c_sw is C
///
/// # Returns
/// Tuple of (u_xy, u_sw) - U values for each node at their respective layers
pub fn prt_compute_both_oriented(c_xy: &[u8], c_sw: &[u8], xy_is_primary: bool) -> (Vec<u8>, Vec<u8>) {
    let len = c_xy.len();
    let mut u_xy = vec![0u8; len];
    let mut u_sw = vec![0u8; len];

    if xy_is_primary {
        // c_xy is C (primary), c_sw is C* (starred)
        // u_xy = U = C + γ*C* = c_xy + γ*c_sw
        // u_sw = U* = γ*C + C* = γ*c_xy + c_sw
        for i in 0..len {
            u_xy[i] = gf_add(c_xy[i], gf_mul(GAMMA, c_sw[i]));
            u_sw[i] = gf_add(gf_mul(GAMMA, c_xy[i]), c_sw[i]);
        }
    } else {
        // c_xy is C* (starred), c_sw is C (primary)
        // u_xy = U* = γ*C + C* = γ*c_sw + c_xy
        // u_sw = U = C + γ*C* = c_sw + γ*c_xy
        for i in 0..len {
            u_xy[i] = gf_add(gf_mul(GAMMA, c_sw[i]), c_xy[i]);
            u_sw[i] = gf_add(c_sw[i], gf_mul(GAMMA, c_xy[i]));
        }
    }

    (u_xy, u_sw)
}

/// PFT: Pairwise Forward Transform (U-plane → C-plane)
///
/// Computes both C and C* from U and U*:
/// ```text
/// [C ]   [1  γ]⁻¹ [U ]
/// [C*] = [γ  1]   [U*]
/// ```
///
/// The inverse matrix is: (1/(1-γ²)) × [1, -γ; -γ, 1]
/// In GF(2^8), subtraction = addition, so: (1/(1+γ²)) × [1, γ; γ, 1]
///
/// # Arguments
/// * `u` - U values (primary)
/// * `u_star` - U* values (companion)
///
/// # Returns
/// Tuple of (C, C*) vectors
pub fn pft_compute_both(u: &[u8], u_star: &[u8]) -> (Vec<u8>, Vec<u8>) {
    let len = u.len();
    let mut c = vec![0u8; len];
    let mut c_star = vec![0u8; len];

    // det = 1 - γ² = 1 + γ² (in GF(2^8), subtraction = addition)
    let det = gf_add(1, gf_mul(GAMMA, GAMMA));
    let det_inv = gf_inv(det);

    for i in 0..len {
        // C = (U + γ*U*) / det
        c[i] = gf_mul(gf_add(u[i], gf_mul(GAMMA, u_star[i])), det_inv);
        // C* = (γ*U + U*) / det
        c_star[i] = gf_mul(gf_add(gf_mul(GAMMA, u[i]), u_star[i]), det_inv);
    }

    (c, c_star)
}

/// Compute C from U and C* (partial PFT)
///
/// Used when we have U at one vertex and C* at its companion.
/// From the PRT equation: U = C + γ*C*
/// Therefore: C = U - γ*C* = U + γ*C* (in GF(2^8))
pub fn compute_c_from_u_and_cstar(u_xy: &[u8], c_companion: &[u8]) -> Vec<u8> {
    let len = u_xy.len();
    let mut c = vec![0u8; len];

    for i in 0..len {
        // C = U + γ*C* (using the fact that U = C + γ*C*)
        c[i] = gf_add(u_xy[i], gf_mul(GAMMA, c_companion[i]));
    }

    c
}

/// Compute C* from C and U (partial transform)
///
/// Used when we have C at one vertex and U at its companion.
/// From PRT: U* = γ*C + C*, so C* = U* - γ*C = U* + γ*C
/// But we have U (not U*) and C, so we use:
/// U = C + γ*C* and U* = γ*C + C*
///
/// If helper is primary (has C), we compute C* from its U* and C:
/// C* = U* + γ*C (since U* = γ*C + C*)
pub fn compute_cstar_from_c_and_u(c_helper: &[u8], u_helper: &[u8], helper_is_primary: bool) -> Vec<u8> {
    let len = c_helper.len();
    let mut companion_c = vec![0u8; len];

    let gamma_inv = gf_inv(GAMMA);

    if helper_is_primary {
        // helper has C, u_helper is U* for companion
        // U* = γ*C + C* => C* = U* + γ*C
        for i in 0..len {
            companion_c[i] = gf_add(u_helper[i], gf_mul(GAMMA, c_helper[i]));
        }
    } else {
        // helper has C*, u_helper is U for companion
        // U = C + γ*C* => C = U + γ*C*
        // But we want C* given C* (helper) and U... this case shouldn't happen
        // Actually if helper is not primary, then helper has C*, and we want C
        // U = C + γ*C* => C = U + γ*C*
        for i in 0..len {
            companion_c[i] = gf_mul(gf_add(u_helper[i], c_helper[i]), gamma_inv);
        }
    }

    companion_c
}

/// Compute U from C and U* (partial transform)
///
/// From PFT inverse, given C and U*:
/// det * C = U + γ*U*
/// Therefore: U = det*C + γ*U* (in GF(2^8))
pub fn compute_u_from_c_and_ustar(c_xy: &[u8], u_companion: &[u8]) -> Vec<u8> {
    let len = c_xy.len();
    let mut u = vec![0u8; len];

    let det = gf_add(1, gf_mul(GAMMA, GAMMA));

    for i in 0..len {
        // U = det*C + γ*U*
        u[i] = gf_add(gf_mul(det, c_xy[i]), gf_mul(GAMMA, u_companion[i]));
    }

    u
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gamma_properties() {
        // Verify γ ≠ 0
        assert_ne!(GAMMA, 0);
        // Verify γ² ≠ 1
        let gamma_sq = gf_mul(GAMMA, GAMMA);
        assert_ne!(gamma_sq, 1);
    }

    #[test]
    fn test_prt_pft_roundtrip() {
        let c = vec![0x12, 0x34, 0x56, 0x78];
        let c_star = vec![0xAB, 0xCD, 0xEF, 0x01];

        // C → U via PRT
        let (u, u_star) = prt_compute_both(&c, &c_star);

        // U → C via PFT
        let (c_back, c_star_back) = pft_compute_both(&u, &u_star);

        assert_eq!(c, c_back);
        assert_eq!(c_star, c_star_back);
    }

    #[test]
    fn test_gf_arithmetic() {
        // Addition is XOR
        assert_eq!(gf_add(5, 3), 6); // 5 XOR 3 = 6

        // Multiplication
        assert_eq!(gf_mul(2, 3), 6); // 2 * 3 = 6 in GF(2^8)

        // Inverse: a^(-1) * a = 1
        assert_eq!(gf_mul(gf_inv(2), 2), 1);
    }
}

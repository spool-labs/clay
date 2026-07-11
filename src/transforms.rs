//! Pairwise coupling transforms for Clay codes
//!
//! PRT (reverse) maps the C-plane to the U-plane and PFT (forward) maps back.
//! Both are 2x2 matrix transforms over GF(2^8) with coupling factor gamma:
//!
//! ```text
//! PRT: [U, U*] = [1, gamma; gamma, 1] x [C, C*]
//! PFT: [C, C*] = [1, gamma; gamma, 1]^-1 x [U, U*]
//! ```
//!
//! With gamma fixed at 2, every multiply reduces to doubling, halving, or a
//! 256-byte constant table, so the byte loops vectorize instead of going
//! through the general multiply table.

/// Gamma value for pairwise transforms; must satisfy gamma != 0 and gamma^2 != 1
pub const GAMMA: u8 = 2;

/// Low byte of the field's reduction polynomial x^8 + x^4 + x^3 + x^2 + 1
const POLY: u8 = 0x1d;

/// Inverse of the coupling matrix determinant 1 + gamma^2 = 5, verified in tests
const DET_INV: u8 = 0xa7;

/// Multiply by det inverse, folded into a table so PFT needs no division
const DET_INV_TABLE: [u8; 256] = build_mul_table(DET_INV);

/// Multiply by gamma times det inverse, the other half of the PFT matrix
const GAMMA_DET_INV_TABLE: [u8; 256] = build_mul_table(gf_mul_const(GAMMA, DET_INV));

/// Multiply by 2 in GF(2^8); a shift and conditional reduction, so it vectorizes
#[inline]
fn gf_double(a: u8) -> u8 {
    (a << 1) ^ (POLY & (a >> 7).wrapping_neg())
}

/// Divide by 2 in GF(2^8), the inverse of gf_double
#[inline]
fn gf_halve(a: u8) -> u8 {
    (a >> 1) ^ (0x8e & (a & 1).wrapping_neg())
}

/// Compile-time GF(2^8) multiply used only to build the constant tables
const fn gf_mul_const(a: u8, b: u8) -> u8 {
    let mut shifted = a as u16;
    let mut remaining = b;
    let mut product: u16 = 0;

    while remaining != 0 {
        if remaining & 1 == 1 {
            product ^= shifted;
        }
        shifted <<= 1;
        if shifted & 0x100 != 0 {
            shifted ^= 0x11d;
        }
        remaining >>= 1;
    }

    product as u8
}

/// Build the 256-entry multiply-by-constant table at compile time
const fn build_mul_table(constant: u8) -> [u8; 256] {
    let mut table = [0u8; 256];
    let mut i = 0;

    while i < 256 {
        table[i] = gf_mul_const(constant, i as u8);
        i += 1;
    }

    table
}

/// PRT: compute U = C + gamma*C* and U* = gamma*C + C* into the output slices
#[inline]
pub fn prt_into(c: &[u8], c_star: &[u8], u: &mut [u8], u_star: &mut [u8]) {
    let len = c.len();
    let c_star = &c_star[..len];
    let u = &mut u[..len];
    let u_star = &mut u_star[..len];

    for i in 0..len {
        u[i] = c[i] ^ gf_double(c_star[i]);
        u_star[i] = gf_double(c[i]) ^ c_star[i];
    }
}

/// PFT: compute C and C* from U and U* into the output slices
///
/// The inverse matrix entries det^-1 and gamma*det^-1 are constant tables,
/// so each byte is two lookups and a xor.
#[inline]
pub fn pft_into(u: &[u8], u_star: &[u8], c: &mut [u8], c_star: &mut [u8]) {
    let len = u.len();
    let u_star = &u_star[..len];
    let c = &mut c[..len];
    let c_star = &mut c_star[..len];

    for i in 0..len {
        c[i] = DET_INV_TABLE[u[i] as usize] ^ GAMMA_DET_INV_TABLE[u_star[i] as usize];
        c_star[i] = GAMMA_DET_INV_TABLE[u[i] as usize] ^ DET_INV_TABLE[u_star[i] as usize];
    }
}

/// Partial transform: C = U + gamma*C*, for when only the companion C* is known
#[inline]
pub fn compute_c_into(u: &[u8], c_star: &[u8], c: &mut [u8]) {
    let len = u.len();
    let c_star = &c_star[..len];
    let c = &mut c[..len];

    for i in 0..len {
        c[i] = u[i] ^ gf_double(c_star[i]);
    }
}

/// Partial transform: U = det*C + gamma*U*, for when only the companion U* is known
#[inline]
pub fn compute_u_into(c: &[u8], u_star: &[u8], u: &mut [u8]) {
    let len = c.len();
    let u_star = &u_star[..len];
    let u = &mut u[..len];

    // det*C with det = 5 is 4*C + C, two doublings and a xor
    for i in 0..len {
        u[i] = gf_double(gf_double(c[i])) ^ c[i] ^ gf_double(u_star[i]);
    }
}

/// Partial transform: C* = (U + C) / gamma, used by repair to recover the lost companion
#[inline]
pub fn compute_cstar_into(c: &[u8], u: &[u8], c_star: &mut [u8]) {
    let len = c.len();
    let u = &u[..len];
    let c_star = &mut c_star[..len];

    for i in 0..len {
        c_star[i] = gf_halve(u[i] ^ c[i]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use reed_solomon_erasure::galois_8::{add as gf_add, div as gf_div, mul as gf_mul};

    // determinant of the coupling matrix
    const DET: u8 = 5;

    // gamma is nonzero and gamma squared is not one
    #[test]
    fn gamma_properties() {
        assert_ne!(GAMMA, 0);
        assert_ne!(gf_mul(GAMMA, GAMMA), 1);
    }

    // doubling matches the field multiply by 2 for every byte
    #[test]
    fn double_matches_field() {
        for a in 0u8..=255 {
            assert_eq!(gf_double(a), gf_mul(2, a));
        }
    }

    // halving undoes doubling for every byte
    #[test]
    fn halve_inverts_double() {
        for a in 0u8..=255 {
            assert_eq!(gf_halve(gf_double(a)), a);
        }
    }

    // the const multiply and tables agree with the field's general multiply
    #[test]
    fn tables_match_field() {
        assert_eq!(gf_mul(DET, DET_INV), 1);
        assert_eq!(DET_INV, gf_div(1, DET));
        assert_eq!(DET, gf_add(1, gf_mul(GAMMA, GAMMA)));
        for a in 0u8..=255 {
            assert_eq!(DET_INV_TABLE[a as usize], gf_mul(DET_INV, a));
            assert_eq!(
                GAMMA_DET_INV_TABLE[a as usize],
                gf_mul(gf_mul(GAMMA, DET_INV), a)
            );
        }
    }

    // PFT recovers the exact C pair that PRT started from
    #[test]
    fn prt_pft_roundtrip() {
        let c = vec![0x12, 0x34, 0x56, 0x78];
        let c_star = vec![0xAB, 0xCD, 0xEF, 0x01];
        let mut u = vec![0u8; 4];
        let mut u_star = vec![0u8; 4];
        let mut c_back = vec![0u8; 4];
        let mut c_star_back = vec![0u8; 4];

        prt_into(&c, &c_star, &mut u, &mut u_star);
        pft_into(&u, &u_star, &mut c_back, &mut c_star_back);

        assert_eq!(c, c_back);
        assert_eq!(c_star, c_star_back);
    }

    // each partial transform agrees with the full PRT
    #[test]
    fn partial_transforms() {
        let c = vec![0x12, 0x34, 0x56, 0x78];
        let c_star = vec![0xAB, 0xCD, 0xEF, 0x01];
        let mut u = vec![0u8; 4];
        let mut u_star = vec![0u8; 4];
        prt_into(&c, &c_star, &mut u, &mut u_star);

        let mut c_recovered = vec![0u8; 4];
        compute_c_into(&u, &c_star, &mut c_recovered);
        assert_eq!(c, c_recovered);

        let mut u_recovered = vec![0u8; 4];
        compute_u_into(&c, &u_star, &mut u_recovered);
        assert_eq!(u, u_recovered);

        let mut c_star_recovered = vec![0u8; 4];
        compute_cstar_into(&c, &u, &mut c_star_recovered);
        assert_eq!(c_star, c_star_recovered);
    }
}

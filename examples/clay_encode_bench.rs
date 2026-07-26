//! Clay encode throughput at tape's production shape (k=7, m=13, d=16).
//! A/B by setting SET_SKEW to 0 in decode.rs.

use std::time::Instant;
use clay_codes::ClayCode;

/// Cheap order-sensitive digest so the two layouts can be compared for
/// byte-identical output across separate runs.
fn digest(chunks: &[Vec<u8>]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for (i, c) in chunks.iter().enumerate() {
        h ^= i as u64;
        for &b in c {
            h ^= b as u64;
            h = h.wrapping_mul(0x100000001b3);
        }
    }
    h
}

fn main() {
    let clay = ClayCode::new(7, 13, 16).expect("clay params");
    println!("{:>12} {:>12} {:>10}", "payload", "ms", "MiB/s");

    for payload in [1024 * 1024usize, 10 * 1024 * 1024, 64 * 1024 * 1024] {
        let data: Vec<u8> = (0..payload).map(|i| (i % 251) as u8).collect();
        let first = clay.encode(&data);
        println!("  digest {payload}: {:016x}", digest(&first));

        let reps = (128 * 1024 * 1024 / payload).clamp(3, 30);
        let start = Instant::now();
        for _ in 0..reps {
            std::hint::black_box(clay.encode(&data));
        }
        let ms = start.elapsed().as_secs_f64() * 1000.0 / reps as f64;
        println!(
            "{payload:>12} {ms:>12.2} {:>10.1}",
            payload as f64 / (ms / 1000.0) / (1024.0 * 1024.0)
        );
    }
}

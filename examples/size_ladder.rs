//! Encode throughput across the size ladder the v0.2.1 notes used, to test
//! whether the unexplained 4 MiB cliff is the row aliasing. A/B by setting
//! SET_SKEW to 0 in decode.rs.

use std::time::Instant;

use clay_codes::ClayCode;

fn main() {
    // (n, k, d) as the v0.2.1 notes label them; ClayCode takes (k, m, d).
    let configs = [
        ("(6,4,5)", 4usize, 2usize, 5usize),
        ("(9,6,8)", 6, 3, 8),
        ("(14,10,13)", 10, 4, 13),
        ("(20,7,16)", 7, 13, 16),
    ];
    let sizes = [1024usize, 10 * 1024, 100 * 1024, 1024 * 1024, 4 * 1024 * 1024];

    print!("{:>12}", "config");
    for s in sizes {
        print!("{:>12}", s);
    }
    println!();

    for (label, k, m, d) in configs {
        let clay = ClayCode::new(k, m, d).expect("clay params");
        print!("{label:>12}");

        for size in sizes {
            let data: Vec<u8> = (0..size).map(|i| (i % 251) as u8).collect();
            clay.encode(&data);

            let reps = (64 * 1024 * 1024 / size).clamp(3, 500);
            let start = Instant::now();
            for _ in 0..reps {
                std::hint::black_box(clay.encode(&data));
            }
            let secs = start.elapsed().as_secs_f64() / reps as f64;
            print!("{:>12.1}", size as f64 / secs / (1024.0 * 1024.0));
        }
        println!();
    }
    println!("payload MiB/s");
}

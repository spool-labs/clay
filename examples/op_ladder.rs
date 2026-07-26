//! Encode, decode and repair throughput across a size ladder, for judging row
//! layout changes. A/B by setting SET_SKEW to 0 in decode.rs and rerunning.

use std::collections::HashMap;
use std::time::Instant;

use clay_codes::ClayCode;

fn bench(label: &str, sizes: &[usize], mut run: impl FnMut(usize) -> f64) {
    print!("{label:>18}");
    for &size in sizes {
        print!("{:>12.1}", run(size));
    }
    println!();
}

fn main() {
    // (n, k, d) as the v0.2.1 notes label them; ClayCode takes (k, m, d).
    let configs = [("(14,10,13)", 10usize, 4usize, 13usize), ("(20,7,16)", 7, 13, 16)];
    let sizes = [1024usize, 10 * 1024, 100 * 1024, 1024 * 1024, 4 * 1024 * 1024];

    print!("{:>18}", "op / config");
    for s in sizes {
        print!("{s:>12}");
    }
    println!();

    for (label, k, m, d) in configs {
        let clay = ClayCode::new(k, m, d).expect("clay params");
        let n = k + m;
        let _ = d;

        bench(&format!("encode {label}"), &sizes, |size| {
            let data: Vec<u8> = (0..size).map(|i| (i % 251) as u8).collect();
            clay.encode(&data);

            let reps = (64 * 1024 * 1024 / size).clamp(3, 200);
            let start = Instant::now();
            for _ in 0..reps {
                std::hint::black_box(clay.encode(&data));
            }
            let secs = start.elapsed().as_secs_f64() / reps as f64;
            size as f64 / secs / (1024.0 * 1024.0)
        });

        bench(&format!("decode {label}"), &sizes, |size| {
            let data: Vec<u8> = (0..size).map(|i| (i % 251) as u8).collect();
            let chunks = clay.encode(&data);

            // Drop m nodes so decode does real recovery work.
            let erasures: Vec<usize> = (0..m).collect();
            let available: HashMap<usize, Vec<u8>> =
                (m..n).map(|i| (i, chunks[i].clone())).collect();

            clay.decode(&available, &erasures).expect("decode");
            let reps = (64 * 1024 * 1024 / size).clamp(3, 200);
            let start = Instant::now();
            for _ in 0..reps {
                std::hint::black_box(clay.decode(&available, &erasures).expect("decode"));
            }
            let secs = start.elapsed().as_secs_f64() / reps as f64;
            size as f64 / secs / (1024.0 * 1024.0)
        });

        bench(&format!("repair {label}"), &sizes, |size| {
            let data: Vec<u8> = (0..size).map(|i| (i % 251) as u8).collect();
            let chunks = clay.encode(&data);
            let chunk_size = chunks[0].len();
            let sub_chunk_size = chunk_size / clay.sub_chunk_no;

            // One lost node, helpers sending only the sub-chunks repair asks for.
            let lost_node = 0usize;
            let available: Vec<usize> = (1..n).collect();
            let helper_info = clay.minimum_to_repair(lost_node, &available).expect("helpers");

            let mut partials: HashMap<usize, Vec<u8>> = HashMap::new();
            for (helper, indices) in &helper_info {
                let mut partial = Vec::new();
                for &sc in indices {
                    partial.extend_from_slice(
                        &chunks[*helper][sc * sub_chunk_size..(sc + 1) * sub_chunk_size],
                    );
                }
                partials.insert(*helper, partial);
            }

            clay.repair(lost_node, &partials, chunk_size).expect("repair");
            let reps = (64 * 1024 * 1024 / size).clamp(3, 200);
            let start = Instant::now();
            for _ in 0..reps {
                std::hint::black_box(
                    clay.repair(lost_node, &partials, chunk_size).expect("repair"),
                );
            }
            let secs = start.elapsed().as_secs_f64() / reps as f64;
            size as f64 / secs / (1024.0 * 1024.0)
        });

    }
    println!("payload MiB/s");
}

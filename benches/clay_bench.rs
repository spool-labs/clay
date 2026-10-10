//! Benchmarks for Clay erasure codes
//!
//! Measures encode, decode, and repair performance across various
//! parameter configurations and data sizes.

use clay_codes::ClayCode;
use criterion::{black_box, criterion_group, criterion_main, BenchmarkId, Criterion, Throughput};
use rand::{Rng, SeedableRng};
use rand::rngs::StdRng;
use std::collections::HashMap;

/// Parameter configurations to test: (k, m, d)
///
/// The trailing comment gives the internal RS shape clay builds, `RS(k+nu, m)`,
/// and the tape encode route it takes. Only the production shape (7,13) is in
/// tape's `GENERATED_SHAPES`, so it is the only config that can reach the
/// generated FFT encode kernels; the rest ride the fused-matrix path.
const CONFIGS: &[(usize, usize, usize)] = &[
    (4, 2, 5),    // (n,k,d)=(6,4,5)    RS(4,2)   fused-matrix
    (6, 3, 8),    // (n,k,d)=(9,6,8)    RS(6,3)   fused-matrix
    (10, 4, 13),  // (n,k,d)=(14,10,13) RS(12,4)  fused-matrix
    (7, 13, 16),  // (n,k,d)=(20,7,16)  RS(7,13)  fft-generated (production shape)
];

/// Data sizes to test (in bytes)
const DATA_SIZES: &[usize] = &[
    1024,            // 1 KB
    10 * 1024,       // 10 KB
    100 * 1024,      // 100 KB
    1024 * 1024,     // 1 MB
    4 * 1024 * 1024, // 4 MiB — large enough that the production shape's
                     // per-sub-chunk length clears STRIP and routes fft-generated
];

fn generate_data(size: usize, seed: u64) -> Vec<u8> {
    let mut rng = StdRng::seed_from_u64(seed);
    (0..size).map(|_| rng.gen()).collect()
}

fn bench_encode(c: &mut Criterion) {
    let mut group = c.benchmark_group("encode");

    for &(k, m, d) in CONFIGS {
        let clay = ClayCode::new(k, m, d).unwrap();
        let config_name = format!("({},{},{})", clay.n, clay.k, clay.d);

        for &size in DATA_SIZES {
            let data = generate_data(size, 42);

            group.throughput(Throughput::Bytes(size as u64));
            group.bench_with_input(
                BenchmarkId::new(&config_name, format_size(size)),
                &data,
                |b, data| {
                    b.iter(|| {
                        black_box(clay.encode(data))
                    });
                },
            );
        }
    }

    group.finish();
}

fn bench_encode_into(c: &mut Criterion) {
    let mut group = c.benchmark_group("encode_into");

    for &(k, m, d) in CONFIGS {
        let clay = ClayCode::new(k, m, d).unwrap();
        let config_name = format!("({},{},{})", clay.n, clay.k, clay.d);

        for &size in DATA_SIZES {
            let data = generate_data(size, 42);
            let mut chunks = vec![vec![0u8; clay.chunk_size(size)]; clay.n];

            group.throughput(Throughput::Bytes(size as u64));
            group.bench_with_input(
                BenchmarkId::new(&config_name, format_size(size)),
                &data,
                |b, data| {
                    b.iter(|| {
                        let mut rows: Vec<&mut [u8]> =
                            chunks.iter_mut().map(|chunk| chunk.as_mut_slice()).collect();
                        clay.encode_into(data, &mut rows).unwrap();
                        black_box(&rows);
                    });
                },
            );
        }
    }

    group.finish();
}

fn bench_decode(c: &mut Criterion) {
    let mut group = c.benchmark_group("decode");

    for &(k, m, d) in CONFIGS {
        let clay = ClayCode::new(k, m, d).unwrap();
        let config_name = format!("({},{},{})", clay.n, clay.k, clay.d);

        for &size in DATA_SIZES {
            let data = generate_data(size, 42);
            let chunks = clay.encode(&data);

            // Prepare decode input with 1 erasure
            let lost_node = 0;
            let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
            for (i, chunk) in chunks.iter().enumerate() {
                if i != lost_node {
                    available.insert(i, chunk.clone());
                }
            }
            let erasures = vec![lost_node];

            group.throughput(Throughput::Bytes(size as u64));
            group.bench_with_input(
                BenchmarkId::new(&config_name, format_size(size)),
                &(&available, &erasures),
                |b, (available, erasures)| {
                    b.iter(|| {
                        black_box(clay.decode(available, erasures).unwrap())
                    });
                },
            );
        }
    }

    group.finish();
}

fn bench_repair(c: &mut Criterion) {
    let mut group = c.benchmark_group("repair");

    for &(k, m, d) in CONFIGS {
        let clay = ClayCode::new(k, m, d).unwrap();
        let config_name = format!("({},{},{})", clay.n, clay.k, clay.d);

        for &size in DATA_SIZES {
            let data = generate_data(size, 42);
            let chunks = clay.encode(&data);
            let chunk_size = chunks[0].len();
            let sub_chunk_size = chunk_size / clay.sub_chunk_no;

            // Prepare repair input
            let lost_node = 0;
            let available_nodes: Vec<usize> = (1..clay.n).collect();
            let helper_info = clay.minimum_to_repair(lost_node, &available_nodes).unwrap();

            let mut partial_data: HashMap<usize, Vec<u8>> = HashMap::new();
            for (helper_idx, indices) in &helper_info {
                let mut helper_partial = Vec::new();
                for &sc_idx in indices {
                    let start = sc_idx * sub_chunk_size;
                    let end = (sc_idx + 1) * sub_chunk_size;
                    helper_partial.extend_from_slice(&chunks[*helper_idx][start..end]);
                }
                partial_data.insert(*helper_idx, helper_partial);
            }

            group.throughput(Throughput::Bytes(chunk_size as u64));
            group.bench_with_input(
                BenchmarkId::new(&config_name, format_size(size)),
                &(&partial_data, chunk_size),
                |b, (partial_data, chunk_size)| {
                    b.iter(|| {
                        black_box(clay.repair(lost_node, partial_data, *chunk_size).unwrap())
                    });
                },
            );
        }
    }

    group.finish();
}

fn bench_metrics_report(c: &mut Criterion) {
    // This benchmark just prints a metrics report, doesn't actually bench
    println!("\n{}", "=".repeat(80));
    println!("CLAY CODES METRICS REPORT");
    println!("{}", "=".repeat(80));

    println!("\n{:<12} {:>6} {:>6} {:>6} {:>8} {:>8} {:>12} {:>12}",
        "Config", "n", "k", "d", "α", "β", "Repair BW", "Storage OH");
    println!("{}", "-".repeat(80));

    for &(k, m, d) in CONFIGS {
        let clay = ClayCode::new(k, m, d).unwrap();
        let repair_bw = clay.normalized_repair_bandwidth();
        let storage_overhead = clay.n as f64 / clay.k as f64;

        println!("({},{},{})      {:>6} {:>6} {:>6} {:>8} {:>8} {:>11.1}% {:>11.2}x",
            clay.n, clay.k, clay.d,
            clay.n, clay.k, clay.d,
            clay.sub_chunk_no, clay.beta,
            repair_bw * 100.0,
            storage_overhead);
    }

    println!("\n{}", "-".repeat(80));
    println!("Storage breakdown by data size:");
    println!("{}", "-".repeat(80));
    println!("\n{:<12} {:>10} {:>12} {:>12} {:>12} {:>12}",
        "Config", "Data Size", "Chunk Size", "Total Store", "Repair BW", "RS Decode");

    for &(k, m, d) in CONFIGS {
        let clay = ClayCode::new(k, m, d).unwrap();

        for &size in DATA_SIZES {
            let data = generate_data(size, 42);
            let chunks = clay.encode(&data);
            let chunk_size = chunks[0].len();
            let total_storage = chunk_size * clay.n;

            // Calculate repair bandwidth
            let available_nodes: Vec<usize> = (1..clay.n).collect();
            let helper_info = clay.minimum_to_repair(0, &available_nodes).unwrap();
            let sub_chunk_size = chunk_size / clay.sub_chunk_no;
            let repair_bytes: usize = helper_info.iter()
                .map(|(_, indices)| indices.len() * sub_chunk_size)
                .sum();

            let rs_decode_bytes = clay.k * chunk_size;

            println!("({},{},{})      {:>10} {:>12} {:>12} {:>12} {:>12}",
                clay.n, clay.k, clay.d,
                format_size(size),
                format_size(chunk_size),
                format_size(total_storage),
                format_size(repair_bytes),
                format_size(rs_decode_bytes));
        }
        println!();
    }

    println!("{}", "=".repeat(80));

    // Dummy benchmark so criterion doesn't complain
    let mut group = c.benchmark_group("metrics");
    group.bench_function("report", |b| b.iter(|| black_box(1 + 1)));
    group.finish();
}

fn format_size(bytes: usize) -> String {
    if bytes >= 1024 * 1024 {
        format!("{:.1} MB", bytes as f64 / (1024.0 * 1024.0))
    } else if bytes >= 1024 {
        format!("{:.1} KB", bytes as f64 / 1024.0)
    } else {
        format!("{} B", bytes)
    }
}

criterion_group!(
    benches,
    bench_metrics_report,
    bench_encode,
    bench_encode_into,
    bench_decode,
    bench_repair,
);

criterion_main!(benches);

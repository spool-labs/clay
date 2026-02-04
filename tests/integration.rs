//! Integration tests for Clay erasure codes

use clay_codes::ClayCode;
use std::collections::HashMap;

/// Test the complete encode → repair flow with bandwidth verification
#[test]
fn test_full_repair_flow_with_bandwidth_check() {
    // (14, 10, 13) parameters from the paper
    let clay = ClayCode::new(10, 4, 13).unwrap();

    // Verify parameters
    assert_eq!(clay.n, 14);
    assert_eq!(clay.k, 10);
    assert_eq!(clay.m, 4);
    assert_eq!(clay.d, 13);
    assert_eq!(clay.q, 4);
    assert_eq!(clay.sub_chunk_no, 256); // α = 4^4 = 256
    assert_eq!(clay.beta, 64); // β = 256/4 = 64

    // Create test data
    let data_size = clay.k * clay.sub_chunk_no;
    let data: Vec<u8> = (0..data_size).map(|i| ((i * 17 + 31) % 256) as u8).collect();

    // Encode
    let chunks = clay.encode(&data);
    assert_eq!(chunks.len(), 14);

    let chunk_size = chunks[0].len();
    let sub_chunk_size = chunk_size / clay.sub_chunk_no;

    // Test repair for node 0
    let available: Vec<usize> = (1..clay.n).collect();
    let helper_info = clay.minimum_to_repair(0, &available).unwrap();

    // Verify we get exactly d helpers
    assert_eq!(helper_info.len(), clay.d);

    // Calculate repair bandwidth
    let repair_bytes: usize = helper_info
        .iter()
        .map(|(_, indices)| indices.len() * sub_chunk_size)
        .sum();

    let full_decode_bytes = clay.k * chunk_size;
    let repair_ratio = repair_bytes as f64 / full_decode_bytes as f64;

    // Expected ratio for (14, 10, 13) is 13/(10*4) = 0.325
    println!("(14, 10, 13) repair bandwidth ratio: {:.3}", repair_ratio);
    assert!(repair_ratio < 0.35, "Repair should use < 35% of full decode bandwidth");

    // Extract partial data for repair
    let mut partial_data: HashMap<usize, Vec<u8>> = HashMap::new();
    for (helper_idx, indices) in &helper_info {
        let mut helper_partial = Vec::new();
        for &sc_idx in indices {
            let start_byte = sc_idx * sub_chunk_size;
            let end_byte = (sc_idx + 1) * sub_chunk_size;
            helper_partial.extend_from_slice(&chunks[*helper_idx][start_byte..end_byte]);
        }
        partial_data.insert(*helper_idx, helper_partial);
    }

    // Perform repair
    let recovered = clay.repair(0, &partial_data, chunk_size).unwrap();

    // Verify correctness
    assert_eq!(recovered, chunks[0], "Repair failed to recover correct data");
}

/// Test multiple erasure decode
#[test]
fn test_multi_erasure_decode() {
    let clay = ClayCode::new(4, 2, 5).unwrap();
    let data: Vec<u8> = (0..512).map(|i| (i % 256) as u8).collect();
    let chunks = clay.encode(&data);

    // Test various erasure patterns
    let erasure_patterns = vec![
        vec![0],
        vec![5],
        vec![0, 5],
        vec![0, 1],
        vec![4, 5],
        vec![1, 3],
    ];

    for erasures in erasure_patterns {
        let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
        for (i, chunk) in chunks.iter().enumerate() {
            if !erasures.contains(&i) {
                available.insert(i, chunk.clone());
            }
        }

        let decoded = clay.decode(&available, &erasures).unwrap();
        assert_eq!(
            &decoded[..data.len()],
            &data[..],
            "Failed to decode with erasures {:?}",
            erasures
        );
    }
}

/// Test that repair bandwidth is consistently less than RS
#[test]
fn test_repair_bandwidth_advantage() {
    let test_params = vec![
        (4, 2, 5),   // normalized = 0.625
        (9, 3, 11),  // normalized ≈ 0.407
        (10, 4, 13), // normalized = 0.325
    ];

    for (k, m, d) in test_params {
        let clay = ClayCode::new(k, m, d).unwrap();
        let data_size = k * clay.sub_chunk_no;
        let data: Vec<u8> = (0..data_size).map(|i| (i % 256) as u8).collect();
        let chunks = clay.encode(&data);
        let chunk_size = chunks[0].len();
        let sub_chunk_size = chunk_size / clay.sub_chunk_no;

        // Test repair for each node
        for lost_node in 0..clay.n {
            let available: Vec<usize> = (0..clay.n).filter(|&i| i != lost_node).collect();
            let helper_info = clay.minimum_to_repair(lost_node, &available).unwrap();

            // Calculate repair bandwidth
            let repair_bytes: usize = helper_info
                .iter()
                .map(|(_, indices)| indices.len() * sub_chunk_size)
                .sum();

            let full_decode_bytes = k * chunk_size;

            // Repair should always use less than full decode
            assert!(
                repair_bytes < full_decode_bytes,
                "Repair bandwidth {} >= full decode {} for node {} with ({}, {}, {})",
                repair_bytes, full_decode_bytes, lost_node, k, m, d
            );
        }
    }
}

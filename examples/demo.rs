//! Demonstration of Clay erasure codes
//!
//! This example shows:
//! 1. Creating a Clay code with specific parameters
//! 2. Encoding data into chunks
//! 3. Decoding data with erasures
//! 4. Repairing a lost chunk with optimal bandwidth

use clay_codes::ClayCode;
use std::collections::HashMap;

fn main() {
    println!("=== Clay Erasure Codes Demo ===\n");

    // Create a Clay code with (n=6, k=4, d=5) parameters
    // This gives: q=2, t=3, α=8, β=4
    let clay = ClayCode::new(4, 2, 5).unwrap();

    println!("Code parameters:");
    println!("  n (total nodes):       {}", clay.n);
    println!("  k (data nodes):        {}", clay.k);
    println!("  m (parity nodes):      {}", clay.m);
    println!("  d (helper nodes):      {}", clay.d);
    println!("  q (coupling factor):   {}", clay.q);
    println!("  t (y-sections):        {}", clay.t);
    println!("  α (sub-chunks/chunk):  {}", clay.sub_chunk_no);
    println!("  β (sub-chunks/repair): {}", clay.beta);
    println!(
        "  Normalized repair BW:  {:.3}",
        clay.normalized_repair_bandwidth()
    );
    println!();

    // Create some test data
    let data = b"Hello, Clay codes! This is test data for the erasure code demo.";
    println!("Original data ({} bytes): {:?}", data.len(), String::from_utf8_lossy(data));
    println!();

    // Encode data into chunks
    let chunks = clay.encode(data);
    println!("Encoded into {} chunks:", chunks.len());
    let chunk_size = chunks[0].len();
    let sub_chunk_size = chunk_size / clay.sub_chunk_no;
    println!("  Chunk size: {} bytes", chunk_size);
    println!("  Sub-chunk size: {} bytes", sub_chunk_size);
    println!();

    // === Decode with erasures ===
    println!("--- Testing Decode ---");
    let lost_node = 0;
    println!("Simulating loss of node {}", lost_node);

    let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
    for (i, chunk) in chunks.iter().enumerate() {
        if i != lost_node {
            available.insert(i, chunk.clone());
        }
    }

    let decoded = clay.decode(&available, &[lost_node]).unwrap();
    println!("Decoded data: {:?}", String::from_utf8_lossy(&decoded[..data.len()]));
    assert_eq!(&decoded[..data.len()], &data[..]);
    println!("✓ Decode successful!\n");

    // === Repair with optimal bandwidth ===
    println!("--- Testing Repair (Bandwidth-Optimal) ---");
    let lost_node = 2;
    println!("Simulating loss of node {}", lost_node);

    // Get minimum data needed for repair
    let available_nodes: Vec<usize> = (0..clay.n).filter(|&i| i != lost_node).collect();
    let helper_info = clay.minimum_to_repair(lost_node, &available_nodes).unwrap();

    println!("Helpers selected: {:?}", helper_info.iter().map(|(n, _)| n).collect::<Vec<_>>());

    // Extract only the required sub-chunks
    let mut partial_data: HashMap<usize, Vec<u8>> = HashMap::new();
    let mut total_repair_bytes = 0;

    for (helper_idx, indices) in &helper_info {
        let mut helper_partial = Vec::new();
        for &sc_idx in indices {
            let start_byte = sc_idx * sub_chunk_size;
            let end_byte = (sc_idx + 1) * sub_chunk_size;
            helper_partial.extend_from_slice(&chunks[*helper_idx][start_byte..end_byte]);
        }
        total_repair_bytes += helper_partial.len();
        partial_data.insert(*helper_idx, helper_partial);
    }

    println!("\nBandwidth comparison:");
    let full_decode_bytes = clay.k * chunk_size;
    println!("  RS decode would need:    {} bytes (k full chunks)", full_decode_bytes);
    println!("  Clay repair needs:       {} bytes (β sub-chunks × d)", total_repair_bytes);
    println!(
        "  Bandwidth saving:        {:.1}%",
        (1.0 - total_repair_bytes as f64 / full_decode_bytes as f64) * 100.0
    );
    println!();

    // Perform the repair
    let recovered = clay.repair(lost_node, &partial_data, chunk_size).unwrap();

    // Verify the repair was successful
    assert_eq!(recovered, chunks[lost_node], "Repair verification failed");
    println!("✓ Repair successful with optimal bandwidth!\n");

    // === Summary ===
    println!("=== Summary ===");
    println!("Clay codes provide:");
    println!("  • Same storage overhead as Reed-Solomon ({:.2}x)", clay.n as f64 / clay.k as f64);
    println!(
        "  • {:.1}% less repair bandwidth",
        (1.0 - clay.normalized_repair_bandwidth()) * 100.0
    );
    println!("  • Repair reads only β={} sub-chunks from each of d={} helpers", clay.beta, clay.d);
    println!("  • Full data recovery with up to m={} node failures", clay.m);
}

# clay-codes

[![Crates.io](https://img.shields.io/crates/v/clay-codes.svg)](https://crates.io/crates/clay-codes)
[![Documentation](https://docs.rs/clay-codes/badge.svg)](https://docs.rs/clay-codes)
[![License](https://img.shields.io/crates/l/clay-codes.svg)](LICENSE)

A Rust implementation of Clay (Coupled-Layer) erasure codes, based on the FAST'18 paper ["Clay Codes: Moulding MDS Codes to Yield an MSR Code"](https://www.usenix.org/conference/fast18/presentation/vajha) ([PDF](https://www.usenix.org/system/files/conference/fast18/fast18-vajha.pdf)). The construction was originally implemented in [Ceph](https://github.com/ceph/ceph/pull/14300/) and is now part of its master codebase.

## Why Clay codes?

In distributed storage, node failures are routine. When a node goes down, the system must repair it by fetching data from surviving nodes. With Reed-Solomon codes, repairing a single node means downloading **k full chunks** across the network -- even though you only need to reconstruct one.

Clay codes are MSR (Minimum Storage Regenerating) codes: they provide the same fault tolerance and storage overhead as Reed-Solomon, but repair a failed node by downloading only a **fraction** of each helper's data. In practice, this reduces repair network traffic by up to **2.9x** and repair time by up to **3x**.

## Quickstart

Add to your `Cargo.toml`:

```toml
[dependencies]
clay-codes = "0.1"
```

### Encode and Decode

```rust
use clay_codes::ClayCode;
use std::collections::HashMap;

// 4 data chunks, 2 parity chunks, 5 helpers for repair
let clay   = ClayCode::new(4, 2, 5).unwrap();
let data   = b"Hello, Clay codes!";
let chunks = clay.encode(data);

// Simulate losing node 0 -- collect the surviving chunks
let mut available: HashMap<usize, Vec<u8>> = HashMap::new();
for (node, chunk) in chunks.iter().enumerate() {
    if node != 0 {
        available.insert(node, chunk.clone());
    }
}

// Recover the original data from the remaining 5 chunks
let recovered = clay.decode(&available, &[0]).unwrap();
assert_eq!(&recovered[..data.len()], &data[..]);
```

### Bandwidth-Optimal Repair

This is the core advantage over Reed-Solomon. Rather than downloading full chunks from `k` nodes, `minimum_to_repair` tells you exactly which sub-chunks to fetch from each helper, and `repair` reconstructs the lost node from that partial data.

```rust
use clay_codes::ClayCode;
use std::collections::HashMap;

let clay           = ClayCode::new(4, 2, 5).unwrap();
let data           = b"Hello, Clay codes!";
let chunks         = clay.encode(data);
let chunk_size     = chunks[0].len();
let sub_chunk_size = chunk_size / clay.sub_chunk_no;

// Which sub-chunks do we need from each helper to repair node 0?
let helpers    = vec![1, 2, 3, 4, 5];
let repair_map = clay.minimum_to_repair(0, &helpers).unwrap();

// Fetch only those sub-chunks from each helper node
let mut partial: HashMap<usize, Vec<u8>> = HashMap::new();

for (helper, sub_chunks) in &repair_map {
    let mut buf = Vec::new();
    for &sc in sub_chunks {
        let start = sc * sub_chunk_size;
        let end   = start + sub_chunk_size;
        buf.extend_from_slice(&chunks[*helper][start..end]);
    }
    partial.insert(*helper, buf);
}

// Reconstruct node 0 from the partial data
let recovered = clay.repair(0, &partial, chunk_size).unwrap();
assert_eq!(recovered, chunks[0]);
```

## Parameters

Clay codes are configured with three values:

| Parameter | Description |
|-----------|-------------|
| `k` | Number of data chunks |
| `m` | Number of parity chunks (tolerates up to `m` simultaneous failures) |
| `d` | Number of helper nodes for repair (`k+1 <= d <= k+m-1`) |

The remaining parameters are derived automatically:

| Derived | Formula | Meaning |
|---------|---------|---------|
| `q` | `d - k + 1` | Coupling factor |
| `alpha` | `q^t` | Sub-chunks per chunk (sub-packetization level) |
| `beta` | `alpha / q` | Sub-chunks downloaded per helper during repair |

### Example Configurations

| Config (k, m, d) | alpha | beta | Repair BW vs RS |
|-------------------|-------|------|-----------------|
| (4, 2, 5)         | 8     | 4    | 37.5% savings   |
| (9, 3, 11)        | 81    | 27   | 59.3% savings   |
| (10, 4, 13)       | 256   | 64   | 67.5% savings   |

Higher `d - k` means more savings, at the cost of larger sub-packetization.

## Documentation

- **API reference**: [docs.rs/clay-codes](https://docs.rs/clay-codes)
- **Implementation guide**: [`docs/clay-practical-implementation.md`](docs/clay-practical-implementation.md) -- storage layouts, repair access patterns, metadata schemas, and performance considerations for integrating Clay codes into a distributed storage system.
- **Paper notes**: [`docs/clay-codes-fast18.md`](docs/clay-codes-fast18.md) -- detailed notes on the FAST'18 paper including construction, algorithms, and experimental results.

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

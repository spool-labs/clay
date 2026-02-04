# Practical Implementation Guide for Clay Codes

This document provides practical guidance for implementing Clay codes in distributed storage systems, focusing on encoding outputs, storage layouts, repair operations, and metadata design.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Encoding: What It Produces](#2-encoding-what-it-produces)
3. [Understanding Sub-chunk Sizes](#3-understanding-sub-chunk-sizes)
4. [Repair Access Patterns](#4-repair-access-patterns)
5. [Storage Layout Options](#5-storage-layout-options)
6. [Recommended Metadata Schema](#6-recommended-metadata-schema)
7. [API Design Patterns](#7-api-design-patterns)
8. [Performance Considerations](#8-performance-considerations)

---

## 1. Overview

Clay (Coupled-Layer) codes are MSR (Minimum Storage Regenerating) codes that achieve optimal repair bandwidth while maintaining MDS (Maximum Distance Separable) properties. They reduce network traffic during node repair by up to 75% compared to traditional Reed-Solomon codes.

### Key Parameters

```
┌─────────────────────────────────────────────────────────────────┐
│  CLAY CODE PARAMETERS                                           │
├─────────────────────────────────────────────────────────────────┤
│  n = total nodes (data + parity)                                │
│  k = data nodes                                                 │
│  m = n - k = parity nodes                                       │
│  d = helper nodes used during repair (k ≤ d ≤ n-1)              │
│                                                                 │
│  Derived:                                                       │
│  q = d - k + 1           (base for layer indexing)              │
│  t = n / q               (number of y-sections)                 │
│  α = qᵗ                  (sub-chunks per node)                  │
│  β = qᵗ⁻¹                (sub-chunks fetched per helper)        │
│                                                                 │
│  Bandwidth savings: β/α = 1/q                                   │
└─────────────────────────────────────────────────────────────────┘
```

### Example Configuration

```
(n=16, k=10, m=6, d=13) Clay Code
─────────────────────────────────
q = d - k + 1 = 4
t = n / q = 4
α = 4⁴ = 256 sub-chunks per node
β = 4³ = 64 sub-chunks per helper during repair

Repair bandwidth: 25% of RS (75% savings)
```

---

## 2. Encoding: What It Produces

### The Encoding Pipeline

```
                         ENCODING FLOW

    ┌──────────────────────────────────────────────────────────┐
    │                     INPUT: Raw File                      │
    │                        (any size)                        │
    └─────────────────────────┬────────────────────────────────┘
                              │
                              ▼
    ┌──────────────────────────────────────────────────────────┐
    │              Step 1: Pad to Stripe Boundary              │
    │                                                          │
    │   File padded to multiple of stripe size S               │
    │   S must be divisible by (k × α)                         │
    └─────────────────────────┬────────────────────────────────┘
                              │
                              ▼
    ┌──────────────────────────────────────────────────────────┐
    │              Step 2: Divide into Stripes                 │
    │                                                          │
    │   ┌─────────┬─────────┬─────────┬─────────┐              │
    │   │Stripe 0 │Stripe 1 │Stripe 2 │  . . .  │              │
    │   │  S bytes│  S bytes│  S bytes│         │              │
    │   └─────────┴─────────┴─────────┴─────────┘              │
    └─────────────────────────┬────────────────────────────────┘
                              │
                              ▼
    ┌──────────────────────────────────────────────────────────┐
    │           Step 3: Encode Each Stripe (Clay)              │
    │                                                          │
    │   For each stripe:                                       │
    │   1. Split into k data chunks                            │
    │   2. Apply PRT to get uncoupled (U) representation       │
    │   3. Compute m parity chunks using MDS code              │
    │   4. Apply PFT to get coupled (C) representation         │
    └─────────────────────────┬────────────────────────────────┘
                              │
                              ▼
    ┌──────────────────────────────────────────────────────────┐
    │                   OUTPUT: n Shards                       │
    │                                                          │
    │   Each shard contains:                                   │
    │   - One chunk per stripe                                 │
    │   - Each chunk has α sub-chunks                          │
    │                                                          │
    │   ┌────────┐ ┌────────┐ ┌────────┐     ┌────────┐        │
    │   │ Shard  │ │ Shard  │ │ Shard  │     │ Shard  │        │
    │   │   0    │ │   1    │ │   2    │ ... │  n-1   │        │
    │   │        │ │        │ │        │     │        │        │
    │   │(Node 0)│ │(Node 1)│ │(Node 2)│     │(Node15)│        │
    │   └────────┘ └────────┘ └────────┘     └────────┘        │
    └──────────────────────────────────────────────────────────┘
```

### Shard Structure

Each node stores a shard with the following structure:

```
SHARD STRUCTURE (per node, per file)
════════════════════════════════════

┌─────────────────────────────────────────────────────────────┐
│                         SHARD                               │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Stripe 0 Chunk                                      │    │
│  │  ┌──────┬──────┬──────┬─────────────────┬──────┐    │    │
│  │  │ SC 0 │ SC 1 │ SC 2 │      . . .      │SC 255│    │    │
│  │  └──────┴──────┴──────┴─────────────────┴──────┘    │    │
│  │         α = 256 sub-chunks                          │    │
│  └─────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Stripe 1 Chunk                                      │    │
│  │  ┌──────┬──────┬──────┬─────────────────┬──────┐    │    │
│  │  │ SC 0 │ SC 1 │ SC 2 │      . . .      │SC 255│    │    │
│  │  └──────┴──────┴──────┴─────────────────┴──────┘    │    │
│  └─────────────────────────────────────────────────────┘    │
│                          . . .                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ Stripe N-1 Chunk                                    │    │
│  │  ┌──────┬──────┬──────┬─────────────────┬──────┐    │    │
│  │  │ SC 0 │ SC 1 │ SC 2 │      . . .      │SC 255│    │    │
│  │  └──────┴──────┴──────┴─────────────────┴──────┘    │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘

SC = Sub-chunk
Each sub-chunk size = S / (k × α)
Total shard size = (file_size / k) × (n / k)  [with overhead]
```

---

## 3. Understanding Sub-chunk Sizes

Sub-chunk size is NOT fixed by Clay parameters alone — it depends on the **stripe size** `S`.

### Formula

```
chunk_size     = S / k              (bytes per node per stripe)
subchunk_size  = S / (k × α)        (bytes per sub-chunk per stripe)
codewords      = S / (k × α)        (number of codewords per stripe)
```

### Constraints

```
S must be divisible by (k × α)

For (n=16, k=10, d=13) with α=256:
  Minimum unit = k × α = 10 × 256 = 2560 bytes
  Valid stripe sizes: 2560, 5120, 7680, ... , 1MB, 64MB, etc.
```

### Example Configurations

| Stripe Size | Chunk Size | Sub-chunk Size | Use Case |
|-------------|------------|----------------|----------|
| 2.5 KB | 256 B | 1 B | Minimum (testing only) |
| 1 MB | 100 KB | 400 B | Small files, low latency |
| 10 MB | 1 MB | 4 KB | General purpose |
| 64 MB | 6.4 MB | 25.6 KB | Large files, throughput |
| 256 MB | 25.6 MB | 102.4 KB | Bulk storage |

### Stripe Size Selection Guidelines

```
┌─────────────────────────────────────────────────────────────────┐
│  STRIPE SIZE TRADE-OFFS                                         │
├──────────────────┬──────────────────────────────────────────────┤
│  Smaller Stripe  │  + Less padding waste for small files        │
│  (1-10 MB)       │  + Lower memory during encode/decode         │
│                  │  - More stripes = more metadata overhead     │
│                  │  - Higher per-stripe fixed costs             │
├──────────────────┼──────────────────────────────────────────────┤
│  Larger Stripe   │  + Fewer stripes = less metadata             │
│  (64-256 MB)     │  + Better throughput for large files         │
│                  │  - Wasteful padding for small files          │
│                  │  - Higher memory requirements                │
└──────────────────┴──────────────────────────────────────────────┘

RECOMMENDATION:
- Variable workloads (mixed file sizes): 1-10 MB stripe
- Large file workloads (video, backups): 64+ MB stripe
- Match stripe size to typical file size when possible
```

---

## 4. Repair Access Patterns

### The Problem: Non-Contiguous Access

During repair, each helper node contributes β sub-chunks out of α total. These β sub-chunks are **scattered** across the α indices in a predictable but non-contiguous pattern.

### How Repair Layers are Determined

Each layer `z` is indexed using base-q representation:

```
z = z₀ + q·z₁ + q²·z₂ + ... + qᵗ⁻¹·zₜ₋₁

For q=4, t=4, α=256:
z = z₀ + 4·z₁ + 16·z₂ + 64·z₃

where each zᵢ ∈ {0, 1, 2, 3}
```

A node at position `(x, y)` has "red dots" (repair layers) where `z_y = x`:

```python
def get_repair_layers(node_x: int, node_y: int, q: int, t: int) -> list[int]:
    """
    Returns the β layer indices needed when this node fails.
    """
    alpha = q ** t
    repair_layers = []

    for z in range(alpha):
        # Extract z_y: the y-th digit in base-q representation
        z_y = (z // (q ** node_y)) % q
        if z_y == node_x:
            repair_layers.append(z)

    return repair_layers  # len = β = q^(t-1)
```

### Example: Repair Pattern for Node (x=2, y=1)

```
Parameters: q=4, t=4, α=256, β=64

Condition: z₁ = 2  →  (z // 4) % 4 == 2

Layer indices where z₁ = 2:
┌────────────────────────────────────────────────────────────────┐
│  z₃=0: 8,9,10,11  24,25,26,27  40,41,42,43  56,57,58,59       │
│  z₃=1: 72,73,74,75  88,89,90,91  104,105,106,107  120,121...  │
│  z₃=2: 136,137,138,139  152,153...  168,169...  184,185...    │
│  z₃=3: 200,201,202,203  216,217...  232,233...  248,249,250,251│
└────────────────────────────────────────────────────────────────┘

Total: 64 indices scattered across 0-255
```

### Visualizing the Access Pattern

```
Sub-chunk indices 0-255 for a helper node (repairing node with y=1, x=2):

     0         8        16        24        32        40        48
     ▼         ▼         ▼         ▼         ▼         ▼         ▼
   ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
   │   │   │   │   │   │   │   │   │ ■ │ ■ │ ■ │ ■ │   │   │   │   │ ...
   └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
                                   └─────────┘
                                   4 contiguous, then gap of 12

Pattern: [4 on, 12 off] repeating, with larger gaps at q² and q³ boundaries

■ = sub-chunk needed for repair
```

### Access Pattern Properties

```
For any failed node:
- β = 64 sub-chunks needed (25% of total)
- Sub-chunks appear in groups of q⁰ = 1 contiguous indices
- Wait, let me recalculate...

Actually, the contiguity depends on which y-section:

y = 0: z₀ = x  →  groups of 1, spaced by 4    (least contiguous)
y = 1: z₁ = x  →  groups of 4, spaced by 16
y = 2: z₂ = x  →  groups of 16, spaced by 64
y = 3: z₃ = x  →  groups of 64, spaced by 256 (most contiguous = single block)

For y = t-1: repair sub-chunks are fully contiguous!
For y = 0: repair sub-chunks are maximally scattered
```

---

## 5. Storage Layout Options

### Option A: Single Contiguous Array

Store all α sub-chunks as one contiguous blob per chunk.

```
STORAGE LAYOUT A
════════════════

Database Schema:
┌─────────────────────────────────────────────────────────────┐
│  chunks (                                                   │
│    file_id      TEXT,                                       │
│    stripe_idx   INT,                                        │
│    node_id      INT,                                        │
│    data         BLOB,      -- size = α × subchunk_size      │
│    PRIMARY KEY (file_id, stripe_idx, node_id)               │
│  )                                                          │
└─────────────────────────────────────────────────────────────┘

On-Disk Layout (per chunk):
┌──────┬──────┬──────┬──────┬──────┬──────┬─────────┬──────┐
│ SC 0 │ SC 1 │ SC 2 │ SC 3 │ SC 4 │ SC 5 │  . . .  │SC 255│
└──────┴──────┴──────┴──────┴──────┴──────┴─────────┴──────┘
│◄──────────────── α × subchunk_size ─────────────────────►│
```

**Repair Read Operation:**
```python
def read_for_repair(file_id: str, stripe_idx: int, node_id: int,
                    repair_layers: list[int], subchunk_size: int) -> bytes:
    # Fetch entire chunk
    chunk = db.get(file_id, stripe_idx, node_id).data

    # Extract needed sub-chunks (multiple seeks on HDD)
    result = bytearray()
    for layer in repair_layers:  # 64 scattered indices
        offset = layer * subchunk_size
        result.extend(chunk[offset:offset + subchunk_size])

    return bytes(result)
```

**Pros:**
- Simple implementation
- Good for full chunk reads (normal data access)
- Single database key per chunk

**Cons:**
- Inefficient repair I/O: 64 scattered reads within the blob
- HDD seek overhead for each non-contiguous access
- Must read entire blob into memory to extract sub-chunks

---

### Option B: Individually Indexed Sub-chunks

Store each sub-chunk as a separate database entry.

```
STORAGE LAYOUT B
════════════════

Database Schema:
┌─────────────────────────────────────────────────────────────┐
│  subchunks (                                                │
│    file_id      TEXT,                                       │
│    stripe_idx   INT,                                        │
│    node_id      INT,                                        │
│    layer_idx    INT,       -- 0 to α-1                      │
│    data         BLOB,      -- size = subchunk_size          │
│    PRIMARY KEY (file_id, stripe_idx, node_id, layer_idx)    │
│  )                                                          │
└─────────────────────────────────────────────────────────────┘

Storage: 256 entries per chunk
```

**Repair Read Operation:**
```python
def read_for_repair(file_id: str, stripe_idx: int, node_id: int,
                    repair_layers: list[int]) -> dict[int, bytes]:
    # Direct lookup of exactly the sub-chunks needed
    result = {}
    for layer in repair_layers:
        result[layer] = db.get(file_id, stripe_idx, node_id, layer).data

    return result

# Can parallelize:
async def read_for_repair_parallel(...):
    tasks = [db.get_async(..., layer) for layer in repair_layers]
    return await asyncio.gather(*tasks)
```

**Pros:**
- Direct access to any sub-chunk
- Parallel fetches possible
- Pay only for what you read during repair

**Cons:**
- 256× more database keys per chunk
- Higher metadata overhead
- More complex data path for normal reads (must reassemble)
- Potential small-write amplification

---

### Option C: Y-Section Grouped Layout (Recommended)

Group sub-chunks by their repair affinity — sub-chunks needed together during repair are stored together.

```
STORAGE LAYOUT C
════════════════

Key Insight:
  When repairing a node in y-section y, we need all layers where z_y = x.
  Group sub-chunks by their z_y value for contiguous repair reads.

Database Schema:
┌─────────────────────────────────────────────────────────────┐
│  chunk_groups (                                             │
│    file_id      TEXT,                                       │
│    stripe_idx   INT,                                        │
│    node_id      INT,                                        │
│    y_group      INT,       -- 0 to t-1 (4 groups for t=4)   │
│    data         BLOB,      -- size = β × subchunk_size      │
│    PRIMARY KEY (file_id, stripe_idx, node_id, y_group)      │
│  )                                                          │
└─────────────────────────────────────────────────────────────┘

Storage: t entries per chunk (4 for our example)
Each entry contains β = 64 sub-chunks, stored contiguously
```

#### Grouping Logic

```
REORGANIZING 256 SUB-CHUNKS INTO 4 GROUPS
═════════════════════════════════════════

Original layer index z has base-4 representation (z₀, z₁, z₂, z₃)

Group by z_y value:

┌─────────┬─────────────────────────────────────────────────────┐
│ y_group │ Contains layers where z_y ∈ {0,1,2,3}              │
│         │ for a specific y position                          │
├─────────┼─────────────────────────────────────────────────────┤
│    0    │ Layers grouped by z₀: used when failed node has y=0│
│         │ Contains 64 sub-chunks for each x ∈ {0,1,2,3}      │
├─────────┼─────────────────────────────────────────────────────┤
│    1    │ Layers grouped by z₁: used when failed node has y=1│
├─────────┼─────────────────────────────────────────────────────┤
│    2    │ Layers grouped by z₂: used when failed node has y=2│
├─────────┼─────────────────────────────────────────────────────┤
│    3    │ Layers grouped by z₃: used when failed node has y=3│
└─────────┴─────────────────────────────────────────────────────┘
```

#### Group Construction

```python
def construct_groups(chunk_data: bytes, q: int, t: int,
                     subchunk_size: int) -> dict[int, bytes]:
    """
    Reorganize α sub-chunks into t groups for optimal repair access.

    Args:
        chunk_data: Original chunk with α contiguous sub-chunks
        q: Base (d - k + 1)
        t: Number of y-sections (n / q)
        subchunk_size: Size of each sub-chunk in bytes

    Returns:
        Dictionary mapping y_group -> grouped sub-chunk data
    """
    alpha = q ** t
    beta = q ** (t - 1)

    groups = {y: bytearray() for y in range(t)}

    # For each y-section, collect sub-chunks in order of (x, remaining coords)
    for y in range(t):
        for x in range(q):  # For each possible x value
            # Collect layers where z_y = x
            for z in range(alpha):
                z_y = (z // (q ** y)) % q
                if z_y == x:
                    offset = z * subchunk_size
                    groups[y].extend(chunk_data[offset:offset + subchunk_size])

    return {y: bytes(data) for y, data in groups.items()}
```

#### Internal Layout of Each Group

```
GROUP INTERNAL STRUCTURE (y_group = 1 example)
══════════════════════════════════════════════

Within y_group=1, sub-chunks are ordered by (x, then other coordinates):

┌─────────────────────────────────────────────────────────────────┐
│  y_group = 1 (β × subchunk_size = 64 × subchunk_size bytes)     │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ x=0 block: 16 sub-chunks (z₁=0)                         │    │
│  │ Layers: 0,1,2,3,16,17,18,19,32,33,34,35,48,49,50,51     │    │
│  └─────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ x=1 block: 16 sub-chunks (z₁=1)                         │    │
│  │ Layers: 4,5,6,7,20,21,22,23,36,37,38,39,52,53,54,55     │    │
│  └─────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ x=2 block: 16 sub-chunks (z₁=2)                         │    │
│  │ Layers: 8,9,10,11,24,25,26,27,40,41,42,43,56,57,58,59   │    │
│  └─────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ x=3 block: 16 sub-chunks (z₁=3)                         │    │
│  │ Layers: 12,13,14,15,28,29,30,31,44,45,46,47,60,61,62,63 │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘

For repair of node (x=2, y=1):
  - Read y_group=1
  - Extract x=2 block (offset = 2 × 16 × subchunk_size, len = 16 × subchunk_size)
  - This is ONE contiguous read within the group!
```

**Wait, there's a subtlety here.** Let me reconsider. When we repair a node at (x, y), we need sub-chunks from ALL helpers where z_y = x. So from each helper, we need the same set of layer indices. The grouping should make accessing those layers contiguous.

Let me refine:

```
REFINED GROUP STRUCTURE
═══════════════════════

For y_group = y, store all layers sorted by z_y first, then by z:

Within group y:
  Offset for layer z = position_in_group(z, y) × subchunk_size

  where position_in_group(z, y) considers z_y as primary sort key

When repairing node (x, y):
  - From each helper: read y_group = y
  - Extract: x × (β/q) sub-chunks at offset x × (β/q) × subchunk_size
  - Length: (β/q) × subchunk_size = (q^(t-1) / q) × ss = q^(t-2) × ss

  Wait, that's not quite right either. Let me reconsider...
```

Actually, the correct approach for Option C is simpler:

```
CORRECTED OPTION C: REPAIR-ALIGNED GROUPS
═════════════════════════════════════════

For each y-section y, store β sub-chunks per x value:

chunk_groups table:
  (file_id, stripe_idx, node_id, y, x) → BLOB of (β/q) sub-chunks

But that's essentially t×q = n groups, back to high overhead.

BETTER APPROACH: Single group per y, indexed by x within

chunk_groups table:
  (file_id, stripe_idx, node_id, y) → BLOB of β sub-chunks

Internal layout: sorted so that sub-chunks for each x are contiguous

During repair of node (x_failed, y_failed):
  - y_section = y_failed
  - For each helper node:
      group_data = db.get(file_id, stripe_idx, helper_node_id, y_section)
      # The entire group_data is what we need! Single contiguous read.
```

**Key Insight:** All helpers contribute sub-chunks from the SAME set of layer indices (determined by the failed node's y-section). So storing by y_group means:
- 1 contiguous read per helper during repair
- t = 4 blobs per chunk instead of 256

**Repair Read Operation (Option C):**
```python
def read_for_repair(file_id: str, stripe_idx: int, node_id: int,
                    failed_node_y: int) -> bytes:
    # Single contiguous read of the entire y-group
    return db.get(file_id, stripe_idx, node_id, failed_node_y).data
```

**Normal Read Operation (Option C):**
```python
def read_full_chunk(file_id: str, stripe_idx: int, node_id: int,
                    q: int, t: int, subchunk_size: int) -> bytes:
    """Reassemble full chunk from groups for normal data access."""
    alpha = q ** t
    chunk = bytearray(alpha * subchunk_size)

    for y in range(t):
        group_data = db.get(file_id, stripe_idx, node_id, y).data
        # Scatter group data back to original positions
        for pos, z in enumerate(get_layers_for_y_group(y, q, t)):
            offset = z * subchunk_size
            group_offset = pos * subchunk_size
            chunk[offset:offset+subchunk_size] = \
                group_data[group_offset:group_offset+subchunk_size]

    return bytes(chunk)
```

**Pros:**
- Optimal repair I/O: single contiguous read per helper
- Only t = 4 database entries per chunk (vs 256 for Option B)
- Reasonable metadata overhead

**Cons:**
- Full reads require reassembly from t groups
- Slightly more complex encoding (must reorder sub-chunks)
- Less intuitive than linear layout

---

### Storage Layout Comparison

| Aspect | Option A | Option B | Option C |
|--------|----------|----------|----------|
| DB entries per chunk | 1 | 256 (α) | 4 (t) |
| Repair read ops | 64 scattered | 64 direct | 1 contiguous |
| Full read ops | 1 contiguous | 256 lookups | 4 + reassembly |
| Metadata overhead | Low | High | Medium |
| HDD performance | Poor repair | N/A (SSD only) | Good |
| SSD performance | Good | Good | Good |
| Implementation | Simple | Complex | Moderate |

**Recommendation:**
- **Option C** for production systems with repair efficiency priority
- **Option A** if repair is rare and read performance is critical
- **Option B** for pure SSD deployments with high IOPS budget

---

## 6. Recommended Metadata Schema

### File Metadata

```sql
CREATE TABLE files (
    file_id         TEXT PRIMARY KEY,
    original_name   TEXT,
    original_size   BIGINT,          -- Original file size in bytes
    padded_size     BIGINT,          -- Size after padding

    -- Clay code parameters
    n               INT,              -- Total nodes
    k               INT,              -- Data nodes
    d               INT,              -- Helper nodes for repair
    q               INT,              -- d - k + 1
    t               INT,              -- n / q
    alpha           INT,              -- q^t (sub-chunks per node)
    beta            INT,              -- q^(t-1) (repair sub-chunks)

    -- Stripe configuration
    stripe_size     BIGINT,           -- Bytes per stripe
    stripe_count    INT,              -- Number of stripes
    subchunk_size   INT,              -- Bytes per sub-chunk

    -- Integrity
    checksum        TEXT,             -- SHA-256 of original file
    created_at      TIMESTAMP,

    -- Status
    status          TEXT              -- 'complete', 'partial', 'failed'
);
```

### Node Layout Metadata

```sql
CREATE TABLE node_layout (
    file_id         TEXT,
    node_index      INT,              -- 0 to n-1
    node_x          INT,              -- x coordinate (0 to q-1)
    node_y          INT,              -- y coordinate (0 to t-1)
    is_data         BOOLEAN,          -- true for data nodes, false for parity

    -- Node location
    storage_node_id TEXT,             -- Physical storage node identifier
    endpoint_url    TEXT,             -- API endpoint for this node

    -- Status
    status          TEXT,             -- 'healthy', 'degraded', 'failed'
    last_verified   TIMESTAMP,

    PRIMARY KEY (file_id, node_index)
);
```

### Stripe Metadata

```sql
CREATE TABLE stripes (
    file_id         TEXT,
    stripe_index    INT,

    -- Integrity per stripe
    data_checksum   TEXT,             -- Checksum of original stripe data

    -- Status tracking
    complete_nodes  INT,              -- Count of nodes with complete data

    PRIMARY KEY (file_id, stripe_index)
);
```

### Chunk Groups (for Option C layout)

```sql
CREATE TABLE chunk_groups (
    file_id         TEXT,
    stripe_index    INT,
    node_index      INT,
    y_group         INT,              -- 0 to t-1

    data            BLOB,             -- β sub-chunks, contiguous
    checksum        TEXT,             -- Checksum of this group

    PRIMARY KEY (file_id, stripe_index, node_index, y_group)
);
```

### Repair Tracking

```sql
CREATE TABLE repair_operations (
    repair_id       TEXT PRIMARY KEY,
    file_id         TEXT,
    failed_node     INT,              -- Node index being repaired
    failed_node_x   INT,
    failed_node_y   INT,

    -- Helper selection
    helper_nodes    INT[],            -- Array of d helper node indices

    -- Progress
    status          TEXT,             -- 'pending', 'in_progress', 'complete', 'failed'
    started_at      TIMESTAMP,
    completed_at    TIMESTAMP,

    -- Metrics
    bytes_fetched   BIGINT,
    bytes_repaired  BIGINT,

    -- Error tracking
    error_message   TEXT
);
```

---

## 7. API Design Patterns

### Core API Endpoints

```
STORAGE NODE API
════════════════

POST /encode
  Upload and encode a file, distribute to n nodes
  Body: multipart file upload
  Response: { file_id, stripe_count, node_assignments[] }

GET /decode/{file_id}
  Retrieve and decode a file
  Response: File stream

GET /chunk/{file_id}/{stripe_idx}/{node_idx}
  Retrieve a full chunk (for normal reads)
  Response: Binary chunk data

GET /repair-data/{file_id}/{stripe_idx}/{node_idx}
  Retrieve sub-chunks needed for repair
  Query params: failed_node_y (determines which y-group to return)
  Response: Binary data (β sub-chunks)

POST /repair/{file_id}/{node_idx}
  Initiate repair of a failed node
  Response: { repair_id, status }

GET /repair/{repair_id}
  Check repair status
  Response: { status, progress, bytes_repaired }
```

### Repair Flow Implementation

```python
class ClayRepairClient:
    def __init__(self, file_metadata: FileMetadata, node_endpoints: list[str]):
        self.meta = file_metadata
        self.endpoints = node_endpoints

    async def repair_node(self, failed_node: int) -> bytes:
        """
        Repair a failed node using optimal Clay repair algorithm.

        Returns the reconstructed shard data for the failed node.
        """
        # Step 1: Determine helper nodes
        helpers = self.minimum_to_repair(failed_node)
        failed_x, failed_y = self.node_to_coords(failed_node)

        # Step 2: Fetch repair data from all helpers (parallel)
        repair_data = {}
        tasks = []

        for helper in helpers:
            endpoint = self.endpoints[helper]
            task = self.fetch_repair_data(
                endpoint=endpoint,
                file_id=self.meta.file_id,
                node_id=helper,
                y_group=failed_y  # This is the key for Option C!
            )
            tasks.append((helper, task))

        results = await asyncio.gather(*[t[1] for t in tasks])
        repair_data = {tasks[i][0]: results[i] for i in range(len(tasks))}

        # Step 3: Reconstruct failed node's data
        return self.clay_repair_decode(
            failed_node=failed_node,
            failed_x=failed_x,
            failed_y=failed_y,
            repair_data=repair_data
        )

    async def fetch_repair_data(self, endpoint: str, file_id: str,
                                 node_id: int, y_group: int) -> bytes:
        """Fetch repair sub-chunks from a helper node."""
        async with aiohttp.ClientSession() as session:
            # Single request fetches all needed sub-chunks (contiguous in Option C)
            url = f"{endpoint}/repair-data/{file_id}"
            params = {"node_id": node_id, "y_group": y_group}

            async with session.get(url, params=params) as resp:
                return await resp.read()

    def minimum_to_repair(self, failed_node: int) -> list[int]:
        """Select d optimal helper nodes for repair."""
        failed_x, failed_y = self.node_to_coords(failed_node)

        # Must include all surviving nodes in same y-section
        y_section_nodes = self.get_y_section_nodes(failed_y)
        mandatory = [n for n in y_section_nodes if n != failed_node]

        # Fill remaining from other y-sections
        other_nodes = [n for n in range(self.meta.n)
                       if n != failed_node and n not in y_section_nodes]

        helpers = mandatory + other_nodes[:self.meta.d - len(mandatory)]
        return helpers[:self.meta.d]

    def node_to_coords(self, node_idx: int) -> tuple[int, int]:
        """Convert linear node index to (x, y) coordinates."""
        # Layout: nodes arranged in t y-sections of q nodes each
        y = node_idx // self.meta.q
        x = node_idx % self.meta.q
        return x, y

    def get_y_section_nodes(self, y: int) -> list[int]:
        """Get all node indices in y-section y."""
        return [y * self.meta.q + x for x in range(self.meta.q)]
```

---

## 8. Performance Considerations

### Bandwidth Comparison

```
REPAIR BANDWIDTH: RS vs CLAY
════════════════════════════

For (n=16, k=10, d=13) code:

RS Code:
  Download k full chunks = k × α × subchunk_size
                        = 10 × 256 × ss
                        = 2560 × ss

Clay Code:
  Download β sub-chunks from d helpers = d × β × subchunk_size
                                       = 13 × 64 × ss
                                       = 832 × ss

Savings: 1 - (832/2560) = 67.5%

┌─────────────────────────────────────────────────────────────┐
│           Repair Bandwidth Reduction: 3× less              │
└─────────────────────────────────────────────────────────────┘
```

### I/O Pattern Comparison

```
DISK I/O PATTERNS BY STORAGE LAYOUT
═══════════════════════════════════

Scenario: Repair single node, 64 MB stripe, 25.6 KB sub-chunks

Option A (contiguous array):
  Per helper: 1 read of 6.4 MB, extract 64 scattered sub-chunks
  Pattern: Read full blob, seek 64 times within buffer
  Actual disk: 1 sequential read + memory operations

Option B (indexed sub-chunks):
  Per helper: 64 random reads of 25.6 KB each
  Pattern: 64 seeks across different locations
  Actual disk: 64 random I/Os (terrible on HDD)

Option C (y-grouped):
  Per helper: 1 read of 1.6 MB (β × subchunk_size)
  Pattern: Single sequential read of exactly what's needed
  Actual disk: 1 sequential read, no waste

┌─────────────────────────────────────────────────────────────┐
│  HDD:  Option C >> Option A >> Option B                     │
│  SSD:  Option C ≈ Option B > Option A (for repair)          │
│  NVMe: Option C ≈ Option B ≈ Option A                       │
└─────────────────────────────────────────────────────────────┘
```

### Memory Requirements

```
MEMORY DURING REPAIR
════════════════════

Repairing one node for a single stripe:

Helper data buffered:
  d × β × subchunk_size = 13 × 64 × 25.6 KB = 21.3 MB

Reconstruction buffer:
  α × subchunk_size = 256 × 25.6 KB = 6.4 MB

U-buffer (uncoupled intermediates):
  n × subchunk_size × repair_layers = varies, ~6-12 MB

Total per stripe: ~35-40 MB

For parallel stripe repair (8 stripes):
  ~280-320 MB
```

### Encoding Overhead

```
ENCODING TIME COMPARISON
════════════════════════

Clay vs RS for same (n, k):

MDS encode (shared):     O(m × k × L)
PRT/PFT transforms:      O(α × L) per parity node

Clay overhead: ~10-30% additional CPU time for transforms

Encoding is write-once; repair is ongoing.
Trade-off favors Clay in read-heavy / repair-frequent workloads.
```

---

## Summary

| Component | Recommendation |
|-----------|----------------|
| **Storage Layout** | Option C (y-grouped) for optimal repair |
| **Stripe Size** | 10-64 MB for typical workloads |
| **Metadata** | Store full Clay params + node coordinates |
| **Repair API** | Single endpoint returning y-group blob |
| **Normal Read** | Reassemble from t groups (parallelizable) |

The key insight is that Clay codes' repair efficiency can only be realized if the storage layout supports contiguous access to repair sub-chunks. Option C achieves this by grouping sub-chunks by their y-section affinity, ensuring that repair operations require only a single sequential read per helper node.

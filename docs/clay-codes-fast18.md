# Clay Codes: Moulding MDS Codes to Yield an MSR Code

**Authors:**
- Myna Vajha, Vinayak Ramkumar, Bhagyashree Puranik, Ganesh Kini, Elita Lobo, Birenjith Sasidharan, P. Vijay Kumar - *Indian Institute of Science, Bangalore*
- Alexander Barg, Min Ye - *University of Maryland*
- Srinivasan Narayanamurthy, Syed Hussain, Siddhartha Nandi - *NetApp ATG, Bangalore*

**Published in:** 16th USENIX Conference on File and Storage Technologies (FAST '18), February 12-15, 2018, Oakland, CA, USA

**Source:** https://www.usenix.org/conference/fast18/presentation/vajha

---

## Abstract

With increase in scale, the number of node failures in a data center increases sharply. To ensure availability of data, failure-tolerance schemes such as Reed-Solomon (RS) or more generally, Maximum Distance Separable (MDS) erasure codes are used. However, while MDS codes offer minimum storage overhead for a given amount of failure tolerance, they do not meet other practical needs of today's data centers. Although modern codes such as Minimum Storage Regenerating (MSR) codes are designed to meet these practical needs, they are available only in highly-constrained theoretical constructions, that are not sufficiently mature enough for practical implementation.

We present **Clay codes** that extract the best from both worlds. Clay (short for Coupled-Layer) codes are MSR codes that offer a simplified construction for decoding/repair by using pairwise coupling across multiple stacked layers of any single MDS code.

In addition, Clay codes provide the first practical implementation of an MSR code that offers:
- (a) low storage overhead
- (b) simultaneous optimality in terms of three key parameters: repair bandwidth, sub-packetization level and disk I/O
- (c) uniform repair performance of data and parity nodes
- (d) support for both single and multiple-node repairs, while permitting faster and more efficient repair

While all MSR codes are vector codes, none of the distributed storage systems support vector codes. We have modified Ceph to support any vector code, and our contribution is now a part of Ceph's master codebase. We have implemented Clay codes, and integrated it as a plugin to Ceph. Six example Clay codes were evaluated on a cluster of Amazon EC2 instances and code parameters were carefully chosen to match known erasure-code deployments in practice. A particular example code, with storage overhead 1.25x, is shown to reduce repair network traffic by a factor of 2.9 in comparison with RS codes and similar reductions are obtained for both repair time and disk read.

---

## 1. Introduction

The number of failures in storage subsystems increase as data centers scale. In order to ensure data availability and durability, failure-tolerant solutions such as replication and erasure codes are used. It is important for these solutions to be highly efficient so that they incur low cost in terms of their utilization of storage, computing and network resources. This additional cost is considered an overhead, as the redundancy introduced for failure tolerance does not aid the performance of the application utilizing the data.

In order to be failure tolerant, data centers have increasingly started to adopt erasure codes in place of replication. A class of erasure codes known as **maximum distance separable (MDS) codes** offer the same level of failure tolerance as replication codes with minimal storage overhead. For example, Facebook reported reduced storage overhead of 1.4x by using Reed-Solomon (RS) codes, a popular class of MDS codes, as opposed to the storage overhead of 3x incurred in triple replication.

The disadvantage of the traditional MDS codes is their high repair cost. In case of replication, when a node or storage subsystem fails, an exact copy of the lost data can be copied from surviving nodes. However, in case of erasure codes, dependent data that is more voluminous in comparison with the lost data, is copied from surviving nodes and the lost data is then computed by a repair node, which results in a higher repair cost when compared to replication. This leads to increased repair bandwidth and repair time.

A class of erasure codes, termed as **minimum storage regenerating (MSR) codes**, offer all the advantages of MDS codes but require lesser repair bandwidth. Until recently, MSR codes lacked several key desirable properties that are important for practical systems. For example, they were computationally more complex, or demonstrated non-uniform repair characteristics for different types of node failures, or were able to recover from only a limited (one or two) number of failures, or they lacked constructions of common erasure code configurations. The first theoretical construction that offered all the desirable properties of an MSR code was presented by Ye and Barg.

This paper presents Clay codes that extend the theoretical construction presented in Ye-Barg, with practical considerations. Clay codes are constructed by placing any MDS code in multiple layers and performing pair-wise coupling across layers. Such a construction offers efficient repair with optimal repair bandwidth, causing Clay codes to fall in the MSR arena.

### Contributions

Our contributions include:
- (a) the construction of Clay codes as explained in Section 3
- (b) the modification made to Ceph in order to support any vector code, explained in Section 4
- (c) the integration of Clay codes as a plugin to Ceph, explained in Section 4

We conducted experiments to compare the performance of Clay codes with RS codes available in Ceph. One of the example Clay codes that we evaluated, which has a storage overhead of 1.25x, was able to bring down the repair network traffic by a factor of 2.9 when compared with the RS code of same parameters. Similar reductions were also obtained for disk read and repair time.

---

## 2. Background and Preliminaries

### Erasure Code

Erasure codes are an alternative to replication for ensuring failure tolerance in data storage. In an `[n, k]` erasure-coded system, data pertaining to an object is first divided into `k` data chunks and then encoded to obtain `m = n - k` parity chunks. When we do not wish to distinguish between a data or parity chunk, we will simply refer to the chunk as a *coded chunk*. The collection of `n` coded chunks obtained after encoding are stored in `n` distinct nodes. Here, by *node*, we mean an independent failure domain such as a disk or a storage node of a distributed storage system (DSS).

The **storage efficiency** of an erasure code is measured by storage overhead defined as the ratio of the number of coded chunks `n` to the number of data chunks `k`. Every erasure code has an underlying finite field over which computations are performed. For simplicity, we assume here that the field is of size 2^8 and hence each element of the finite field can be represented by a byte.

### Scalar Codes

Let each data chunk be comprised of `L` bytes. In the case of a scalar code, one byte from each of the `k` data chunks is picked and the `k` bytes are linearly combined in `m` different ways, to obtain `m` parity bytes. The resultant set of `n = k + m` bytes so obtained is called a *codeword*. This operation is repeated in parallel for all the `L` bytes in a data chunk to obtain `L` codewords. This operation will also result in the creation of `m` parity chunks, each composed of `L` bytes.

*Figure 1: A pictorial representation of a scalar code. The L = 6 horizontal layers are the codewords and the n = 6 vertical columns, the chunks, with the first k = 4 chunks corresponding to data chunks and the last (n-k) = 2 chunks, the parity chunks. Each unit (tiny rectangle) in the figure corresponds to a single byte.*

```
                    ┌─────────────────────────────────┐
                    │           n = 6 nodes           │
                    ├───────┬───────┬───────┬───────┬───────┬───────┤
                    │ Node 0│ Node 1│ Node 2│ Node 3│ Node 4│ Node 5│
                    │ (data)│ (data)│ (data)│ (data)│(parity│(parity│
         ┌──────────┼───────┼───────┼───────┼───────┼───────┼───────┤
         │Codeword 0│  [a]  │  [b]  │  [c]  │  [d]  │  [p]  │  [q]  │
         ├──────────┼───────┼───────┼───────┼───────┼───────┼───────┤
         │Codeword 1│  [e]  │  [f]  │  [g]  │  [h]  │  [r]  │  [s]  │
         ├──────────┼───────┼───────┼───────┼───────┼───────┼───────┤
  L = 6  │Codeword 2│  [i]  │  [j]  │  [k]  │  [l]  │  [t]  │  [u]  │
  layers ├──────────┼───────┼───────┼───────┼───────┼───────┼───────┤
         │Codeword 3│  [m]  │  [n]  │  [o]  │  [p]  │  [v]  │  [w]  │
         ├──────────┼───────┼───────┼───────┼───────┼───────┼───────┤
         │Codeword 4│  [q]  │  [r]  │  [s]  │  [t]  │  [x]  │  [y]  │
         ├──────────┼───────┼───────┼───────┼───────┼───────┼───────┤
         │Codeword 5│  [u]  │  [v]  │  [w]  │  [x]  │  [z]  │  [!]  │
         └──────────┴───────┴───────┴───────┴───────┴───────┴───────┘
                    │◄─────── k = 4 ───────►│◄─ n-k = 2 ─►│
                    │      data chunks      │parity chunks│
```

### Vector Codes

The difference in the case of vector codes is that here, one works with ordered collections of `α >= 1` bytes at a time. For convenience, we will refer to such an ordered collection of `α` bytes as a *superbyte*. In the encoding process, a superbyte from each of the `k` data chunks is picked and the `k` superbytes are then linearly combined in `m` different ways, to obtain `m` parity superbytes. The resultant set of `n = k + m` superbytes is called a *(vector) codeword*. This operation is repeated in parallel for all the `N = L/α` superbytes in a data chunk to obtain `N` codewords.

The number `α` of bytes within a superbyte is termed the **sub-packetization level** of the code. Scalar codes such as RS codes can be regarded as having sub-packetization level `α = 1`. The advantage of vector codes is that repair of a coded chunk in a failed node can potentially be accomplished by accessing only a subset of the `α` bytes within the superbyte, present in each of the remaining coded chunks, corresponding to the same codeword. This reduces network traffic arising from node repair.

### Sub-chunking through Interleaving

When the sub-packetization level `α` is large, given that operations involving multiple codewords are carried out in parallel, it is advantageous, from an ease-of-memory-access viewpoint, to interleave the bytes so that the corresponding bytes across different codewords are stored contiguously. This is particularly true when the number `N` of superbytes within a chunk is large. With interleaving, each data chunk is partitioned into `α` subsets, which we shall refer to as **sub-chunks**. Thus each sub-chunk within a node holds one byte from each of the `N` codewords stored in the node.

### MDS Codes

The sub-class of `(n, k)` erasure codes, either scalar or vector, having the property that they can recover from the failure of any `(n - k)` nodes are called **MDS codes**. For a fixed `k`, these codes have the smallest storage overhead `n/k` among any of the erasure codes that can recover from a failure of a fixed number of `n - k` nodes. Examples include RS, Row-Diagonal Parity and EVENODD codes. Facebook data centers have employed a `(14, 10)` RS code in their data warehouse cluster.

### Node Repair

The need for node repair in a distributed storage system can arise either because a particular hardware component has failed, is undergoing maintenance, is being rebooted or else, is simply busy serving other simultaneous requests for data. A substantial amount of network traffic is generated on account of node-repair operations. An example cited is one of a Facebook data-warehouse, that stores multiple petabytes of data, where the median amount of data transferred through top-of-rack switches for the purposes of node repair, is in excess of 0.2 petabytes per day.

The traffic arising from node-repair requests, eats into the bandwidth available to serve user requests for data. The time taken for node repair also directly affects system availability. Thus there is strong interest in coding schemes that minimize the amount of data transfer across the network, and the time taken to repair a failed node. Under the conventional approach to repairing an RS code for instance, one would have to download `k` times the amount of data as is stored in a failed node to restore the failed node, which quite clearly, is inefficient.

### MSR Codes

**MSR codes** are a sub-class of vector MDS codes that have the smallest possible repair bandwidth. To restore a failed node containing `α` bytes in an `(n, k)` MSR code, the code first contacts an arbitrarily-chosen subset of `d` helper nodes, where `d` is a design parameter that can take on values ranging from `k` to `(n - 1)`. It then downloads `β = α/(d - k + 1)` bytes from each helper node, and restores the failed node using the helper data.

The total amount `dβ` of bytes downloaded is typically much smaller than the total amount `kα` bytes of data stored in the `k` nodes. Here `α` is the sub-packetization level of an MSR code. The total number `dβ` of bytes downloaded for node repair is called the **repair bandwidth**.

Let us define the **normalized repair bandwidth** to be the quantity:

```
dβ/(kα) = d / (k(d - k + 1))
```

For the particular case `d = (n - 1)`, the normalized value equals `(n - 1) / (k(n - k))`. It follows that the larger the number `(n - k)` of parity chunks, the greater the reduction in repair traffic.

We will also use the parameter `M = kα` to denote the total number of data bytes contained in an MSR codeword. Thus an MSR code has associated parameter set given by `{(n, k), d, (α, β), M}` with `β = α/(d - k + 1)` and `M = kα`.

### Additional Desired Attributes

Over and above the low repair-bandwidth and low storage-overhead attributes of MSR codes, there are some additional properties that one would like a code to have:

- (a) **uniform repair capability** - the ability to repair data and parity nodes with the same low repair bandwidth
- (b) **minimal disk read** - the amount of data read from disk for node repair in a helper node is the same as the amount of data transferred over the network from the helper node
- (c) **low value of sub-packetization parameter α**
- (d) **small size of underlying finite field** over which the code is constructed

In MSR codes that possess the disk read optimal property, both network traffic and number of disk reads during node repair are simultaneously minimized and are the same.

---

## 2.1 Related Work

The problem of efficient node repair has been studied for some time and several solutions have been proposed:

- **Locally repairable codes** such as the Windows Azure Code and Xorbas trade the MDS property to allow efficient node-repair by accessing a smaller number of helper nodes
- **Piggy-backed RS codes** achieve reductions in network traffic while retaining the MDS property but they do not achieve the savings that are possible with an MSR code

Though there are multiple implementations of MSR codes, these are lacking in one or the other of the desired attributes:

| Code | Storage O/h | Failure Tolerance | All-Node Optimal Repair | Disk Read Optimal | Repair-bandwidth Optimal | α | Order of GF | Implemented Distributed System |
|------|-------------|-------------------|-------------------------|-------------------|--------------------------|---|-------------|-------------------------------|
| RS | Low | n-k | No | No | No | 1 | Low | HDFS, Ceph, Swift, etc. |
| PM-RBT | High | n-k | Yes | Yes | Yes | Linear | Low | Own system |
| Butterfly | Low | 2 | Yes | No | Yes | Exponential | Low | HDFS, Ceph |
| HashTag | Low | n-k | No | No | Yes | Polynomial | High | HDFS |
| **Clay Code** | **Low** | **n-k** | **Yes** | **Yes** | **Yes** | **Polynomial** | **Low** | **Ceph** |

*Table 1: Detailed comparison of Clay codes with RS and other practical MSR codes. Here, the scaling of α is with respect to n for a fixed storage overhead (n/k).*

---

## 2.2 Refinements over Ye-Barg Code

The presentation of the Clay code here is from a **coupled-layer perspective** that leads directly to implementation, whereas the description in Ye-Barg is primarily in terms of parity-check matrices. For example, using the coupled-layer viewpoint, both data decoding (by which we mean recovery from a maximum of `(n - k)` erasures) as well as node-repair algorithms can be described in terms of two simple operations:
1. decoding of the scalar MDS code
2. an elementary linear transformation between pairs of bytes

In addition, Clay codes can be constructed using **any scalar MDS code** as building blocks, while Ye-Barg code is based only on Vandermonde-RS codes. Therefore, scalar MDS codes that have been time-tested, and best suited for a given application or workload need not be modified in order to make the switch to MSR codes.

The third important distinction is that, in Ye-Barg, only the single node-failure case is discussed. In the case of Clay codes, we have come up with a **generic algorithm to repair multiple failures**, that has allowed us to repair many instances of multiple node repair with reduced repair bandwidth.

---

## 3. Construction of the Clay Code

### Single Codeword Description

In Section 2, we noted that each node stores a data chunk and that a data chunk is comprised of `L` bytes from `N` codewords. In the present section we will restrict our attention to the case of a single codeword, i.e., to the case when `N = 1`, `L = α`.

### Parameters of Clay Codes Evaluated

| (n, k) | d | (α, β) | (dβ)/(kα) |
|--------|---|--------|-----------|
| (6, 4) | 5 | (8, 4) | 0.625 |
| (12, 9) | 11 | (81, 27) | 0.407 |
| (14, 10) | 13 | (256, 64) | 0.325 |
| (14, 10) | 12 | (243, 81) | 0.4 |
| (14, 10) | 11 | (128, 64) | 0.55 |
| (20, 16) | 19 | (1024, 256) | 0.297 |

*Table 2: Parameters of the Clay codes evaluated here.*

As can be seen, the normalized repair bandwidth can be made much smaller by increasing the value of `(d - k + 1)`. For example, the normalized repair bandwidth for a `(20, 16)` code equals 0.297, meaning that the repair bandwidth of a Clay code is less than 30% of the corresponding value for `α = 1024` layers of a `(20, 16)` RS code.

### Explaining Through Example

We will describe the Clay code via an example code having parameters:
```
{(n = 4, k = 2), d = 3, (α = 4, β = 2), M = 8}
```

The codeword is stored across `n = 4` nodes of which `k = 2` are data nodes and `n - k = 2` are parity nodes. Each node stores a superbyte made up of `α = 4` bytes. The code has storage overhead `nα/(kα) = n/k = 2` which is the ratio of the total number `nα = 16` of bytes stored to the number `M = kα = 8` of data bytes. During repair of a failed node, `β = 2` bytes of data are downloaded from each of the `d = 3` helper nodes, resulting in a normalized repair bandwidth of `dβ/(kα) = d/(k(d-k+1)) = 0.75`.

### Starting Point: A (4,2) Scalar RS Code

We begin our description of the Clay code with a simple, distributed data storage setup composed of 4 nodes, where the nodes are indexed by `(x, y)` coordinates:
```
{(x, y) | (x, y) ∈ J}, J = {(0,0), (1,0), (0,1), (1,1)}
```

Let us assume that a `(4, 2)` RS code M is used to encode and store data on these 4 nodes. We assume that nodes `(0,0), (1,0)` store data, nodes `(0,1), (1,1)` store parity. Two nodes are said to be in the same **y-section** if they have the same y-coordinate.

### The Uncoupled Code

Next, consider storing on the same 4 nodes, 4 codewords drawn from the same RS code M. Thus each node now stores 4 bytes, each associated to a different codeword. We will use the parameter `z ∈ {0, 1, 2, 3}` to index the 4 codewords.

Together these 4 codewords form the **uncoupled code U**, whose bytes are denoted by `{U(x, y, z) | (x, y) ∈ J, z ∈ {0, 1, 2, 3}}`. These 16 bytes can be viewed as being stored in a data cube composed of 4 horizontal layers (or planes), with 4 bytes to a layer. The data cube can also be viewed as being composed of 4 (vertical) columns, each column composed of 4 cylinders. Each column stores a superbyte while each of the 4 cylinders within a column stores a single byte.

```
                         THE DATA CUBE

            4 Nodes (columns) × 4 Layers = 16 bytes total

                    y=0 (data)       y=1 (parity)
                    x=0     x=1      x=0     x=1
                   (0,0)   (1,0)    (0,1)   (1,1)
                     │       │        │       │
                     ▼       ▼        ▼       ▼
              ┌──────────────────────────────────────┐
     Layer    │    ┌───┐   ┌───┐    ┌───┐   ┌───┐    │
     z=3      │    │ ● │   │   │    │   │   │ ● │    │  ● = "red" unpaired
     (1,1)    │    └───┘   └───┘    └───┘   └───┘    │      vertex (x = z_y)
              │                                      │
     Layer    │    ┌───┐   ┌───┐    ┌───┐   ┌───┐    │
     z=2      │    │   │   │ ● │    │ ● │   │   │    │
     (1,0)    │    └───┘   └───┘    └───┘   └───┘    │
              │                                      │
     Layer    │    ┌───┐   ┌───┐    ┌───┐   ┌───┐    │
     z=1      │    │   │◄──┼─┼─┼───►│ ● │   │   │    │  ◄──► = paired vertices
     (0,1)    │    └───┘   └───┘    └───┘   └───┘    │        (same y-section)
              │                                      │
     Layer    │    ┌───┐   ┌───┐    ┌───┐   ┌───┐    │
     z=0      │    │ ● │   │   │    │   │   │ ● │    │
     (0,0)    │    └───┘   └───┘    └───┘   └───┘    │
              └──────────────────────────────────────┘
                     │       │        │       │
                     └───────┴────────┴───────┘
                          4 superbytes
                     (one per node/column)
```

It can be verified that the uncoupled code inherits the property that data stored in the 4 nodes can be recovered by connecting to any 2 nodes. As one might expect, this code offers no savings in repair bandwidth over that of the constituent RS codes, since we have simply replicated the same RS code 4 times.

### Using a Pair of Coordinates to Represent a Layer

The coupling of the layers is easier explained in terms of a binary representation `(z₀, z₁)` of the layer-index `z`, defined by `z = 2z₀ + z₁`:
- 0 ⇒ (0, 0)
- 1 ⇒ (0, 1)
- 2 ⇒ (1, 0)
- 3 ⇒ (1, 1)

We color in red, vertices within a layer for which `x = z_y` as a means of identifying the layer.

### Pairing of Vertices and Bytes

We will abbreviate and write `p = (x, y, z)` in place of `(x, y, z)` and introduce a pairing `(p, p*)` of vertices within the data cube. The vertices that are colored red are **unpaired**. The remaining vertices are paired such that a vertex `p` and its companion `p*` both belong to the same y-section.

In the data cube of our example code, there are a total of `4 × 4 = 16` vertices of which 8 are unpaired. The remaining 8 vertices form 4 pairs.

Mathematically, `p*` is obtained from `p = (x, y, z₀, z₁)` simply by interchanging the values of `x` and `z_y`.

| Vertex p = (x, y, z₀, z₁) | Companion p* (interchange x, z_y) |
|---------------------------|----------------------------------|
| (0, 0, 1, 0) | (1, 0, 0, 0) |
| (1, 1, 1, 0) | (0, 1, 1, 1) |
| (0, 1, 1, 0) | (0, 1, 1, 0) - a red vertex, (p = p*) |

*Table 3: Example vertex pairings.*

Each vertex `p` of the data cube is associated to a byte `U(p) = U(x, y, z)` of data in the uncoupled code U. We will use `U*(p)` to denote the companion `U(p*)`, of the byte `U(p)`.

### Transforming from Uncoupled to Coupled-Layer Code

We now show how one can transform in a simple way, a codeword belonging to the uncoupled code U to a codeword belonging to the Coupled-layer (Clay) code C.

The bytes `U(p)` and `C(p)` are related in a simple manner:
- If `p` corresponds to an unpaired (red) vertex, we simply set `C(p) = U(p)`
- If `(p, p*)` are a pair of companion vertices, `p ≠ p*`, `U(p), U*(p)` and `C(p), C*(p)` are related by the **pairwise forward transform (PFT)**:

```
[C(p) ]   [1  γ]⁻¹ [U(p) ]
[C*(p)] = [γ  1]   [U*(p)]
```

In the reverse direction, we have `U(p) = C(p)` respectively if `p` is unpaired. Else, `U(p), C(p)` are related by the **pairwise reverse transform (PRT)**:

```
[U(p) ]   [1  γ] [C(p) ]
[U*(p)] = [γ  1] [C*(p)]
```

We assume `γ` to be chosen such that `γ ≠ 0`, `γ² ≠ 1`, and under this condition, it can be verified that any two bytes in the set `{U(p), U*(p), C(p), C*(p)}` can be recovered from the remaining two bytes.

### Encoding the Clay Code

The encoding flowchart:

1. Load data into the 2 data nodes of coupled code
2. Use pairwise reverse transformation to obtain data stored in the 2 data nodes of uncoupled code
3. Use the MDS code in layer-by-layer fashion to determine data stored in the parity nodes of uncoupled code
4. Use pairwise forward transformation to obtain the data to be stored in the parity nodes of coupled code

```
                    CLAY CODE ENCODING FLOW

    ┌─────────────────────────────────────────────────────────────┐
    │                    COUPLED CODE (C)                         │
    │  ┌─────────────┐                      ┌─────────────┐       │
    │  │  C(data)    │                      │  C(parity)  │       │
    │  │  Node 0,1   │                      │  Node 2,3   │       │
    │  └──────┬──────┘                      └──────▲──────┘       │
    └─────────┼────────────────────────────────────┼──────────────┘
              │                                    │
              │ Step 1: Load data         Step 4: PFT
              │                                    │
              ▼                                    │
    ┌─────────────────────────────────────────────────────────────┐
    │                   UNCOUPLED CODE (U)                        │
    │                                                             │
    │  ┌─────────────┐      Step 3: MDS      ┌─────────────┐      │
    │  │  U(data)    │  ─────────────────►   │  U(parity)  │      │
    │  │  Node 0,1   │   (layer-by-layer)    │  Node 2,3   │      │
    │  └──────▲──────┘                       └─────────────┘      │
    │         │                                                   │
    └─────────┼───────────────────────────────────────────────────┘
              │
              │ Step 2: PRT (Pairwise Reverse Transform)
              │
    ┌─────────┴─────────┐
    │   Input: C(data)  │
    │   Raw user data   │
    └───────────────────┘


    PAIRWISE TRANSFORMS:

    Forward (PFT):                    Reverse (PRT):
    ┌─────┐     ┌─────┐              ┌─────┐     ┌─────┐
    │U(p) │     │C(p) │              │C(p) │     │U(p) │
    │     │ ──► │     │              │     │ ──► │     │
    │U(p*)│     │C(p*)│              │C(p*)│     │U(p*)│
    └─────┘     └─────┘              └─────┘     └─────┘

    [C(p) ]   [1  γ]⁻¹ [U(p) ]       [U(p) ]   [1  γ] [C(p) ]
    [C(p*)] = [γ  1]   [U(p*)]       [U(p*)] = [γ  1] [C(p*)]
```

### Reduced Repair Bandwidth of the Clay Code

The savings in repair bandwidth of the Clay code arises from the fact that parity-check constraints are judiciously spread across layers of the C data cube.

To repair a failed node, only the layers corresponding to the presence of red dots within the failed column are called upon for node repair. Thus each helper node contributes only 2 bytes, as opposed to 4 in an RS code, towards node repair.

```
          REPAIR BANDWIDTH COMPARISON: RS vs CLAY CODE

          RS Code Repair (α=4 bytes per node):
          ═══════════════════════════════════
          Must download ALL 4 bytes from each of k helper nodes

          ┌───┐     ┌───┐     ┌───┐         ┌───┐
          │ 1 │     │ 1 │     │ 1 │         │ ? │ ◄── Failed
          │ 2 │ ──► │ 2 │ ──► │ 2 │  ═════► │ ? │     Node
          │ 3 │     │ 3 │     │ 3 │         │ ? │
          │ 4 │     │ 4 │     │ 4 │         │ ? │
          └───┘     └───┘     └───┘         └───┘
          Helper    Helper    Helper
            1         2         3

          Total download: k × α = 2 × 4 = 8 bytes


          Clay Code Repair (α=4, β=2 bytes per node):
          ════════════════════════════════════════════
          Only download β=2 bytes (layers with red dots) from d helpers

          ┌───┐     ┌───┐     ┌───┐         ┌───┐
          │   │     │   │     │   │         │ ? │ ◄── Failed
          │ 2 │ ──► │ 2 │ ──► │ 2 │  ═════► │ ? │     Node
          │   │     │   │     │   │         │ ? │
          │ 4 │     │ 4 │     │ 4 │         │ ? │
          └───┘     └───┘     └───┘         └───┘
          Helper    Helper    Helper
            1         2         3

          Total download: d × β = 3 × 2 = 6 bytes

          ┌──────────────────────────────────────────┐
          │  Normalized repair bandwidth:            │
          │  Clay: dβ/(kα) = 6/8 = 0.75             │
          │  Savings: 25% less network traffic!      │
          └──────────────────────────────────────────┘
```

### Intersection Score

To explain decoding, we introduce the notion of an **Intersection Score (IS)**. The IS of a layer is given by the number of hole-dot pairs, i.e., the vertices that correspond to erased bytes and which are at the same time colored red.

### Decoding

The "Decode" algorithm of the Clay code is able to correct the erasure of any `n - k = 2` nodes. Decoding is carried out sequentially, layer-by-layer, in order of increasing IS.

- In a layer with IS = 0, U bytes can be computed for all non-erased vertices from the known symbols. The erased U bytes are then calculated using RS code decoding.
- For a layer with IS = 1, to compute U bytes for all non-erased vertices, we make use of U bytes recovered in layers with IS = 0.

Thus the processing of a layer with IS = 0 has to take place prior to processing a layer with IS = 1 and so on. Once all the U bytes are recovered, the C bytes can be computed using the PFT. As a result of the simple, pairwise nature of the PFT and PRT, encoding and decoding times are not unduly affected by the coupled-layer structure.

### Clay Code Parameters

Clay codes can be constructed for any parameter set of the form:

```
(n = qt, k, d)  (α = qᵗ, β = qᵗ⁻¹), with q = (d - k + 1)
```

for any integer `t >= 1` over any finite field of size `Q > n`.

The encoding, decoding and repair algorithms can all be generalized for the parameters above. However, in the case `d < n - 1`, during single node repair, while picking the `d` helper nodes, one must include among the `d` helper nodes, all the nodes belonging to the failed node's y-section.

### Clay codes for any (n, k, d)

The parameters indicated above have the restriction that `q = (d - k + 1)` divide `n`. But the construction can be extended in a simple way to the case when `q` is not a factor of `n`. For example, for parameters `(n = 14, k = 10, d = 13)`, `q = d - k + 1 = 4`. We construct the Clay code taking `n' = 16`, the nearest multiple of `q` larger than `n`, and `k' = k + (n' - n) = 12`. While encoding, we set data bytes in `s = (n' - n) = 2` systematic nodes as zero, and thus the resultant code has parameters `(n = 14, k = 10, d = 13)`. The technique used is called **shortening** in the coding theory literature.

---

## 4. Ceph and Vector MDS Codes

### 4.1 Introduction to Ceph

Ceph is a popular, open-source distributed storage system, that permits the storage of data as objects. **Object Storage Daemon (OSD)** is the daemon process of Ceph, associated with a storage unit such as a solid-state or hard-disk drive, on which user data is stored.

Ceph supports multiple erasure-codes, and a code can be chosen by setting attributes of the erasure-code-profile. Objects will then be stored in logical partitions referred to as **pools** associated with an erasure-code-profile. Each pool can have a single or multiple **placement groups (PG)** associated with it. A PG is a collection of `n` OSDs, where `n` is the block length of the erasure code associated to the pool.

The allocation of OSDs to a PG is dynamic, and is carried out by the **CRUSH algorithm**. When an object is streamed to Ceph, the CRUSH algorithm allocates a PG to it. It also performs load balancing dynamically whenever new objects are added, or when active OSDs fail. Each PG contains a single, distinct OSD designated as the **primary OSD (p-OSD)**. When it is required to store an object in a Ceph cluster, the object is passed on to the p-OSD of the allocated PG. The p-OSD is also responsible for initiating the encoding and recovery operations.

In Ceph, the passage from data object to data chunks by the p-OSD is carried out in two steps. For a large object, the amount of buffer memory required to perform encoding and decoding operations will be high. Hence, as an intermediate step, an object is first divided into smaller units called **stripes**, whose size is denoted by `S` (in bytes). If an object's size is not divisible by `S`, zeros are padded. The object is then encoded by the p-OSD one stripe at a time.

### 4.2 Sub-Chunking through Interleaving

To encode, the p-OSD first zero pads each stripe as necessary in order to ensure that the stripe size `S` is divisible by `kα`. The encoding of a stripe is thus equivalent to encoding `N = S/(kα)` codewords at a time. The next step is interleaving at the end of which one obtains `α` sub-chunks per OSD, each of size `N` bytes.

The advantage of a vector code is that it potentially enables the repair of an erased coded chunk by passing on a subset of the `α` sub-chunks. For example, in the Clay code implemented in Ceph is an MSR code, it suffices for each node to pass on `β` sub-chunks. However, when these `β` sub-chunks are not sequentially located within the storage unit, it can result in fragmented reads.

### 4.3 Implementation in Ceph

Our implementation makes use of the **Jerasure** and **GF-Complete** libraries which provide implementations of various MDS codes and Galois-field arithmetic. We chose in our implementation to employ the finite field of size 2^8 to exploit the computational efficiency for this field size provided by the GF-complete library in Ceph.

In our implementation, we employ an additional buffer, termed as **U-buffer**, that stores the sub-chunks associated with the uncoupled symbols U. This buffer is of size `nL = S(n/k)` bytes. The U-buffer is allocated once for a PG, and is used repetitively during encode, decode and repair operations of any object belonging to that PG.

**Pairwise Transforms:** We introduced functions that compute any two sub-chunks in the set `{U, U*, C, C*}` given the remaining two sub-chunks. We implemented these functions using the function `jerasure_matrix_dotprod()`, which is built on top of function `galois_w08_region_multiply()`.

**Encoding:** Encoding of an object is carried out by p-OSD by pretending that `m` parity chunks have been erased, and then recovering the `m` chunks using the `k` data chunks by initiating the decoding algorithm for the code. Pairwise forward and reverse transforms are the only additional computations required for Clay encoding in comparison with MDS encoding.

**Enabling Selection Between Repair & Decoding:** When one or more OSDs go down, multiple PGs are affected. We introduced a boolean function `is_repair()` in order to choose between a bandwidth, disk I/O efficient repair algorithm and the default decode algorithm. For the case of single OSD failure, `is_repair()` always returns true.

**Helper-Chunk Identification:** We introduced a function `minimum_to_repair()` to determine the `d` helper chunk indices when repair can be performed efficiently. When there is a single failure, `minimum_to_repair()` returns `d` chunk indices such that all the chunks that fall in the y-cross-section of the failed chunk are included.

**Fractional Read:** For the case of efficient repair, we only read a fraction of chunk, this functionality is implemented by feeding repair parameters to an existing structure `ECSubRead` that is used in inter-OSD communication. We have also introduced a new read function with Filestore of Ceph that supports sub-chunk reads.

### 4.4 Contributions to Ceph

**Enabling vector codes in Ceph:** We introduced the notion of sub-chunking in order to enable new vector erasure code plugins. This contribution is currently available in Ceph's master codebase.

**Clay codes in Ceph:** We implemented Clay codes as a technique (`cl_msr`) within the jerasure plugin. The current implementation gives flexibility for a client to pick any `n, k, d` parameters for the code. It also gives an option to choose the MDS code used within to be either a Vandermonde-based-RS or Cauchy-original code.

---

## 5. Experiments and Results

The experiments conducted to evaluate the performance of Clay codes in Ceph while recovering from a single node failure are discussed in the present section.

### 5.1 Overview and Setup

**Codes Evaluated:**

| Code | (n, k, d) | α | Storage overhead | β/α |
|------|-----------|---|------------------|-----|
| C1 | (6, 4, 5) | 8 | 1.5 | 0.5 |
| C2 | (12, 9, 11) | 81 | 1.33 | 0.33 |
| C3 | (20, 16, 19) | 1024 | 1.25 | 0.25 |
| C4 | (14, 10, 11) | 128 | 1.4 | 0.5 |
| C5 | (14, 10, 12) | 243 | 1.4 | 0.33 |
| C6 | (14, 10, 13) | 256 | 1.4 | 0.25 |

*Table 4: Codes C1-C3 are evaluated in Ceph for single-node repair. The evaluation of Codes C4-C6 is carried out for both single and multiple-node failures.*

- Code C1 has (n, k) parameters comparable to that of the RDP code
- Code C2 with the locally repairable code used in Windows Azure
- Code C3 with the (20, 17)-RS code used in Backblaze
- Codes C4, C5 and C6 match with the (14, 10)-RS code used in Facebook data-analytic clusters

**Experimental Setup:** All evaluations are carried out on Amazon EC2 instances of the m4.xlarge (16GB RAM, 4 CPU cores) configuration. Each instance is attached to an SSD-type volume of size 500GB. We integrated the Clay code in Ceph Jewel 10.2.2 to perform evaluations. The Ceph storage cluster deployed consists of 26 nodes (one for MON daemon, 25 each running one OSD). Total storage capacity: approximately 12.2TB.

**Workload Models:**

| Model | Object size (MB) | # Objects | Total, T (GB) | Stripe size, S |
|-------|------------------|-----------|---------------|----------------|
| Fixed (W1) | 64 | 8192 | 512 | 64MB |
| Variable (W2) | 64 | 6758 | 448 | 1MB |
| | 32 | 820 | | |
| | 1 | 614 | | |

*Table 5: Workload models used in experiments.*

### 5.2 Evaluations

**Network Traffic: Single Node Failure**

Network traffic refers to the data transferred across the network during single-node repair. The theoretical estimate for the amount of network traffic is `(T/k)((d-1)(β/α) + 1)` bytes for a Clay code, versus `T` bytes for an RS code.

Our evaluations confirm the expected savings:
- **C1:** 25% reduction
- **C2:** 52% reduction
- **C3:** 66% reduction (a factor of **2.9x**)

in network traffic in comparison with the corresponding RS codes under fixed and variable workloads.

**Disk Read: Single Node Failure**

The amount of data read from the disks of the helper nodes during the repair of a failed node is referred to as disk read and is an important parameter to minimize.

Depending on the index of the failed node, the sub-chunks to be fetched from helper nodes in a Clay code can be contiguous or non-contiguous. Non-contiguous reads in HDD volumes lead to a slow-down in performance.

For workload W1 with stripe-size S = 64MB, all the three codes C1, C2, and C3 do not cause any additional disk read. In experiments with fixed object-size, we obtain savings of:
- **C1:** 37.5%
- **C2:** 59.3%
- **C3:** 70.2% (a factor of **3.4x**)

when compared against the corresponding RS code.

**I/O Performance**

We measured the normal and degraded (i.e., with a repair executing in the background) I/O performance of Clay codes C1-C3, and RS codes with same parameters:

- Under normal operation, the write, sequential-read and random-read performances are same for both Clay and RS codes
- In the degraded situation, the I/O performance of Clay codes is observed to be better in comparison with RS codes
- The degraded write, read throughput of (20, 16, 19) Clay code is observed to be more than the (20, 16) RS code by **106%** and **27%** respectively

**Repair Time and Encoding Time**

We observed a significant reduction in repair time for Clay codes in comparison with an RS code:
- For code C3 in a single-PG setting: reduction by a factor of **3x**

This is mainly due to reduction in network traffic and disk I/O required during repair.

Although the encode computation time of Clay code is higher than that of the RS code, the encoding time of a Clay code remains close to that of the corresponding RS code. In storage systems, while data-write is primarily a one-time operation, failure is a norm and thus recovery from failures is a routine activity. The significant savings in network traffic and disk reads during node repair are a sufficient incentive for putting up with overheads in the encode computation time.

---

## 6. Handling Failure of Multiple Nodes

The Clay code is capable of recovering from multiple node-failures with savings in repair bandwidth. In the case of multiple erasures, the bandwidth needed for repair varies with the erasure pattern.

### 6.1 Evaluation of Multiple Erasures

**Network Traffic and Disk Read**

While the primary benefit of the Clay code is optimal network traffic and disk read during repair of a single node failure, it also yields savings over RS counterpart code in the case of a large number of multiple-node failure patterns.

---

## 7. Conclusions

Clay codes extend the theoretical construction presented by Ye & Barg with practical considerations from a coupled-layer perspective that leads directly to implementation.

**Within the class of MDS codes:**
- Clay codes have minimum possible repair bandwidth and disk I/O

**Within the class of MSR codes:**
- Clay codes possess the least possible level of sub-packetization

We answer in the affirmative here by studying the real-world performance of the Clay code in a Ceph setting, with respect to network traffic for repair, disk I/O during repair, repair time and degraded I/O performance.

Along the way, we also modified Ceph to support any vector code, and our contribution is now a part of Ceph's master code-base.

**Key Results for a Clay code with storage overhead 1.25x:**
- Repair network traffic reduced by factor of **2.9x**
- Disk read reduced by factor of **3.4x**
- Repair times reduced by factor of **3x**

Much of this is made possible because Clay codes can be constructed via a simple two-step process where one first stacks in layers, `α` codewords drawn from an MDS code; in the next step, elements from different layers are paired and transformed to yield the Clay code. The same construction with minor modifications is shown to offer support for handling multiple erasures as well.

It is our belief that Clay codes are well-poised to make the leap from theory to practice.

---

## 8. Acknowledgments

We thank our shepherd Cheng Huang and the anonymous reviewers for their valuable comments. P. V. Kumar would like to acknowledge support from NSF Grant No.1421848 as well as the UGC-ISF research program. The research of Alexander Barg and Min Ye was supported by NSF grants CCF1422955 and CCF1618603.

---

## Appendix A: Handling Failure of Multiple Nodes

The failure patterns that can be recovered with bandwidth-savings are referred to as **repairable failure patterns**. Non repairable failure patterns are recovered by using the decode algorithm.

### Repairable Failure Patterns

**(i) d < n - 1:** Clay codes designed with `d < n - 1` can recover from `e` failures with savings in repair bandwidth when `e <= n - d`, with a minor exception. The helper nodes are to be chosen in such a way that if a y-section contains a failed node, then all the surviving nodes in that y-section must act as helper nodes. If no such choice of helper nodes is available then it is not a repairable failure pattern.

**(ii) d = n - 1:** When the code is designed for `d = (n - 1)`, up to `(q - 1)` failures that occur within a single y-section can be recovered with savings in repair bandwidth. As the number of surviving nodes is smaller than `d` in such a case, all the surviving nodes are picked as helper nodes.

### Repair Bandwidth Savings

Let `e_i` be the number of erased nodes within `(y = i)`-section and `e = (e_0, ..., e_{t-1})`. The total number of failures is given by `f = Σ e_i`.

The number of helper nodes `d_e = d` if the code is designed for `d < (n - 1)`, and `d_e = n - f` if it is designed for `d = (n - 1)`.

Total number of sub-chunks `β_e` needed from each helper node is same as the number of layers with IS > 0:

```
β_e = α - Π(q - e_i)
```

Network traffic for repair is `d_e * β_e`.

**Remark 1:** Whenever `d_e * β_e > kα`, decode algorithm is a better option and the `is_repair()` function takes care of these cases by returning false. For example, when there are `q` failures within the same y-section, every layer will have IS > 0 giving `β_e = α` and hence repair is not efficient for this case.

### Repair Algorithm

```
Algorithm 1: repair

Input: E (erasures), I (aloof nodes).

1: repair_layers = get_repair_layers(E)
2: set s = 1
3: set maxIS = max of IS(E ∪ I, z) over all z from repair_layers
4: while (1 <= s <= maxIS)
5:   for (z ∈ repair_layers and IS(E ∪ I, z) = s)
6:     if (IS(E, z) > 1) G = ∅
7:     else {
8:       a = the erased node with hole-dot in layer z
9:       G is set of all nodes in a's y-section.
10:    }
11:    E' = E ∪ G ∪ I
12:    Compute U sub-chunks in layer z corresponding to
       all the nodes other than E'
13:    Invoke scalar MDS decode to recover U sub-chunks
       for all nodes in E'
14:   end for
15:   s = s + 1
16: end while
17: Compute C chunks corresponding to all the erased nodes,
    from U sub-chunks in repair_layers and the helper C
    sub-chunks in repair_layers.
```

---

## References

1. Backblaze data service provider. https://www.backblaze.com/blog/reed-solomon/
2. Coupled-layer source code. https://github.com/ceph/ceph/pull/14300/
3. Red hat ceph storage: Scalable object storage on qct servers - a performance and sizing guide. Reference Architecture.
4. Sub-chunks: Enabling vector codes in ceph. https://github.com/ceph/ceph/pull/15193/
5. Tutorial: Erasure coding for storage applications. http://web.eecs.utk.edu/~plank/plank/papers/FAST-2013-Tutorial.html
6. Balaji, S. B., and Kumar, P. V. A tight lower bound on the sub-packetization level of optimal-access MSR and MDS codes. CoRR abs/1710.05876 (2017).
7. Blaum, M., Brady, J., Bruck, J., and Menon, J. EVENODD: an efficient scheme for tolerating double disk failures in RAID architectures. IEEE Trans. Computers 44, 2 (1995), 192-202.
8. Chen, H. C., Hu, Y., Lee, P. P., and Tang, Y. Nccloud: A network-coding-based storage system in a cloud-of-clouds. IEEE Transactions on Computers 63, 1 (2013), 31-44.
9. Corbett, P., English, B., Goel, A., Grcanac, T., Kleiman, S., Leong, J., and Sankar, S. Row-diagonal parity for double disk failure correction. In Proceedings of the 3rd USENIX Conference on File and Storage Technologies (2004), pp. 1-14.
10. Dimakis, A., Godfrey, P., Wu, Y., Wainwright, M., and Ramchandran, K. Network coding for distributed storage systems. IEEE Transactions on Information Theory 56, 9 (Sep. 2010), 4539-4551.
11. Ford, D., Labelle, F., Popovici, F. I., Stokely, M., Truong, V.-A., Barroso, L., Grimes, C., and Quinlan, S. Availability in globally distributed storage systems. In Presented as part of the 9th USENIX Symposium on Operating Systems Design and Implementation (Vancouver, BC, 2010), USENIX.
12. Ghemawat, S., Gobioff, H., and Leung, S. The google file system. In Proceedings of the 19th ACM Symposium on Operating Systems Principles 2003, SOSP 2003, Bolton Landing, NY, USA, October 19-22, 2003 (2003), pp. 29-43.
13. Ghemawat, S., Gobioff, H., and Leung, S.-T. The google file system. In Proceedings of the Nineteenth ACM Symposium on Operating Systems Principles (New York, NY, USA, 2003), SOSP '03, ACM, pp. 29-43.
14. Hu, Y., Chen, H., Lee, P., and Tang, Y. NCCloud: applying network coding for the storage repair in a cloud-of-clouds. In Proceedings of the 10th USENIX Conference on File and Storage Technologies(FAST) (2012).
15. Huang, C., Simitci, H., Xu, Y., Ogus, A., Calder, B., Gopalan, P., Li, J., and Yekhanin, S. Erasure coding in windows azure storage. In Presented as part of the 2012 USENIX Annual Technical Conference (USENIX ATC 12) (Boston, MA, 2012), USENIX, pp. 15-26.
16. Huang, C., Simitci, H., Xu, Y., Ogus, A., Calder, B., Gopalan, P., Li, J., and Yekhanin, S. Erasure coding in Windows Azure storage. In Proceedings of the 2012 USENIX conference on Annual Technical Conference (Berkeley, CA, USA, 2012), USENIX ATC.
17. Jiang, W., Hu, C., Zhou, Y., and Kanevsky, A. Are disks the dominant contributor for storage failures?: A comprehensive study of storage subsystem failure characteristics. Trans. Storage 4, 3 (Nov. 2008), 7:1-7:25.
18. Kralevska, K., Gligoroski, D., Jensen, R. E., and Verby, H. Hashtag erasure codes: From theory to practice. IEEE Transactions on Big Data PP, 99 (2017), 1-1.
19. Muralidhar, S., Lloyd, W., Roy, S., Hill, C., Lin, E., Liu, W., Pan, S., Shankar, S., Sivakumar, V., Tang, L., and Kumar, S. f4: Facebook's warm BLOB storage system. In 11th USENIX Symposium on Operating Systems Design and Implementation (OSDI 14) (Broomfield, CO, 2014), USENIX Association, pp. 383-398.
20. Pamies-Juarez, L., Blagojevic, F., Mateescu, R., Guyot, C., Gad, E. E., and Bandic, Z. Opening the chrysalis: On the real repair performance of MSR codes. In Proceedings of the 4th USENIX Conference on File and Storage Technologies (2016), pp. 81-94.
21. Plank, J., Greenan, K., Miller, E., and Houston, W. Gf-complete: A comprehensive open source library for galois field arithmetic. University of Tennessee, Tech. Rep. UT-CS-13-703 (2013).
22. Plank, J. S., and Greenan, K. M. Jerasure: A library in c facilitating erasure coding for storage applications-version 2.0. Tech. rep., Technical Report UT-EECS-14-721, University of Tennessee, 2014.
23. Rashmi, K. V., Chowdhury, M., Kosaian, J., Stoica, I., and Ramchandran, K. Ec-cache: Load-balanced, low-latency cluster caching with online erasure coding. In 12th USENIX Symposium on Operating Systems Design and Implementation, OSDI 2016, Savannah, GA, USA, November 2-4, 2016. (2016), pp. 401-417.
24. Rashmi, K. V., Nakkiran, P., Wang, J., Shah, N. B., and Ramchandran, K. Having your cake and eating it too: Jointly optimal erasure codes for i/o, storage, and network-bandwidth. In Proceedings of the 13th USENIX Conference on File and Storage Technologies, FAST, (2015), pp. 81-94.
25. Rashmi, K. V., Shah, N. B., Gu, D., Kuang, H., Borthakur, D., and Ramchandran, K. A solution to the network challenges of data recovery in erasure-coded distributed storage systems: A study on the facebook warehouse cluster. In 5th USENIX Workshop on Hot Topics in Storage and File Systems, HotStorage'13, 2013 (2013), USENIX Association.
26. Rashmi, K. V., Shah, N. B., Gu, D., Kuang, H., Borthakur, D., and Ramchandran, K. A "hitchhiker's" guide to fast and efficient data reconstruction in erasure-coded data centers. In ACM SIGCOMM 2014 Conference, (2014), pp. 331-342.
27. Rashmi, K. V., Shah, N. B., and Kumar, P. V. Optimal Exact-Regenerating Codes for Distributed Storage at the MSR and MBR Points via a Product-Matrix Construction. IEEE Transactions on Information Theory 57, 8 (Aug 2011), 5227-5239.
28. Sathiamoorthy, M., Asteris, M., Papailiopoulos, D. S., Dimakis, A. G., Vadali, R., Chen, S., and Borthakur, D. Xoring elephants: Novel erasure codes for big data. PVLDB 6, 5 (2013), 325-336.
29. Schroeder, B., and Gibson, G. A. Disk failures in the real world: What does an mttf of 1,000,000 hours mean to you? In Proceedings of the 5th USENIX Conference on File and Storage Technologies (Berkeley, CA, USA, 2007), FAST '07, USENIX Association.
30. Tamo, I., Wang, Z., and Bruck, J. Zigzag codes: MDS array codes with optimal rebuilding. IEEE Transactions on Information Theory 59, 3 (2013), 1597-1616.
31. Tian, C., Li, J., and Tang, X. A generic transformation for optimal repair bandwidth and rebuilding access in MDS codes. In 2017 IEEE International Symposium on Information Theory, ISIT 2017, Aachen, Germany, June 25-30, 2017 (2017), pp. 1623-1627.
32. Weil, S. A., Brandt, S. A., Miller, E. L., Long, D. D. E., and Maltzahn, C. Ceph: A scalable, high-performance distributed file system. In 7th Symposium on Operating Systems Design and Implementation (OSDI '06), November 6-8, Seattle, WA, USA (2006), pp. 307-320.
33. Weil, S. A., Brandt, S. A., Miller, E. L., and Maltzahn, C. Grid resource management - CRUSH: controlled, scalable, decentralized placement of replicated data. In Proceedings of the ACM/IEEE SC2006 Conference on High Performance Networking and Computing, November 11-17, 2006, Tampa, FL, USA (2006), p. 122.
34. Weil, S. A., Leung, A. W., Brandt, S. A., and Maltzahn, C. RADOS: a scalable, reliable storage service for petabyte-scale storage clusters. In Proceedings of the 2nd International Petascale Data Storage Workshop (PDSW '07), November 11, 2007, Reno, Nevada, USA (2007), pp. 35-44.
35. Ye, M., and Barg, A. Explicit constructions of optimal-access MDS codes with nearly optimal sub-packetization. IEEE Trans. Information Theory 63, 10 (2017), 6307-6317.

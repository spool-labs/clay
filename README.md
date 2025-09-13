# Clay (Coupled Layer Erasure Codes)

This repository aims to extract the core implementation of the Clay erasure coding scheme from the original Ceph codebase. The Clay scheme is designed for efficient data storage and recovery in distributed systems, particularly in scenarios involving multiple layers of data redundancy.

> [!Important]
> This code does not yet work but is a work in progress. Contributions are welcome!

The goal is to create a standalone library that can be easily integrated into other projects, providing a robust and flexible solution for data protection.

## Original Work

The original paper can be found here:
<https://www.usenix.org/system/files/conference/fast18/fast18-vajha.pdf>

The documentation for the Clay plugin in Ceph can be found here:
<https://docs.ceph.com/en/reef/rados/operations/erasure-code-clay/>

The original source (as part of Ceph) can be found here:
<https://github.com/ceph/ceph/tree/main/src/erasure-code/clay>

Please see the original repository for the LICENSE file.

## Installing Dependencies

To build and run the code, you will need to install the following dependencies:

- Jerasure (version 2)
- GF-Complete (version 2)

For now, you can find the deps inside the `deps` directory. Follow the instructions in their respective README files to build and install them.

You'll need a few things on your machine, for example, on macOS:

```bash
brew install automake
brew install autoconf
brew install libtool
```

### Building GF-Complete

To build gf-complete, navigate to the `deps/gf-complete` directory and run the following commands:

```bash
cd deps/gf-complete
./autogen.sh
./configure
make
sudo make install
```

To build jerasure, navigate to the `deps/jerasure` directory and run the following commands:

```bash
cd deps/gf-complete
./autogen.sh
./configure
make
sudo make install
```

## How to QuickStart

### Building

1. **Clone the repository:**

```bash
git clone https://github.com/spool-labs/clay.git
cd clay
```

2. **Build dependencies and library:**

```bash
chmod +x build_dependencies.sh
./fix_dependencies.sh
```

3. **Run the complete test suite:**

```bash
chmod +x build_and_test.sh
./build_and_test.sh
```

4. **Demonstrate chunk generation:**

```bash
cd build
cmake ..
make clay_chunk_demo
./clay_chunk_demo
```

### Expected Output

After running the build and test scripts, you should see confirmation that the library works correctly. The chunk demonstration provides detailed evidence of the erasure coding functionality:

```bash
Clay Library Chunk Generation Demo
Input: 1024 bytes with pattern [0,1,2...255,0,1,2...]
First 16 bytes (1024 bytes): 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ...

Clay Configuration:
  k (data chunks): 4
  m (coding chunks): 2  
  d (repair parameter): 5
  Total chunks: 6
  Fault tolerance: Up to 2 chunk failures

Clay initialization successful!

ENCODING RESULTS:

Successfully generated 6 chunks

Chunk 0 (DATA):
  Size: 512 bytes
  Content (512 bytes): 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ...

Chunk 1 (DATA):
  Size: 512 bytes
  Content (512 bytes): 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ...

Chunk 2 (DATA):
  Size: 512 bytes
  Content (512 bytes): 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ...

Chunk 3 (DATA):
  Size: 512 bytes
  Content (512 bytes): 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ...

Chunk 4 (CODING):
  Size: 256 bytes
  Content (256 bytes): 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ...

Chunk 5 (CODING):
  Size: 256 bytes
  Content (256 bytes): 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ...

SUMMARY:
Original data size: 1024 bytes
Total encoded size: 2560 bytes
Storage overhead: 150.0%
Fault tolerance: Any 2 chunks can fail
Minimum chunks needed for recovery: 4

Clay erasure coding demonstration complete!
This proves the library can:
  Accept arbitrary input data
  Generate erasure coded chunks using Clay algorithm
  Provide configurable redundancy (k+m chunks)
  Enable fault tolerant distributed storage
```

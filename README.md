# Clay (Coupled Layer Erasure Codes)

This repository aims to extract the core implementation of the Clay erasure coding scheme from the original Ceph codebase. The Clay scheme is designed for efficient data storage and recovery in distributed systems, particularly in scenarios involving multiple layers of data redundancy.

The goal is to create a standalone library that can be easily integrated into other projects, providing a robust and flexible solution for data protection.

> [!Important] 
> Work in progress. Contributions are welcome! Currently runs a test, not actually a library yet.

## Original Work

The original paper can be found here:
https://www.usenix.org/system/files/conference/fast18/fast18-vajha.pdf

The documentation for the Clay plugin in Ceph can be found here:
https://docs.ceph.com/en/reef/rados/operations/erasure-code-clay/

The original source (as part of Ceph) can be found here:
https://github.com/ceph/ceph/tree/main/src/erasure-code/clay

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

### Building Dependencies

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

## Building the Project

To build the Clay project, navigate to the root directory of the repository and run the following commands:

```bash
make
```

## Example Output

```
Input size set to 1048576 bytes (1MB)
Computed chunk size: 131072 bytes
Padded size for 8 data chunks: 1048576 bytes
Padded size alignment verified
Generating random input data...
Created input data: 1048576 bytes of random data, padded to 1048576 bytes
Input bufferlist created with length: 1048576 bytes
Encoding data into 12 chunks (k=8, m=4)...
Encoding completed. Generated 12 chunks:
  Chunk D0: 88 cb 9d b8 9c 6b e1 5c 09 9e ce 71 1b de b6 2b 60 c4 74 9c ...
  Chunk D1: b8 9a b5 20 02 96 66 1d 0a 72 b5 df ac 53 1f 63 41 b5 0a 72 ...
  Chunk D2: 2f 26 21 32 7e 4d a7 f4 b2 54 19 ee 29 02 b9 04 94 18 b1 9f ...
  Chunk D3: f9 a1 6b 94 9c e4 9e 67 83 d5 10 e6 24 28 f3 96 3d 21 91 9c ...
  Chunk D4: 60 60 8c c8 96 38 37 11 a5 dc ea df 79 2a 6c 4d 1b 33 cd 7a ...
  Chunk D5: 3a 84 72 89 94 1e 05 72 b2 91 90 2e af f5 fd 06 b4 02 d8 92 ...
  Chunk D6: a4 59 e5 a3 b9 b3 30 c3 7e 0c e3 59 ac 4b cd 59 51 78 f8 9b ...
  Chunk D7: b1 04 07 e3 bb 79 02 74 1b 49 86 14 3f 02 ba 25 c9 a6 54 8f ...
  Chunk C0: 99 23 1d 57 41 78 5b e2 94 93 75 c8 3d 23 63 b7 52 6b 57 a8 ...
  Chunk C1: 24 6c e5 22 df 76 66 b4 ec 3c 68 f8 78 b4 20 7c 4c 06 83 3e ...
  Chunk C2: b1 b6 4e 93 1e 52 f5 77 47 e1 cf 73 75 ea 5d 43 cd bd 5a ee ...
  Chunk C3: 4f b5 4d df ee 60 3b 59 df 91 95 87 5a 3d 7a d6 9e e3 d5 7d ...

Simulating loss of 4 chunks...
  Chunk 4 erased.
  Chunk 1 erased.
  Chunk 0 erased.
  Chunk 6 erased.

Total available chunks after erasure: 8
Chunk contents at stage: After Erasing Chunks {0 1 4 6 }
  Chunk D0: [ERASED]
  Chunk D1: [ERASED]
  Chunk D2: 2f 26 21 32 7e 4d a7 f4 b2 54 19 ee 29 02 b9 04 94 18 b1 9f ...
  Chunk D3: f9 a1 6b 94 9c e4 9e 67 83 d5 10 e6 24 28 f3 96 3d 21 91 9c ...
  Chunk D4: [ERASED]
  Chunk D5: 3a 84 72 89 94 1e 05 72 b2 91 90 2e af f5 fd 06 b4 02 d8 92 ...
  Chunk D6: [ERASED]
  Chunk D7: b1 04 07 e3 bb 79 02 74 1b 49 86 14 3f 02 ba 25 c9 a6 54 8f ...
  Chunk C0: 99 23 1d 57 41 78 5b e2 94 93 75 c8 3d 23 63 b7 52 6b 57 a8 ...
  Chunk C1: 24 6c e5 22 df 76 66 b4 ec 3c 68 f8 78 b4 20 7c 4c 06 83 3e ...
  Chunk C2: b1 b6 4e 93 1e 52 f5 77 47 e1 cf 73 75 ea 5d 43 cd bd 5a ee ...
  Chunk C3: 4f b5 4d df ee 60 3b 59 df 91 95 87 5a 3d 7a d6 9e e3 d5 7d ...

Determining minimum chunks to repair chunks {0 1 4 6 } (d=11)...
Available chunks for repair: 2 3 5 7 8 9 10 11
Number of sub-chunks needed for repair: 48
Estimated repair bandwidth: 98304 bytes total (8936 bytes per chunk from d=11 chunks)
Repairing chunks {0 1 4 6 }...
Chunk 0 repaired successfully. Repaired chunk size: 131072 bytes
Chunk 1 repaired successfully. Repaired chunk size: 131072 bytes
Chunk 4 repaired successfully. Repaired chunk size: 131072 bytes
Chunk 6 repaired successfully. Repaired chunk size: 131072 bytes
Verifying repaired chunk 0 against original chunk 0...
SUCCESS: Repaired chunk 0 matches original chunk 0
Verifying repaired chunk 1 against original chunk 1...
SUCCESS: Repaired chunk 1 matches original chunk 1
Verifying repaired chunk 4 against original chunk 4...
SUCCESS: Repaired chunk 4 matches original chunk 4
Verifying repaired chunk 6 against original chunk 6...
SUCCESS: Repaired chunk 6 matches original chunk 6
Repaired chunk 0 added back.
Repaired chunk 1 added back.
Repaired chunk 4 added back.
Repaired chunk 6 added back.
Total available chunks after repair: 12
Chunk contents at stage: After Repairing Chunks {0 1 4 6 }
  Chunk D0: 88 cb 9d b8 9c 6b e1 5c 09 9e ce 71 1b de b6 2b 60 c4 74 9c ...
  Chunk D1: b8 9a b5 20 02 96 66 1d 0a 72 b5 df ac 53 1f 63 41 b5 0a 72 ...
  Chunk D2: 2f 26 21 32 7e 4d a7 f4 b2 54 19 ee 29 02 b9 04 94 18 b1 9f ...
  Chunk D3: f9 a1 6b 94 9c e4 9e 67 83 d5 10 e6 24 28 f3 96 3d 21 91 9c ...
  Chunk D4: 60 60 8c c8 96 38 37 11 a5 dc ea df 79 2a 6c 4d 1b 33 cd 7a ...
  Chunk D5: 3a 84 72 89 94 1e 05 72 b2 91 90 2e af f5 fd 06 b4 02 d8 92 ...
  Chunk D6: a4 59 e5 a3 b9 b3 30 c3 7e 0c e3 59 ac 4b cd 59 51 78 f8 9b ...
  Chunk D7: b1 04 07 e3 bb 79 02 74 1b 49 86 14 3f 02 ba 25 c9 a6 54 8f ...
  Chunk C0: 99 23 1d 57 41 78 5b e2 94 93 75 c8 3d 23 63 b7 52 6b 57 a8 ...
  Chunk C1: 24 6c e5 22 df 76 66 b4 ec 3c 68 f8 78 b4 20 7c 4c 06 83 3e ...
  Chunk C2: b1 b6 4e 93 1e 52 f5 77 47 e1 cf 73 75 ea 5d 43 cd bd 5a ee ...
  Chunk C3: 4f b5 4d df ee 60 3b 59 df 91 95 87 5a 3d 7a d6 9e e3 d5 7d ...

Reconstructing original data from data chunks...
Data chunks decoded: 8 chunks
Concatenating data chunks for reconstruction...
Reconstruction completed. Reconstructed data size: 1048576 bytes
Verifying reconstructed data against original input...
SUCCESS: Reconstructed data matches original input
CLAY erasure coding test with random chunk loss completed successfully
```

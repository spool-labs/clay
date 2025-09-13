# Clay (Coupled Layer Erasure Codes)

This repository aims to extract the core implementation of the Clay erasure coding scheme from the original Ceph codebase. The Clay scheme is designed for efficient data storage and recovery in distributed systems, particularly in scenarios involving multiple layers of data redundancy.

The goal is to create a standalone library that can be easily integrated into other projects, providing a robust and flexible solution for data protection.

> [!Important]
> Work in progress. Contributions are welcome!

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

## Using the Executable

After building, you should have an executable named `clay_ec` in the root directory. It lets you encode and decode files using the Clay erasure coding scheme.

```
./clay_ec decode chunked_files/ output_file
Configuring CLAY with k=8, m=4, d=11, scalar_mds=jerasure, technique=reed_sol_van
parse (q,t,nu)=(4,3,0)
technique=reed_sol_van
CLAY initialized successfully
Read metadata: input_size=175496
Chunk contents at stage: Available Chunks
  Chunk D0: [ERASED]
  Chunk D1: 1f 00 0a eb 00 01 80 9a 48 01 40 f9 0b 01 40 f9 6b 00 00 b4 ...
  Chunk D2: fd 7b 4b a9 f4 4f 4a a9 f6 57 49 a9 f8 5f 48 a9 fa 67 47 a9 ...
  Chunk D3: e1 03 14 aa e2 03 40 b9 03 00 80 52 98 c5 ff 97 e8 0f 40 f9 ...
  Chunk D4: [ERASED]
  Chunk D5: 64 20 69 6e 70 75 74 20 66 69 6c 65 3a 20 00 20 28 00 20 62 ...
  Chunk D6: 39 61 6c 6c 6f 63 61 74 6f 72 49 63 45 45 45 45 53 39 5f 00 ...
  Chunk D7: 68 61 72 5f 74 72 61 69 74 73 49 63 45 45 4e 53 5f 39 61 6c ...
  Chunk C0: [ERASED]
  Chunk C1: 83 2b 5a f9 e8 4d b1 5e 0c 10 42 9e b1 57 e1 57 99 ac a8 85 ...
  Chunk C2: [ERASED]
  Chunk C3: 41 3e 20 ed d6 84 c4 7c a5 55 9e c8 fa 1a 1f b4 f7 f0 50 92 ...

Reconstructed data written to: output_file (175496 bytes)
```

## Quick Setup

```bash
# Clone the repository
git clone https://github.com/spool-labs/clay.git
cd clay

# Build dependencies (Jerasure and GF-Complete are bundled)
# No additional setup needed!
```

## Build and Test

### One-command build and test

```bash
./build_and_test.sh
```

**Expected output:**

```bash
Test Summary
Tests run: 23
Tests passed: 23
Tests failed: 0

ALL TESTS PASSED! Clay is working correctly.
[SUCCESS] Clay API tests passed
```

## Run Examples

```bash
./run_examples.sh
```

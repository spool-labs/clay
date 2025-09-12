# Clay (Coupled Layer Erasure Codes)

This repository aims to extract the core implementation of the Clay erasure coding scheme from the original Ceph codebase. The Clay scheme is designed for efficient data storage and recovery in distributed systems, particularly in scenarios involving multiple layers of data redundancy.

> [!Important] 
> Work in progress. Contributions are welcome!

The goal is to create a standalone library that can be easily integrated into other projects, providing a robust and flexible solution for data protection.

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

# Clay (Coupled Layer Erasure Codes)

This repository aims to extract the core implementation of the Clay erasure coding scheme from the original Ceph codebase. The Clay scheme is designed for efficient data storage and recovery in distributed systems, particularly in scenarios involving multiple layers of data redundancy.

> [!Important] 
> This code does not yet work but is a work in progress. Contributions are welcome!

The goal is to create a standalone library that can be easily integrated into other projects, providing a robust and flexible solution for data protection.

## Original Work

The original paper can be found here:
https://www.usenix.org/system/files/conference/fast18/fast18-vajha.pdf

The documentation for the Clay plugin in Ceph can be found here:
https://docs.ceph.com/en/reef/rados/operations/erasure-code-clay/

The original source (as part of Ceph) can be found here:
https://github.com/ceph/ceph/tree/main/src/erasure-code/clay

Please see the original repository for the LICENSE file.


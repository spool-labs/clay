# Clay Erasure Code Library - Usage Guide

## Quick Start

### Building the Library

1. **Build dependencies first:**
   ```bash
   ./build_deps.sh
   ```

2. **Build with CMake:**
   ```bash
   mkdir build && cd build
   cmake ..
   make -j$(nproc)
   ```

3. **Run the test:**
   ```bash
   export LD_LIBRARY_PATH=./deps/jerasure/install/lib:./deps/gf-complete/install/lib:$LD_LIBRARY_PATH
   ./clay_ec
   ```

### Basic Usage

```cpp
#include "ErasureCodeClay.h"
#include "BufferList.h"

// Initialize Clay erasure code
ErasureCodeClay clay;
ErasureCodeProfile profile;
profile["k"] = "4";  // 4 data chunks
profile["m"] = "2";  // 2 parity chunks  
profile["d"] = "5";  // 5 chunks needed for repair
profile["plugin"] = "clay";

int result = clay.init(profile, std::cerr);

// Encode data
std::set<int> want_to_encode;
for (int i = 0; i < clay.k + clay.m; ++i) {
    want_to_encode.insert(i);
}

BufferList input;
input.append("your data here", data_length);

std::map<int, BufferList> encoded_chunks;
clay.encode(want_to_encode, input, &encoded_chunks);

// Decode data (after chunk loss)
std::set<int> want_to_read;
for (int i = 0; i < clay.k; ++i) {
    want_to_read.insert(i);
}

std::map<int, BufferList> decoded_chunks;
size_t chunk_size = available_chunks.begin()->second.length();
clay.decode(want_to_read, available_chunks, &decoded_chunks, chunk_size);
```

### Library Integration

**Using pkg-config:**
```bash
# After installation
pkg-config --cflags --libs clay
```

**Manual linking:**
```bash
g++ -std=c++17 your_app.cpp -lclay_ec -lJerasure -lgf_complete
```

### Parameters

- **k**: Number of data chunks (default: 4)
- **m**: Number of parity chunks (default: 2)  
- **d**: Number of chunks needed for repair (default: 5)
- **q**: Sub-packetization level (calculated automatically)
- **t**: Number of local groups (calculated automatically)

### Performance Notes

- Clay codes are optimized for repair bandwidth efficiency
- Best performance with k=4-8, m=2-4
- Repair operations are more efficient than traditional Reed-Solomon
- Sub-chunking provides fine-grained repair capabilities

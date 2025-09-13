#include <clay/ErasureCode.h>
#include <sstream>
#include <algorithm>
#include <cassert>

ErasureCode::ErasureCode() {
}

ErasureCode::~ErasureCode() {
}

int ErasureCode::init(ErasureCodeProfile &profile, std::ostream * /*ss*/) {
    profile_ = profile;
    return 0;
}

int ErasureCode::sanity_check_k(int k, std::ostream *ss) {
    if (k <= 0) {
        if (ss) {
            *ss << "k=" << k << " must be a positive integer" << std::endl;
        }
        return -1;
    }
    return 0;
}

int ErasureCode::minimum_to_decode(const std::set<int> & /*want_to_read*/,
                                   const std::set<int> &available,
                                   std::map<int, std::vector<std::pair<int, int>>> *minimum) {
    // we need at least k chunks to decode
    if (available.size() < get_data_chunk_count()) {
        return -1;  // insufficient chunks
    }

    minimum->clear();
    auto it = available.begin();
    for (unsigned int i = 0; i < get_data_chunk_count() && it != available.end(); ++i, ++it) {
        (*minimum)[*it] = std::vector<std::pair<int, int>>{std::make_pair(0, 1)};
    }

    return 0;
}

int ErasureCode::encode_prepare(const BufferList &raw, std::map<int, BufferList> &encoded) const {
    unsigned int k = get_data_chunk_count();
    unsigned int m = get_coding_chunk_count();
    unsigned alignment = get_alignment();

    unsigned tail = raw.length() % alignment;
    unsigned padded_length = raw.length() + (tail ? (alignment - tail) : 0);

    while (padded_length % k != 0) {
        padded_length++;
    }
    
    unsigned chunk_size = padded_length / k;

    for (unsigned int i = 0; i < k; i++) {
        BufferList chunk(chunk_size, alignment);
        chunk.zero();
        size_t offset = i * chunk_size;
        size_t copy_size = std::min(static_cast<size_t>(chunk_size), 
                                   static_cast<size_t>(std::max(0, static_cast<int>(raw.length()) - static_cast<int>(offset))));
        
        if (copy_size > 0) {
            chunk.append(raw.c_str() + offset, copy_size);
        }
        
        encoded[i] = std::move(chunk);
    }

    for (unsigned int i = 0; i < m; i++) {
        BufferList chunk(chunk_size, alignment);
        chunk.zero();
        encoded[k + i] = std::move(chunk);
    }
    
    return 0;
}

int ErasureCode::encode(const std::set<int> &want_to_encode,
                        const BufferList &in,
                        std::map<int, BufferList> *encoded) {
    int ret = encode_prepare(in, *encoded);
    if (ret != 0) {
        return ret;
    }

    return encode_chunks(want_to_encode, encoded);
}

int ErasureCode::decode(const std::set<int> &want_to_read,
                        const std::map<int, BufferList> &chunks,
                        std::map<int, BufferList> *decoded, int /*chunk_size*/) {
    return _decode(want_to_read, chunks, decoded);
}

int ErasureCode::_decode(const std::set<int> &want_to_read,
                         const std::map<int, BufferList> &chunks,
                         std::map<int, BufferList> *decoded) {
    for (auto chunk_id : want_to_read) {
        if (chunks.find(chunk_id) != chunks.end()) {
            (*decoded)[chunk_id] = chunks.at(chunk_id);
        } else {
            auto first_chunk = chunks.begin();
            if (first_chunk != chunks.end()) {
                BufferList empty_chunk(first_chunk->second.length(), get_alignment());
                empty_chunk.zero();
                (*decoded)[chunk_id] = std::move(empty_chunk);
            }
        }
    }

    std::set<int> missing_chunks;
    for (auto chunk_id : want_to_read) {
        if (chunks.find(chunk_id) == chunks.end()) {
            missing_chunks.insert(chunk_id);
        }
    }
    
    if (!missing_chunks.empty()) {
        return decode_chunks(want_to_read, chunks, decoded);
    }
    
    return 0;
}

static int to_int(const std::string& key, ErasureCodeProfile& profile, int* value, const std::string& default_val, std::ostream* ss) {
    auto iter = profile.find(key);
    if (iter != profile.end()) {
        try {
            *value = std::stoi(iter->second);
            return 0;
        } catch (const std::exception& e) {
            if (ss) *ss << "Error parsing " << key << ": " << e.what() << std::endl;
            return -1;
        }
    } else {
        try {
            *value = std::stoi(default_val);
            return 0;
        } catch (const std::exception& e) {
            if (ss) *ss << "Error parsing default " << key << ": " << e.what() << std::endl;
            return -1;
        }
    }
}

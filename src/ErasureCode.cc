#include "ErasureCode.h"
#include "BufferList.h"
#include <algorithm>
#include <sstream>

#define EINVAL 22

int ErasureCode::init(ErasureCodeProfile &profile, std::ostream *ss) {
    int err = parse(profile, ss);
    if (err) {
        return err;
    }
    _profile = profile;
    return 0;
}

int ErasureCode::sanity_check_k(int k, std::ostream *ss) {
    if (k < 2) {
        *ss << "k=" << k << " must be >= 2" << std::endl;
        return -EINVAL;
    }
    return 0;
}

int ErasureCode::chunk_index(unsigned int i) const {
    return chunk_mapping.size() > i ? chunk_mapping[i] : i;
}

int ErasureCode::_minimum_to_decode(const std::set<int> &want_to_read,
                                   const std::set<int> &available_chunks,
                                   std::set<int> *minimum) {
    if (std::includes(available_chunks.begin(), available_chunks.end(),
                      want_to_read.begin(), want_to_read.end())) {
        *minimum = want_to_read;
    } else {
        unsigned int k = get_data_chunk_count();
        if (available_chunks.size() < k) {
            return -EIO;
        }
        auto i = available_chunks.begin();
        for (unsigned j = 0; j < k; ++j, ++i) {
            minimum->insert(*i);
        }
    }
    return 0;
}

int ErasureCode::minimum_to_decode(const std::set<int> &want_to_read,
                                  const std::set<int> &available,
                                  std::map<int, std::vector<std::pair<int, int>>> *minimum) {
    std::set<int> minimum_shard_ids;
    int r = _minimum_to_decode(want_to_read, available, &minimum_shard_ids);
    if (r != 0) {
        return r;
    }
    std::vector<std::pair<int, int>> default_subchunks;
    default_subchunks.emplace_back(0, get_sub_chunk_count());
    for (auto id : minimum_shard_ids) {
        minimum->emplace(id, default_subchunks);
    }
    return 0;
}

int ErasureCode::minimum_to_decode_with_cost(const std::set<int> &want_to_read,
                                             const std::map<int, int> &available,
                                             std::set<int> *minimum) {
    std::set<int> available_chunks;
    for (const auto &i : available) {
        available_chunks.insert(i.first);
    }
    return _minimum_to_decode(want_to_read, available_chunks, minimum);
}

int ErasureCode::encode_prepare(const BufferList &raw,
                                std::map<int, BufferList> &encoded) const {
    unsigned int k = get_data_chunk_count();
    unsigned int m = get_chunk_count() - k;
    unsigned blocksize = get_chunk_size(raw.length());
    unsigned padded_chunks = k - raw.length() / blocksize;
    BufferList prepared = raw;

    for (unsigned int i = 0; i < k - padded_chunks; i++) {
        BufferList &chunk = encoded[chunk_index(i)];
        chunk.substr_of(prepared, i * blocksize, blocksize);
        chunk.rebuild_aligned_size_and_memory(blocksize, SIMD_ALIGN);
    }
    if (padded_chunks) {
        unsigned remainder = raw.length() - (k - padded_chunks) * blocksize;
        BufferList buf(remainder, SIMD_ALIGN);
        prepared.substr_of(prepared, (k - padded_chunks) * blocksize, remainder);
        buf.zero(remainder, blocksize - remainder);
        encoded[chunk_index(k - padded_chunks)] = std::move(buf);

        for (unsigned int i = k - padded_chunks + 1; i < k; i++) {
            BufferList buf(blocksize, SIMD_ALIGN);
            buf.zero();
            encoded[chunk_index(i)] = std::move(buf);
        }
    }
    for (unsigned int i = k; i < k + m; i++) {
        BufferList &chunk = encoded[chunk_index(i)];
        chunk = BufferList(blocksize, SIMD_ALIGN);
    }
    return 0;
}

int ErasureCode::encode(const std::set<int> &want_to_encode,
                        const BufferList &in,
                        std::map<int, BufferList> *encoded) {
    unsigned int k = get_data_chunk_count();
    unsigned int m = get_chunk_count() - k;
    int err = encode_prepare(in, *encoded);
    if (err) {
        return err;
    }
    encode_chunks(want_to_encode, encoded);
    for (unsigned int i = 0; i < k + m; i++) {
        if (want_to_encode.count(i) == 0) {
            encoded->erase(i);
        }
    }
    return 0;
}

int ErasureCode::encode_chunks(const std::set<int> &want_to_encode,
                               std::map<int, BufferList> *encoded) {
    throw std::runtime_error("ErasureCode::encode_chunks not implemented");
}

int ErasureCode::_decode(const std::set<int> &want_to_read,
                        const std::map<int, BufferList> &chunks,
                        std::map<int, BufferList> *decoded) {
    std::vector<int> have;
    have.reserve(chunks.size());
    for (const auto &i : chunks) {
        have.push_back(i.first);
    }
    if (std::includes(have.begin(), have.end(), want_to_read.begin(), want_to_read.end())) {
        for (auto i : want_to_read) {
            (*decoded)[i] = chunks.find(i)->second;
        }
        return 0;
    }
    unsigned int k = get_data_chunk_count();
    unsigned int m = get_chunk_count() - k;
    unsigned blocksize = chunks.begin()->second.length();
    for (unsigned int i = 0; i < k + m; i++) {
        if (chunks.find(i) == chunks.end()) {
            BufferList tmp(blocksize, SIMD_ALIGN);
            tmp.zero();
            (*decoded)[i] = std::move(tmp);
        } else {
            (*decoded)[i] = chunks.find(i)->second;
            (*decoded)[i].rebuild_aligned(SIMD_ALIGN);
        }
    }
    return decode_chunks(want_to_read, chunks, decoded);
}

int ErasureCode::decode(const std::set<int> &want_to_read,
                        const std::map<int, BufferList> &chunks,
                        std::map<int, BufferList> *decoded, int chunk_size) {
    return _decode(want_to_read, chunks, decoded);
}

int ErasureCode::decode_chunks(const std::set<int> &want_to_read,
                               const std::map<int, BufferList> &chunks,
                               std::map<int, BufferList> *decoded) {
    throw std::runtime_error("ErasureCode::decode_chunks not implemented");
}

int ErasureCode::parse(ErasureCodeProfile &profile, std::ostream *ss) {
    return to_mapping(profile, ss);
}

int ErasureCode::to_mapping(ErasureCodeProfile &profile, std::ostream *ss) {
    if (profile.find("mapping") != profile.end()) {
        std::string mapping = profile.find("mapping")->second;
        int position = 0;
        std::vector<int> coding_chunk_mapping;
        for (auto it = mapping.begin(); it != mapping.end(); ++it) {
            if (*it == 'D') {
                chunk_mapping.push_back(position);
            } else {
                coding_chunk_mapping.push_back(position);
            }
            position++;
        }
        chunk_mapping.insert(chunk_mapping.end(),
                             coding_chunk_mapping.begin(),
                             coding_chunk_mapping.end());
    }
    return 0;
}

int ErasureCode::to_int(const std::string &name,
                        ErasureCodeProfile &profile,
                        int *value,
                        const std::string &default_value,
                        std::ostream *ss) {
    if (profile.find(name) == profile.end() || profile.find(name)->second.empty()) {
        profile[name] = default_value;
    }
    std::string p = profile[name];
    try {
        *value = std::stoi(p);
    } catch (const std::exception &e) {
        *ss << "could not convert " << name << "=" << p
            << " to int because " << e.what()
            << ", set to default " << default_value << std::endl;
        *value = std::stoi(default_value);
        return -EINVAL;
    }
    return 0;
}

int ErasureCode::to_bool(const std::string &name,
                         ErasureCodeProfile &profile,
                         bool *value,
                         const std::string &default_value,
                         std::ostream *ss) {
    if (profile.find(name) == profile.end() || profile.find(name)->second.empty()) {
        profile[name] = default_value;
    }
    const std::string p = profile[name];
    *value = (p == "yes" || p == "true");
    return 0;
}

int ErasureCode::to_string(const std::string &name,
                           ErasureCodeProfile &profile,
                           std::string *value,
                           const std::string &default_value,
                           std::ostream *ss) {
    if (profile.find(name) == profile.end() || profile.find(name)->second.empty()) {
        profile[name] = default_value;
    }
    *value = profile[name];
    return 0;
}

int ErasureCode::decode_concat(const std::map<int, BufferList> &chunks,
                               BufferList *decoded) {
    std::set<int> want_to_read;
    for (unsigned int i = 0; i < get_data_chunk_count(); i++) {
        want_to_read.insert(chunk_index(i));
    }
    std::map<int, BufferList> decoded_map;
    int r = _decode(want_to_read, chunks, &decoded_map);
    if (r == 0) {
        for (unsigned int i = 0; i < get_data_chunk_count(); i++) {
            decoded->claim_append(decoded_map[chunk_index(i)]);
        }
    }
    return r;
}

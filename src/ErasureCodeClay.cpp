#include <clay/ErasureCodeClay.h>
#include <clay/ErasureCodeInterface.h>
#include <clay/ErasureCodeProfile.h>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>

#define LARGEST_VECTOR_WORDSIZE 16
#define SIMD_ALIGN 32

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

static int pow_int(int a, int x) {
    int power = 1;
    while (x) {
        if (x & 1) power *= a;
        x /= 2;
        a *= a;
    }
    return power;
}

ErasureCodeClay::~ErasureCodeClay() {
    for (int i = 0; i < q * t; i++) {
        if (U_buf[i].length() != 0) {
            U_buf[i].clear();
        }
    }
}

int ErasureCodeClay::init(ErasureCodeProfile &profile, std::ostream *ss) {
    int r = parse(profile, ss);
    if (r) {
        return r;
    }
    r = ErasureCode::init(profile, ss);
    if (r) {
        return r;
    }
    
    // here we instantiate jerasure reed solomon for mds and pft
    mds.erasure_code = std::make_shared<ErasureCodeJerasureReedSolomonVandermonde>();
    pft.erasure_code = std::make_shared<ErasureCodeJerasureReedSolomonVandermonde>();
    
    r = mds.erasure_code->init(mds.profile, ss);
    if (r) {
        return r;
    }
    r = pft.erasure_code->init(pft.profile, ss);
    return r;
}

unsigned int ErasureCodeClay::get_chunk_size(unsigned int object_size) const {
    unsigned alignment = get_alignment();
    unsigned tail = object_size % alignment;
    unsigned padded_length = object_size + (tail ? (alignment - tail) : 0);

    assert(padded_length % (k * sub_chunk_no) == 0);

    return padded_length / k;
}

int ErasureCodeClay::minimum_to_decode(const std::set<int> &want_to_read,
                                      const std::set<int> &available,
                                      std::map<int, std::vector<std::pair<int, int>>> *minimum) {
    if (is_repair(want_to_read, available)) {
        return minimum_to_repair(want_to_read, available, minimum);
    } else {
        return ErasureCode::minimum_to_decode(want_to_read, available, minimum);
    }
}

int ErasureCodeClay::decode(const std::set<int> &want_to_read,
                            const std::map<int, BufferList> &chunks,
                            std::map<int, BufferList> *decoded, int chunk_size) {
    std::set<int> avail;
    for (const auto& chunk_pair : chunks) {
        avail.insert(chunk_pair.first);
    }

    if (is_repair(want_to_read, avail)) {
        return repair(want_to_read, chunks, decoded, chunk_size);
    } else {
        return ErasureCode::_decode(want_to_read, chunks, decoded);
    }
}

int ErasureCodeClay::encode_chunks(const std::set<int> & /*want_to_encode*/,
                                   std::map<int, BufferList> *encoded) {
    std::map<int, BufferList> chunks;
    std::set<int> parity_chunks;
    int chunk_size = (*encoded)[0].length();

    for (int i = 0; i < k + m; i++) {
        if (i < k) {
            chunks[i] = (*encoded)[i];
        } else {
            chunks[i + nu] = (*encoded)[i];
            parity_chunks.insert(i + nu);
        }
    }

    for (int i = k; i < k + nu; i++) {
        BufferList buf(chunk_size, SIMD_ALIGN);
        buf.zero();
        chunks[i] = std::move(buf);
    }

    int res = decode_layered(parity_chunks, &chunks);
    for (int i = k; i < k + nu; i++) {
        chunks[i].clear();
    }
    return res;
}

int ErasureCodeClay::decode_chunks(const std::set<int> & /*want_to_read*/,
                                   const std::map<int, BufferList> &chunks,
                                   std::map<int, BufferList> *decoded) {
    std::set<int> erasures;
    std::map<int, BufferList> coded_chunks;

    for (int i = 0; i < k + m; i++) {
        if (chunks.count(i) == 0) {
            erasures.insert(i < k ? i : i + nu);
        }
        assert(decoded->count(i) > 0);
        coded_chunks[i < k ? i : i + nu] = (*decoded)[i];
    }

    return decode_layered(erasures, &coded_chunks);
}

int ErasureCodeClay::parse(ErasureCodeProfile &profile, std::ostream *ss) {
    int err = 0;
    err |= to_int("k", profile, &k, DEFAULT_K, ss);
    err |= to_int("m", profile, &m, DEFAULT_M, ss);
    err |= to_int("d", profile, &d, std::to_string(k + m - 1), ss);
    err |= to_int("w", profile, &w, DEFAULT_W, ss);
    
    if (err) {
        return err;
    }

    q = w;
    t = 2;  
    nu = q * t;
    sub_chunk_no = q * q;
    
    // setting up profiles for mds and pft
    mds.profile = profile;
    pft.profile = profile;
    pft.profile["k"] = "4";  
    pft.profile["m"] = "4";
    
    return 0;
}

unsigned ErasureCodeClay::get_alignment() const {
    return SIMD_ALIGN;
}

int ErasureCodeClay::is_repair(const std::set<int> & /*want_to_read*/, const std::set<int> & /*available_chunks*/) {
    return 0; 
}

int ErasureCodeClay::minimum_to_repair(const std::set<int> & /*want_to_read*/,
                                       const std::set<int> &available_chunks,
                                       std::map<int, std::vector<std::pair<int, int>>> *minimum) {
    minimum->clear();
    auto it = available_chunks.begin();
    int count = 0;
    while (it != available_chunks.end() && count < d) {
        (*minimum)[*it] = std::vector<std::pair<int, int>>{std::make_pair(0, 1)};
        ++it;
        ++count;
    }
    return count >= d ? 0 : -1;
}

int ErasureCodeClay::repair(const std::set<int> & /*want_to_read*/,
                            const std::map<int, BufferList> & /*chunks*/,
                            std::map<int, BufferList> * /*recovered*/, int /*chunk_size*/) {
    return 0; 
}

int ErasureCodeClay::decode_layered(std::set<int> & /*erased_chunks*/, std::map<int, BufferList> * /*chunks*/) {
    return 0;  
}

int ErasureCodeClay::repair_one_lost_chunk(std::map<int, BufferList> & /*recovered_data*/,
                                            std::set<int> & /*aloof_nodes*/,
                                            std::map<int, BufferList> & /*helper_data*/,
                                            int /*repair_blocksize*/,
                                            std::vector<std::pair<int, int>> & /*repair_sub_chunks_ind*/) {
    return 0;  
}

void ErasureCodeClay::get_repair_subchunks(const int & /*lost_node*/,
                                            std::vector<std::pair<int, int>> & /*repair_sub_chunks_ind*/) {
    
}

int ErasureCodeClay::get_repair_sub_chunk_count(const std::set<int> & /*want_to_read*/) {
    return 1; 
}

int ErasureCodeClay::decode_erasures(const std::set<int> & /*erased_chunks*/, int /*z*/,
                                     std::map<int, BufferList> * /*chunks*/, int /*sc_size*/) {
    return 0;  
}

int ErasureCodeClay::decode_uncoupled(const std::set<int> & /*erasures*/, int /*z*/, int /*ss_size*/) {
    return 0;  
}

void ErasureCodeClay::set_planes_sequential_decoding_order(int * /*order*/, std::set<int> & /*erasures*/) {
    
}

void ErasureCodeClay::recover_type1_erasure(std::map<int, BufferList> * /*chunks*/, int /*x*/, int /*y*/, int /*z*/,
                                            int * /*z_vec*/, int /*sc_size*/) {
    
}

void ErasureCodeClay::get_uncoupled_from_coupled(std::map<int, BufferList> * /*chunks*/, int /*x*/, int /*y*/, int /*z*/,
                                                 int * /*z_vec*/, int /*sc_size*/) {
    
}

void ErasureCodeClay::get_coupled_from_uncoupled(std::map<int, BufferList> * /*chunks*/, int /*x*/, int /*y*/, int /*z*/,
                                                 int * /*z_vec*/, int /*sc_size*/) {
    
}

void ErasureCodeClay::get_plane_vector(int /*z*/, int * /*z_vec*/) {
    
}

int ErasureCodeClay::get_max_iscore(std::set<int> & /*erased_chunks*/) {
    return 1;  
}

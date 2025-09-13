#include <clay/ErasureCodeJerasure.h>
#include <sstream>
#include <cstring>
#include <cassert>

extern "C" {
#include "jerasure.h"
#include "reed_sol.h"
}

ErasureCodeJerasureReedSolomonVandermonde::ErasureCodeJerasureReedSolomonVandermonde()
    : k_(0), m_(0), w_(8), matrix_(nullptr), bitmatrix_(nullptr), schedule_(nullptr) {
}

ErasureCodeJerasureReedSolomonVandermonde::~ErasureCodeJerasureReedSolomonVandermonde() {
    cleanup_matrices();
}

int ErasureCodeJerasureReedSolomonVandermonde::parse(ErasureCodeProfile &profile, std::ostream *ss) {
    auto k_iter = profile.find("k");
    if (k_iter != profile.end()) {
        k_ = std::stoi(k_iter->second);
    } else {
        k_ = 4; 
    }

    auto m_iter = profile.find("m");
    if (m_iter != profile.end()) {
        m_ = std::stoi(m_iter->second);
    } else {
        m_ = 2; 
    }

    auto w_iter = profile.find("w");
    if (w_iter != profile.end()) {
        w_ = std::stoi(w_iter->second);
    } else {
        w_ = 8; 
    }

    if (k_ <= 0 || m_ <= 0) {
        if (ss) *ss << "k and m must be positive integers" << std::endl;
        return -1;
    }

    if (w_ != 8 && w_ != 16 && w_ != 32) {
        if (ss) *ss << "w must be 8, 16, or 32" << std::endl;
        return -1;
    }

    return init_matrices();
}

int ErasureCodeJerasureReedSolomonVandermonde::init_matrices() {
    cleanup_matrices();

    matrix_ = reed_sol_vandermonde_coding_matrix(k_, m_, w_);
    if (!matrix_) {
        return -1;
    }

    bitmatrix_ = jerasure_matrix_to_bitmatrix(k_, m_, w_, matrix_);
    if (!bitmatrix_) {
        return -1;
    }

    schedule_ = jerasure_smart_bitmatrix_to_schedule(k_, m_, w_, bitmatrix_);
    if (!schedule_) {
        return -1;
    }

    return 0;
}

void ErasureCodeJerasureReedSolomonVandermonde::cleanup_matrices() {
    if (matrix_) {
        free(matrix_);
        matrix_ = nullptr;
    }
    if (bitmatrix_) {
        free(bitmatrix_);
        bitmatrix_ = nullptr;
    }
    if (schedule_) {
        jerasure_free_schedule(schedule_);
        schedule_ = nullptr;
    }
}

int ErasureCodeJerasureReedSolomonVandermonde::encode_chunks(const std::set<int> & /*want_to_encode*/,
                                                             std::map<int, BufferList> *encoded) {
    if (!matrix_ || !schedule_) {
        return -1;
    }

    auto first_chunk = encoded->begin();
    if (first_chunk == encoded->end()) {
        return -1;
    }
    size_t chunk_size = first_chunk->second.length();

    char **data_ptrs = new char*[k_];
    char **coding_ptrs = new char*[m_];

    for (int i = 0; i < k_; i++) {
        auto it = encoded->find(i);
        if (it != encoded->end()) {
            data_ptrs[i] = it->second.c_str();
        } else {
            delete[] data_ptrs;
            delete[] coding_ptrs;
            return -1;
        }
    }

    for (int i = 0; i < m_; i++) {
        auto it = encoded->find(k_ + i);
        if (it != encoded->end()) {
            coding_ptrs[i] = it->second.c_str();
        } else {
            delete[] data_ptrs;
            delete[] coding_ptrs;
            return -1;
        }
    }

    jerasure_schedule_encode(k_, m_, w_, schedule_, 
                             data_ptrs, coding_ptrs, 
                             chunk_size, w_/8);

    delete[] data_ptrs;
    delete[] coding_ptrs;
    
    return 0;
}

int ErasureCodeJerasureReedSolomonVandermonde::decode_chunks(const std::set<int> & /*want_to_read*/,
                                                             const std::map<int, BufferList> &chunks,
                                                             std::map<int, BufferList> *decoded) {
    if (!matrix_ || !schedule_) {
        return -1;
    }

    auto first_chunk = chunks.begin();
    if (first_chunk == chunks.end()) {
        return -1;
    }
    size_t chunk_size = first_chunk->second.length();

    char **data_ptrs = new char*[k_];
    char **coding_ptrs = new char*[m_];
    int *erasures = new int[k_ + m_ + 1];

    int erasure_count = 0;

    for (int i = 0; i < k_; i++) {
        auto chunk_it = chunks.find(i);
        auto decoded_it = decoded->find(i);
        
        if (chunk_it != chunks.end()) {
            data_ptrs[i] = const_cast<char*>(chunk_it->second.c_str());
        } else if (decoded_it != decoded->end()) {
            data_ptrs[i] = decoded_it->second.c_str();
            erasures[erasure_count++] = i;
        } else {
            data_ptrs[i] = nullptr;
            erasures[erasure_count++] = i;
        }
    }
    for (int i = 0; i < m_; i++) {
        auto chunk_it = chunks.find(k_ + i);
        auto decoded_it = decoded->find(k_ + i);
        
        if (chunk_it != chunks.end()) {
            coding_ptrs[i] = const_cast<char*>(chunk_it->second.c_str());
        } else if (decoded_it != decoded->end()) {
            coding_ptrs[i] = decoded_it->second.c_str();
            erasures[erasure_count++] = k_ + i;
        } else {
            coding_ptrs[i] = nullptr;
            erasures[erasure_count++] = k_ + i;
        }
    }

    erasures[erasure_count] = -1;

    int result = jerasure_schedule_decode_lazy(k_, m_, w_, bitmatrix_, erasures,
                                               data_ptrs, coding_ptrs,
                                               chunk_size, w_/8, 1);

    delete[] data_ptrs;
    delete[] coding_ptrs;
    delete[] erasures;

    return result;
}
unsigned int ErasureCodeJerasureReedSolomonVandermonde::get_chunk_size(unsigned int object_size) const {
    unsigned alignment = 32; 
    unsigned tail = object_size % alignment;
    unsigned padded_length = object_size + (tail ? (alignment - tail) : 0);

    while (padded_length % k_ != 0) {
        padded_length++;
    }
    
    return padded_length / k_;
}

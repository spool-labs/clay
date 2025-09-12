#include "ErasureCodeJerasure.h"
#include "BufferList.h"
#include <sstream>
#include <stdexcept>

extern "C" {
#include "jerasure.h"
#include "reed_sol.h"
#include "galois.h"
}

#define LARGEST_VECTOR_WORDSIZE 16

int ErasureCodeJerasure::init(ErasureCodeProfile &profile, std::ostream *ss) {
    int err = parse(profile, ss);
    if (err) {
        return err;
    }
    prepare();
    return ErasureCode::init(profile, ss);
}

int ErasureCodeJerasure::parse(ErasureCodeProfile &profile, std::ostream *ss) {
    int err = ErasureCode::parse(profile, ss);
    err |= to_int("k", profile, &k, DEFAULT_K, ss);
    err |= to_int("m", profile, &m, DEFAULT_M, ss);
    err |= to_int("w", profile, &w, DEFAULT_W, ss);
    if (chunk_mapping.size() > 0 && static_cast<int>(chunk_mapping.size()) != k + m) {
        *ss << "mapping " << profile.at("mapping")
            << " maps " << chunk_mapping.size() << " chunks instead of"
            << " the expected " << k + m << " and will be ignored" << std::endl;
        chunk_mapping.clear();
        err = -EINVAL;
    }
    err |= sanity_check_k(k, ss);
    return err;
}

unsigned int ErasureCodeJerasure::get_chunk_size(unsigned int object_size) const {
    unsigned alignment = get_alignment();
    if (per_chunk_alignment) {
        unsigned chunk_size = object_size / k;
        if (object_size % k) {
            chunk_size++;
        }
        unsigned modulo = chunk_size % alignment;
        if (modulo) {
            chunk_size += alignment - modulo;
        }
        return chunk_size;
    } else {
        unsigned tail = object_size % alignment;
        unsigned padded_length = object_size + (tail ? (alignment - tail) : 0);
        return padded_length / k;
    }
}

int ErasureCodeJerasure::encode_chunks(const std::set<int> &want_to_encode,
                                      std::map<int, BufferList> *encoded) {
    char *chunks[k + m];
    for (int i = 0; i < k + m; i++) {
        chunks[i] = (*encoded)[i].c_str();
    }
    jerasure_encode(&chunks[0], &chunks[k], (*encoded)[0].length());
    return 0;
}

int ErasureCodeJerasure::decode_chunks(const std::set<int> &want_to_read,
                                       const std::map<int, BufferList> &chunks,
                                       std::map<int, BufferList> *decoded) {
    unsigned blocksize = (*chunks.begin()).second.length();
    int erasures[k + m + 1];
    int erasures_count = 0;
    char *data[k];
    char *coding[m];
    for (int i = 0; i < k + m; i++) {
        if (chunks.find(i) == chunks.end()) {
            erasures[erasures_count] = i;
            erasures_count++;
        }
        if (i < k) {
            data[i] = (*decoded)[i].c_str();
        } else {
            coding[i - k] = (*decoded)[i].c_str();
        }
    }
    erasures[erasures_count] = -1;

    if (erasures_count == 0) {
        return 0; // No erasures to decode
    }
    return jerasure_decode(erasures, data, coding, blocksize);
}

bool ErasureCodeJerasure::is_prime(int value) {
    int prime55[] = {
      2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,
      73,79,83,89,97,101,103,107,109,113,127,131,137,139,149,
      151,157,163,167,173,179,
      181,191,193,197,199,211,223,227,229,233,239,241,251,257
    };
    for (int i = 0; i < 55; i++) {
        if (value == prime55[i]) {
            return true;
        }
    }
    return false;
}

// ErasureCodeJerasureReedSolomonVandermonde
void ErasureCodeJerasureReedSolomonVandermonde::jerasure_encode(char **data,
                                                               char **coding,
                                                               int blocksize) {
    jerasure_matrix_encode(k, m, w, matrix, data, coding, blocksize);
}

int ErasureCodeJerasureReedSolomonVandermonde::jerasure_decode(int *erasures,
                                                              char **data,
                                                              char **coding,
                                                              int blocksize) {
    return jerasure_matrix_decode(k, m, w, matrix, 1, erasures, data, coding, blocksize);
}

unsigned int ErasureCodeJerasureReedSolomonVandermonde::get_alignment() const {
    if (per_chunk_alignment) {
        return w * LARGEST_VECTOR_WORDSIZE;
    } else {
        unsigned alignment = k * w * sizeof(int);
        if ((w * sizeof(int)) % LARGEST_VECTOR_WORDSIZE) {
            alignment = k * w * LARGEST_VECTOR_WORDSIZE;
        }
        return alignment;
    }
}

int ErasureCodeJerasureReedSolomonVandermonde::parse(ErasureCodeProfile &profile, std::ostream *ss) {
    int err = ErasureCodeJerasure::parse(profile, ss);
    if (w != 8 && w != 16 && w != 32) {
        *ss << "ReedSolomonVandermonde: w=" << static_cast<int>(w)
            << " must be one of {8, 16, 32} : revert to " << DEFAULT_W << std::endl;
        w = std::stoi(DEFAULT_W);
        err = -EINVAL;
    }
    err |= to_bool("jerasure-per-chunk-alignment", profile, &per_chunk_alignment, "false", ss);
    return err;
}

void ErasureCodeJerasureReedSolomonVandermonde::prepare() {
    matrix = reed_sol_vandermonde_coding_matrix(k, m, w);
}

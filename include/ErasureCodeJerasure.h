#ifndef ERASURE_CODE_JERASURE_H
#define ERASURE_CODE_JERASURE_H

#include <string>
#include "ErasureCodeProfile.h"
#include "ErasureCode.h"
#include "BufferList.h"

class ErasureCodeJerasure : public ErasureCode {
public:
    int k;
    std::string DEFAULT_K;
    int m;
    std::string DEFAULT_M;
    int w;
    std::string DEFAULT_W;
    const char *technique;
    std::string rule_root;
    std::string rule_failure_domain;
    bool per_chunk_alignment;
    uint64_t flags;

    explicit ErasureCodeJerasure(const char *_technique)
        : k(0),
          DEFAULT_K("2"),
          m(0),
          DEFAULT_M("1"),
          w(0),
          DEFAULT_W("8"),
          technique(_technique),
          per_chunk_alignment(false) {
        flags = FLAG_EC_PLUGIN_PARTIAL_READ_OPTIMIZATION |
                FLAG_EC_PLUGIN_PARTIAL_WRITE_OPTIMIZATION |
                FLAG_EC_PLUGIN_ZERO_INPUT_ZERO_OUTPUT_OPTIMIZATION |
                FLAG_EC_PLUGIN_PARITY_DELTA_OPTIMIZATION;
        
        // Use string comparison instead of string_view literals
        if (std::string(technique) == "reed_sol_van") {
            flags |= FLAG_EC_PLUGIN_OPTIMIZED_SUPPORTED;
        } else if (std::string(technique) != "cauchy_orig") {
            flags |= FLAG_EC_PLUGIN_CRC_ENCODE_DECODE_SUPPORT;
        }
    }

    ~ErasureCodeJerasure() override {}

    uint64_t get_supported_optimizations() const override {
        return flags;
    }

    unsigned int get_chunk_count() const override {
        return k + m;
    }

    unsigned int get_data_chunk_count() const override {
        return k;
    }

    unsigned int get_chunk_size(unsigned int stripe_width) const override;

    int encode_chunks(const std::set<int> &want_to_encode,
                     std::map<int, bufferlist> *encoded) override;

    int decode_chunks(const std::set<int> &want_to_read,
                     const std::map<int, bufferlist> &chunks,
                     std::map<int, bufferlist> *decoded) override;

    void encode_delta(const bufferptr &old_data,
                      const bufferptr &new_data,
                      bufferptr *delta_maybe_in_place);

    int init(ErasureCodeProfile &profile, std::ostream *ss) override;

    virtual void jerasure_encode(char **data,
                                 char **coding,
                                 int blocksize) = 0;

    virtual int jerasure_decode(int *erasures,
                                char **data,
                                char **coding,
                                int blocksize) = 0;

    virtual unsigned get_alignment() const = 0;
    virtual void prepare() = 0;

    static bool is_prime(int value);
    void do_scheduled_ops(char **ptrs, int **operations, int packetsize, int s, int d);

protected:
    virtual int parse(ErasureCodeProfile &profile, std::ostream *ss);
};

class ErasureCodeJerasureReedSolomonVandermonde : public ErasureCodeJerasure {
public:
    int *matrix;

    ErasureCodeJerasureReedSolomonVandermonde() :
        ErasureCodeJerasure("reed_sol_van"),
        matrix(0)
    {
        DEFAULT_K = "7";
        DEFAULT_M = "3";
        DEFAULT_W = "8";
    }

    ~ErasureCodeJerasureReedSolomonVandermonde() override {
        if (matrix)
            free(matrix);
    }

    void jerasure_encode(char **data,
                         char **coding,
                         int blocksize) override;

    int jerasure_decode(int *erasures,
                        char **data,
                        char **coding,
                        int blocksize) override;

    unsigned get_alignment() const override;

    size_t get_minimum_granularity() override {
        return 1;
    }

    void prepare() override;

private:
    int parse(ErasureCodeProfile& profile, std::ostream *ss) override;
};

#define DEFAULT_PACKETSIZE "2048"

#endif
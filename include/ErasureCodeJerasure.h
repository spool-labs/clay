#ifndef CEPH_ERASURE_CODE_JERASURE_H
#define CEPH_ERASURE_CODE_JERASURE_H

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
    bool per_chunk_alignment;

    explicit ErasureCodeJerasure(const char *_technique) :
        k(0), DEFAULT_K("2"), m(0), DEFAULT_M("1"), w(0), DEFAULT_W("8"),
        technique(_technique), per_chunk_alignment(false) {}

    ~ErasureCodeJerasure() override {}

    unsigned int get_chunk_count() const override { return k + m; }
    unsigned int get_data_chunk_count() const override { return k; }
    unsigned int get_chunk_size(unsigned int object_size) const override;
    int encode_chunks(const std::set<int> &want_to_encode,
                     std::map<int, BufferList> *encoded) override;
    int decode_chunks(const std::set<int> &want_to_read,
                      const std::map<int, BufferList> &chunks,
                      std::map<int, BufferList> *decoded) override;
    int init(ErasureCodeProfile &profile, std::ostream *ss) override;
    virtual void jerasure_encode(char **data, char **coding, int blocksize) = 0;
    virtual int jerasure_decode(int *erasures, char **data, char **coding, int blocksize) = 0;
    virtual unsigned get_alignment() const = 0;
    virtual void prepare() = 0;
    static bool is_prime(int value);

protected:
    virtual int parse(ErasureCodeProfile &profile, std::ostream *ss) override;
};

class ErasureCodeJerasureReedSolomonVandermonde : public ErasureCodeJerasure {
public:
    int *matrix;
    ErasureCodeJerasureReedSolomonVandermonde() :
        ErasureCodeJerasure("reed_sol_van"), matrix(nullptr) {
        DEFAULT_K = "7";
        DEFAULT_M = "3";
        DEFAULT_W = "8";
    }
    ~ErasureCodeJerasureReedSolomonVandermonde() override {
        if (matrix) free(matrix);
    }
    void jerasure_encode(char **data, char **coding, int blocksize) override;
    int jerasure_decode(int *erasures, char **data, char **coding, int blocksize) override;
    unsigned get_alignment() const override;
    void prepare() override;

private:
    int parse(ErasureCodeProfile &profile, std::ostream *ss) override;
};

#endif

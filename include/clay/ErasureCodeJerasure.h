#ifndef CLAY_ERASURE_CODE_JERASURE_H
#define CLAY_ERASURE_CODE_JERASURE_H

#include <clay/ErasureCode.h>
#include <memory>

class ErasureCodeJerasureReedSolomonVandermonde : public ErasureCode {
public:
    ErasureCodeJerasureReedSolomonVandermonde();
    virtual ~ErasureCodeJerasureReedSolomonVandermonde() override;
    unsigned int get_chunk_count() const override { return k_ + m_; }
    unsigned int get_data_chunk_count() const override { return k_; }
    unsigned int get_chunk_size(unsigned int object_size) const override;
    int encode_chunks(const std::set<int> &want_to_encode,
                      std::map<int, BufferList> *encoded) override;
    int decode_chunks(const std::set<int> &want_to_read,
                      const std::map<int, BufferList> &chunks,
                      std::map<int, BufferList> *decoded) override;
    int parse(ErasureCodeProfile &profile, std::ostream *ss) override;

private:
    int k_;  ///< number of data chunks
    int m_;  ///< number of coding chunks
    int w_;  ///< word size for Galois field arithmetic
    
    int *matrix_;           ///< encoding matrix
    int *bitmatrix_;        ///< bit matrix for encoding
    int **schedule_;        ///< encoding schedule

    int init_matrices();
    void cleanup_matrices();
};

using ErasureCodeJerasure = ErasureCodeJerasureReedSolomonVandermonde;

#endif
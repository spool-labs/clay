#ifndef CLAY_ERASURE_CODE_H
#define CLAY_ERASURE_CODE_H

#include <clay/ErasureCodeInterface.h>
#include <clay/BufferList.h>
#include <vector>
#include <map>
#include <set>
#include <string>

class ErasureCode : public ErasureCodeInterface {
public:
    static const unsigned SIMD_ALIGN = 32; 
    ErasureCode();
    virtual ~ErasureCode() override;
    int init(ErasureCodeProfile &profile, std::ostream *ss) override;

    const ErasureCodeProfile &get_profile() const override { return profile_; }

    int sanity_check_k(int k, std::ostream *ss);

    unsigned int get_coding_chunk_count() const override { 
        return get_chunk_count() - get_data_chunk_count(); 
    }
    virtual int get_sub_chunk_count() { return 1; }

    int minimum_to_decode(const std::set<int> &want_to_read,
                         const std::set<int> &available,
                         std::map<int, std::vector<std::pair<int, int>>> *minimum) override;

    int encode_prepare(const BufferList &raw, std::map<int, BufferList> &encoded) const;

    int encode(const std::set<int> &want_to_encode,
               const BufferList &in,
               std::map<int, BufferList> *encoded) override;

    virtual int encode_chunks(const std::set<int> &want_to_encode,
                              std::map<int, BufferList> *encoded) = 0;

    int decode(const std::set<int> &want_to_read,
               const std::map<int, BufferList> &chunks,
               std::map<int, BufferList> *decoded, int chunk_size) override;

    int _decode(const std::set<int> &want_to_read,
                const std::map<int, BufferList> &chunks,
                std::map<int, BufferList> *decoded);

    virtual int decode_chunks(const std::set<int> &want_to_read,
                              const std::map<int, BufferList> &chunks,
                              std::map<int, BufferList> *decoded) = 0;

    const std::vector<int> &get_chunk_mapping() const { return chunk_mapping_; }

    virtual int parse(ErasureCodeProfile &profile, std::ostream *ss) = 0;

protected:
    std::vector<int> chunk_mapping_;  ///< chunk mapping for advanced schemes
    ErasureCodeProfile profile_;      ///< configuration profile

    virtual unsigned get_alignment() const { return SIMD_ALIGN; }
};

#endif 
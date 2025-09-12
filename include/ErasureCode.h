#ifndef CEPH_ERASURE_CODE_H
#define CEPH_ERASURE_CODE_H

#include "ErasureCodeInterface.h"
#include "BufferList.h"
#include "ErasureCodeProfile.h"

class ErasureCode : public ErasureCodeInterface {
public:
    static const unsigned SIMD_ALIGN = 32;

    std::vector<int> chunk_mapping;
    ErasureCodeProfile _profile;

    ~ErasureCode() override {}

    int init(ErasureCodeProfile &profile, std::ostream *ss) override;
    const ErasureCodeProfile &get_profile() const override { return _profile; }
    int sanity_check_k(int k, std::ostream *ss);
    unsigned int get_coding_chunk_count() const override { return get_chunk_count() - get_data_chunk_count(); }
    int get_sub_chunk_count() override { return 1; }
    int minimum_to_decode(const std::set<int> &want_to_read,
                         const std::set<int> &available,
                         std::map<int, std::vector<std::pair<int, int>>> *minimum) override;
    int minimum_to_decode_with_cost(const std::set<int> &want_to_read,
                                   const std::map<int, int> &available,
                                   std::set<int> *minimum) override;
    int encode_prepare(const BufferList &raw, std::map<int, BufferList> &encoded) const;
    int encode(const std::set<int> &want_to_encode,
               const BufferList &in,
               std::map<int, BufferList> *encoded) override;
    int encode_chunks(const std::set<int> &want_to_encode,
                      std::map<int, BufferList> *encoded) override;
    int decode(const std::set<int> &want_to_read,
               const std::map<int, BufferList> &chunks,
               std::map<int, BufferList> *decoded, int chunk_size) override;
    int _decode(const std::set<int> &want_to_read,
                const std::map<int, BufferList> &chunks,
                std::map<int, BufferList> *decoded);
    int decode_chunks(const std::set<int> &want_to_read,
                      const std::map<int, BufferList> &chunks,
                      std::map<int, BufferList> *decoded) override;
    const std::vector<int> &get_chunk_mapping() const override { return chunk_mapping; }
    int decode_concat(const std::map<int, BufferList> &chunks, BufferList *decoded) override;

    virtual int _minimum_to_decode(const std::set<int> &want_to_read,
                                   const std::set<int> &available_chunks,
                                   std::set<int> *minimum);

protected:
    virtual int parse(ErasureCodeProfile &profile, std::ostream *ss);
    int to_mapping(ErasureCodeProfile &profile, std::ostream *ss);
    static int to_int(const std::string &name, ErasureCodeProfile &profile,
                     int *value, const std::string &default_value, std::ostream *ss);
    static int to_bool(const std::string &name, ErasureCodeProfile &profile,
                      bool *value, const std::string &default_value, std::ostream *ss);
    static int to_string(const std::string &name, ErasureCodeProfile &profile,
                        std::string *value, const std::string &default_value, std::ostream *ss);

private:
    int chunk_index(unsigned int i) const;
};

#endif

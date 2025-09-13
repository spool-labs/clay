#ifndef ERASURE_CODE_H
#define ERASURE_CODE_H

#include <map>
#include <set>
#include <vector>
#include <string>
#include <ostream>
#include <stdexcept>
#include <cerrno>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <algorithm>
#include "ErasureCodeInterface.h"

int strict_strtol(const char* str, int base, std::string* err);

inline unsigned round_up_to(unsigned value, unsigned alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

class ErasureCode : public ErasureCodeInterface {
public:
    static const unsigned SIMD_ALIGN;
    std::vector<int> chunk_mapping;
    ErasureCodeProfile _profile;

    ~ErasureCode() override = default;

    int init(ErasureCodeProfile& profile, std::ostream* ss) override;
    
    const ErasureCodeProfile& get_profile() const override {
        return _profile;
    }

    int sanity_check_k_m(int k, int m, std::ostream *ss);

    virtual int create_rule(const std::string& name, const std::string& profile, std::ostream* ss);

    unsigned int get_chunk_count() const override;
    unsigned int get_data_chunk_count() const override; 
    unsigned int get_chunk_size(unsigned int stripe_width) const override;

    unsigned int get_coding_chunk_count() const override {
        return get_chunk_count() - get_data_chunk_count();
    }

    int get_sub_chunk_count() override {
        return 1;
    }

    virtual unsigned int get_alignment() const;

    int _minimum_to_decode(const std::set<int> &want_to_read,
                          const std::set<int> &available_chunks,
                          std::set<int> *minimum);

    int minimum_to_decode(const std::set<int> &want_to_read,
                          const std::set<int> &available,
                          std::map<int, std::vector<std::pair<int, int>>> *minimum) override;

    int minimum_to_decode_with_cost(const std::set<int> &want_to_read,
                                    const std::map<int, int> &available,
                                    std::set<int> *minimum) override;

    int encode_prepare(const bufferlist &raw,
                       std::map<int, bufferlist> &encoded) const;

    int encode(const std::set<int> &want_to_encode,
               const bufferlist &in,
               std::map<int, bufferlist> *encoded) override;

    int _decode(const std::set<int> &want_to_read,
               const std::map<int, bufferlist> &chunks,
               std::map<int, bufferlist> *decoded);

    int decode(const std::set<int> &want_to_read,
               const std::map<int, bufferlist> &chunks,
               std::map<int, bufferlist> *decoded,
               int chunk_size) override;

    static int to_int(const std::string &name,
                      ErasureCodeProfile &profile,
                      int *value,
                      const std::string &default_value,
                      std::ostream *ss);

    static int to_bool(const std::string &name,
                       ErasureCodeProfile &profile,
                       bool *value,
                       const std::string &default_value,
                       std::ostream *ss);

    static int to_string(const std::string &name,
                         ErasureCodeProfile &profile,
                         std::string *value,
                         const std::string &default_value,
                         std::ostream *ss);

    int decode_concat(const std::set<int> &want_to_read,
                      const std::map<int, bufferlist> &chunks,
                      bufferlist *decoded) override;

    int decode_concat(const std::map<int, bufferlist> &chunks,
                      bufferlist *decoded) override;

    void encode_delta(const bufferptr &old_data,
                      const bufferptr &new_data,
                      bufferptr *delta_maybe_in_place) override {
        std::abort();
    }

protected:
    virtual int parse(const ErasureCodeProfile& profile, std::ostream* ss) {
        return 0;
    }

private:
    unsigned int chunk_index(unsigned int i) const {
        return chunk_mapping.empty() ? i : chunk_mapping[i];
    }
};

#endif // ERASURE_CODE_H
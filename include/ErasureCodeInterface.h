#ifndef ERASURE_CODE_INTERFACE_H
#define ERASURE_CODE_INTERFACE_H

#include <map>
#include <set>
#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include "BufferList.h"
#include "ErasureCodeProfile.h"

inline std::ostream& operator<<(std::ostream& out, const ErasureCodeProfile& profile) {
    out << "{";
    for (auto it = profile.begin(); it != profile.end(); ++it) {
        if (it != profile.begin()) out << ",";
        out << it->first << "=" << it->second;
    }
    out << "}";
    return out;
}

class ErasureCodeInterface {
public:
    virtual ~ErasureCodeInterface() {}

    virtual int init(ErasureCodeProfile &profile, std::ostream *ss) = 0;
    virtual const ErasureCodeProfile &get_profile() const = 0;
    virtual unsigned int get_chunk_count() const = 0;
    virtual unsigned int get_data_chunk_count() const = 0;
    virtual unsigned int get_coding_chunk_count() const = 0;
    virtual int get_sub_chunk_count() = 0;
    virtual unsigned int get_chunk_size(unsigned int object_size) const = 0;
    virtual int minimum_to_decode(const std::set<int> &want_to_read,
                                 const std::set<int> &available,
                                 std::map<int, std::vector<std::pair<int, int>>> *minimum) = 0;
    virtual int minimum_to_decode_with_cost(const std::set<int> &want_to_read,
                                           const std::map<int, int> &available,
                                           std::set<int> *minimum) = 0;
    virtual int encode(const std::set<int> &want_to_encode,
                      const BufferList &in,
                      std::map<int, BufferList> *encoded) = 0;
    virtual int encode_chunks(const std::set<int> &want_to_encode,
                              std::map<int, BufferList> *encoded) = 0;
    virtual int decode(const std::set<int> &want_to_read,
                       const std::map<int, BufferList> &chunks,
                       std::map<int, BufferList> *decoded, int chunk_size) = 0;
    virtual int decode_chunks(const std::set<int> &want_to_read,
                              const std::map<int, BufferList> &chunks,
                              std::map<int, BufferList> *decoded) = 0;
    virtual const std::vector<int> &get_chunk_mapping() const = 0;
    virtual int decode_concat(const std::map<int, BufferList> &chunks,
                             BufferList *decoded) = 0;
};

typedef std::shared_ptr<ErasureCodeInterface> ErasureCodeInterfaceRef;


#endif

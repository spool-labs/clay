#ifndef CLAY_ERASURE_CODE_INTERFACE_H
#define CLAY_ERASURE_CODE_INTERFACE_H

#include <map>
#include <set>
#include <string>
#include <memory>
#include <set>
#include <vector>
#include <iostream>
#include <vector>
#include <iostream>

class BufferList;

using ErasureCodeProfile = std::map<std::string, std::string>;

class ErasureCodeInterface {
public:
    virtual ~ErasureCodeInterface() = default;
    virtual int init(ErasureCodeProfile& profile, std::ostream* ss) = 0;
    virtual unsigned int get_chunk_count() const = 0;
    virtual unsigned int get_data_chunk_count() const = 0;
    virtual unsigned int get_coding_chunk_count() const = 0;
    virtual unsigned int get_chunk_size(unsigned int object_size) const = 0;
    virtual int decode_chunks(const std::set<int>& want_to_read,
                              const std::map<int, BufferList>& chunks,
                              std::map<int, BufferList>* decoded) = 0;
    virtual const ErasureCodeProfile& get_profile() const = 0;
    virtual int encode(const std::set<int>& want_to_encode,
                       const BufferList& in,
                       std::map<int, BufferList>* encoded) = 0;
    virtual int decode(const std::set<int>& want_to_read,
                       const std::map<int, BufferList>& chunks,
                       std::map<int, BufferList>* decoded,
                       int chunk_size) = 0;
    virtual int minimum_to_decode(const std::set<int>& want_to_read,
                                  const std::set<int>& available,
                                  std::map<int, std::vector<std::pair<int, int>>>* minimum) = 0;
};

using ErasureCodeInterfaceRef = std::shared_ptr<ErasureCodeInterface>;

#endif 
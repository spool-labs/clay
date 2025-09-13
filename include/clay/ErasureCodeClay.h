#ifndef CLAY_ERASURE_CODE_CLAY_H
#define CLAY_ERASURE_CODE_CLAY_H

#include <clay/ErasureCode.h>
#include <clay/ErasureCodeJerasure.h>
#include <clay/BufferList.h>
#include <clay/ErasureCodeInterface.h>
#include <map>
#include <set>
#include <string>
#include <memory>

#define LARGEST_VECTOR_WORDSIZE 16

class ErasureCodeClay : public ErasureCode {
public:
    std::string DEFAULT_K{"4"};
    std::string DEFAULT_M{"2"};
    std::string DEFAULT_W{"8"};
    int k = 0, m = 0, d = 0, w = 8;
    int q = 0, t = 0, nu = 0;
    int sub_chunk_no = 0;
    std::map<int, BufferList> U_buf;

    struct ScalarMDS {
        ErasureCodeInterfaceRef erasure_code;
        ErasureCodeProfile profile;
    };
    ScalarMDS mds;
    ScalarMDS pft;

    explicit ErasureCodeClay() = default;
    ~ErasureCodeClay() override;

    unsigned int get_chunk_count() const override { return k + m; }
    unsigned int get_data_chunk_count() const override { return k; }
    int get_sub_chunk_count() override { return sub_chunk_no; }
    unsigned int get_chunk_size(unsigned int object_size) const override;
    int minimum_to_decode(const std::set<int> &want_to_read,
                         const std::set<int> &available,
                         std::map<int, std::vector<std::pair<int, int>>> *minimum) override;
    int decode(const std::set<int> &want_to_read,
               const std::map<int, BufferList> &chunks,
               std::map<int, BufferList> *decoded, int chunk_size) override;
    int encode_chunks(const std::set<int> &want_to_encode,
                      std::map<int, BufferList> *encoded) override;
    int decode_chunks(const std::set<int> &want_to_read,
                      const std::map<int, BufferList> &chunks,
                      std::map<int, BufferList> *decoded) override;
    int init(ErasureCodeProfile &profile, std::ostream *ss) override;

private:
    int is_repair(const std::set<int> &want_to_read, const std::set<int> &available_chunks);
    int minimum_to_repair(const std::set<int> &want_to_read,
                          const std::set<int> &available_chunks,
                          std::map<int, std::vector<std::pair<int, int>>> *minimum);
    int repair(const std::set<int> &want_to_read,
               const std::map<int, BufferList> &chunks,
               std::map<int, BufferList> *recovered, int chunk_size);
    int decode_layered(std::set<int> &erased_chunks, std::map<int, BufferList> *chunks);
    int repair_one_lost_chunk(std::map<int, BufferList> &recovered_data,
                              std::set<int> &aloof_nodes,
                              std::map<int, BufferList> &helper_data,
                              int repair_blocksize,
                              std::vector<std::pair<int, int>> &repair_sub_chunks_ind);
    void get_repair_subchunks(const int &lost_node,
                              std::vector<std::pair<int, int>> &repair_sub_chunks_ind);
    int get_repair_sub_chunk_count(const std::set<int> &want_to_read);
    int decode_erasures(const std::set<int> &erased_chunks, int z,
                        std::map<int, BufferList> *chunks, int sc_size);
    int decode_uncoupled(const std::set<int> &erasures, int z, int ss_size);
    void set_planes_sequential_decoding_order(int *order, std::set<int> &erasures);
    void recover_type1_erasure(std::map<int, BufferList> *chunks, int x, int y, int z,
                               int *z_vec, int sc_size);
    void get_uncoupled_from_coupled(std::map<int, BufferList> *chunks, int x, int y, int z,
                                    int *z_vec, int sc_size);
    void get_coupled_from_uncoupled(std::map<int, BufferList> *chunks, int x, int y, int z,
                                    int *z_vec, int sc_size);
    void get_plane_vector(int z, int *z_vec);
    int get_max_iscore(std::set<int> &erased_chunks);
    unsigned get_alignment() const;
    int parse(ErasureCodeProfile &profile, std::ostream *ss) override;
};

#endif

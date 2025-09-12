#include "ErasureCodeClay.h"
#include "BufferList.h"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cassert>

#define LARGEST_VECTOR_WORDSIZE 16
#define talloc(type, num) (type *) malloc(sizeof(type)*(num))

static int pow_int(int a, int x) {
    int power = 1;
    while (x) {
        if (x & 1) power *= a;
        x /= 2;
        a *= a;
    }
    return power;
}

ErasureCodeClay::~ErasureCodeClay() {
    for (int i = 0; i < q * t; i++) {
        if (U_buf[i].length() != 0) {
            U_buf[i].clear();
        }
    }
}

int ErasureCodeClay::init(ErasureCodeProfile &profile, std::ostream *ss) {
    int r = parse(profile, ss);
    if (r) {
        return r;
    }
    r = ErasureCode::init(profile, ss);
    if (r) {
        return r;
    }
    // Instantiate Jerasure Reed-Solomon for mds and pft
    mds.erasure_code = std::make_shared<ErasureCodeJerasureReedSolomonVandermonde>();
    pft.erasure_code = std::make_shared<ErasureCodeJerasureReedSolomonVandermonde>();
    r = mds.erasure_code->init(mds.profile, ss);
    if (r) {
        return r;
    }
    r = pft.erasure_code->init(pft.profile, ss);
    return r;
}

unsigned int ErasureCodeClay::get_chunk_size(unsigned int object_size) const {
    unsigned alignment = get_alignment();
    unsigned tail = object_size % alignment;
    unsigned padded_length = object_size + (tail ? (alignment - tail) : 0);

    assert(padded_length % (k * sub_chunk_no) == 0);

    return padded_length / k;
}

int ErasureCodeClay::minimum_to_decode(const std::set<int> &want_to_read,
                                      const std::set<int> &available,
                                      std::map<int, std::vector<std::pair<int, int>>> *minimum) {
    if (is_repair(want_to_read, available)) {
        return minimum_to_repair(want_to_read, available, minimum);
    } else {
        return ErasureCode::minimum_to_decode(want_to_read, available, minimum);
    }
}

int ErasureCodeClay::decode(const std::set<int> &want_to_read,
                            const std::map<int, BufferList> &chunks,
                            std::map<int, BufferList> *decoded, int chunk_size) {
    std::set<int> avail;
    for (const auto& [node, bl] : chunks) {
        avail.insert(node);
    }

    if (is_repair(want_to_read, avail)) {
        return repair(want_to_read, chunks, decoded, chunk_size);
    } else {
        return ErasureCode::_decode(want_to_read, chunks, decoded);
    }
}

int ErasureCodeClay::encode_chunks(const std::set<int> &want_to_encode,
                                   std::map<int, BufferList> *encoded) {
    std::map<int, BufferList> chunks;
    std::set<int> parity_chunks;
    int chunk_size = (*encoded)[0].length();

    for (int i = 0; i < k + m; i++) {
        if (i < k) {
            chunks[i] = (*encoded)[i];
        } else {
            chunks[i + nu] = (*encoded)[i];
            parity_chunks.insert(i + nu);
        }
    }

    for (int i = k; i < k + nu; i++) {
        BufferList buf(chunk_size, SIMD_ALIGN);
        buf.zero();
        chunks[i] = std::move(buf);
    }

    int res = decode_layered(parity_chunks, &chunks);
    for (int i = k; i < k + nu; i++) {
        chunks[i].clear();
    }
    return res;
}

int ErasureCodeClay::decode_chunks(const std::set<int> &want_to_read,
                                   const std::map<int, BufferList> &chunks,
                                   std::map<int, BufferList> *decoded) {
    std::set<int> erasures;
    std::map<int, BufferList> coded_chunks;

    for (int i = 0; i < k + m; i++) {
        if (chunks.count(i) == 0) {
            erasures.insert(i < k ? i : i + nu);
        }
        assert(decoded->count(i) > 0);
        coded_chunks[i < k ? i : i + nu] = (*decoded)[i];
    }
    int chunk_size = coded_chunks[0].length();

    for (int i = k; i < k + nu; i++) {
        BufferList buf(chunk_size, SIMD_ALIGN);
        buf.zero();
        coded_chunks[i] = std::move(buf);
    }

    int res = decode_layered(erasures, &coded_chunks);
    for (int i = k; i < k + nu; i++) {
        coded_chunks[i].clear();
    }
    return res;
}

unsigned int ErasureCodeClay::get_alignment() const {
    unsigned alignment = k * sub_chunk_no * w * sizeof(int);
    if ((w * sizeof(int)) % LARGEST_VECTOR_WORDSIZE) {
        alignment = k * sub_chunk_no * w * LARGEST_VECTOR_WORDSIZE;
    }
    return alignment;
}

int ErasureCodeClay::parse(ErasureCodeProfile &profile, std::ostream *ss) {
    int err = 0;
    err = ErasureCode::parse(profile, ss);
    err |= to_int("k", profile, &k, DEFAULT_K, ss);
    err |= to_int("m", profile, &m, DEFAULT_M, ss);

    err |= sanity_check_k(k, ss);

    err |= to_int("d", profile, &d, std::to_string(k + m - 1), ss);

    // Check for scalar_mds in profile input
    if (profile.find("scalar_mds") == profile.end() ||
        profile.find("scalar_mds")->second.empty()) {
        mds.profile["plugin"] = "jerasure";
        pft.profile["plugin"] = "jerasure";
    } else {
        std::string p = profile.find("scalar_mds")->second;
        if (p == "jerasure") {
            mds.profile["plugin"] = p;
            pft.profile["plugin"] = p;
        } else {
            *ss << "scalar_mds " << p
                << " is not supported, use 'jerasure'" << std::endl;
            err = -EINVAL;
            return err;
        }
    }

    if (profile.find("technique") == profile.end() ||
        profile.find("technique")->second.empty()) {
        mds.profile["technique"] = "reed_sol_van";
        pft.profile["technique"] = "reed_sol_van";
    } else {
        std::string p = profile.find("technique")->second;
        if (p == "reed_sol_van") {
            mds.profile["technique"] = p;
            pft.profile["technique"] = p;
        } else {
            *ss << "technique " << p
                << " is not supported, use 'reed_sol_van'" << std::endl;
            err = -EINVAL;
            return err;
        }
    }

    if ((d < k) || (d > k + m - 1)) {
        *ss << "value of d " << d
            << " must be within [" << k << "," << k + m - 1 << "]" << std::endl;
        err = -EINVAL;
        return err;
    }

    q = d - k + 1;
    if ((k + m) % q) {
        nu = q - (k + m) % q;
    } else {
        nu = 0;
    }

    if (k + m + nu > 254) {
        err = -EINVAL;
        return err;
    }

    mds.profile["k"] = std::to_string(k + nu);
    mds.profile["m"] = std::to_string(m);
    mds.profile["w"] = "8";

    pft.profile["k"] = "2";
    pft.profile["m"] = "2";
    pft.profile["w"] = "8";

    t = (k + m + nu) / q;
    sub_chunk_no = pow_int(q, t);

    return err;
}

int ErasureCodeClay::is_repair(const std::set<int> &want_to_read,
                               const std::set<int> &available_chunks) {
    if (std::includes(available_chunks.begin(), available_chunks.end(),
                      want_to_read.begin(), want_to_read.end())) {
        return 0;
    }
    if (want_to_read.size() > 1) {
        return 0;
    }

    int i = *want_to_read.begin();
    int lost_node_id = (i < k) ? i : i + nu;
    for (int x = 0; x < q; x++) {
        int node = (lost_node_id / q) * q + x;
        node = (node < k) ? node : node - nu;
        if (node != i) { // node in the same group other than erased node
            if (available_chunks.count(node) == 0) {
                return 0;
            }
        }
    }

    if (available_chunks.size() < static_cast<unsigned>(d)) {
        return 0;
    }
    return 1;
}

int ErasureCodeClay::minimum_to_repair(const std::set<int> &want_to_read,
                                       const std::set<int> &available_chunks,
                                       std::map<int, std::vector<std::pair<int, int>>> *minimum) {
    int i = *want_to_read.begin();
    int lost_node_index = (i < k) ? i : i + nu;
    int rep_node_index = 0;

    // Add all the nodes in lost node's y column.
    std::vector<std::pair<int, int>> sub_chunk_ind;
    get_repair_subchunks(lost_node_index, sub_chunk_ind);
    if (available_chunks.size() >= static_cast<unsigned>(d)) {
        for (int j = 0; j < q; j++) {
            if (j != lost_node_index % q) {
                rep_node_index = (lost_node_index / q) * q + j;
                if (rep_node_index < k) {
                    minimum->insert(std::make_pair(rep_node_index, sub_chunk_ind));
                } else if (rep_node_index >= k + nu) {
                    minimum->insert(std::make_pair(rep_node_index - nu, sub_chunk_ind));
                }
            }
        }
        for (auto chunk : available_chunks) {
            if (minimum->size() >= static_cast<unsigned>(d)) {
                break;
            }
            if (!minimum->count(chunk)) {
                minimum->emplace(chunk, sub_chunk_ind);
            }
        }
    } else {
        throw std::runtime_error("minimum_to_repair: insufficient available chunks");
    }
    assert(minimum->size() == static_cast<unsigned>(d));
    return 0;
}

void ErasureCodeClay::get_repair_subchunks(const int &lost_node,
                                           std::vector<std::pair<int, int>> &repair_sub_chunks_ind) {
    const int y_lost = lost_node / q;
    const int x_lost = lost_node % q;

    const int seq_sc_count = pow_int(q, t - 1 - y_lost);
    const int num_seq = pow_int(q, y_lost);

    int index = x_lost * seq_sc_count;
    for (int ind_seq = 0; ind_seq < num_seq; ind_seq++) {
        repair_sub_chunks_ind.push_back(std::make_pair(index, seq_sc_count));
        index += q * seq_sc_count;
    }
}

int ErasureCodeClay::get_repair_sub_chunk_count(const std::set<int> &want_to_read) {
    std::vector<int> weight_vector(t, 0); // Initialize vector of size t with zeros
    for (auto to_read : want_to_read) {
        weight_vector[to_read / q]++;
    }

    int repair_subchunks_count = 1;
    for (int y = 0; y < t; y++) {
        repair_subchunks_count = repair_subchunks_count * (q - weight_vector[y]);
    }

    return sub_chunk_no - repair_subchunks_count;
}

int ErasureCodeClay::repair(const std::set<int> &want_to_read,
                            const std::map<int, BufferList> &chunks,
                            std::map<int, BufferList> *repaired, int chunk_size) {
    assert(want_to_read.size() == 1 && chunks.size() == static_cast<unsigned>(d));

    int repair_sub_chunk_no = get_repair_sub_chunk_count(want_to_read);
    std::vector<std::pair<int, int>> repair_sub_chunks_ind;

    unsigned repair_blocksize = chunks.begin()->second.length();
    assert(repair_blocksize % repair_sub_chunk_no == 0);

    unsigned sub_chunksize = repair_blocksize / repair_sub_chunk_no;
    unsigned chunksize = sub_chunk_no * sub_chunksize;

    assert(chunksize == static_cast<unsigned>(chunk_size));

    std::map<int, BufferList> recovered_data;
    std::map<int, BufferList> helper_data;
    std::set<int> aloof_nodes;

    for (int i = 0; i < k + m; i++) {
        if (auto found = chunks.find(i); found != chunks.end()) { // i is a helper
            if (i < k) {
                helper_data[i] = found->second;
            } else {
                helper_data[i + nu] = found->second;
            }
        } else {
            if (i != *want_to_read.begin()) { // aloof node case
                int aloof_node_id = (i < k) ? i : i + nu;
                aloof_nodes.insert(aloof_node_id);
            } else {
                BufferList ptr(chunk_size, SIMD_ALIGN);
                ptr.zero();
                int lost_node_id = (i < k) ? i : i + nu;
                (*repaired)[i] = std::move(ptr);
                recovered_data[lost_node_id] = (*repaired)[i];
                get_repair_subchunks(lost_node_id, repair_sub_chunks_ind);
            }
        }
    }

    // Handle shortened codes (when nu > 0)
    for (int i = k; i < k + nu; i++) {
        BufferList ptr(repair_blocksize, SIMD_ALIGN);
        ptr.zero();
        helper_data[i] = std::move(ptr);
    }

    assert(helper_data.size() + aloof_nodes.size() + recovered_data.size() == static_cast<unsigned>(q * t));

    int r = repair_one_lost_chunk(recovered_data, aloof_nodes, helper_data, repair_blocksize, repair_sub_chunks_ind);

    // Clear buffers created for shortening
    for (int i = k; i < k + nu; i++) {
        helper_data[i].clear();
    }

    return r;
}

int ErasureCodeClay::repair_one_lost_chunk(std::map<int, BufferList> &recovered_data,
                                           std::set<int> &aloof_nodes,
                                           std::map<int, BufferList> &helper_data,
                                           int repair_blocksize,
                                           std::vector<std::pair<int, int>> &repair_sub_chunks_ind) {
    unsigned repair_subchunks = static_cast<unsigned>(sub_chunk_no) / q;
    unsigned sub_chunksize = repair_blocksize / repair_subchunks;

    int z_vec[t];
    std::map<int, std::set<int>> ordered_planes;
    std::map<int, int> repair_plane_to_ind;
    int count_retrieved_sub_chunks = 0;
    int plane_ind = 0;

    BufferList temp_buf(sub_chunksize, SIMD_ALIGN);
    temp_buf.zero();

    for (const auto& [index, count] : repair_sub_chunks_ind) {
        for (int j = index; j < index + count; j++) {
            get_plane_vector(j, z_vec);
            int order = 0;
            for (const auto& [node, bl] : recovered_data) {
                if (node % q == z_vec[node / q]) {
                    order++;
                }
            }
            for (auto node : aloof_nodes) {
                if (node % q == z_vec[node / q]) {
                    order++;
                }
            }
            assert(order > 0);
            ordered_planes[order].insert(j);
            repair_plane_to_ind[j] = plane_ind;
            plane_ind++;
        }
    }
    assert(static_cast<unsigned>(plane_ind) == repair_subchunks);

    for (int i = 0; i < q * t; i++) {
        if (U_buf[i].length() == 0) {
            BufferList buf(sub_chunk_no * sub_chunksize, SIMD_ALIGN);
            buf.zero();
            U_buf[i] = std::move(buf);
        }
    }

    int lost_chunk;
    int count = 0;
    for (const auto& [node, bl] : recovered_data) {
        lost_chunk = node;
        count++;
    }
    assert(count == 1);

    std::set<int> erasures;
    for (int i = 0; i < q; i++) {
        erasures.insert(lost_chunk - lost_chunk % q + i);
    }
    for (auto node : aloof_nodes) {
        erasures.insert(node);
    }

    for (int order = 1; ; order++) {
        if (ordered_planes.count(order) == 0) {
            break;
        }
        for (auto z : ordered_planes[order]) {
            get_plane_vector(z, z_vec);

            for (int y = 0; y < t; y++) {
                for (int x = 0; x < q; x++) {
                    int node_xy = y * q + x;
                    std::map<int, BufferList> known_subchunks;
                    std::map<int, BufferList> pftsubchunks;
                    std::set<int> pft_erasures;
                    if (erasures.count(node_xy) == 0) {
                        assert(helper_data.count(node_xy) > 0);
                        int z_sw = z + (x - z_vec[y]) * pow_int(q, t - 1 - y);
                        int node_sw = y * q + z_vec[y];
                        int i0 = 0, i1 = 1, i2 = 2, i3 = 3;
                        if (z_vec[y] > x) {
                            i0 = 1;
                            i1 = 0;
                            i2 = 3;
                            i3 = 2;
                        }
                        if (aloof_nodes.count(node_sw) > 0) {
                            assert(repair_plane_to_ind.count(z) > 0);
                            assert(repair_plane_to_ind.count(z_sw) > 0);
                            pft_erasures.insert(i2);
                            known_subchunks[i0].substr_of(helper_data[node_xy], repair_plane_to_ind[z] * sub_chunksize, sub_chunksize);
                            known_subchunks[i3].substr_of(U_buf[node_sw], z_sw * sub_chunksize, sub_chunksize);
                            pftsubchunks[i0] = known_subchunks[i0];
                            pftsubchunks[i1] = temp_buf;
                            pftsubchunks[i2].substr_of(U_buf[node_xy], z * sub_chunksize, sub_chunksize);
                            pftsubchunks[i3] = known_subchunks[i3];
                            for (int i = 0; i < 4; i++) {
                                if (pftsubchunks[i].length() > 0) {
                                    pftsubchunks[i].rebuild_aligned(SIMD_ALIGN);
                                }
                            }
                            pft.erasure_code->decode_chunks(pft_erasures, known_subchunks, &pftsubchunks);
                        } else {
                            assert(helper_data.count(node_sw) > 0);
                            assert(repair_plane_to_ind.count(z) > 0);
                            if (z_vec[y] != x) {
                                pft_erasures.insert(i2);
                                assert(repair_plane_to_ind.count(z_sw) > 0);
                                known_subchunks[i0].substr_of(helper_data[node_xy], repair_plane_to_ind[z] * sub_chunksize, sub_chunksize);
                                known_subchunks[i1].substr_of(helper_data[node_sw], repair_plane_to_ind[z_sw] * sub_chunksize, sub_chunksize);
                                pftsubchunks[i0] = known_subchunks[i0];
                                pftsubchunks[i1] = known_subchunks[i1];
                                pftsubchunks[i2].substr_of(U_buf[node_xy], z * sub_chunksize, sub_chunksize);
                                pftsubchunks[i3] = temp_buf;
                                for (int i = 0; i < 4; i++) {
                                    if (pftsubchunks[i].length() > 0) {
                                        pftsubchunks[i].rebuild_aligned(SIMD_ALIGN);
                                    }
                                }
                                pft.erasure_code->decode_chunks(pft_erasures, known_subchunks, &pftsubchunks);
                            } else {
                                char* uncoupled_chunk = U_buf[node_xy].c_str();
                                char* coupled_chunk = helper_data[node_xy].c_str();
                                memcpy(&uncoupled_chunk[z * sub_chunksize],
                                       &coupled_chunk[repair_plane_to_ind[z] * sub_chunksize],
                                       sub_chunksize);
                            }
                        }
                    }
                } // x
            } // y
            assert(erasures.size() <= static_cast<unsigned>(m));
            decode_uncoupled(erasures, z, sub_chunksize);

            for (auto i : erasures) {
                int x = i % q;
                int y = i / q;
                int node_sw = y * q + z_vec[y];
                int z_sw = z + (x - z_vec[y]) * pow_int(q, t - 1 - y);
                std::set<int> pft_erasures;
                std::map<int, BufferList> known_subchunks;
                std::map<int, BufferList> pftsubchunks;
                int i0 = 0, i1 = 1, i2 = 2, i3 = 3;
                if (z_vec[y] > x) {
                    i0 = 1;
                    i1 = 0;
                    i2 = 3;
                    i3 = 2;
                }
                if (aloof_nodes.count(i) == 0) {
                    if (x == z_vec[y]) { // hole-dot pair (type 0)
                        char* coupled_chunk = recovered_data[i].c_str();
                        char* uncoupled_chunk = U_buf[i].c_str();
                        memcpy(&coupled_chunk[z * sub_chunksize],
                               &uncoupled_chunk[z * sub_chunksize],
                               sub_chunksize);
                        count_retrieved_sub_chunks++;
                    } else {
                        assert(y == lost_chunk / q);
                        assert(node_sw == lost_chunk);
                        assert(helper_data.count(i) > 0);
                        pft_erasures.insert(i1);
                        known_subchunks[i0].substr_of(helper_data[i], repair_plane_to_ind[z] * sub_chunksize, sub_chunksize);
                        known_subchunks[i2].substr_of(U_buf[i], z * sub_chunksize, sub_chunksize);
                        pftsubchunks[i0] = known_subchunks[i0];
                        pftsubchunks[i1].substr_of(recovered_data[node_sw], z_sw * sub_chunksize, sub_chunksize);
                        pftsubchunks[i2] = known_subchunks[i2];
                        pftsubchunks[i3] = temp_buf;
                        for (int i = 0; i < 4; i++) {
                            if (pftsubchunks[i].length() > 0) {
                                pftsubchunks[i].rebuild_aligned(SIMD_ALIGN);
                            }
                        }
                        pft.erasure_code->decode_chunks(pft_erasures, known_subchunks, &pftsubchunks);
                    }
                }
            } // recover all erasures
        } // planes of particular order
    } // order

    return 0;
}

int ErasureCodeClay::decode_layered(std::set<int> &erased_chunks,
                                    std::map<int, BufferList> *chunks) {
    int num_erasures = erased_chunks.size();

    int size = (*chunks)[0].length();
    assert(size % sub_chunk_no == 0);
    int sc_size = size / sub_chunk_no;

    assert(num_erasures > 0);

    for (int i = k + nu; (num_erasures < m) && (i < q * t); i++) {
        if (erased_chunks.emplace(i).second) {
            num_erasures++;
        }
    }
    assert(num_erasures == m);

    int max_iscore = get_max_iscore(erased_chunks);
    std::vector<int> order(sub_chunk_no, 0);
    std::vector<int> z_vec(t);
    for (int i = 0; i < q * t; i++) {
        if (U_buf[i].length() == 0) {
            BufferList buf(size, SIMD_ALIGN);
            buf.zero();
            U_buf[i] = std::move(buf);
        }
    }

    set_planes_sequential_decoding_order(order.data(), erased_chunks);

    for (int iscore = 0; iscore <= max_iscore; iscore++) {
        for (int z = 0; z < sub_chunk_no; z++) {
            if (order[z] == iscore) {
                decode_erasures(erased_chunks, z, chunks, sc_size);
            }
        }

        for (int z = 0; z < sub_chunk_no; z++) {
            if (order[z] == iscore) {
                get_plane_vector(z, z_vec.data());
                for (auto node_xy : erased_chunks) {
                    int x = node_xy % q;
                    int y = node_xy / q;
                    int node_sw = y * q + z_vec[y];
                    if (z_vec[y] != x) {
                        if (erased_chunks.count(node_sw) == 0) {
                            recover_type1_erasure(chunks, x, y, z, z_vec.data(), sc_size);
                        } else if (z_vec[y] < x) {
                            assert(erased_chunks.count(node_sw) > 0);
                            assert(z_vec[y] != x);
                            get_coupled_from_uncoupled(chunks, x, y, z, z_vec.data(), sc_size);
                        }
                    } else {
                        char* C = (*chunks)[node_xy].c_str();
                        char* U = U_buf[node_xy].c_str();
                        memcpy(&C[z * sc_size], &U[z * sc_size], sc_size);
                    }
                }
            }
        } // plane
    } // iscore, order

    return 0;
}

int ErasureCodeClay::decode_erasures(const std::set<int> &erased_chunks, int z,
                                     std::map<int, BufferList> *chunks, int sc_size) {
    std::vector<int> z_vec(t);

    get_plane_vector(z, z_vec.data());

    for (int x = 0; x < q; x++) {
        for (int y = 0; y < t; y++) {
            int node_xy = q * y + x;
            int node_sw = q * y + z_vec[y];
            if (erased_chunks.count(node_xy) == 0) {
                if (z_vec[y] < x) {
                    get_uncoupled_from_coupled(chunks, x, y, z, z_vec.data(), sc_size);
                } else if (z_vec[y] == x) {
                    char* uncoupled_chunk = U_buf[node_xy].c_str();
                    char* coupled_chunk = (*chunks)[node_xy].c_str();
                    memcpy(&uncoupled_chunk[z * sc_size], &coupled_chunk[z * sc_size], sc_size);
                } else {
                    if (erased_chunks.count(node_sw) > 0) {
                        get_uncoupled_from_coupled(chunks, x, y, z, z_vec.data(), sc_size);
                    }
                }
            }
        }
    }
    return decode_uncoupled(erased_chunks, z, sc_size);
}

int ErasureCodeClay::decode_uncoupled(const std::set<int> &erased_chunks, int z, int sc_size) {
    std::map<int, BufferList> known_subchunks;
    std::map<int, BufferList> all_subchunks;

    for (int i = 0; i < q * t; i++) {
        if (erased_chunks.count(i) == 0) {
            known_subchunks[i].substr_of(U_buf[i], z * sc_size, sc_size);
            all_subchunks[i] = known_subchunks[i];
        } else {
            all_subchunks[i].substr_of(U_buf[i], z * sc_size, sc_size);
        }
        all_subchunks[i].rebuild_aligned_size_and_memory(sc_size, SIMD_ALIGN);
        assert(all_subchunks[i].is_contiguous());
    }

    return mds.erasure_code->decode_chunks(erased_chunks, known_subchunks, &all_subchunks);
}

void ErasureCodeClay::set_planes_sequential_decoding_order(int* order, std::set<int>& erasures) {
    std::vector<int> z_vec(t);
    for (int z = 0; z < sub_chunk_no; z++) {
        get_plane_vector(z, z_vec.data());
        order[z] = 0;
        for (auto i : erasures) {
            if (i % q == z_vec[i / q]) {
                order[z]++;
            }
        }
    }
}

void ErasureCodeClay::recover_type1_erasure(std::map<int, BufferList> *chunks,
                                            int x, int y, int z,
                                            int *z_vec, int sc_size) {
    std::set<int> erased_chunks;

    int node_xy = y * q + x;
    int node_sw = y * q + z_vec[y];
    int z_sw = z + (x - z_vec[y]) * pow_int(q, t - 1 - y);

    std::map<int, BufferList> known_subchunks;
    std::map<int, BufferList> pftsubchunks;
    BufferList ptr(sc_size, SIMD_ALIGN);
    ptr.zero();

    int i0 = 0, i1 = 1, i2 = 2, i3 = 3;
    if (z_vec[y] > x) {
        i0 = 1;
        i1 = 0;
        i2 = 3;
        i3 = 2;
    }

    erased_chunks.insert(i0);
    pftsubchunks[i0].substr_of((*chunks)[node_xy], z * sc_size, sc_size);
    known_subchunks[i1].substr_of((*chunks)[node_sw], z_sw * sc_size, sc_size);
    known_subchunks[i2].substr_of(U_buf[node_xy], z * sc_size, sc_size);
    pftsubchunks[i1] = known_subchunks[i1];
    pftsubchunks[i2] = known_subchunks[i2];
    pftsubchunks[i3] = ptr;

    for (int i = 0; i < 4; i++) {
        if (pftsubchunks[i].length() > 0) {
            pftsubchunks[i].rebuild_aligned_size_and_memory(sc_size, SIMD_ALIGN);
        }
    }

    pft.erasure_code->decode_chunks(erased_chunks, known_subchunks, &pftsubchunks);
}

void ErasureCodeClay::get_coupled_from_uncoupled(std::map<int, BufferList> *chunks,
                                                 int x, int y, int z,
                                                 int *z_vec, int sc_size) {
    std::set<int> erased_chunks = {0, 1};

    int node_xy = y * q + x;
    int node_sw = y * q + z_vec[y];
    int z_sw = z + (x - z_vec[y]) * pow_int(q, t - 1 - y);

    assert(z_vec[y] < x);
    std::map<int, BufferList> uncoupled_subchunks;
    uncoupled_subchunks[2].substr_of(U_buf[node_xy], z * sc_size, sc_size);
    uncoupled_subchunks[3].substr_of(U_buf[node_sw], z_sw * sc_size, sc_size);

    std::map<int, BufferList> pftsubchunks;
    pftsubchunks[0].substr_of((*chunks)[node_xy], z * sc_size, sc_size);
    pftsubchunks[1].substr_of((*chunks)[node_sw], z_sw * sc_size, sc_size);
    pftsubchunks[2] = uncoupled_subchunks[2];
    pftsubchunks[3] = uncoupled_subchunks[3];

    for (int i = 0; i < 4; i++) {
        if (pftsubchunks[i].length() > 0) {
            pftsubchunks[i].rebuild_aligned_size_and_memory(sc_size, SIMD_ALIGN);
        }
    }
    pft.erasure_code->decode_chunks(erased_chunks, uncoupled_subchunks, &pftsubchunks);
}

void ErasureCodeClay::get_uncoupled_from_coupled(std::map<int, BufferList> *chunks,
                                                 int x, int y, int z,
                                                 int *z_vec, int sc_size) {
    std::set<int> erased_chunks = {2, 3};

    int node_xy = y * q + x;
    int node_sw = y * q + z_vec[y];
    int z_sw = z + (x - z_vec[y]) * pow_int(q, t - 1 - y);

    int i0 = 0, i1 = 1, i2 = 2, i3 = 3;
    if (z_vec[y] > x) {
        i0 = 1;
        i1 = 0;
        i2 = 3;
        i3 = 2;
    }
    std::map<int, BufferList> coupled_subchunks;
    coupled_subchunks[i0].substr_of((*chunks)[node_xy], z * sc_size, sc_size);
    coupled_subchunks[i1].substr_of((*chunks)[node_sw], z_sw * sc_size, sc_size);

    std::map<int, BufferList> pftsubchunks;
    pftsubchunks[0] = coupled_subchunks[0];
    pftsubchunks[1] = coupled_subchunks[1];
    pftsubchunks[i2].substr_of(U_buf[node_xy], z * sc_size, sc_size);
    pftsubchunks[i3].substr_of(U_buf[node_sw], z_sw * sc_size, sc_size);
    for (int i = 0; i < 4; i++) {
        if (pftsubchunks[i].length() > 0) {
            pftsubchunks[i].rebuild_aligned_size_and_memory(sc_size, SIMD_ALIGN);
        }
    }
    pft.erasure_code->decode_chunks(erased_chunks, coupled_subchunks, &pftsubchunks);
}

int ErasureCodeClay::get_max_iscore(std::set<int> &erased_chunks) {
    std::vector<int> weight_vec(t, 0);
    int iscore = 0;

    for (auto i : erased_chunks) {
        if (weight_vec[i / q] == 0) {
            weight_vec[i / q] = 1;
            iscore++;
        }
    }
    return iscore;
}

void ErasureCodeClay::get_plane_vector(int z, int *z_vec) {
    for (int i = 0; i < t; i++) {
        z_vec[t - 1 - i] = z % q;
        z = (z - z_vec[t - 1 - i]) / q;
    }
}

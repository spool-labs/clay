#ifndef CLAY_H
#define CLAY_H

#include <clay/Buffer.h>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>

namespace clay {

struct ClayParams {
    int k = 4;          ///< number of data chunks
    int m = 2;          ///< number of coding chunks  
    int d = 5;          ///< repair parameter, k <= d <= k+m-1
    int w = 8;          ///< sub packetization parameter
    bool is_valid() const;
    std::string to_string() const;
    ClayParams() = default;

    ClayParams(int k_, int m_, int d_, int w_ = 8) : k(k_), m(m_), d(d_), w(w_) {}
};

enum class ClayResult {
    SUCCESS = 0,           ///< executed successfully
    INVALID_PARAMS,        ///< invalid Clay parameters
    INSUFFICIENT_CHUNKS,   ///< not enough chunks for decoding/repair
    DECODE_FAILED,         ///< decoding failed
    ENCODE_FAILED,         ///< encoding failed
    REPAIR_FAILED,         ///< repair failed
    MEMORY_ERROR,          ///< memory allocation error
    INTERNAL_ERROR         ///< internal library error
};

class ClayCode {
public:
    explicit ClayCode(const ClayParams& params);
    
    ~ClayCode();

    // non copyable, moveable
    ClayCode(const ClayCode&) = delete;
    ClayCode& operator=(const ClayCode&) = delete;
    ClayCode(ClayCode&&) noexcept;
    ClayCode& operator=(ClayCode&&) noexcept;

    const ClayParams& params() const;

    size_t chunk_size(size_t data_size) const;

    ClayResult encode(const Buffer& data, std::vector<Buffer>& chunks);

    ClayResult decode(const std::map<int, Buffer>& available_chunks, Buffer& decoded_data);

    ClayResult repair(const std::set<int>& failed_chunks,
                      const std::map<int, Buffer>& available_chunks,
                      std::map<int, Buffer>& repaired_chunks);
    int min_chunks_to_decode() const;
    int total_chunks() const;
    bool is_valid() const;
    const std::string& last_error() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;  ///< private implementation, pimpl idiom
};

namespace util {
    std::string result_to_string(ClayResult result);
    ClayParams make_params(int k, int m, int d = -1);
    bool validate_params(const ClayParams& params, std::string* error = nullptr);
    int calculate_optimal_w(int k, int m);
    std::string get_params_info(const ClayParams& params);

} 

}

#endif 
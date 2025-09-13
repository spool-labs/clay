#include <clay/clay.h>
#include <clay/ErasureCodeClay.h>
#include <clay/ErasureCodeInterface.h>
#include <clay/ErasureCodeProfile.h>
#include <clay/BufferList.h>
#include <stdexcept>
#include <sstream>
#include <memory>

namespace clay {

struct ClayOperationResult {
    bool success = false;
    std::string error_message;
    
    ClayOperationResult() = default;
    ClayOperationResult(bool s) : success(s) {}
    ClayOperationResult(bool s, const std::string& msg) : success(s), error_message(msg) {}
    
    static ClayOperationResult Success() { return ClayOperationResult(true); }
    static ClayOperationResult Error(const std::string& msg) { return ClayOperationResult(false, msg); }

    ClayResult to_enum() const {
        return success ? ClayResult::SUCCESS : ClayResult::INTERNAL_ERROR;
    }
};


class ClayCode::Impl {
public:
    explicit Impl(const ClayParams& params);
    ~Impl() = default;

    ClayResult encode(const Buffer& data, std::vector<Buffer>& chunks);
    ClayResult decode(const std::map<int, Buffer>& available_chunks, Buffer& decoded_data);
    ClayResult repair(const std::set<int>& failed_chunks,
                      const std::map<int, Buffer>& available_chunks,
                      std::map<int, Buffer>& repaired_chunks);

    size_t chunk_size(size_t data_size) const;
    const ClayParams& params() const { return params_; }
    const std::string& last_error() const { return last_error_; }
    
    int total_chunks() const { return params_.k + params_.m; }
    int min_chunks_to_decode() const { return params_.k; }

private:
    ClayParams params_;
    std::unique_ptr<ErasureCodeClay> clay_impl_;
    mutable std::string last_error_;

    void set_error(const std::string& error) const { last_error_ = error; }

    void buffer_to_bufferlist(const Buffer& buffer, BufferList& bl) const;
    void bufferlist_to_buffer(const BufferList& bl, Buffer& buffer) const;
};

// ClayCode::Impl 

ClayCode::Impl::Impl(const ClayParams& params) : params_(params) {
    if (!params_.is_valid()) {
        throw std::invalid_argument("Invalid Clay parameters: " + params_.to_string());
    }

    clay_impl_ = std::make_unique<ErasureCodeClay>();

    ErasureCodeProfile profile;
    profile["k"] = std::to_string(params_.k);
    profile["m"] = std::to_string(params_.m);
    profile["d"] = std::to_string(params_.d);
    profile["jerasure-per-chunk-alignment"] = "false";

    int init_result = clay_impl_->init(profile, nullptr);
    if (init_result != 0) {
        throw std::runtime_error("Failed to initialize Clay with parameters: " + params_.to_string());
    }
}

void ClayCode::Impl::buffer_to_bufferlist(const Buffer& buffer, BufferList& bl) const {
    bl.clear();
    if (!buffer.empty()) {
        bl.append(buffer.c_str(), buffer.size());
    }
}

void ClayCode::Impl::bufferlist_to_buffer(const BufferList& bl, Buffer& buffer) const {
    buffer.assign(bl.c_str(), bl.length());
}

size_t ClayCode::Impl::chunk_size(size_t data_size) const {
    // here we divide data among k chunks, round up
    return (data_size + params_.k - 1) / params_.k;
}

ClayResult ClayCode::Impl::encode(const Buffer& data, std::vector<Buffer>& chunks) {
    try {
        last_error_.clear();
        
        if (data.empty()) {
            set_error("Input data is empty");
            return ClayResult::INVALID_PARAMS;
        }

        BufferList input_bl;
        buffer_to_bufferlist(data, input_bl);

        std::set<int> want_to_encode;
        for (int i = 0; i < total_chunks(); ++i) {
            want_to_encode.insert(i);
        }

        std::map<int, BufferList> encoded_bls;
        int result = clay_impl_->encode(want_to_encode, input_bl, &encoded_bls);
        
        if (result != 0) {
            set_error("Clay encoding failed with code: " + std::to_string(result));
            return ClayResult::ENCODE_FAILED;
        }

        chunks.clear();
        chunks.resize(total_chunks());
        
        for (const auto& pair : encoded_bls) {
            int index = pair.first;
            const BufferList& bl = pair.second;
            if (index >= 0 && index < total_chunks()) {
                bufferlist_to_buffer(bl, chunks[index]);
            }
        }

        return ClayResult::SUCCESS;

    } catch (const std::exception& e) {
        set_error("Exception during encoding: " + std::string(e.what()));
        return ClayResult::INTERNAL_ERROR;
    }
}

ClayResult ClayCode::Impl::decode(const std::map<int, Buffer>& available_chunks, Buffer& decoded_data) {
    try {
        last_error_.clear();
        
        if (available_chunks.size() < static_cast<size_t>(min_chunks_to_decode())) {
            set_error("Insufficient chunks for decoding");
            return ClayResult::INSUFFICIENT_CHUNKS;
        }

        std::map<int, BufferList> available_bls;
        for (const auto& pair : available_chunks) {
            buffer_to_bufferlist(pair.second, available_bls[pair.first]);
        }

        std::set<int> want_to_read;
        for (const auto& pair : available_chunks) {
            want_to_read.insert(pair.first);
        }
        std::map<int, BufferList> decoded_bls;
        int result = clay_impl_->decode(want_to_read, available_bls, &decoded_bls, 0);
        
        if (result != 0) {
            set_error("Clay decoding failed with code: " + std::to_string(result));
            return ClayResult::DECODE_FAILED;
        }

        if (!decoded_bls.empty()) {
            bufferlist_to_buffer(decoded_bls.begin()->second, decoded_data);
        }
        
        return ClayResult::SUCCESS;

    } catch (const std::exception& e) {
        set_error("Exception during decoding: " + std::string(e.what()));
        return ClayResult::INTERNAL_ERROR;
    }
}

ClayResult ClayCode::Impl::repair(const std::set<int>& failed_chunks,
                                  const std::map<int, Buffer>& available_chunks,
                                  std::map<int, Buffer>& repaired_chunks) {
    try {
        last_error_.clear();

        std::map<int, BufferList> available_bls;
        for (const auto& pair : available_chunks) {
            buffer_to_bufferlist(pair.second, available_bls[pair.first]);
        }
        std::set<int> want_to_read;
        for (int chunk : failed_chunks) {
            want_to_read.insert(chunk);
        }

        std::map<int, BufferList> repaired_bls;
        int result = clay_impl_->decode(want_to_read, available_bls, &repaired_bls, 0);
        
        if (result != 0) {
            set_error("Clay repair failed with code: " + std::to_string(result));
            return ClayResult::REPAIR_FAILED;
        }

        // here we convert repaired BufferLists back to Buffers
        repaired_chunks.clear();
        for (const auto& pair : repaired_bls) {
            bufferlist_to_buffer(pair.second, repaired_chunks[pair.first]);
        }
        
        return ClayResult::SUCCESS;

    } catch (const std::exception& e) {
        set_error("Exception during repair: " + std::string(e.what()));
        return ClayResult::INTERNAL_ERROR;
    }
}

// ClayCode public interface 

ClayCode::ClayCode(const ClayParams& params) 
    : pimpl_(std::make_unique<Impl>(params)) {
}

ClayCode::~ClayCode() = default;

ClayCode::ClayCode(ClayCode&&) noexcept = default;
ClayCode& ClayCode::operator=(ClayCode&&) noexcept = default;

const ClayParams& ClayCode::params() const {
    return pimpl_->params();
}

size_t ClayCode::chunk_size(size_t data_size) const {
    return pimpl_->chunk_size(data_size);
}

ClayResult ClayCode::encode(const Buffer& data, std::vector<Buffer>& chunks) {
    return pimpl_->encode(data, chunks);
}

ClayResult ClayCode::decode(const std::map<int, Buffer>& available_chunks, Buffer& decoded_data) {
    return pimpl_->decode(available_chunks, decoded_data);
}

ClayResult ClayCode::repair(const std::set<int>& failed_chunks,
                            const std::map<int, Buffer>& available_chunks,
                            std::map<int, Buffer>& repaired_chunks) {
    return pimpl_->repair(failed_chunks, available_chunks, repaired_chunks);
}

int ClayCode::min_chunks_to_decode() const {
    return pimpl_->min_chunks_to_decode();
}

int ClayCode::total_chunks() const {
    return pimpl_->total_chunks();
}

bool ClayCode::is_valid() const {
    return params().is_valid();
}

const std::string& ClayCode::last_error() const {
    return pimpl_->last_error();
}

} 

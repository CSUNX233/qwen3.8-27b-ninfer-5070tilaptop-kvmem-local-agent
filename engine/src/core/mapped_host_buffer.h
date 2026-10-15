// Modified in the CSUNX233 RTX 5070 Ti Laptop distribution. See ../docs/MODIFICATIONS.md.
#pragma once

#include <cstddef>
#include <cuda_runtime_api.h>

namespace ninfer {

// Owning, pinned system-memory allocation with a stable CUDA device alias.
// Kernels may use device_data(); the CPU may use host_data() only after ordering
// prior GPU accesses. The allocation must outlive every graph referring to it.
// This is explicit host storage, independent of automatic device-memory fallback.
class MappedHostBuffer {
public:
    explicit MappedHostBuffer(std::size_t size_bytes);
    ~MappedHostBuffer();
    MappedHostBuffer(const MappedHostBuffer&) = delete;
    MappedHostBuffer& operator=(const MappedHostBuffer&) = delete;

    [[nodiscard]] void* host_data() const noexcept { return host_; }
    [[nodiscard]] void* device_data() const noexcept { return device_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_; }
    // Stream-ordered upload from pinned storage; valid during graph capture.
    [[nodiscard]] cudaError_t copy_to_device_async(void* destination, std::size_t count,
                                                  std::size_t offset, cudaStream_t stream) const;

private:
    void* host_ = nullptr;
    void* device_ = nullptr;
    std::size_t bytes_ = 0;
};

} // namespace ninfer

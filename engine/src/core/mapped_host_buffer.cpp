// Modified in the CSUNX233 RTX 5070 Ti Laptop distribution. See ../docs/MODIFICATIONS.md.
#include "core/mapped_host_buffer.h"
#include "core/device.h"

namespace ninfer {

MappedHostBuffer::MappedHostBuffer(std::size_t size_bytes) : bytes_(size_bytes) {
    if (bytes_ == 0) { return; }
    CUDA_CHECK(cudaHostAlloc(&host_, bytes_, cudaHostAllocMapped));
    const cudaError_t status = cudaHostGetDevicePointer(&device_, host_, 0);
    if (status != cudaSuccess) {
        (void)cudaFreeHost(host_);
        host_ = nullptr;
        CUDA_CHECK(status);
    }
}

MappedHostBuffer::~MappedHostBuffer() {
    // As for DeviceBuffer, the caller must complete outstanding accesses before
    // releasing storage; graph ownership remains with its execution owner.
    if (host_ != nullptr) { (void)cudaFreeHost(host_); }
}

cudaError_t MappedHostBuffer::copy_to_device_async(void* destination, std::size_t count,
                                                 std::size_t offset, cudaStream_t stream) const {
    if (offset > bytes_ || count > bytes_ - offset) { return cudaErrorInvalidValue; }
    if (count == 0) { return cudaSuccess; }
    return cudaMemcpyAsync(destination, static_cast<const char*>(host_) + offset,
                           count, cudaMemcpyHostToDevice, stream);
}

} // namespace ninfer

#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <cstring>

struct Buffer {
    std::vector<uint8_t> data;
    
    Buffer() = default;
    explicit Buffer(size_t size) : data(size) {}
    explicit Buffer(const std::vector<uint8_t>& d) : data(d) {}
    explicit Buffer(std::vector<uint8_t>&& d) : data(std::move(d)) {}
    
    size_t size() const { return data.size(); }
    uint8_t* get() { return data.data(); }
    const uint8_t* get() const { return data.data(); }
    bool empty() const { return data.empty(); }
};

using BufferPtr = std::shared_ptr<Buffer>;

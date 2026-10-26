// src/tools/tool_registry.cpp — hash table from tool name to Tool (spec §4.3).
//
// Collision strategy: separate chaining, one std::forward_list per bucket.
// Chaining degrades gracefully as the table fills and never needs the
// "no empty slot left" care that open addressing does, which matters here
// because P3 never removes a tool.
//
// Hash function: FNV-1a (Appendix A), which uses every character of the key
// and is order-sensitive, unlike a plain sum of characters.
//
// Resize policy: double the bucket count whenever the NEXT insert would push
// load_factor() above 0.75, then reinsert every tool at its new index. The
// check happens before inserting (see the pitfall in spec §5.3): inserting
// first and resizing after would let load_factor() briefly exceed 1.0.

#include "tools/tool_registry.h"

#include <cstdint>
#include <string>
#include <utility>

namespace {
constexpr std::size_t kInitialBucketCount = 8;      // power of two
constexpr double      kMaxLoadFactor      = 0.75;   // typical for chaining
}  // namespace

ToolRegistry::ToolRegistry() : buckets_(kInitialBucketCount) {}

ToolRegistry::~ToolRegistry() = default;   // forward_list<unique_ptr<Tool>> frees everything

std::size_t ToolRegistry::hash(std::string_view key) noexcept {
    std::uint64_t h = 1469598103934665603ULL;        // FNV-1a 64-bit offset basis
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;                       // FNV prime
    }
    return static_cast<std::size_t>(h);
}

void ToolRegistry::rehash(std::size_t new_bucket_count) {
    std::vector<std::forward_list<std::unique_ptr<Tool>>> new_buckets(new_bucket_count);
    for (auto& chain : buckets_) {
        for (auto& tool : chain) {
            std::size_t index = hash(tool->name()) & (new_bucket_count - 1);
            new_buckets[index].push_front(std::move(tool));
        }
    }
    buckets_ = std::move(new_buckets);
}

bool ToolRegistry::add(std::unique_ptr<Tool> tool) {
    if (tool == nullptr) return false;

    const std::string name = tool->name();
    if (find(name) != nullptr) return false;   // duplicate: `tool` is destroyed on return

    // Check the load factor BEFORE inserting, so it never exceeds the
    // threshold once the new entry goes in.
    if (static_cast<double>(size_ + 1) > kMaxLoadFactor * static_cast<double>(buckets_.size()))
        rehash(buckets_.size() * 2);

    const std::size_t index = hash(name) & (buckets_.size() - 1);
    buckets_[index].push_front(std::move(tool));
    ++size_;
    return true;
}

Tool* ToolRegistry::find(std::string_view name) const {
    const std::size_t index = hash(name) & (buckets_.size() - 1);
    for (const auto& tool : buckets_[index]) {
        if (tool->name() == name) return tool.get();
    }
    return nullptr;
}

std::size_t ToolRegistry::size() const noexcept { return size_; }

void ToolRegistry::for_each(const std::function<void(const Tool&)>& fn) const {
    for (const auto& chain : buckets_) {
        for (const auto& tool : chain) {
            fn(*tool);
        }
    }
}

std::size_t ToolRegistry::bucket_count() const noexcept { return buckets_.size(); }

double ToolRegistry::load_factor() const noexcept {
    return static_cast<double>(size_) / static_cast<double>(buckets_.size());
}

std::size_t ToolRegistry::max_probe_length() const {
    std::size_t longest = 0;
    for (const auto& chain : buckets_) {
        std::size_t length = 0;
        for (const auto& tool : chain) {
            (void)tool;
            ++length;
        }
        if (length > longest) longest = length;
    }
    return longest;
}

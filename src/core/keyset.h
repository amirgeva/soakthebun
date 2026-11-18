#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace soak {

// Open-addressing hash set for non-zero 64-bit keys (0 marks an empty slot).
// Much faster than std::unordered_set for the millions of lookups a Sokoban
// state search performs.
class KeySet {
public:
    explicit KeySet(size_t expected = 1024) { reset(expected); }

    void reset(size_t expected) {
        size_t cap = 1024;
        while (cap < expected * 2) cap <<= 1;
        table_.assign(cap, 0);
        mask_ = cap - 1;
        size_ = 0;
    }

    // Returns true if the key was newly inserted.
    bool insert(uint64_t key) {
        if ((size_ + 1) * 2 > table_.size()) grow();
        size_t i = size_t(hash(key)) & mask_;
        while (table_[i] != 0) {
            if (table_[i] == key) return false;
            i = (i + 1) & mask_;
        }
        table_[i] = key;
        ++size_;
        return true;
    }

    size_t size() const { return size_; }

private:
    static uint64_t hash(uint64_t x) {
        x ^= x >> 33;
        x *= 0xFF51AFD7ED558CCDull;
        x ^= x >> 33;
        x *= 0xC4CEB9FE1A85EC53ull;
        x ^= x >> 33;
        return x;
    }

    void grow() {
        std::vector<uint64_t> old;
        old.swap(table_);
        table_.assign(old.size() * 2, 0);
        mask_ = table_.size() - 1;
        for (uint64_t k : old) {
            if (!k) continue;
            size_t i = size_t(hash(k)) & mask_;
            while (table_[i] != 0) i = (i + 1) & mask_;
            table_[i] = k;
        }
    }

    std::vector<uint64_t> table_;
    size_t mask_ = 0;
    size_t size_ = 0;
};

}  // namespace soak

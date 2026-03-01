#pragma once

#include "information_set.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace poker {

class InfoSetStore {
   public:
    explicit InfoSetStore(int num_shards = 256);

    InfoSetData& get_or_create(InfoSetKey key, int num_actions);
    const InfoSetData* find(InfoSetKey key) const;

    size_t size() const;
    size_t memory_bytes() const;

    void save(const std::string& path) const;
    void load(const std::string& path);

    void apply_discounting(float positive_discount, float negative_discount,
                           float strategy_discount);

    void clear();
    void reserve(size_t total_entries);

   private:
    static constexpr uint32_t EMPTY_SLOT = UINT32_MAX;
    static constexpr size_t CHUNK_SHIFT = 12;  // 4K entries per chunk
    static constexpr size_t CHUNK_SIZE = size_t(1) << CHUNK_SHIFT;
    static constexpr size_t CHUNK_MASK = CHUNK_SIZE - 1;
    static constexpr float MAX_LOAD_FACTOR = 0.7f;

    struct Entry {
        InfoSetKey key;
        InfoSetData data;
    };

    struct Shard {
        mutable std::shared_mutex mutex;

        // Open-addressing index table (hash slot → entry index)
        std::vector<uint32_t> table;
        size_t table_mask = 0;

        // Chunked pool for stable references (entries never move)
        std::vector<std::unique_ptr<Entry[]>> chunks;
        size_t count = 0;

        Entry& entry_at(uint32_t idx) { return chunks[idx >> CHUNK_SHIFT][idx & CHUNK_MASK]; }
        const Entry& entry_at(uint32_t idx) const {
            return chunks[idx >> CHUNK_SHIFT][idx & CHUNK_MASK];
        }

        uint32_t alloc_entry() {
            uint32_t idx = static_cast<uint32_t>(count);
            if ((count >> CHUNK_SHIFT) >= chunks.size()) {
                chunks.push_back(std::make_unique<Entry[]>(CHUNK_SIZE));
            }
            count++;
            return idx;
        }

        void grow_table();
    };

    std::vector<Shard> shards_;
    int num_shards_;

    int shard_index(InfoSetKey key) const {
        return static_cast<int>(key % static_cast<uint64_t>(num_shards_));
    }
};

}  // namespace poker

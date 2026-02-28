#pragma once

#include "information_set.h"
#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <string>
#include <cstddef>

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

    // DCFR discounting: multiply regrets and strategy sums
    void apply_discounting(float positive_discount, float negative_discount,
                           float strategy_discount);

    // Clear all data
    void clear();

   private:
    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<InfoSetKey, InfoSetData> data;
    };

    std::vector<Shard> shards_;
    int num_shards_;

    int shard_index(InfoSetKey key) const {
        return static_cast<int>(key % static_cast<uint64_t>(num_shards_));
    }
};

}  // namespace poker

#include "infoset_store.h"
#include "utils.h"
#include <fstream>
#include <algorithm>

namespace poker {

InfoSetStore::InfoSetStore(int num_shards)
    : shards_(num_shards), num_shards_(num_shards) {}

InfoSetData& InfoSetStore::get_or_create(InfoSetKey key, int num_actions) {
    int idx = shard_index(key);
    auto& shard = shards_[idx];

    // Try read lock first
    {
        std::shared_lock lock(shard.mutex);
        auto it = shard.data.find(key);
        if (it != shard.data.end()) {
            return it->second;
        }
    }

    // Upgrade to write lock
    std::unique_lock lock(shard.mutex);
    auto [it, inserted] = shard.data.try_emplace(key, InfoSetData(num_actions));
    return it->second;
}

const InfoSetData* InfoSetStore::find(InfoSetKey key) const {
    int idx = shard_index(key);
    const auto& shard = shards_[idx];
    std::shared_lock lock(shard.mutex);
    auto it = shard.data.find(key);
    if (it != shard.data.end()) {
        return &it->second;
    }
    return nullptr;
}

size_t InfoSetStore::size() const {
    size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        total += shard.data.size();
    }
    return total;
}

size_t InfoSetStore::memory_bytes() const {
    // Approximate: each entry is ~sizeof(InfoSetData) + hash overhead
    return size() * (sizeof(InfoSetData) + sizeof(InfoSetKey) + 64);
}

void InfoSetStore::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        log_error("Failed to open file for writing: " + path);
        return;
    }

    uint64_t total_entries = size();
    write_binary(out, total_entries);

    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        for (const auto& [key, data] : shard.data) {
            write_binary(out, key);
            write_binary(out, data.num_actions);
            for (int a = 0; a < data.num_actions; ++a) {
                write_binary(out, data.cumulative_regret[a]);
            }
            for (int a = 0; a < data.num_actions; ++a) {
                write_binary(out, data.strategy_sum[a]);
            }
        }
    }

    log_info("Saved " + std::to_string(total_entries) + " info sets to " + path);
}

void InfoSetStore::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        log_error("Failed to open file for reading: " + path);
        return;
    }

    clear();

    uint64_t total_entries;
    read_binary(in, total_entries);

    for (uint64_t i = 0; i < total_entries; ++i) {
        InfoSetKey key;
        uint8_t num_actions;
        read_binary(in, key);
        read_binary(in, num_actions);

        auto& data = get_or_create(key, num_actions);
        for (int a = 0; a < num_actions; ++a) {
            read_binary(in, data.cumulative_regret[a]);
        }
        for (int a = 0; a < num_actions; ++a) {
            read_binary(in, data.strategy_sum[a]);
        }
    }

    log_info("Loaded " + std::to_string(total_entries) +
             " info sets from " + path);
}

void InfoSetStore::apply_discounting(float positive_discount,
                                      float negative_discount,
                                      float strategy_discount) {
    for (auto& shard : shards_) {
        std::unique_lock lock(shard.mutex);
        for (auto& [key, data] : shard.data) {
            for (int a = 0; a < data.num_actions; ++a) {
                if (data.cumulative_regret[a] > 0) {
                    data.cumulative_regret[a] *= positive_discount;
                } else {
                    data.cumulative_regret[a] *= negative_discount;
                }
                data.strategy_sum[a] *= strategy_discount;
            }
        }
    }
}

void InfoSetStore::clear() {
    for (auto& shard : shards_) {
        std::unique_lock lock(shard.mutex);
        shard.data.clear();
    }
}

} // namespace poker

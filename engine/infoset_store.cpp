#include "infoset_store.h"
#include "utils.h"
#include <fstream>

namespace poker {

InfoSetStore::InfoSetStore(int num_shards) : shards_(num_shards), num_shards_(num_shards) {
    for (auto& shard : shards_) {
        size_t initial_capacity = 1024;
        shard.table.assign(initial_capacity, EMPTY_SLOT);
        shard.table_mask = initial_capacity - 1;
    }
}

InfoSetData& InfoSetStore::get_or_create(InfoSetKey key, int num_actions) {
    int idx = shard_index(key);
    auto& shard = shards_[idx];

    // Try read lock first (fast path for existing entries)
    {
        std::shared_lock lock(shard.mutex);
        size_t slot = static_cast<size_t>(key) & shard.table_mask;
        while (true) {
            uint32_t entry_idx = shard.table[slot];
            if (entry_idx == EMPTY_SLOT)
                break;
            if (shard.entry_at(entry_idx).key == key) {
                return shard.entry_at(entry_idx).data;
            }
            slot = (slot + 1) & shard.table_mask;
        }
    }

    // Upgrade to write lock
    std::unique_lock lock(shard.mutex);

    // Double-check after acquiring write lock
    size_t slot = static_cast<size_t>(key) & shard.table_mask;
    while (true) {
        uint32_t entry_idx = shard.table[slot];
        if (entry_idx == EMPTY_SLOT)
            break;
        if (shard.entry_at(entry_idx).key == key) {
            return shard.entry_at(entry_idx).data;
        }
        slot = (slot + 1) & shard.table_mask;
    }

    // Grow table if needed
    if (shard.count >= static_cast<size_t>(shard.table.size() * MAX_LOAD_FACTOR)) {
        shard.grow_table();
        // Re-find slot in resized table
        slot = static_cast<size_t>(key) & shard.table_mask;
        while (shard.table[slot] != EMPTY_SLOT) {
            slot = (slot + 1) & shard.table_mask;
        }
    }

    // Allocate and insert
    uint32_t new_idx = shard.alloc_entry();
    auto& entry = shard.entry_at(new_idx);
    entry.key = key;
    entry.data = InfoSetData(num_actions);
    shard.table[slot] = new_idx;

    return entry.data;
}

void InfoSetStore::Shard::grow_table() {
    size_t new_capacity = table.size() * 2;
    std::vector<uint32_t> new_table(new_capacity, EMPTY_SLOT);
    size_t new_mask = new_capacity - 1;

    for (size_t i = 0; i < table.size(); ++i) {
        uint32_t entry_idx = table[i];
        if (entry_idx == EMPTY_SLOT)
            continue;

        size_t slot = static_cast<size_t>(entry_at(entry_idx).key) & new_mask;
        while (new_table[slot] != EMPTY_SLOT) {
            slot = (slot + 1) & new_mask;
        }
        new_table[slot] = entry_idx;
    }

    table = std::move(new_table);
    table_mask = new_mask;
}

const InfoSetData* InfoSetStore::find(InfoSetKey key) const {
    int idx = shard_index(key);
    const auto& shard = shards_[idx];
    std::shared_lock lock(shard.mutex);

    size_t slot = static_cast<size_t>(key) & shard.table_mask;
    while (true) {
        uint32_t entry_idx = shard.table[slot];
        if (entry_idx == EMPTY_SLOT)
            return nullptr;
        if (shard.entry_at(entry_idx).key == key) {
            return &shard.entry_at(entry_idx).data;
        }
        slot = (slot + 1) & shard.table_mask;
    }
}

size_t InfoSetStore::size() const {
    size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        total += shard.count;
    }
    return total;
}

size_t InfoSetStore::memory_bytes() const {
    size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        // Index table
        total += shard.table.capacity() * sizeof(uint32_t);
        // Data chunks (allocated in full CHUNK_SIZE blocks)
        total += shard.chunks.size() * CHUNK_SIZE * sizeof(Entry);
    }
    return total;
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
        for (size_t i = 0; i < shard.count; ++i) {
            const auto& entry = shard.entry_at(static_cast<uint32_t>(i));
            write_binary(out, entry.key);
            write_binary(out, entry.data.num_actions);
            for (int a = 0; a < entry.data.num_actions; ++a) {
                write_binary(out, entry.data.cumulative_regret[a]);
            }
            for (int a = 0; a < entry.data.num_actions; ++a) {
                write_binary(out, entry.data.strategy_sum[a]);
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

    // Pre-allocate for efficiency
    reserve(total_entries);

    for (uint64_t i = 0; i < total_entries; ++i) {
        InfoSetKey key;
        uint8_t num_actions;
        read_binary(in, key);
        read_binary(in, num_actions);

        // Clamp to MAX_ACTIONS for safety
        int clamped = std::min(static_cast<int>(num_actions), InfoSetData::MAX_ACTIONS);

        auto& data = get_or_create(key, clamped);
        for (int a = 0; a < num_actions; ++a) {
            float val;
            read_binary(in, val);
            if (a < clamped)
                data.cumulative_regret[a] = val;
        }
        for (int a = 0; a < num_actions; ++a) {
            float val;
            read_binary(in, val);
            if (a < clamped)
                data.strategy_sum[a] = val;
        }
    }

    log_info("Loaded " + std::to_string(total_entries) + " info sets from " + path);
}

void InfoSetStore::apply_discounting(float positive_discount, float negative_discount,
                                     float strategy_discount) {
    for (auto& shard : shards_) {
        std::unique_lock lock(shard.mutex);
        for (size_t i = 0; i < shard.count; ++i) {
            auto& data = shard.entry_at(static_cast<uint32_t>(i)).data;
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
        shard.table.assign(shard.table.size(), EMPTY_SLOT);
        shard.chunks.clear();
        shard.count = 0;
    }
}

void InfoSetStore::reserve(size_t total_entries) {
    size_t per_shard = (total_entries + num_shards_ - 1) / num_shards_;
    for (auto& shard : shards_) {
        // Size index table
        size_t table_size = 1024;
        while (static_cast<size_t>(table_size * MAX_LOAD_FACTOR) < per_shard) {
            table_size *= 2;
        }
        shard.table.assign(table_size, EMPTY_SLOT);
        shard.table_mask = table_size - 1;

        // Pre-allocate chunks
        size_t num_chunks = (per_shard + CHUNK_SIZE - 1) / CHUNK_SIZE;
        shard.chunks.reserve(num_chunks);
    }
}

}  // namespace poker

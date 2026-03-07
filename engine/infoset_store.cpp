#include "infoset_store.h"
#include "utils.h"
#include <chrono>
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
    // Read entire file into memory for fast parsing (avoids millions of tiny reads)
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        log_error("Failed to open file for reading: " + path);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long long file_size = _ftelli64(fp);
    fseek(fp, 0, SEEK_SET);

    fprintf(stdout, "  Reading %.1f GB into memory...\n", file_size / (1024.0 * 1024.0 * 1024.0));
    fflush(stdout);

    std::vector<uint8_t> file_data(static_cast<size_t>(file_size));
    size_t bytes_read = fread(file_data.data(), 1, static_cast<size_t>(file_size), fp);
    fclose(fp);

    if (bytes_read != static_cast<size_t>(file_size)) {
        log_error("Failed to read entire file (got " + std::to_string(bytes_read) + " of " +
                  std::to_string(file_size) + " bytes)");
        return;
    }

    fprintf(stdout, "  File loaded into memory.\n");
    fflush(stdout);

    clear();

    // Parse from memory buffer
    const uint8_t* ptr = file_data.data();
    const uint8_t* end = ptr + file_size;

    uint64_t total_entries;
    memcpy(&total_entries, ptr, sizeof(total_entries));
    ptr += sizeof(total_entries);

    fprintf(stdout, "  Inserting %llu info sets into hash map...\n",
            static_cast<unsigned long long>(total_entries));
    fflush(stdout);

    reserve(total_entries);

    auto start = std::chrono::high_resolution_clock::now();
    int last_pct = -1;

    for (uint64_t i = 0; i < total_entries; ++i) {
        InfoSetKey key;
        memcpy(&key, ptr, sizeof(key));
        ptr += sizeof(key);

        uint8_t num_actions = *ptr++;
        int clamped = std::min(static_cast<int>(num_actions), InfoSetData::MAX_ACTIONS);

        // Direct insert without locks (single-threaded bulk load)
        int shard_idx = shard_index(key);
        auto& data = shards_[shard_idx].insert_no_lock(key, clamped);

        size_t floats_size = sizeof(float) * num_actions;

        // Regrets
        memcpy(data.cumulative_regret, ptr, sizeof(float) * clamped);
        ptr += floats_size;

        // Strategy sums
        memcpy(data.strategy_sum, ptr, sizeof(float) * clamped);
        ptr += floats_size;

        // Progress every 1%
        int pct = static_cast<int>((i + 1) * 100 / total_entries);
        if (pct != last_pct) {
            last_pct = pct;
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();

            fprintf(stdout, "  [%d%%] %llu/%llu", pct, static_cast<unsigned long long>(i + 1),
                    static_cast<unsigned long long>(total_entries));
            if (pct > 0 && pct < 100) {
                int eta = static_cast<int>(elapsed / pct * (100 - pct));
                fprintf(stdout, "  (ETA: %ds)", eta);
            } else if (pct == 100) {
                fprintf(stdout, "  (%ds)", static_cast<int>(elapsed));
            }
            fprintf(stdout, "\n");
            fflush(stdout);
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

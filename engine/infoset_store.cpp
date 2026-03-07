#include "infoset_store.h"
#include "utils.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

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

    fprintf(stdout, "  %llu info sets to load\n", static_cast<unsigned long long>(total_entries));
    fflush(stdout);

    reserve(total_entries);

    // Pass 1: scan buffer to build per-shard offset lists (4 bytes each instead of 24)
    fprintf(stdout, "  Indexing entries by shard...\n");
    fflush(stdout);

    // Store just the byte offset from start of entries data — much smaller than EntryRef
    const uint8_t* entries_start = ptr;
    std::vector<std::vector<uint32_t>> shard_indices(num_shards_);
    {
        size_t approx_per_shard = static_cast<size_t>(total_entries / num_shards_ * 1.1);
        for (auto& v : shard_indices)
            v.reserve(approx_per_shard);

        // We need a global entry index to find the entry in the buffer later
        // Store per-shard lists of entry indices
        // Also build a compact offset table to find each entry in the buffer
        // Since entries are variable-size, we need offsets
    }

    // Build offset table: entry index -> byte offset from entries_start
    // Use uint32_t offsets for entries < 4GB? No, 9GB file. Use a flat scan approach instead.
    // Approach: scan once to get per-shard entry pointers (just the raw pointer, 8 bytes each)
    std::vector<std::vector<const uint8_t*>> shard_ptrs(num_shards_);
    {
        size_t approx_per_shard = static_cast<size_t>(total_entries / num_shards_ * 1.1);
        for (auto& v : shard_ptrs)
            v.reserve(approx_per_shard);

        auto scan_start = std::chrono::high_resolution_clock::now();
        for (uint64_t i = 0; i < total_entries; ++i) {
            const uint8_t* entry_ptr = ptr;

            InfoSetKey key;
            memcpy(&key, ptr, sizeof(key));
            ptr += sizeof(key);

            uint8_t num_actions = *ptr++;
            ptr += sizeof(float) * num_actions * 2;

            int shard_idx = shard_index(key);
            shard_ptrs[shard_idx].push_back(entry_ptr);
        }
        auto scan_end = std::chrono::high_resolution_clock::now();
        fprintf(stdout, "  Indexed in %.1fs\n",
                std::chrono::duration<double>(scan_end - scan_start).count());
        fflush(stdout);
    }

    // Insert per shard in parallel — each thread owns distinct shards, no locking needed
    int num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    fprintf(stdout, "  Inserting into hash map (%d threads)...\n", num_threads);
    fflush(stdout);

    auto insert_start = std::chrono::high_resolution_clock::now();
    std::atomic<int> shards_done{0};

    auto worker = [&](int thread_id) {
        for (int s = thread_id; s < num_shards_; s += num_threads) {
            auto& shard = shards_[s];
            for (const uint8_t* entry_ptr : shard_ptrs[s]) {
                const uint8_t* p = entry_ptr;

                InfoSetKey key;
                memcpy(&key, p, sizeof(key));
                p += sizeof(key);

                uint8_t num_actions = *p++;
                int clamped = std::min(static_cast<int>(num_actions), InfoSetData::MAX_ACTIONS);

                auto& data = shard.insert_no_lock(key, clamped);
                memcpy(data.cumulative_regret, p, sizeof(float) * clamped);
                p += sizeof(float) * num_actions;
                memcpy(data.strategy_sum, p, sizeof(float) * clamped);
            }
            // Free this shard's pointer list immediately to reduce peak memory
            std::vector<const uint8_t*>().swap(shard_ptrs[s]);

            int done = shards_done.fetch_add(1, std::memory_order_relaxed) + 1;
            if (done % (num_shards_ / 10 + 1) == 0 || done == num_shards_) {
                int pct = done * 100 / num_shards_;
                fprintf(stdout, "  [%d%%] %d/%d shards\n", pct, done, num_shards_);
                fflush(stdout);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
        threads.emplace_back(worker, t);
    for (auto& t : threads)
        t.join();

    // Free file buffer now that all data is in the hash map
    std::vector<uint8_t>().swap(file_data);

    auto insert_end = std::chrono::high_resolution_clock::now();
    fprintf(stdout, "  Inserted in %.1fs\n",
            std::chrono::duration<double>(insert_end - insert_start).count());
    fflush(stdout);

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

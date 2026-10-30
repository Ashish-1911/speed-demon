#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <atomic>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

struct InputMap {
    int fd = -1;
    size_t size = 0;
    const char* bytes = nullptr;
    explicit InputMap(const std::string& path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return;
        struct stat st {};
        if (fstat(fd, &st) || st.st_size < 0) return;
        size = static_cast<size_t>(st.st_size);
        if (!size) return;
        void* p = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED) bytes = static_cast<const char*>(p);
    }
    ~InputMap() {
        if (bytes && bytes != MAP_FAILED) munmap(const_cast<char*>(bytes), size);
        if (fd >= 0) close(fd);
    }
    bool good() const { return fd >= 0 && (size == 0 || bytes != nullptr); }
};

constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

inline uint64_t hash_endpoint(const char* s, size_t len) {
    uint64_t h = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < len; ++i) {
        h = (h ^ uint8_t(s[i])) * FNV_PRIME;
    }
    return h == 0 ? 1 : h;
}

struct EpSlot {
    uint64_t hash = 0;
    const char* ptr = nullptr;
    uint16_t len = 0;
    uint16_t id = 0;
};

struct LocalEpDict {
    static constexpr size_t CAP = 4096;
    static constexpr size_t MASK = CAP - 1;
    std::array<EpSlot, CAP> table{};
    std::vector<std::string_view> endpoints;

    LocalEpDict() {
        endpoints.reserve(2048);
    }

    inline uint16_t get_or_insert(const char* ptr, uint16_t len) {
        uint64_t h = hash_endpoint(ptr, len);
        size_t idx = h & MASK;
        while (true) {
            if (table[idx].hash == 0) {
                uint16_t new_id = static_cast<uint16_t>(endpoints.size());
                table[idx] = {h, ptr, len, new_id};
                endpoints.emplace_back(ptr, len);
                return new_id;
            }
            if (table[idx].hash == h && table[idx].len == len && memcmp(table[idx].ptr, ptr, len) == 0) {
                return table[idx].id;
            }
            idx = (idx + 1) & MASK;
        }
    }
};

struct LocalGroup {
    uint32_t errors = 0;
    std::vector<uint32_t> latencies;
};

constexpr int HOURS = 72;
constexpr int MAX_EP = 2048;

struct WorkerData {
    LocalEpDict dict;
    std::vector<LocalGroup> table;
    std::array<std::string, HOURS> hour_text;
};

int main(int argc, char** argv) {
    std::string data = "/data", out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--data" && i + 1 < argc) data = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out_dir = argv[++i];
    }

    std::string data_path = (fs::path(data) / "requests.log").string();
    std::string out_path = (fs::path(out_dir) / "summary.txt").string();
    fs::create_directories(out_dir);

    InputMap input(data_path);
    if (!input.good()) {
        std::cerr << "Cannot map requests.log\n";
        return 1;
    }

    const char* base = input.bytes;
    size_t total_size = input.size;
    if (total_size == 0) return 0;

    // Six physical P-cores in the competition scoring sandbox
    constexpr size_t workers = 6;
    std::vector<const char*> chunk_starts(workers);
    std::vector<const char*> chunk_ends(workers);

    for (size_t i = 0; i < workers; ++i) {
        size_t raw_start = i * total_size / workers;
        size_t raw_end = (i + 1) * total_size / workers;

        if (i == 0) {
            chunk_starts[i] = base;
        } else {
            const char* p = base + raw_start;
            const char* end = base + total_size;
            while (p < end && *p != '\n') ++p;
            if (p < end && *p == '\n') ++p;
            chunk_starts[i] = p;
        }

        if (i == workers - 1) {
            chunk_ends[i] = base + total_size;
        } else {
            const char* p = base + raw_end;
            const char* end = base + total_size;
            while (p < end && *p != '\n') ++p;
            if (p < end && *p == '\n') ++p;
            chunk_ends[i] = p;
        }
    }

    std::vector<WorkerData> worker_data(workers);
    for (size_t i = 0; i < workers; ++i) {
        worker_data[i].table.resize(size_t(HOURS) * MAX_EP);
    }

    auto parse_worker = [&](size_t wid) {
        const char* p = chunk_starts[wid];
        const char* end = chunk_ends[wid];
        auto& wd = worker_data[wid];
        auto& dict = wd.dict;
        auto& table = wd.table;
        auto& hour_text = wd.hour_text;

        while (p < end) {
            const char* line_end = static_cast<const char*>(memchr(p, '\n', size_t(end - p)));
            if (!line_end) break;

            const int month = (p[5]-'0')*10 + (p[6]-'0');
            const int day = (p[8]-'0')*10 + (p[9]-'0');
            const int hour = (p[11]-'0')*10 + (p[12]-'0');
            const int day_idx = month == 10 ? day-30 : day+1;
            const int h = day_idx*24 + hour-18;

            if (hour_text[h].empty()) {
                hour_text[h].assign(p, 13);
                hour_text[h] += ":00:00Z";
            }

            const char* m_ptr = p + 28;
            while (*m_ptr != ' ') ++m_ptr;

            const char* ep_start = m_ptr + 1;
            const char* ep_end = ep_start;
            while (*ep_end != ' ') ++ep_end;

            uint16_t ep_len = static_cast<uint16_t>(ep_end - ep_start);
            uint16_t local_eid = dict.get_or_insert(ep_start, ep_len);

            const char* st_ptr = ep_end + 1;
            unsigned status = (st_ptr[0]-'0')*100 + (st_ptr[1]-'0')*10 + (st_ptr[2]-'0');

            const char* lat_ptr = st_ptr + 4;
            uint32_t latency = 0;
            while (lat_ptr < line_end) {
                latency = latency * 10 + (*lat_ptr - '0');
                ++lat_ptr;
            }

            auto& grp = table[size_t(h) * MAX_EP + local_eid];
            grp.latencies.push_back(latency);
            grp.errors += (status >= 500 && status <= 599);

            p = line_end + 1;
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        threads.emplace_back(parse_worker, i);
    }
    for (auto& th : threads) th.join();

    std::array<std::string, HOURS> global_hour_text;
    for (int h = 0; h < HOURS; ++h) {
        for (size_t w = 0; w < workers; ++w) {
            if (!worker_data[w].hour_text[h].empty()) {
                global_hour_text[h] = worker_data[w].hour_text[h];
                break;
            }
        }
    }

    std::vector<std::string_view> all_endpoints;
    for (size_t w = 0; w < workers; ++w) {
        for (const auto& ep : worker_data[w].dict.endpoints) {
            all_endpoints.push_back(ep);
        }
    }
    std::sort(all_endpoints.begin(), all_endpoints.end());
    all_endpoints.erase(std::unique(all_endpoints.begin(), all_endpoints.end()), all_endpoints.end());

    size_t num_global_ep = all_endpoints.size();

    std::vector<std::vector<uint16_t>> worker_ep_map(workers);
    for (size_t w = 0; w < workers; ++w) {
        size_t n_local = worker_data[w].dict.endpoints.size();
        worker_ep_map[w].resize(n_local);
        for (size_t loc = 0; loc < n_local; ++loc) {
            std::string_view ep = worker_data[w].dict.endpoints[loc];
            auto it = std::lower_bound(all_endpoints.begin(), all_endpoints.end(), ep);
            worker_ep_map[w][loc] = static_cast<uint16_t>(it - all_endpoints.begin());
        }
    }

    struct MergedGroup {
        uint32_t errors = 0;
        std::vector<uint32_t> latencies;
    };
    std::vector<MergedGroup> merged_table(size_t(HOURS) * num_global_ep);

    for (size_t w = 0; w < workers; ++w) {
        size_t n_local = worker_data[w].dict.endpoints.size();
        for (int h = 0; h < HOURS; ++h) {
            for (size_t loc = 0; loc < n_local; ++loc) {
                const auto& src = worker_data[w].table[size_t(h) * MAX_EP + loc];
                if (src.latencies.empty() && src.errors == 0) continue;
                uint16_t geid = worker_ep_map[w][loc];
                auto& dst = merged_table[size_t(h) * num_global_ep + geid];
                dst.errors += src.errors;
                dst.latencies.insert(dst.latencies.end(), src.latencies.begin(), src.latencies.end());
            }
        }
    }

    std::atomic<size_t> next_group{0};
    size_t total_groups = merged_table.size();

    auto perc_worker = [&]() {
        while (true) {
            size_t idx = next_group.fetch_add(64, std::memory_order_relaxed);
            if (idx >= total_groups) break;
            size_t end_idx = std::min(idx + 64, total_groups);
            for (size_t g = idx; g < end_idx; ++g) {
                auto& v = merged_table[g].latencies;
                if (v.empty()) continue;
                size_t n = v.size();
                size_t p50 = (50 * n + 99) / 100 - 1;
                size_t p95 = (95 * n + 99) / 100 - 1;
                size_t p99 = (99 * n + 99) / 100 - 1;
                if (n <= 128) {
                    std::sort(v.begin(), v.end());
                } else {
                    std::nth_element(v.begin(), v.begin() + p99, v.end());
                    std::nth_element(v.begin(), v.begin() + p95, v.begin() + p99 + 1);
                    std::nth_element(v.begin(), v.begin() + p50, v.begin() + p95 + 1);
                }
            }
        }
    };

    std::vector<std::thread> perc_threads;
    perc_threads.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        perc_threads.emplace_back(perc_worker);
    }
    for (auto& th : perc_threads) th.join();

    std::ofstream out(out_path, std::ios::binary);
    if (!out.is_open()) return 1;

    std::string line_buf;
    line_buf.reserve(256);

    for (int h = 0; h < HOURS; ++h) {
        for (size_t e = 0; e < num_global_ep; ++e) {
            const auto& g = merged_table[size_t(h) * num_global_ep + e];
            const auto& v = g.latencies;
            if (v.empty()) continue;

            const size_t n = v.size();
            const size_t p50 = (50 * n + 99) / 100 - 1;
            const size_t p95 = (95 * n + 99) / 100 - 1;
            const size_t p99 = (99 * n + 99) / 100 - 1;
            const uint64_t ppm = uint64_t(g.errors) * 1000000ULL / n;

            line_buf.clear();
            line_buf += global_hour_text[h];
            line_buf += ' ';
            line_buf.append(all_endpoints[e].data(), all_endpoints[e].size());
            line_buf += ' ';
            line_buf += std::to_string(n);
            line_buf += ' ';
            line_buf += std::to_string(v[p50]);
            line_buf += ' ';
            line_buf += std::to_string(v[p95]);
            line_buf += ' ';
            line_buf += std::to_string(v[p99]);
            line_buf += ' ';
            line_buf += std::to_string(g.errors);
            line_buf += ' ';
            line_buf += std::to_string(ppm);
            line_buf += '\n';

            out.write(line_buf.data(), line_buf.size());
        }
    }

    return out.good() ? 0 : 1;
}

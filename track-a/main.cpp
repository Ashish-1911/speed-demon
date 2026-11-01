#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

struct InputMap {
    int fd = -1;
    size_t size = 0;
    const char *bytes = nullptr;
    explicit InputMap(const std::string &path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0)
            return;
        struct stat st{};
        if (fstat(fd, &st) || st.st_size < 0)
            return;
        size = static_cast<size_t>(st.st_size);
        if (!size)
            return;
        void *p = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED)
            bytes = static_cast<const char *>(p);
    }
    ~InputMap() {
        if (bytes && bytes != MAP_FAILED)
            munmap(const_cast<char *>(bytes), size);
        if (fd >= 0)
            close(fd);
    }
    bool good() const { return fd >= 0 && (size == 0 || bytes != nullptr); }
};

constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

struct EpSlot {
    uint64_t hash = 0;
    const char *ptr = nullptr;
    uint16_t len = 0;
    uint16_t id = 0;
};

struct LocalEpDict {
    static constexpr size_t CAP = 4096;
    static constexpr size_t MASK = CAP - 1;
    std::array<EpSlot, CAP> table{};
    std::vector<std::string_view> endpoints;

    LocalEpDict() { endpoints.reserve(2048); }

    inline uint16_t get_or_insert(const char *ptr, uint16_t len, uint64_t h) {
        if (h == 0)
            h = 1;
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
    uint32_t count = 0;
};

constexpr int HOURS = 72;
constexpr int MAX_EP = 2048;

struct WorkerData {
    struct Record {
        uint32_t group, latency;
    };
    std::vector<Record> records;
    LocalEpDict dict;
    std::vector<LocalGroup> table;
};

static const char digits[201] = "00010203040506070809"
                                "10111213141516171819"
                                "20212223242526272829"
                                "30313233343536373839"
                                "40414243444546474849"
                                "50515253545556575859"
                                "60616263646566676869"
                                "70717273747576777879"
                                "80818283848586878889"
                                "90919293949596979899";

inline char *append_u32(char *p, uint32_t val) {
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    char buf[12];
    char *cur = buf + 12;
    while (val >= 100) {
        uint32_t rem = val % 100;
        val /= 100;
        cur -= 2;
        memcpy(cur, digits + (rem * 2), 2);
    }
    if (val >= 10) {
        cur -= 2;
        memcpy(cur, digits + (val * 2), 2);
    } else {
        *--cur = char('0' + val);
    }
    size_t len = (buf + 12) - cur;
    memcpy(p, cur, len);
    return p + len;
}

inline char *append_u64(char *p, uint64_t val) {
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    char buf[24];
    char *cur = buf + 24;
    while (val >= 100) {
        uint32_t rem = val % 100;
        val /= 100;
        cur -= 2;
        memcpy(cur, digits + (rem * 2), 2);
    }
    if (val >= 10) {
        cur -= 2;
        memcpy(cur, digits + (val * 2), 2);
    } else {
        *--cur = char('0' + val);
    }
    size_t len = (buf + 24) - cur;
    memcpy(p, cur, len);
    return p + len;
}

int main(int argc, char **argv) {
    std::string data = "/data", out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--data" && i + 1 < argc)
            data = argv[++i];
        else if (arg == "--out" && i + 1 < argc)
            out_dir = argv[++i];
    }

    std::string data_path = (fs::path(data) / "requests.log").string();
    std::string out_path = (fs::path(out_dir) / "summary.txt").string();
    fs::create_directories(out_dir);

    InputMap input(data_path);
    if (!input.good()) {
        std::cerr << "Cannot map requests.log\n";
        return 1;
    }

    const char *base = input.bytes;
    size_t total_size = input.size;
    if (total_size == 0) {
        int fd = open(out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0)
            return 1;
        close(fd);
        return 0;
    }

    // Six physical P-cores
    constexpr size_t workers = 6;
    std::vector<const char *> chunk_starts(workers);
    std::vector<const char *> chunk_ends(workers);

    for (size_t i = 0; i < workers; ++i) {
        size_t raw_start = i * total_size / workers;
        size_t raw_end = (i + 1) * total_size / workers;

        if (i == 0) {
            chunk_starts[i] = base;
        } else {
            const char *p = base + raw_start;
            const char *end = base + total_size;
            while (p < end && *p != '\n')
                ++p;
            if (p < end && *p == '\n')
                ++p;
            chunk_starts[i] = p;
        }

        if (i == workers - 1) {
            chunk_ends[i] = base + total_size;
        } else {
            const char *p = base + raw_end;
            const char *end = base + total_size;
            while (p < end && *p != '\n')
                ++p;
            if (p < end && *p == '\n')
                ++p;
            chunk_ends[i] = p;
        }
    }

    std::vector<WorkerData> worker_data(workers);
    for (size_t i = 0; i < workers; ++i) {
        worker_data[i].table.resize(size_t(HOURS) * MAX_EP);
    }

    // Precompute global hour strings (exact 72 hours from 2026-10-30T18:00:00Z to 2026-11-02T17:00:00Z)
    std::array<std::string, HOURS> global_hour_text;
    {
        for (int h = 0; h < HOURS; ++h) {
            int abs_hour = 18 + h;
            int day = 30 + abs_hour / 24;
            int rem_hour = abs_hour % 24;
            int month = 10;
            if (day > 31) {
                day -= 31;
                month = 11;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "2026-%02d-%02dT%02d:00:00Z", month, day, rem_hour);
            global_hour_text[h] = buf;
        }
    }

    auto parse_worker = [&](size_t wid) {
        const char *p = chunk_starts[wid];
        const char *end = chunk_ends[wid];
        auto &wd = worker_data[wid];
        auto &dict = wd.dict;
        auto &table = wd.table;
        wd.records.reserve(size_t(end - p) / 40 + 1);

        while (p < end) {
            const char *line_end = static_cast<const char *>(memchr(p, '\n', size_t(end - p)));
            if (!line_end)
                break;

            const int month = (p[5] - '0') * 10 + (p[6] - '0');
            const int day = (p[8] - '0') * 10 + (p[9] - '0');
            const int hour = (p[11] - '0') * 10 + (p[12] - '0');
            const int day_idx = month == 10 ? day - 30 : day + 1;
            const int h = day_idx * 24 + hour - 18;

            const char *m_ptr = p + 28;
            while (*m_ptr != ' ')
                ++m_ptr;

            const char *ep_start = m_ptr + 1;
            const char *ep_end = ep_start;
            uint64_t ep_hash = FNV_OFFSET_BASIS;
            while (*ep_end != ' ') {
                ep_hash = (ep_hash ^ uint8_t(*ep_end++)) * FNV_PRIME;
            }

            uint16_t ep_len = static_cast<uint16_t>(ep_end - ep_start);
            uint16_t local_eid = dict.get_or_insert(ep_start, ep_len, ep_hash);

            const char *st_ptr = ep_end + 1;
            unsigned status = (st_ptr[0] - '0') * 100 + (st_ptr[1] - '0') * 10 + (st_ptr[2] - '0');

            const char *lat_ptr = st_ptr + 4;
            uint32_t latency = 0;
            while (lat_ptr < line_end) {
                latency = latency * 10 + (*lat_ptr - '0');
                ++lat_ptr;
            }

            auto &grp = table[size_t(h) * MAX_EP + local_eid];
            ++grp.count;
            wd.records.push_back({uint32_t(h * MAX_EP + local_eid), latency});
            grp.errors += (status >= 500 && status <= 599);

            p = line_end + 1;
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        threads.emplace_back(parse_worker, i);
    }
    for (auto &th : threads)
        th.join();

    // Collect and sort global endpoints
    std::vector<std::string_view> all_endpoints;
    for (size_t w = 0; w < workers; ++w) {
        for (const auto &ep : worker_data[w].dict.endpoints) {
            all_endpoints.push_back(ep);
        }
    }
    std::sort(all_endpoints.begin(), all_endpoints.end());
    all_endpoints.erase(std::unique(all_endpoints.begin(), all_endpoints.end()), all_endpoints.end());

    size_t num_global_ep = all_endpoints.size();

    // Map each worker's local ep ID to global ep ID
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

    // Compute each worker's disjoint destination spans, then scatter once.
    size_t groups = size_t(HOURS) * num_global_ep;
    std::vector<uint32_t> offsets(groups + 1), errors(groups);
    for (size_t w = 0; w < workers; ++w)
        for (int h = 0; h < HOURS; ++h)
            for (size_t loc = 0; loc < worker_ep_map[w].size(); ++loc) {
                const auto &g = worker_data[w].table[size_t(h) * MAX_EP + loc];
                size_t global = size_t(h) * num_global_ep + worker_ep_map[w][loc];
                offsets[global + 1] += g.count;
                errors[global] += g.errors;
            }
    for (size_t g = 0; g < groups; ++g)
        offsets[g + 1] += offsets[g];
    std::vector<uint32_t> values(offsets.back()), cursor = offsets;
    for (size_t w = 0; w < workers; ++w)
        for (int h = 0; h < HOURS; ++h)
            for (size_t loc = 0; loc < worker_ep_map[w].size(); ++loc) {
                auto &g = worker_data[w].table[size_t(h) * MAX_EP + loc];
                size_t global = size_t(h) * num_global_ep + worker_ep_map[w][loc];
                uint32_t count = g.count;
                g.count = cursor[global];
                cursor[global] += count;
            }
    threads.clear();
    for (size_t w = 0; w < workers; ++w)
        threads.emplace_back([&, w] {
            auto &wd = worker_data[w];
            for (const auto &r : wd.records)
                values[wd.table[r.group].count++] = r.latency;
            std::vector<WorkerData::Record>().swap(wd.records);
        });
    for (auto &th : threads)
        th.join();
    for (auto &wd : worker_data)
        std::vector<LocalGroup>().swap(wd.table);
    std::vector<uint32_t>().swap(cursor);

    // Parallelize merging, percentile calculation, and output formatting
    std::vector<std::string> thread_out(workers);
    for (size_t w = 0; w < workers; ++w) {
        thread_out[w].resize(2 * 1024 * 1024);
    }
    std::vector<size_t> thread_out_len(workers, 0);

    auto process_worker = [&](size_t wid) {
        int h_start = static_cast<int>(wid * HOURS / workers);
        int h_end = static_cast<int>((wid + 1) * HOURS / workers);

        std::string &out_str = thread_out[wid];
        char *out_ptr = out_str.data();
        char *out_end = out_ptr + out_str.size();

        // Format lines in exact order
        for (int h = h_start; h < h_end; ++h) {
            const std::string &h_str = global_hour_text[h];

            for (size_t e = 0; e < num_global_ep; ++e) {
                size_t group = size_t(h) * num_global_ep + e;
                std::span<uint32_t> v(values.data() + offsets[group], offsets[group + 1] - offsets[group]);
                if (v.empty())
                    continue;

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

                uint64_t ppm = uint64_t(errors[group]) * 1000000ULL / n;

                if (size_t(out_end - out_ptr) < 256) {
                    size_t cur_len = out_ptr - out_str.data();
                    out_str.resize(out_str.size() * 2);
                    out_ptr = out_str.data() + cur_len;
                    out_end = out_str.data() + out_str.size();
                }

                memcpy(out_ptr, h_str.data(), 20);
                out_ptr += 20;
                *out_ptr++ = ' ';

                memcpy(out_ptr, all_endpoints[e].data(), all_endpoints[e].size());
                out_ptr += all_endpoints[e].size();
                *out_ptr++ = ' ';

                out_ptr = append_u32(out_ptr, static_cast<uint32_t>(n));
                *out_ptr++ = ' ';

                out_ptr = append_u32(out_ptr, v[p50]);
                *out_ptr++ = ' ';

                out_ptr = append_u32(out_ptr, v[p95]);
                *out_ptr++ = ' ';

                out_ptr = append_u32(out_ptr, v[p99]);
                *out_ptr++ = ' ';

                out_ptr = append_u32(out_ptr, errors[group]);
                *out_ptr++ = ' ';

                out_ptr = append_u64(out_ptr, ppm);
                *out_ptr++ = '\n';
            }
        }
        thread_out_len[wid] = out_ptr - out_str.data();
    };

    std::vector<std::thread> p_threads;
    p_threads.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        p_threads.emplace_back(process_worker, i);
    }
    for (auto &th : p_threads)
        th.join();

    // Fast write to output file
    int out_fd = open(out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd < 0)
        return 1;

    for (size_t w = 0; w < workers; ++w) {
        if (thread_out_len[w] > 0) {
            size_t offset = 0;
            while (offset < thread_out_len[w]) {
                ssize_t written = write(out_fd, thread_out[w].data() + offset, thread_out_len[w] - offset);
                if (written < 0 && errno == EINTR)
                    continue;
                if (written <= 0) {
                    close(out_fd);
                    return 1;
                }
                offset += size_t(written);
            }
        }
    }
    close(out_fd);

    return 0;
}

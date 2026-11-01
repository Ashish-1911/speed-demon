// Speed Demon 2026 Track C — High-performance parallel join & aggregation
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <immintrin.h>
#include <iostream>
#include <memory>
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
    const char *ptr = nullptr;
    explicit InputMap(const std::string &path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0)
            return;
        struct stat st{};
        if (fstat(fd, &st) != 0 || st.st_size < 0)
            return;
        size = size_t(st.st_size);
        if (!size)
            return;
        void *p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p != MAP_FAILED) {
            ptr = static_cast<const char *>(p);
            madvise(p, size, MADV_WILLNEED | MADV_SEQUENTIAL);
        }
    }
    ~InputMap() {
        if (ptr)
            munmap(const_cast<char *>(ptr), size);
        if (fd >= 0)
            close(fd);
    }
    bool good() const { return fd >= 0 && (size == 0 || ptr != nullptr); }
};

static inline uint64_t fnv1a(const char *data, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint8_t>(data[i]);
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

struct Slot {
    uint64_t hash = 0;
    const char *ptr = nullptr;
    uint32_t len = 0;
    uint32_t pad = 0;
    uint64_t mask[4] = {0, 0, 0, 0};
};

// Each partition is accessed only by its assigned owner until all owners join.
struct alignas(64) Partition {
    static constexpr size_t mask = 255;
    std::vector<Slot> slots{256};
    std::vector<Slot> overflow;

    inline void insert_or_update(const char *p, uint32_t len, uint64_t h, uint16_t c) {
        size_t idx = h & mask;
        for (size_t step = 0; step < 256; ++step) {
            Slot &s = slots[idx];
            if (s.hash == 0) {
                s.hash = h;
                s.ptr = p;
                s.len = len;
                s.mask[c >> 6] |= (1ULL << (c & 63));
                return;
            }
            if (s.hash == h && s.len == len && (len == 0 || std::memcmp(s.ptr, p, len) == 0)) {
                s.mask[c >> 6] |= (1ULL << (c & 63));
                return;
            }
            idx = (idx + 1) & mask;
        }
        for (Slot &s : overflow) {
            if (s.hash == h && s.len == len && (len == 0 || std::memcmp(s.ptr, p, len) == 0)) {
                s.mask[c >> 6] |= (1ULL << (c & 63));
                return;
            }
        }
        Slot s{};
        s.hash = h;
        s.ptr = p;
        s.len = len;
        s.mask[c >> 6] |= (1ULL << (c & 63));
        overflow.push_back(s);
    }
};

static inline uint64_t number(const char *&p, const char *end) {
    uint64_t x = 0;
    while (p < end && *p >= '0' && *p <= '9')
        x = x * 10 + uint64_t(*p++ - '0');
    return x;
}

struct Aggregates {
    uint64_t count = 0;
    uint64_t name_bytes = 0;
    uint64_t note_bytes = 0;
    int64_t id_sum = 0;
    int64_t sum = 0;
    int64_t min = 0;
    int64_t max = 0;
    uint32_t names_distinct = 0;

    inline void add(uint64_t id, int64_t amount, size_t nbytes, size_t note) {
        if (!count)
            min = max = amount;
        else {
            if (amount < min)
                min = amount;
            if (amount > max)
                max = amount;
        }
        ++count;
        id_sum += static_cast<int64_t>(id);
        sum += amount;
        name_bytes += nbytes;
        note_bytes += note;
    }

    void merge(const Aggregates &o) {
        if (!o.count)
            return;
        if (!count) {
            min = o.min;
            max = o.max;
        } else {
            if (o.min < min)
                min = o.min;
            if (o.max > max)
                max = o.max;
        }
        count += o.count;
        id_sum += o.id_sum;
        sum += o.sum;
        name_bytes += o.name_bytes;
        note_bytes += o.note_bytes;
    }
};

static inline std::string money(int64_t value) {
    bool neg = value < 0;
    uint64_t x = neg ? uint64_t(-(value + 1)) + 1 : uint64_t(value);
    std::string s = neg ? "-" : "";
    s += std::to_string(x / 100);
    s += '.';
    if (x % 100 < 10)
        s += '0';
    s += std::to_string(x % 100);
    return s;
}

static std::vector<const char *> find_chunk_boundaries(const char *data, size_t size, int num_threads) {
    std::vector<const char *> bounds;
    bounds.reserve(num_threads + 1);

    const char *p = data;
    const char *end = data + size;

    if (end - p >= 3 && uint8_t(p[0]) == 0xef && uint8_t(p[1]) == 0xbb && uint8_t(p[2]) == 0xbf)
        p += 3;
    while (p < end && *p != '\n')
        ++p;
    if (p < end)
        ++p;
    bounds.push_back(p);

    if (num_threads <= 1) {
        bounds.push_back(end);
        return bounds;
    }

    size_t chunk_size = size / num_threads;
    uint64_t total_quotes = 0;
    __m256i q = _mm256_set1_epi8('"');

    int target_thread = 1;
    size_t next_target = chunk_size;

    while (p < end && target_thread < num_threads) {
        if ((size_t)(p - data) + 128 < next_target && p + 128 <= end) {
            __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
            __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + 32));
            __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + 64));
            __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + 96));
            uint32_t m0 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v0, q));
            uint32_t m1 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v1, q));
            uint32_t m2 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v2, q));
            uint32_t m3 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v3, q));
            total_quotes += _mm_popcnt_u32(m0) + _mm_popcnt_u32(m1) + _mm_popcnt_u32(m2) + _mm_popcnt_u32(m3);
            p += 128;
            continue;
        }

        char b = *p++;
        if (b == '"')
            total_quotes++;
        else if (b == '\n') {
            if ((size_t)(p - data) >= next_target && (total_quotes % 2 == 0)) {
                bounds.push_back(p);
                target_thread++;
                next_target = target_thread * chunk_size;
            }
        }
    }

    while (bounds.size() <= (size_t)num_threads) {
        bounds.push_back(end);
    }
    return bounds;
}

// Input mappings outlive the consumers; names need no allocated byte copies.
struct NameRef {
    uint64_t hash;
    const char *ptr;
    uint32_t len;
    uint16_t country;
};
// One producer and one consumer per queue. Release/acquire transfers a whole batch;
// a producer cannot reuse its ring slot until the consumer releases it.
struct NameQueue {
    static constexpr unsigned SIZE = 256, BLOCKS = 4;
    struct Batch {
        alignas(64) std::atomic<bool> ready{false};
        unsigned count = 0;
        NameRef names[SIZE];
    };
    std::array<Batch, BLOCKS> batches;
    alignas(64) unsigned write_index = 0, count = 0;
    alignas(64) unsigned read_index = 0;
    void add(NameRef item) {
        auto &batch = batches[write_index % BLOCKS];
        if (count == 0)
            while (batch.ready.load(std::memory_order_acquire))
                _mm_pause();
        batch.names[count++] = item;
        if (count == SIZE)
            flush();
    }
    void flush() {
        if (!count)
            return;
        auto &batch = batches[write_index % BLOCKS];
        batch.count = count;
        batch.ready.store(true, std::memory_order_release);
        ++write_index;
        count = 0;
    }
};

int main(int argc, char **argv) {
    std::string data_dir = "/data", out_dir = "/out";
    int num_threads = int(std::thread::hardware_concurrency());
    if (num_threads <= 0)
        num_threads = 6;
    if (num_threads > 12)
        num_threads = 12;

    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        if (a == "--data" && i + 1 < argc)
            data_dir = argv[++i];
        else if (a == "--out" && i + 1 < argc)
            out_dir = argv[++i];
        else if (a == "--threads" && i + 1 < argc)
            num_threads = std::stoi(argv[++i]);
    }

    int owner_count = std::max(1, num_threads / 2);
    num_threads = std::max(1, num_threads - owner_count);
    constexpr uint16_t ABSENT = 0xffff;
    std::vector<uint16_t> region(1000000, ABSENT);
    std::vector<std::string> codes;
    {
        InputMap rmap((fs::path(data_dir) / "regions.csv").string());
        if (!rmap.good() || !rmap.ptr) {
            std::cerr << "Cannot map regions.csv\n";
            return 1;
        }
        const char *p = rmap.ptr;
        const char *end = p + rmap.size;
        while (p < end && *p != '\n')
            ++p;
        if (p < end)
            ++p;

        std::vector<uint16_t> code_to_id(17576, ABSENT);
        codes.reserve(256);

        while (p < end) {
            if (*p == '\r' || *p == '\n') {
                ++p;
                continue;
            }
            uint32_t id = 0;
            while (p < end && *p >= '0' && *p <= '9')
                id = id * 10 + (*p++ - '0');
            if (p < end && *p == ',')
                ++p;
            const char *c_start = p;
            while (p < end && *p != '\r' && *p != '\n')
                ++p;
            size_t clen = size_t(p - c_start);
            if (p < end && *p == '\r')
                ++p;
            if (p < end && *p == '\n')
                ++p;

            if (clen == 3) {
                int key = (c_start[0] - 'A') * 676 + (c_start[1] - 'A') * 26 + (c_start[2] - 'A');
                uint16_t cid = code_to_id[key];
                if (cid == ABSENT) {
                    cid = uint16_t(codes.size());
                    codes.emplace_back(c_start, 3);
                    code_to_id[key] = cid;
                }
                if (id >= region.size())
                    region.resize(std::max(size_t(id + 1), region.size() * 2), ABSENT);
                region[id] = cid;
            }
        }
    }
    uint16_t unmatched = uint16_t(codes.size());
    codes.emplace_back("_UNMATCHED");

    InputMap file((fs::path(data_dir) / "records.csv").string());
    if (!file.good() || !file.ptr) {
        std::cerr << "Cannot map records.csv\n";
        return 1;
    }

    auto bounds = find_chunk_boundaries(file.ptr, file.size, num_threads);

    std::vector<std::vector<Aggregates>> worker_aggs(num_threads, std::vector<Aggregates>(codes.size()));
    std::vector<Partition> partitions(1024);

    auto queues = std::make_unique<NameQueue[]>(num_threads * owner_count);
    auto done = std::make_unique<std::atomic<bool>[]>(num_threads);
    std::vector<std::thread> owners;
    for (int owner = 0; owner < owner_count; ++owner)
        owners.emplace_back([&, owner] {
            while (true) {
                bool finished = true;
                for (int producer = 0; producer < num_threads; ++producer) {
                    auto &q = queues[producer * owner_count + owner];
                    auto &batch = q.batches[q.read_index % NameQueue::BLOCKS];
                    // Acquire completion before checking the queue: final publication precedes it.
                    bool producer_done = done[producer].load(std::memory_order_acquire);
                    if (batch.ready.load(std::memory_order_acquire)) {
                        for (unsigned i = 0; i < batch.count; ++i) {
                            const auto &x = batch.names[i];
                            partitions[(x.hash >> 54) & 1023].insert_or_update(x.ptr, x.len, x.hash,
                                                                               x.country);
                        }
                        batch.ready.store(false, std::memory_order_release);
                        ++q.read_index;
                        finished = false;
                    } else if (!producer_done)
                        finished = false;
                }
                if (finished)
                    break;
                _mm_pause();
            }
        });
    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (int tid = 0; tid < num_threads; ++tid) {
        workers.emplace_back([&, tid]() {
            const char *p = bounds[tid];
            const char *end = bounds[tid + 1];
            auto &aggs = worker_aggs[tid];
            const uint16_t *region_data = region.data();
            const size_t region_size = region.size();

            while (p < end) {
                if (end - p >= 3 && uint8_t(p[0]) == 0xef && uint8_t(p[1]) == 0xbb && uint8_t(p[2]) == 0xbf)
                    p += 3;
                if (p == end)
                    break;
                uint64_t id = number(p, end);
                if (p == end || *p++ != ',')
                    break;

                const char *name_ptr = nullptr;
                size_t name_raw_len = 0;
                size_t name_len = 0;
                if (*p == '"') {
                    ++p;
                    const char *name_start = p;
                    size_t escaped_quotes = 0;
                    while (p < end) {
                        const char *q = static_cast<const char *>(std::memchr(p, '"', end - p));
                        if (!q) {
                            p = end;
                            break;
                        }
                        if (q + 1 < end && q[1] == '"') {
                            ++escaped_quotes;
                            p = q + 2;
                        } else {
                            p = q;
                            break;
                        }
                    }
                    if (p >= end)
                        break;
                    name_raw_len = size_t(p - name_start);
                    name_len = name_raw_len - escaped_quotes;
                    name_ptr = name_start;
                    ++p;
                    if (p == end || *p++ != ',')
                        break;
                } else {
                    if (*p != ',')
                        break;
                    ++p;
                    name_ptr = nullptr;
                    name_raw_len = 0;
                    name_len = 0;
                }

                size_t note_bytes = 0;
                if (*p == '"') {
                    ++p;
                    while (p < end) {
                        const char *q = static_cast<const char *>(std::memchr(p, '"', end - p));
                        if (!q) {
                            p = end;
                            break;
                        }
                        note_bytes += size_t(q - p);
                        if (q + 1 < end && q[1] == '"') {
                            ++note_bytes;
                            p = q + 2;
                        } else {
                            p = q + 1;
                            break;
                        }
                    }
                } else {
                    const char *comma = static_cast<const char *>(std::memchr(p, ',', end - p));
                    if (!comma)
                        break;
                    note_bytes = size_t(comma - p);
                    p = comma;
                }
                if (p == end || *p++ != ',')
                    break;

                uint64_t r_id = number(p, end);
                if (p == end || *p++ != ',')
                    break;

                bool negative = false;
                if (p < end && *p == '-') {
                    negative = true;
                    ++p;
                }
                uint64_t whole = number(p, end);
                if (p == end || *p++ != '.')
                    break;
                if (end - p < 2)
                    break;
                const int64_t cents =
                    int64_t(whole * 100 + 10 * (p[0] - '0') + (p[1] - '0')) * (negative ? -1 : 1);
                p += 2;
                if (p < end && *p == '\r')
                    ++p;
                if (p < end && *p == '\n')
                    ++p;

                uint16_t c = r_id < region_size ? region_data[size_t(r_id)] : unmatched;
                if (c == 0xffff)
                    c = unmatched;

                aggs[c].add(id, cents, name_len, note_bytes);

                uint64_t h = fnv1a(name_ptr, name_raw_len);
                uint16_t part = uint16_t((h >> 54) & 1023);
                queues[tid * owner_count + part % owner_count].add({h, name_ptr, uint32_t(name_raw_len), c});
            }
            for (int owner = 0; owner < owner_count; ++owner)
                queues[tid * owner_count + owner].flush();
            done[tid].store(true, std::memory_order_release);
        });
    }
    for (auto &w : workers)
        w.join();
    for (auto &owner : owners)
        owner.join();

    std::vector<Aggregates> final_aggs(codes.size());
    for (int i = 0; i < num_threads; ++i) {
        for (size_t c = 0; c < codes.size(); ++c) {
            final_aggs[c].merge(worker_aggs[i][c]);
        }
    }

    for (const auto &part : partitions) {
        for (const auto &s : part.slots) {
            if (s.hash == 0)
                continue;
            for (int w = 0; w < 4; ++w) {
                uint64_t mask = s.mask[w];
                while (mask) {
                    int b = __builtin_ctzll(mask);
                    size_t c = (w << 6) | b;
                    if (c < final_aggs.size()) {
                        final_aggs[c].names_distinct++;
                    }
                    mask &= mask - 1;
                }
            }
        }
        for (const auto &s : part.overflow) {
            for (int w = 0; w < 4; ++w) {
                uint64_t mask = s.mask[w];
                while (mask) {
                    int b = __builtin_ctzll(mask);
                    size_t c = (w << 6) | b;
                    if (c < final_aggs.size()) {
                        final_aggs[c].names_distinct++;
                    }
                    mask &= mask - 1;
                }
            }
        }
    }

    std::vector<size_t> order;
    for (size_t i = 0; i < final_aggs.size(); ++i)
        if (final_aggs[i].count)
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return codes[a] < codes[b]; });

    fs::create_directories(out_dir);
    std::string out_str;
    out_str.reserve(65536);
    for (size_t c : order) {
        const auto &a = final_aggs[c];
        out_str += codes[c];
        out_str += ' ';
        out_str += std::to_string(a.count);
        out_str += ' ';
        out_str += std::to_string(a.id_sum);
        out_str += ' ';
        out_str += money(a.sum);
        out_str += ' ';
        out_str += money(a.min);
        out_str += ' ';
        out_str += money(a.max);
        out_str += ' ';
        out_str += std::to_string(a.names_distinct);
        out_str += ' ';
        out_str += std::to_string(a.name_bytes);
        out_str += ' ';
        out_str += std::to_string(a.note_bytes);
        out_str += '\n';
    }

    int out_fd = open((fs::path(out_dir) / "aggregates.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd >= 0) {
        ssize_t written = write(out_fd, out_str.data(), out_str.size());
        (void)written;
        close(out_fd);
    }
    return 0;
}

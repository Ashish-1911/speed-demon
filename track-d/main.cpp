#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
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
        if (fd < 0) return;
        struct stat st{};
        if (fstat(fd, &st) != 0 || st.st_size < 0) return;
        size = size_t(st.st_size);
        if (!size) return;
        void *p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p != MAP_FAILED) {
            ptr = static_cast<const char *>(p);
            madvise(p, size, MADV_WILLNEED | MADV_SEQUENTIAL);
        }
    }
    ~InputMap() {
        if (ptr) munmap(const_cast<char *>(ptr), size);
        if (fd >= 0) close(fd);
    }
    bool good() const { return fd >= 0 && (size == 0 || ptr != nullptr); }
};

static inline uint32_t fast_u32(const char *&p, const char *end) {
    while (p < end && *p <= ' ') ++p;
    uint32_t x = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        x = x * 10 + (*p++ - '0');
    }
    return x;
}

static inline uint32_t parse_u32_fast(const char *&p) {
    uint32_t x = *p++ - '0';
    while (*p >= '0' && *p <= '9') {
        x = x * 10 + (*p++ - '0');
    }
    return x;
}

struct RawEdge { uint32_t u, v, w; };
struct Edge { uint32_t v, w; };

template <typename T, typename Priority = uint64_t> struct RadixHeap {
    struct Entry { Priority priority; T value; };
    std::array<std::vector<Entry>, 65> buckets;
    Priority last = 0;
    size_t count = 0;
    static unsigned bucket(Priority x) { return std::bit_width(x); }
    void clear() {
        for (auto &b : buckets) b.clear();
        last = 0;
        count = 0;
    }
    bool empty() const { return count == 0; }
    size_t size() const { return count; }
    void push(Priority p, T v) {
        buckets[bucket(p ^ last)].push_back({p, v});
        ++count;
    }
    const Entry &top() {
        if (buckets[0].empty()) {
            unsigned i = 1;
            while (buckets[i].empty()) ++i;
            last = UINT64_MAX;
            for (const auto &e : buckets[i]) last = std::min(last, e.priority);
            for (const auto &e : buckets[i]) buckets[bucket(e.priority ^ last)].push_back(e);
            buckets[i].clear();
        }
        return buckets[0].back();
    }
    Entry pop() {
        Entry e = top();
        buckets[0].pop_back();
        --count;
        return e;
    }
};

struct QueryState {
    struct Value {
        uint32_t node = 0, stamp = 0;
        uint64_t f, b;
    };
    std::vector<Value> slots = std::vector<Value>(2048);
    uint32_t epoch = 0;
    size_t used = 0;

    void clear() {
        used = 0;
        if (++epoch == 0) {
            for (auto &s : slots) s.stamp = 0;
            epoch = 1;
        }
    }

    void rehash() {
        auto old = std::move(slots);
        slots = std::vector<Value>(old.size() * 2);
        used = 0;
        size_t mask = slots.size() - 1;
        for (const auto &s : old) {
            if (s.stamp == epoch) {
                size_t i = (s.node * 2654435761u) & mask;
                while (slots[i].stamp == epoch) i = (i + 1) & mask;
                slots[i] = s;
                ++used;
            }
        }
    }

    inline void init(uint32_t s, uint32_t t) {
        clear();
        size_t mask = slots.size() - 1;
        size_t i = (s * 2654435761u) & mask;
        while (slots[i].stamp == epoch && slots[i].node != s) i = (i + 1) & mask;
        slots[i] = {s, epoch, 0, UINT64_MAX};
        size_t j = (t * 2654435761u) & mask;
        while (slots[j].stamp == epoch && slots[j].node != t) j = (j + 1) & mask;
        slots[j] = {t, epoch, UINT64_MAX, 0};
        used = 2;
    }

    template <bool Reverse>
    inline uint64_t dist(uint32_t node) const {
        size_t mask = slots.size() - 1, i = (node * 2654435761u) & mask;
        while (slots[i].node != node) i = (i + 1) & mask;
        return Reverse ? slots[i].b : slots[i].f;
    }

    template <bool Reverse>
    inline void relax(uint32_t node, uint64_t nd, uint64_t &best, RadixHeap<uint32_t, uint64_t> &pq) {
        if ((used + 1) * 2 > slots.size()) rehash();
        size_t mask = slots.size() - 1, i = (node * 2654435761u) & mask;
        while (slots[i].stamp == epoch && slots[i].node != node) i = (i + 1) & mask;
        if (slots[i].stamp != epoch) {
            slots[i] = {node, epoch, Reverse ? UINT64_MAX : nd, Reverse ? nd : UINT64_MAX};
            ++used;
            pq.push(nd, node);
        } else {
            uint64_t &cur = Reverse ? slots[i].b : slots[i].f;
            if (nd < cur) {
                cur = nd;
                pq.push(nd, node);
                uint64_t opp = Reverse ? slots[i].f : slots[i].b;
                if (opp != UINT64_MAX && nd + opp < best) best = nd + opp;
            }
        }
    }
};

int main(int argc, char *argv[]) {
    std::string data_dir = "/data", out_dir = "/out";
    int num_workers = int(std::thread::hardware_concurrency());
    if (num_workers <= 0) num_workers = 6;
    if (num_workers > 12) num_workers = 12;

    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        if (a == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--threads" && i + 1 < argc) num_workers = std::stoi(argv[++i]);
    }

    InputMap g_map((fs::path(data_dir) / "graph.txt").string());
    if (!g_map.good() || !g_map.ptr) return 1;
    const char *gp = g_map.ptr;
    const char *gend = gp + g_map.size;

    uint32_t n = fast_u32(gp, gend);
    uint32_t m = fast_u32(gp, gend);
    while (gp < gend && *gp != '\n') ++gp;
    if (gp < gend) ++gp;

    int parse_workers = std::min(num_workers, 6);
    if (parse_workers <= 0) parse_workers = 1;

    std::vector<const char *> chunk_ptrs(parse_workers + 1);
    chunk_ptrs[0] = gp;
    size_t bytes_per_chunk = (gend - gp) / parse_workers;
    for (int i = 1; i < parse_workers; ++i) {
        const char *p = gp + i * bytes_per_chunk;
        while (p < gend && *p != '\n') ++p;
        if (p < gend) ++p;
        chunk_ptrs[i] = p;
    }
    chunk_ptrs[parse_workers] = gend;

    std::vector<std::vector<RawEdge>> worker_edges(parse_workers);
    std::vector<std::thread> parse_threads;
    for (int wid = 0; wid < parse_workers; ++wid)
        parse_threads.emplace_back([&, wid] {
            auto &out = worker_edges[wid];
            out.reserve(m / parse_workers + 32768);
            const char *p = chunk_ptrs[wid];
            const char *end = chunk_ptrs[wid + 1];
            while (p < end) {
                while (p < end && *p <= ' ') ++p;
                if (p == end) break;
                uint32_t u = parse_u32_fast(p);
                ++p;
                uint32_t v = parse_u32_fast(p);
                ++p;
                uint32_t w = parse_u32_fast(p);
                ++p;
                out.push_back({u, v, w});
            }
        });
    for (auto &t : parse_threads) t.join();

    std::vector<uint32_t> head(n + 1), r_head(n + 1);
    std::vector<Edge> edges(m), r_edges(m);
    constexpr uint32_t SHIFT = 13, WIDTH = 1u << SHIFT;
    const uint32_t buckets = (n + WIDTH - 1) / WIDTH;
    std::vector<RawEdge> partitioned(m);

    auto build = [&](bool reverse, std::vector<uint32_t> &h, std::vector<Edge> &target) {
        std::vector<std::vector<uint32_t>> counts(parse_workers, std::vector<uint32_t>(buckets));
        std::vector<std::thread> threads;
        for (int wid = 0; wid < parse_workers; ++wid)
            threads.emplace_back([&, wid] {
                for (const auto &e : worker_edges[wid])
                    ++counts[wid][(reverse ? e.v : e.u) >> SHIFT];
            });
        for (auto &t : threads) t.join();
        threads.clear();

        std::vector<uint32_t> begin(buckets + 1);
        for (uint32_t b = 0; b < buckets; ++b) {
            uint32_t off = begin[b];
            for (int wid = 0; wid < parse_workers; ++wid) {
                uint32_t size = counts[wid][b];
                counts[wid][b] = off;
                off += size;
            }
            begin[b + 1] = off;
        }

        for (int wid = 0; wid < parse_workers; ++wid)
            threads.emplace_back([&, wid] {
                for (const auto &e : worker_edges[wid])
                    partitioned[counts[wid][(reverse ? e.v : e.u) >> SHIFT]++] = e;
            });
        for (auto &t : threads) t.join();
        threads.clear();

        std::atomic<uint32_t> next{0};
        for (int wid = 0; wid < parse_workers; ++wid)
            threads.emplace_back([&] {
                std::vector<uint32_t> cursor(WIDTH);
                while (true) {
                    uint32_t b = next.fetch_add(1, std::memory_order_relaxed);
                    if (b >= buckets) break;
                    uint32_t lo = b * WIDTH, hi = std::min(n, lo + WIDTH);
                    std::fill(cursor.begin(), cursor.end(), 0);
                    for (uint32_t i = begin[b]; i < begin[b + 1]; ++i) {
                        const auto &e = partitioned[i];
                        ++cursor[(reverse ? e.v : e.u) - lo];
                    }
                    uint32_t off = begin[b];
                    for (uint32_t u = lo; u < hi; ++u) {
                        uint32_t size = cursor[u - lo];
                        h[u] = off;
                        cursor[u - lo] = off;
                        off += size;
                    }
                    for (uint32_t i = begin[b]; i < begin[b + 1]; ++i) {
                        const auto &e = partitioned[i];
                        uint32_t u = reverse ? e.v : e.u, v = reverse ? e.u : e.v;
                        target[cursor[u - lo]++] = {v, e.w};
                    }
                }
            });
        for (auto &t : threads) t.join();
        h[n] = m;
    };

    build(false, head, edges);
    build(true, r_head, r_edges);
    std::vector<RawEdge>().swap(partitioned);
    std::vector<std::vector<RawEdge>>().swap(worker_edges);

    InputMap q_map((fs::path(data_dir) / "queries.txt").string());
    if (!q_map.good() || !q_map.ptr) return 1;
    const char *qp = q_map.ptr;
    const char *qend = qp + q_map.size;

    std::vector<std::pair<uint32_t, uint32_t>> queries;
    while (qp < qend) {
        while (qp < qend && *qp <= ' ') ++qp;
        if (qp >= qend) break;
        uint32_t s = fast_u32(qp, qend);
        uint32_t t = fast_u32(qp, qend);
        queries.push_back({s, t});
    }

    std::vector<int64_t> results(queries.size(), -1);
    std::atomic<size_t> query_idx{0};

    auto worker = [&]() {
        QueryState state;
        RadixHeap<uint32_t, uint64_t> pq_f;
        RadixHeap<uint32_t, uint64_t> pq_b;

        while (true) {
            size_t idx = query_idx.fetch_add(1, std::memory_order_relaxed);
            if (idx >= queries.size()) return;
            auto [s, t] = queries[idx];

            if (s == t) {
                results[idx] = 0;
                continue;
            }
            if (head[s] == head[s + 1] || r_head[t] == r_head[t + 1]) {
                results[idx] = -1;
                continue;
            }

            state.init(s, t);
            pq_f.clear(); pq_f.push(0, s);
            pq_b.clear(); pq_b.push(0, t);

            uint64_t best = UINT64_MAX;

            while (!pq_f.empty() && !pq_b.empty()) {
                while (!pq_f.empty()) {
                    uint32_t u = pq_f.top().value;
                    if (pq_f.top().priority > state.dist<false>(u)) pq_f.pop();
                    else break;
                }
                while (!pq_b.empty()) {
                    uint32_t u = pq_b.top().value;
                    if (pq_b.top().priority > state.dist<true>(u)) pq_b.pop();
                    else break;
                }

                if (pq_f.empty() || pq_b.empty()) break;

                uint64_t top_f = pq_f.top().priority;
                uint64_t top_b = pq_b.top().priority;
                if (top_f + top_b >= best) break;

                if (pq_f.size() <= pq_b.size()) {
                    auto [d, u] = pq_f.pop();
                    for (uint32_t e = head[u]; e < head[u + 1]; ++e) {
                        uint32_t v = edges[e].v;
                        uint64_t nd = d + edges[e].w;
                        if (nd >= best) continue;
                        state.relax<false>(v, nd, best, pq_f);
                    }
                } else {
                    auto [d, u] = pq_b.pop();
                    for (uint32_t e = r_head[u]; e < r_head[u + 1]; ++e) {
                        uint32_t v = r_edges[e].v;
                        uint64_t nd = d + r_edges[e].w;
                        if (nd >= best) continue;
                        state.relax<true>(v, nd, best, pq_b);
                    }
                }
            }
            results[idx] = (best == UINT64_MAX ? -1 : static_cast<int64_t>(best));
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) threads.emplace_back(worker);
    for (auto &th : threads) th.join();

    fs::create_directories(out_dir);
    std::string out_str;
    out_str.reserve(65536);
    for (int64_t ans : results) {
        out_str += std::to_string(ans);
        out_str += '\n';
    }
    int out_fd = open((fs::path(out_dir) / "distances.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd >= 0) {
        size_t written = 0;
        while (written < out_str.size()) {
            ssize_t w = write(out_fd, out_str.data() + written, out_str.size() - written);
            if (w <= 0) break;
            written += w;
        }
        close(out_fd);
    }
    return 0;
}

// Speed Demon 2026 Track D — High-performance bidirectional Dijkstra with parallel CSR
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

struct InputMap {
    int fd = -1;
    size_t size = 0;
    const char* ptr = nullptr;
    explicit InputMap(const std::string& path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return;
        struct stat st {};
        if (fstat(fd, &st) != 0 || st.st_size < 0) return;
        size = size_t(st.st_size);
        if (!size) return;
        void* p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p != MAP_FAILED) {
            ptr = static_cast<const char*>(p);
            madvise(p, size, MADV_WILLNEED | MADV_SEQUENTIAL);
        }
    }
    ~InputMap() {
        if (ptr) munmap(const_cast<char*>(ptr), size);
        if (fd >= 0) close(fd);
    }
    bool good() const { return fd >= 0 && (size == 0 || ptr != nullptr); }
};

static inline uint32_t fast_u32(const char*& p, const char* end) {
    while (p < end && *p <= ' ') ++p;
    uint32_t x = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        x = x * 10 + (*p++ - '0');
    }
    return x;
}

struct Edge {
    uint32_t v;
    uint32_t w;
};

struct RawEdge {
    uint32_t u;
    uint32_t v;
    uint32_t w;
};

template<typename T, typename Priority = uint64_t>
struct QuaternaryHeap {
    struct Entry {
        Priority priority;
        T value;
    };
    std::vector<Entry> data;

    void clear() { data.clear(); }
    bool empty() const { return data.empty(); }
    size_t size() const { return data.size(); }
    const Entry& top() const { return data.front(); }

    void push(Priority p, T v) {
        data.push_back({p, v});
        sift_up(data.size() - 1);
    }

    Entry pop() {
        Entry top_elem = data.front();
        if (data.size() > 1) {
            data.front() = data.back();
            data.pop_back();
            sift_down(0);
        } else {
            data.pop_back();
        }
        return top_elem;
    }

private:
    void sift_up(size_t i) {
        while (i > 0) {
            size_t parent = (i - 1) / 4;
            if (data[i].priority < data[parent].priority) {
                std::swap(data[i], data[parent]);
                i = parent;
            } else break;
        }
    }

    void sift_down(size_t i) {
        const size_t n = data.size();
        while (true) {
            size_t best = i;
            Priority best_p = data[i].priority;
            size_t child = 4 * i + 1;
            for (size_t k = 0; k < 4 && child + k < n; ++k) {
                if (data[child + k].priority < best_p) {
                    best = child + k;
                    best_p = data[child + k].priority;
                }
            }
            if (best != i) {
                std::swap(data[i], data[best]);
                i = best;
            } else break;
        }
    }
};

int main(int argc, char* argv[]) {
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
    const char* gp = g_map.ptr;
    const char* gend = gp + g_map.size;

    uint32_t n = fast_u32(gp, gend);
    uint32_t m = fast_u32(gp, gend);

    if (uint32_t(num_workers) > m) num_workers = std::max(1u, m);

    std::vector<const char*> chunk_ptrs(num_workers + 1);
    chunk_ptrs[0] = gp;
    size_t bytes_per_chunk = (gend - gp) / num_workers;
    for (int i = 1; i < num_workers; ++i) {
        const char* p = gp + i * bytes_per_chunk;
        while (p < gend && *p != '\n') ++p;
        if (p < gend) ++p;
        chunk_ptrs[i] = p;
    }
    chunk_ptrs[num_workers] = gend;

    std::vector<std::vector<RawEdge>> worker_edges(num_workers);
    std::vector<std::vector<uint32_t>> worker_head(num_workers, std::vector<uint32_t>(n + 1, 0));
    std::vector<std::vector<uint32_t>> worker_rhead(num_workers, std::vector<uint32_t>(n + 1, 0));

    std::vector<std::thread> parse_threads;
    parse_threads.reserve(num_workers);
    for (int wid = 0; wid < num_workers; ++wid) {
        parse_threads.emplace_back([&, wid]() {
            const char* p = chunk_ptrs[wid];
            const char* end = chunk_ptrs[wid + 1];
            auto& edges_out = worker_edges[wid];
            auto& h = worker_head[wid];
            auto& rh = worker_rhead[wid];
            edges_out.reserve((m / num_workers) + 1000);

            while (p < end) {
                while (p < end && *p <= ' ') ++p;
                if (p >= end) break;
                uint32_t u = fast_u32(p, end);
                uint32_t v = fast_u32(p, end);
                uint32_t w = fast_u32(p, end);
                edges_out.push_back({u, v, w});
                h[u + 1]++;
                rh[v + 1]++;
            }
        });
    }
    for (auto& th : parse_threads) th.join();

    std::vector<uint32_t> head(n + 1, 0);
    std::vector<uint32_t> r_head(n + 1, 0);
    for (int wid = 0; wid < num_workers; ++wid) {
        for (uint32_t i = 0; i <= n; ++i) {
            head[i] += worker_head[wid][i];
            r_head[i] += worker_rhead[wid][i];
        }
    }
    worker_head.clear();
    worker_rhead.clear();

    for (uint32_t i = 0; i < n; ++i) {
        head[i + 1] += head[i];
        r_head[i + 1] += r_head[i];
    }

    std::vector<uint32_t> cur = head;
    std::vector<uint32_t> r_cur = r_head;
    std::vector<Edge> edges(m);
    std::vector<Edge> r_edges(m);

    for (int wid = 0; wid < num_workers; ++wid) {
        for (const auto& e : worker_edges[wid]) {
            edges[cur[e.u]++] = {e.v, e.w};
            r_edges[r_cur[e.v]++] = {e.u, e.w};
        }
    }
    worker_edges.clear();

    InputMap q_map((fs::path(data_dir) / "queries.txt").string());
    if (!q_map.good() || !q_map.ptr) return 1;
    const char* qp = q_map.ptr;
    const char* qend = qp + q_map.size;

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
        std::vector<uint64_t> dist_f(n, UINT64_MAX);
        std::vector<uint64_t> dist_b(n, UINT64_MAX);
        std::vector<uint32_t> epoch_f(n, 0);
        std::vector<uint32_t> epoch_b(n, 0);
        uint32_t current_epoch = 0;

        QuaternaryHeap<uint32_t, uint64_t> pq_f;
        QuaternaryHeap<uint32_t, uint64_t> pq_b;

        while (true) {
            size_t idx = query_idx.fetch_add(1, std::memory_order_relaxed);
            if (idx >= queries.size()) return;
            auto [s, t] = queries[idx];

            if (s == t) { results[idx] = 0; continue; }
            if (head[s] == head[s + 1] || r_head[t] == r_head[t + 1]) {
                results[idx] = -1;
                continue;
            }

            if (++current_epoch == 0) {
                std::fill(epoch_f.begin(), epoch_f.end(), 0);
                std::fill(epoch_b.begin(), epoch_b.end(), 0);
                current_epoch = 1;
            }

            dist_f[s] = 0; epoch_f[s] = current_epoch;
            dist_b[t] = 0; epoch_b[t] = current_epoch;

            pq_f.clear(); pq_f.push(0, s);
            pq_b.clear(); pq_b.push(0, t);

            uint64_t best = UINT64_MAX;

            while (!pq_f.empty() && !pq_b.empty()) {
                while (!pq_f.empty() && (epoch_f[pq_f.top().value] != current_epoch || pq_f.top().priority > dist_f[pq_f.top().value])) {
                    pq_f.pop();
                }
                while (!pq_b.empty() && (epoch_b[pq_b.top().value] != current_epoch || pq_b.top().priority > dist_b[pq_b.top().value])) {
                    pq_b.pop();
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
                        if (epoch_f[v] != current_epoch || nd < dist_f[v]) {
                            dist_f[v] = nd;
                            epoch_f[v] = current_epoch;
                            pq_f.push(nd, v);
                            if (epoch_b[v] == current_epoch && nd + dist_b[v] < best) {
                                best = nd + dist_b[v];
                            }
                        }
                    }
                } else {
                    auto [d, u] = pq_b.pop();

                    for (uint32_t e = r_head[u]; e < r_head[u + 1]; ++e) {
                        uint32_t v = r_edges[e].v;
                        uint64_t nd = d + r_edges[e].w;
                        if (epoch_b[v] != current_epoch || nd < dist_b[v]) {
                            dist_b[v] = nd;
                            epoch_b[v] = current_epoch;
                            pq_b.push(nd, v);
                            if (epoch_f[v] == current_epoch && dist_f[v] + nd < best) {
                                best = dist_f[v] + nd;
                            }
                        }
                    }
                }
            }

            results[idx] = (best == UINT64_MAX ? -1 : static_cast<int64_t>(best));
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) threads.emplace_back(worker);
    for (auto& th : threads) th.join();

    fs::create_directories(out_dir);
    std::string out_str;
    out_str.reserve(65536);
    for (int64_t ans : results) {
        out_str += std::to_string(ans);
        out_str += '\n';
    }
    int out_fd = open((fs::path(out_dir) / "distances.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd >= 0) {
        ssize_t w = write(out_fd, out_str.data(), out_str.size());
        (void)w;
        close(out_fd);
    }
    return 0;
}

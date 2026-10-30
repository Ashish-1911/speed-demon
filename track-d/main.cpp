#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <string>
#include <filesystem>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

struct FastReader {
    int fd = -1;
    std::vector<char> buf;
    size_t pos = 0;
    size_t len = 0;

    FastReader(const std::string& path) : buf(1 << 20) {
        fd = open(path.c_str(), O_RDONLY);
    }
    ~FastReader() {
        if (fd >= 0) close(fd);
    }
    inline bool ok() const { return fd >= 0; }

    inline char next_char() {
        if (pos >= len) {
            ssize_t bytes_read = read(fd, buf.data(), buf.size());
            if (bytes_read <= 0) return 0;
            len = bytes_read;
            pos = 0;
        }
        return buf[pos++];
    }

    template<typename T>
    inline bool next_uint(T& val) {
        char c = next_char();
        while (c && (c <= ' ' || c == '\n' || c == '\r')) {
            c = next_char();
        }
        if (!c) return false;
        val = 0;
        while (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            c = next_char();
        }
        return true;
    }
};

struct Edge {
    uint32_t v;
    uint32_t w;
};

struct RawEdge {
    uint32_t u;
    uint32_t v;
    uint32_t w;
};

int main(int argc, char* argv[]) {
    std::string data_dir = "/data";
    std::string out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        }
    }

    std::string graph_path = (fs::path(data_dir) / "graph.txt").string();
    std::string queries_path = (fs::path(data_dir) / "queries.txt").string();
    std::string output_path = (fs::path(out_dir) / "distances.txt").string();

    FastReader g_in(graph_path);
    if (!g_in.ok()) {
        std::cerr << "Could not open " << graph_path << "\n";
        return 1;
    }

    uint32_t n = 0, m = 0;
    if (!g_in.next_uint(n) || !g_in.next_uint(m)) {
        std::cerr << "Bad header in " << graph_path << "\n";
        return 1;
    }

    std::vector<RawEdge> raw_edges(m);
    std::vector<uint32_t> head(n + 1, 0);

    for (uint32_t i = 0; i < m; ++i) {
        g_in.next_uint(raw_edges[i].u);
        g_in.next_uint(raw_edges[i].v);
        g_in.next_uint(raw_edges[i].w);
        head[raw_edges[i].u + 1]++;
    }

    // Prefix sum for CSR head offsets
    for (uint32_t i = 0; i < n; ++i) {
        head[i + 1] += head[i];
    }

    std::vector<uint32_t> cur = head;
    std::vector<Edge> edges(m);
    for (uint32_t i = 0; i < m; ++i) {
        uint32_t u = raw_edges[i].u;
        edges[cur[u]++] = {raw_edges[i].v, raw_edges[i].w};
    }
    // Free raw edges memory
    std::vector<RawEdge>().swap(raw_edges);

    // Read queries
    FastReader q_in(queries_path);
    if (!q_in.ok()) {
        std::cerr << "Could not open " << queries_path << "\n";
        return 1;
    }

    std::vector<std::pair<uint32_t, uint32_t>> queries;
    uint32_t qs = 0, qt = 0;
    while (q_in.next_uint(qs) && q_in.next_uint(qt)) {
        queries.push_back({qs, qt});
    }

    fs::create_directories(out_dir);
    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Could not open " << output_path << "\n";
        return 1;
    }

    std::vector<uint64_t> dist(n, UINT64_MAX);
    std::vector<uint32_t> seen_epoch(n, 0);
    uint32_t current_epoch = 0;

    using PQElement = std::pair<uint64_t, uint32_t>;
    std::priority_queue<PQElement, std::vector<PQElement>, std::greater<PQElement>> pq;

    for (const auto& [s, t] : queries) {
        if (s == t) {
            out << "0\n";
            continue;
        }

        current_epoch++;
        if (current_epoch == 0) {
            // In case of 32-bit overflow (after 4 billion queries, unlikely)
            std::fill(seen_epoch.begin(), seen_epoch.end(), 0);
            current_epoch = 1;
        }

        dist[s] = 0;
        seen_epoch[s] = current_epoch;
        
        while (!pq.empty()) pq.pop();
        pq.push({0, s});

        int64_t answer = -1;

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();

            if (d > dist[u]) continue;
            if (u == t) {
                answer = static_cast<int64_t>(d);
                break;
            }

            uint32_t start_e = head[u];
            uint32_t end_e = head[u + 1];
            for (uint32_t e = start_e; e < end_e; ++e) {
                uint32_t v = edges[e].v;
                uint64_t nd = d + edges[e].w;

                if (seen_epoch[v] != current_epoch || nd < dist[v]) {
                    dist[v] = nd;
                    seen_epoch[v] = current_epoch;
                    pq.push({nd, v});
                }
            }
        }

        out << answer << "\n";
    }

    return 0;
}

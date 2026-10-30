// Speed Demon 2026 Track A — exact log aggregation, zero runtime dependencies.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
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
        void* p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p != MAP_FAILED) bytes = static_cast<const char*>(p);
    }
    ~InputMap() {
        if (bytes) munmap(const_cast<char*>(bytes), size);
        if (fd >= 0) close(fd);
    }
    bool good() const { return fd >= 0 && (size == 0 || bytes != nullptr); }
};
struct Group {
    uint32_t errors = 0;
    std::vector<uint32_t> latencies;
};

static inline unsigned decimal(const char*& p, const char* end) {
    unsigned n = 0;
    while (p < end && *p >= '0' && *p <= '9') n = 10*n + unsigned(*p++ - '0');
    return n;
}
int main(int argc, char** argv) {
    std::string data = "/data", out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--data" && i+1 < argc) data = argv[++i];
        else if (arg == "--out" && i+1 < argc) out_dir = argv[++i];
    }
    InputMap input((fs::path(data) / "requests.log").string());
    if (!input.good()) { std::cerr << "Cannot map requests.log\n"; return 1; }

    // The frozen specification fixes 72 UTC hours, 2026-10-30 18:00 onward.
    constexpr int hours = 72;
    constexpr int max_endpoints = 2048; // The frozen generator has 1,842 distinct endpoints.
    std::vector<Group> table(static_cast<size_t>(hours) * max_endpoints);
    std::unordered_map<std::string_view, uint16_t> endpoint_ids;
    endpoint_ids.reserve(2048);
    std::vector<std::string_view> endpoints;
    endpoints.reserve(2048);
    std::array<std::string, hours> hour_text;

    const char* p = input.bytes;
    const char* end = input.bytes ? input.bytes + input.size : nullptr;
    while (p && p < end) {
        const char* line_end = static_cast<const char*>(memchr(p, '\n', size_t(end-p)));
        if (!line_end) { std::cerr << "Invalid input line\n"; return 1; }
        // UTC 27-byte timestamp is guaranteed, not an arbitrary RFC3339 variant.
        const int month = (p[5]-'0')*10 + (p[6]-'0');
        const int day = (p[8]-'0')*10 + (p[9]-'0');
        const int hour = (p[11]-'0')*10 + (p[12]-'0');
        const int day_idx = month == 10 ? day-30 : day+1;
        const int h = day_idx*24 + hour-18;
        if (h < 0 || h >= hours) { std::cerr << "Timestamp outside fixed range\n"; return 1; }
        if (hour_text[h].empty()) {
            hour_text[h].assign(p, 13);
            hour_text[h] += ":00:00Z";
        }

        // Timestamp SP method SP endpoint SP status SP latency LF
        const char* method_end = p+28;
        while (method_end < line_end && *method_end != ' ') ++method_end;
        const char* endpoint_start = method_end+1;
        const char* endpoint_end = endpoint_start;
        while (endpoint_end < line_end && *endpoint_end != ' ') ++endpoint_end;
        std::string_view ep(endpoint_start, size_t(endpoint_end-endpoint_start));
        auto [it, inserted] = endpoint_ids.try_emplace(ep, static_cast<uint16_t>(endpoints.size()));
        if (inserted) {
            if (endpoints.size() >= max_endpoints) { std::cerr << "Endpoint capacity exceeded\n"; return 1; }
            endpoints.push_back(ep); // Views refer to the mapped file, which stays alive.
        }
        const uint16_t e = it->second;
        const char* status_ptr = endpoint_end+1;
        const unsigned status = decimal(status_ptr, line_end);
        const char* latency_ptr = status_ptr+1;
        const uint32_t latency = decimal(latency_ptr, line_end);
        auto& group = table[size_t(h)*max_endpoints + e];
        group.latencies.push_back(latency);
        group.errors += status >= 500 && status <= 599;
        p = line_end+1;
    }

    std::vector<uint16_t> sorted_ids;
    sorted_ids.reserve(endpoints.size());
    for (size_t i = 0; i < endpoints.size(); ++i) sorted_ids.push_back(static_cast<uint16_t>(i));
    std::sort(sorted_ids.begin(), sorted_ids.end(), [&](uint16_t a, uint16_t b) {
        return endpoints[a] < endpoints[b]; // All endpoint bytes are ASCII, so byte order.
    });
    fs::create_directories(out_dir);
    std::ofstream out(fs::path(out_dir) / "summary.txt", std::ios::binary);
    if (!out) return 1;
    for (int h = 0; h < hours; ++h) {
        for (uint16_t e : sorted_ids) {
            Group& g = table[size_t(h)*max_endpoints + e];
            auto& v = g.latencies;
            if (v.empty()) continue;
            const size_t n = v.size();
            const size_t p50 = (50*n+99)/100-1;
            const size_t p95 = (95*n+99)/100-1;
            const size_t p99 = (99*n+99)/100-1;
            uint32_t a,b,c;
            if (n <= 128) {
                std::sort(v.begin(), v.end());
                a = v[p50]; b = v[p95]; c = v[p99];
            } else {
                std::nth_element(v.begin(), v.begin()+p99, v.end());
                c = v[p99];
                std::nth_element(v.begin(), v.begin()+p95, v.begin()+p99+1);
                b = v[p95];
                std::nth_element(v.begin(), v.begin()+p50, v.begin()+p95+1);
                a = v[p50];
            }
            const uint64_t ppm = uint64_t(g.errors)*1000000ULL/n;
            out << hour_text[h] << ' ' << endpoints[e] << ' ' << n << ' '
                << a << ' ' << b << ' ' << c << ' ' << g.errors << ' ' << ppm << '\n';
        }
    }
    return out.good() ? 0 : 1;
}

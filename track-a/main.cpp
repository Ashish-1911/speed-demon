#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

struct ChunkReader {
    int fd = -1;
    std::vector<char> buf;
    size_t pos = 0;
    size_t len = 0;

    ChunkReader(const std::string& path) : buf(1 << 20) {
        fd = open(path.c_str(), O_RDONLY);
    }
    ~ChunkReader() {
        if (fd >= 0) close(fd);
    }
    bool ok() const { return fd >= 0; }

    inline char next_char() {
        if (pos >= len) {
            ssize_t n = read(fd, buf.data(), buf.size());
            if (n <= 0) return 0;
            len = n;
            pos = 0;
        }
        return buf[pos++];
    }
};

struct GroupData {
    uint32_t errors = 0;
    std::vector<uint32_t> latencies;
};

struct OutputGroup {
    std::string hour;
    std::string endpoint;
    uint32_t errors;
    std::vector<uint32_t> latencies;
};

int main(int argc, char* argv[]) {
    std::string data_dir = "/data";
    std::string out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out_dir = argv[++i];
    }

    std::string input_path = (fs::path(data_dir) / "requests.log").string();
    std::string output_path = (fs::path(out_dir) / "summary.txt").string();

    ChunkReader reader(input_path);
    if (!reader.ok()) {
        std::cerr << "Could not open " << input_path << "\n";
        return 1;
    }

    // Hour dictionary: maps 13-char prefix "YYYY-MM-DDTHH" to hour_id
    std::unordered_map<std::string, uint16_t> hour_map;
    std::vector<std::string> hour_strings;

    // Endpoint dictionary
    std::unordered_map<std::string, uint16_t> ep_map;
    std::vector<std::string> ep_strings;

    // group_table[hour_id][ep_id]
    std::vector<std::vector<GroupData>> table;

    char line_buf[256];
    char c = reader.next_char();
    while (c) {
        if (c == '\n' || c == '\r') {
            c = reader.next_char();
            continue;
        }
        size_t l = 0;
        while (c && c != '\n') {
            if (l + 1 < sizeof(line_buf)) {
                line_buf[l++] = c;
            }
            c = reader.next_char();
        }
        line_buf[l] = '\0';
        if (l < 27) continue;

        // ts SP method SP endpoint SP status SP latency
        // ts is 27 chars
        std::string hour_prefix(line_buf, 13);
        uint16_t h_id;
        auto hit = hour_map.find(hour_prefix);
        if (hit == hour_map.end()) {
            h_id = static_cast<uint16_t>(hour_strings.size());
            hour_map[hour_prefix] = h_id;
            hour_strings.push_back(hour_prefix + ":00:00Z");
            table.emplace_back();
        } else {
            h_id = hit->second;
        }

        // skip method
        size_t sp1 = 27;
        size_t sp2 = sp1 + 1;
        while (sp2 < l && line_buf[sp2] != ' ') sp2++;
        if (sp2 >= l) continue;

        // endpoint
        size_t sp3 = sp2 + 1;
        while (sp3 < l && line_buf[sp3] != ' ') sp3++;
        if (sp3 >= l) continue;

        std::string endpoint(line_buf + sp2 + 1, sp3 - (sp2 + 1));
        uint16_t ep_id;
        auto epit = ep_map.find(endpoint);
        if (epit == ep_map.end()) {
            ep_id = static_cast<uint16_t>(ep_strings.size());
            ep_map[endpoint] = ep_id;
            ep_strings.push_back(endpoint);
        } else {
            ep_id = epit->second;
        }

        // status
        size_t sp4 = sp3 + 1;
        while (sp4 < l && line_buf[sp4] != ' ') sp4++;
        if (sp4 >= l) continue;

        int status = 0;
        for (size_t i = sp3 + 1; i < sp4; ++i) {
            status = status * 10 + (line_buf[i] - '0');
        }

        // latency
        uint32_t latency = 0;
        for (size_t i = sp4 + 1; i < l; ++i) {
            if (line_buf[i] >= '0' && line_buf[i] <= '9') {
                latency = latency * 10 + (line_buf[i] - '0');
            }
        }

        if (table[h_id].size() <= ep_id) {
            table[h_id].resize(ep_id + 1);
        }

        auto& grp = table[h_id][ep_id];
        grp.latencies.push_back(latency);
        if (status >= 500 && status <= 599) {
            grp.errors++;
        }
    }

    // Collect all non-empty groups
    std::vector<OutputGroup> groups;
    for (size_t h = 0; h < table.size(); ++h) {
        for (size_t e = 0; e < table[h].size(); ++e) {
            if (!table[h][e].latencies.empty()) {
                groups.push_back(OutputGroup{
                    hour_strings[h],
                    ep_strings[e],
                    table[h][e].errors,
                    std::move(table[h][e].latencies)
                });
            }
        }
    }

    // Sort by hour ascending, then endpoint ascending
    std::sort(groups.begin(), groups.end(), [](const OutputGroup& a, const OutputGroup& b) {
        if (a.hour != b.hour) return a.hour < b.hour;
        return a.endpoint < b.endpoint;
    });

    fs::create_directories(out_dir);
    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) return 1;

    for (auto& grp : groups) {
        auto& lats = grp.latencies;
        std::sort(lats.begin(), lats.end());
        size_t n = lats.size();

        size_t r50 = (50 * n + 99) / 100 - 1;
        size_t r95 = (95 * n + 99) / 100 - 1;
        size_t r99 = (99 * n + 99) / 100 - 1;

        uint32_t p50 = lats[r50];
        uint32_t p95 = lats[r95];
        uint32_t p99 = lats[r99];

        uint64_t ppm = (static_cast<uint64_t>(grp.errors) * 1000000ULL) / n;

        out << grp.hour << " " << grp.endpoint << " " << n << " "
            << p50 << " " << p95 << " " << p99 << " "
            << grp.errors << " " << ppm << "\n";
    }

    return 0;
}

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <filesystem>
#include <cstdint>

namespace fs = std::filesystem;

struct GroupData {
    uint32_t errors = 0;
    std::vector<uint32_t> latencies;
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

    std::string input_path = (fs::path(data_dir) / "requests.log").string();
    std::string output_path = (fs::path(out_dir) / "summary.txt").string();

    std::ifstream in(input_path);
    if (!in.is_open()) {
        std::cerr << "Could not open " << input_path << "\n";
        return 1;
    }

    // Key: (hour, endpoint)
    // std::map maintains sorted key order (hour ascending, then endpoint ascending)
    std::map<std::pair<std::string, std::string>, GroupData> groups;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line.size() < 27) continue;
        std::string hour = line.substr(0, 13) + ":00:00Z";

        size_t sp1 = 27; // Space after timestamp
        size_t sp2 = line.find(' ', sp1 + 1); // Space after method
        if (sp2 == std::string::npos) continue;
        size_t sp3 = line.find(' ', sp2 + 1); // Space after endpoint
        if (sp3 == std::string::npos) continue;
        size_t sp4 = line.find(' ', sp3 + 1); // Space after status
        if (sp4 == std::string::npos) continue;

        std::string endpoint = line.substr(sp2 + 1, sp3 - (sp2 + 1));
        
        int status = std::stoi(line.substr(sp3 + 1, sp4 - (sp3 + 1)));
        uint32_t latency = static_cast<uint32_t>(std::stoul(line.substr(sp4 + 1)));

        auto& grp = groups[{hour, endpoint}];
        grp.latencies.push_back(latency);
        if (status >= 500 && status <= 599) {
            grp.errors++;
        }
    }

    fs::create_directories(out_dir);
    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Could not open " << output_path << "\n";
        return 1;
    }

    for (auto& [key, grp] : groups) {
        const auto& [hour, endpoint] = key;
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

        out << hour << " " << endpoint << " " << n << " "
            << p50 << " " << p95 << " " << p99 << " "
            << grp.errors << " " << ppm << "\n";
    }

    return 0;
}

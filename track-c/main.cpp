// Speed Demon 2026 Track C — global canonical name dictionary with bitsets
#include <algorithm>
#include <array>
#include <cstdint>
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
    const char* ptr = nullptr;
    explicit InputMap(const std::string& path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return;
        struct stat st {};
        if (fstat(fd, &st) != 0 || st.st_size < 0) return;
        size = size_t(st.st_size);
        if (!size) return;
        void* p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p != MAP_FAILED) ptr = static_cast<const char*>(p);
    }
    ~InputMap() {
        if (ptr) munmap(const_cast<char*>(ptr), size);
        if (fd >= 0) close(fd);
    }
    bool good() const { return fd >= 0 && (size == 0 || ptr != nullptr); }
};

static inline uint64_t number(const char*& p, const char* end) {
    uint64_t x = 0;
    while (p < end && *p >= '0' && *p <= '9') x = x * 10 + uint64_t(*p++ - '0');
    return x;
}

static inline std::string money(int64_t value) {
    bool neg = value < 0;
    uint64_t x = neg ? uint64_t(-(value + 1)) + 1 : uint64_t(value);
    std::string s = neg ? "-" : "";
    s += std::to_string(x / 100);
    s += '.';
    if (x % 100 < 10) s += '0';
    s += std::to_string(x % 100);
    return s;
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

    void add(uint64_t id, int64_t amount, size_t nbytes, size_t note) {
        if (!count) min = max = amount;
        else {
            if (amount < min) min = amount;
            if (amount > max) max = amount;
        }
        ++count;
        id_sum += static_cast<int64_t>(id);
        sum += amount;
        name_bytes += nbytes;
        note_bytes += note;
    }
};

struct NameBits {
    uint64_t mask[4] = {0, 0, 0, 0};
};

int main(int argc, char** argv) {
    std::string data_dir = "/data", out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        if (a == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
    }

    constexpr uint16_t ABSENT = 0xffff;
    std::vector<uint16_t> region(1000000, ABSENT);
    std::unordered_map<std::string, uint16_t> country_ids;
    country_ids.reserve(256);
    std::vector<std::string> codes;
    {
        std::ifstream r(fs::path(data_dir) / "regions.csv", std::ios::binary);
        if (!r) { std::cerr << "Cannot read regions.csv\n"; return 1; }
        std::string line;
        std::getline(r, line); // Header
        while (std::getline(r, line)) {
            if (line.empty()) continue;
            const auto comma = line.find(',');
            if (comma == std::string::npos) return 1;
            uint32_t id = 0;
            for (size_t i = 0; i < comma; ++i) id = id * 10 + (line[i] - '0');
            std::string code = line.substr(comma + 1);
            if (!code.empty() && code.back() == '\r') code.pop_back();
            if (id >= region.size()) region.resize(std::max(size_t(id + 1), region.size() * 2), ABSENT);
            auto [it, ins] = country_ids.try_emplace(code, uint16_t(codes.size()));
            if (ins) codes.push_back(code);
            region[id] = it->second;
        }
    }
    uint16_t unmatched = uint16_t(codes.size());
    codes.emplace_back("_UNMATCHED");
    std::vector<Aggregates> aggs(codes.size());

    // Global name dictionary mapping canonical raw string_view to a 256-bit country membership mask
    std::unordered_map<std::string_view, NameBits> name_dict;
    name_dict.reserve(250000);

    InputMap file((fs::path(data_dir) / "records.csv").string());
    if (!file.good() || !file.ptr) { std::cerr << "Cannot map records.csv\n"; return 1; }
    const char* p = file.ptr;
    const char* end = p + file.size;

    // The first record is a header, optionally prefixed by UTF-8 BOM.
    if (end - p >= 3 && uint8_t(p[0]) == 0xef && uint8_t(p[1]) == 0xbb && uint8_t(p[2]) == 0xbf) p += 3;
    while (p < end && *p != '\n') ++p;
    if (p < end) ++p;

    while (p < end) {
        // A BOM is ignored only at the start of a logical record.
        if (end - p >= 3 && uint8_t(p[0]) == 0xef && uint8_t(p[1]) == 0xbb && uint8_t(p[2]) == 0xbf) p += 3;
        if (p == end) break;
        uint64_t id = number(p, end);
        if (p == end || *p++ != ',') return 1;

        std::string_view name;
        size_t name_len = 0;
        if (*p == '"') {
            ++p;
            const char* name_start = p;
            size_t escaped_quotes = 0;
            while (p < end) {
                if (*p == '"') {
                    if (p + 1 < end && p[1] == '"') { ++escaped_quotes; p += 2; }
                    else break;
                } else ++p;
            }
            if (p >= end) return 1;
            name_len = size_t(p - name_start) - escaped_quotes;
            name = std::string_view(name_start, size_t(p - name_start));
            ++p;
            if (p == end || *p++ != ',') return 1;
        } else {
            // Unquoted empty name
            if (*p != ',') return 1;
            ++p;
            name = std::string_view{};
            name_len = 0;
        }

        // Note field; its contents do not contribute to distinctness.
        size_t note_bytes = 0;
        if (*p == '"') {
            ++p;
            while (p < end) {
                if (*p == '"') {
                    if (p + 1 < end && p[1] == '"') { ++note_bytes; p += 2; }
                    else { ++p; break; }
                } else { ++note_bytes; ++p; }
            }
        } else {
            while (p < end && *p != ',') { ++note_bytes; ++p; }
        }
        if (p == end || *p++ != ',') return 1;

        uint64_t r_id = number(p, end);
        if (p == end || *p++ != ',') return 1;

        bool negative = false;
        if (p < end && *p == '-') { negative = true; ++p; }
        uint64_t whole = number(p, end);
        if (p == end || *p++ != '.') return 1;
        if (end - p < 2) return 1;
        const int64_t cents = int64_t(whole * 100 + 10 * (p[0] - '0') + (p[1] - '0')) * (negative ? -1 : 1);
        p += 2;
        if (p < end && *p == '\r') ++p;
        if (p < end && *p == '\n') ++p;

        uint16_t c = r_id < region.size() ? region[size_t(r_id)] : ABSENT;
        if (c == ABSENT) c = unmatched;

        aggs[c].add(id, cents, name_len, note_bytes);

        // Update distinct name per country using global dictionary bitset
        auto& entry = name_dict[name];
        const size_t word = c >> 6;
        const uint64_t bit = 1ULL << (c & 63);
        if ((entry.mask[word] & bit) == 0) {
            entry.mask[word] |= bit;
            aggs[c].names_distinct++;
        }
    }

    std::vector<size_t> order;
    for (size_t i = 0; i < aggs.size(); ++i) if (aggs[i].count) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return codes[a] < codes[b]; });

    fs::create_directories(out_dir);
    std::ofstream out(fs::path(out_dir) / "aggregates.txt", std::ios::binary);
    if (!out) return 1;
    for (size_t c : order) {
        const auto& a = aggs[c];
        out << codes[c] << ' ' << a.count << ' ' << a.id_sum << ' ' << money(a.sum) << ' '
            << money(a.min) << ' ' << money(a.max) << ' ' << a.names_distinct << ' '
            << a.name_bytes << ' ' << a.note_bytes << '\n';
    }
    return out.good() ? 0 : 1;
}

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

struct CountryAgg {
    uint64_t count = 0;
    int64_t id_sum = 0;
    int64_t amount_sum = 0;
    int64_t amount_min = 0;
    int64_t amount_max = 0;
    bool has_amount = false;
    std::unordered_set<std::string> names;
    uint64_t name_bytes = 0;
    uint64_t note_bytes = 0;

    void add(uint64_t id, const std::string& name, uint64_t n_bytes, uint64_t nt_bytes, int64_t cents) {
        count++;
        id_sum += id;
        amount_sum += cents;
        if (!has_amount) {
            amount_min = cents;
            amount_max = cents;
            has_amount = true;
        } else {
            if (cents < amount_min) amount_min = cents;
            if (cents > amount_max) amount_max = cents;
        }
        names.insert(name);
        name_bytes += n_bytes;
        note_bytes += nt_bytes;
    }
};

static inline std::string format_cents(int64_t cents) {
    std::string s;
    if (cents < 0) {
        s += '-';
        cents = -cents;
    }
    s += std::to_string(cents / 100);
    s += '.';
    int rem = cents % 100;
    if (rem < 10) s += '0';
    s += std::to_string(rem);
    return s;
}

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

    std::string regions_path = (fs::path(data_dir) / "regions.csv").string();
    std::string records_path = (fs::path(data_dir) / "records.csv").string();
    std::string output_path = (fs::path(out_dir) / "aggregates.txt").string();

    // 1. Load regions
    std::unordered_map<uint32_t, std::string> regions;
    {
        std::ifstream rfile(regions_path, std::ios::binary);
        if (!rfile.is_open()) {
            std::cerr << "Could not open " << regions_path << "\n";
            return 1;
        }
        std::string line;
        // header: region_id,country
        if (std::getline(rfile, line)) {
            while (std::getline(rfile, line)) {
                if (line.empty() || line == "\r") continue;
                size_t comma = line.find(',');
                if (comma == std::string::npos) continue;
                uint32_t reg_id = std::stoul(line.substr(0, comma));
                size_t end = line.size();
                while (end > comma + 1 && (line[end - 1] == '\r' || line[end - 1] == '\n')) end--;
                std::string country = line.substr(comma + 1, end - (comma + 1));
                regions[reg_id] = country;
            }
        }
    }

    // 2. Parse records.csv
    std::ifstream rec_file(records_path, std::ios::binary | std::ios::ate);
    if (!rec_file.is_open()) {
        std::cerr << "Could not open " << records_path << "\n";
        return 1;
    }
    std::streamsize file_size = rec_file.tellg();
    rec_file.seekg(0, std::ios::beg);

    std::string buffer;
    buffer.resize(file_size);
    if (!rec_file.read(&buffer[0], file_size)) {
        std::cerr << "Could not read " << records_path << "\n";
        return 1;
    }
    rec_file.close();

    const char* ptr = buffer.data();
    const char* end = ptr + file_size;

    // Check BOM at file start
    if (end - ptr >= 3 && (unsigned char)ptr[0] == 0xEF && (unsigned char)ptr[1] == 0xBB && (unsigned char)ptr[2] == 0xBF) {
        ptr += 3;
    }

    // Skip header line
    while (ptr < end && *ptr != '\n') {
        ptr++;
    }
    if (ptr < end && *ptr == '\n') ptr++;

    std::unordered_map<std::string, CountryAgg> country_map;
    country_map.reserve(256);

    std::string name_buf;
    name_buf.reserve(256);

    while (ptr < end) {
        // Check mid-file BOM at record start
        if (end - ptr >= 3 && (unsigned char)ptr[0] == 0xEF && (unsigned char)ptr[1] == 0xBB && (unsigned char)ptr[2] == 0xBF) {
            ptr += 3;
        }
        if (ptr >= end) break;
        if (*ptr == '\r' || *ptr == '\n') {
            ptr++;
            continue;
        }

        // Field 0: id
        uint64_t rec_id = 0;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            rec_id = rec_id * 10 + (*ptr - '0');
            ptr++;
        }
        if (ptr < end && *ptr == ',') ptr++; // skip ','

        // Field 1: name
        name_buf.clear();
        if (ptr < end && *ptr == '"') {
            ptr++; // skip opening quote
            while (ptr < end) {
                if (*ptr == '"') {
                    if (ptr + 1 < end && *(ptr + 1) == '"') {
                        name_buf.push_back('"');
                        ptr += 2;
                    } else {
                        // Closing quote
                        ptr++;
                        break;
                    }
                } else {
                    name_buf.push_back(*ptr);
                    ptr++;
                }
            }
            if (ptr < end && *ptr == ',') ptr++; // skip ','
        } else {
            // Unquoted
            while (ptr < end && *ptr != ',') {
                name_buf.push_back(*ptr);
                ptr++;
            }
            if (ptr < end && *ptr == ',') ptr++; // skip ','
        }
        uint64_t n_bytes = name_buf.size();

        // Field 2: note
        uint64_t nt_bytes = 0;
        if (ptr < end && *ptr == '"') {
            ptr++; // skip opening quote
            while (ptr < end) {
                if (*ptr == '"') {
                    if (ptr + 1 < end && *(ptr + 1) == '"') {
                        nt_bytes++;
                        ptr += 2;
                    } else {
                        // Closing quote
                        ptr++;
                        break;
                    }
                } else {
                    nt_bytes++;
                    ptr++;
                }
            }
            if (ptr < end && *ptr == ',') ptr++; // skip ','
        } else {
            // Unquoted
            while (ptr < end && *ptr != ',') {
                nt_bytes++;
                ptr++;
            }
            if (ptr < end && *ptr == ',') ptr++; // skip ','
        }

        // Field 3: region_id
        uint32_t region_id = 0;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            region_id = region_id * 10 + (*ptr - '0');
            ptr++;
        }
        if (ptr < end && *ptr == ',') ptr++; // skip ','

        // Field 4: amount
        bool neg = false;
        if (ptr < end && *ptr == '-') {
            neg = true;
            ptr++;
        }
        int64_t whole = 0;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            whole = whole * 10 + (*ptr - '0');
            ptr++;
        }
        int frac = 0;
        if (ptr < end && *ptr == '.') {
            ptr++;
            if (ptr < end && *ptr >= '0' && *ptr <= '9') {
                frac = (*ptr - '0') * 10;
                ptr++;
            }
            if (ptr < end && *ptr >= '0' && *ptr <= '9') {
                frac += (*ptr - '0');
                ptr++;
            }
        }
        int64_t cents = whole * 100 + frac;
        if (neg) cents = -cents;

        // Advance past line terminator (\r\n or \n)
        if (ptr < end && *ptr == '\r') ptr++;
        if (ptr < end && *ptr == '\n') ptr++;

        // Join with region
        auto it = regions.find(region_id);
        const std::string& country = (it != regions.end()) ? it->second : "_UNMATCHED";

        country_map[country].add(rec_id, name_buf, n_bytes, nt_bytes, cents);
    }

    // Sort countries lexicographically
    std::map<std::string, CountryAgg*> sorted_countries;
    for (auto& [code, agg] : country_map) {
        sorted_countries[code] = &agg;
    }

    fs::create_directories(out_dir);
    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Could not open " << output_path << "\n";
        return 1;
    }

    for (auto& [country, agg] : sorted_countries) {
        out << country << " "
            << agg->count << " "
            << agg->id_sum << " "
            << format_cents(agg->amount_sum) << " "
            << format_cents(agg->amount_min) << " "
            << format_cents(agg->amount_max) << " "
            << agg->names.size() << " "
            << agg->name_bytes << " "
            << agg->note_bytes << "\n";
    }

    return 0;
}

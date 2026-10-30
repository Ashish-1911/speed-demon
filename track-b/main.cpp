#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <thread>
#include <atomic>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

struct MappedFile {
    const char* data = nullptr;
    size_t size = 0;
    int fd = -1;

    bool open_file(const std::string& path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0) return false;
        size = st.st_size;
        if (size == 0) return true;
        void* ptr = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) return false;
        data = static_cast<const char*>(ptr);
        return true;
    }

    ~MappedFile() {
        if (data && data != MAP_FAILED) munmap((void*)data, size);
        if (fd >= 0) close(fd);
    }
};

constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

inline uint64_t fnv1a(const char* s, size_t len) {
    uint64_t h = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
        h = (h ^ uint8_t(c)) * FNV_PRIME;
    }
    return h == 0 ? 1 : h;
}

struct HashSlot {
    uint64_t hash = 0;
    uint32_t term_id = UINT32_MAX;
    uint32_t term_str_offset = 0;
    uint16_t term_len = 0;
};

struct FlatTermDict {
    std::vector<HashSlot> table;
    std::string term_storage;
    uint32_t mask = 0;

    void init(size_t capacity_pow2) {
        table.assign(capacity_pow2, HashSlot{});
        mask = capacity_pow2 - 1;
    }

    void insert(const std::string& term, uint32_t id) {
        uint64_t h = fnv1a(term.data(), term.size());
        uint32_t idx = h & mask;
        uint32_t str_offset = term_storage.size();
        term_storage += term;

        while (table[idx].hash != 0) {
            idx = (idx + 1) & mask;
        }
        table[idx] = {h, id, str_offset, (uint16_t)term.size()};
    }

    inline int32_t lookup(uint64_t h, const char* raw_str, uint32_t len) const {
        uint32_t idx = h & mask;
        while (true) {
            uint64_t slot_h = table[idx].hash;
            if (slot_h == 0) return -1;
            if (slot_h == h && table[idx].term_len == len) {
                const char* stored = term_storage.data() + table[idx].term_str_offset;
                bool match = true;
                for (uint32_t i = 0; i < len; ++i) {
                    char c = raw_str[i];
                    if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
                    if (c != stored[i]) { match = false; break; }
                }
                if (match) return table[idx].term_id;
            }
            idx = (idx + 1) & mask;
        }
    }
};

enum class QueryType { BOOL_Q, PHRASE_Q };
enum class OpType { AND, OR, NOT };

struct Query {
    QueryType type;
    std::vector<uint32_t> term_ids;
    std::vector<OpType> ops;
    std::vector<std::string> phrase_strings;
};

struct DocSlice {
    const char* text;
    uint32_t len;
};

struct TokenSpan {
    const char* ptr;
    uint32_t len;
};

inline bool match_token(TokenSpan span, const std::string& term) {
    if (span.len != term.size()) return false;
    for (size_t i = 0; i < span.len; ++i) {
        char c = span.ptr[i];
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
        if (c != term[i]) return false;
    }
    return true;
}

std::vector<uint32_t> and_op(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    std::vector<uint32_t> res;
    res.reserve(std::min(a.size(), b.size()));
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { res.push_back(a[i]); ++i; ++j; }
        else if (a[i] < b[j]) ++i;
        else ++j;
    }
    return res;
}

std::vector<uint32_t> or_op(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    std::vector<uint32_t> res;
    res.reserve(a.size() + b.size());
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { res.push_back(a[i]); ++i; ++j; }
        else if (a[i] < b[j]) res.push_back(a[i++]);
        else res.push_back(b[j++]);
    }
    while (i < a.size()) res.push_back(a[i++]);
    while (j < b.size()) res.push_back(b[j++]);
    return res;
}

std::vector<uint32_t> not_op(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    std::vector<uint32_t> res;
    res.reserve(a.size());
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { ++i; ++j; }
        else if (a[i] < b[j]) res.push_back(a[i++]);
        else ++j;
    }
    while (i < a.size()) res.push_back(a[i++]);
    return res;
}

int main(int argc, char* argv[]) {
    std::string data_dir = "/data";
    std::string out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out_dir = argv[++i];
    }

    std::string queries_path = (fs::path(data_dir) / "queries.txt").string();
    std::string docs_path = (fs::path(data_dir) / "docs.txt").string();
    std::string output_path = (fs::path(out_dir) / "results.txt").string();

    // 1. Read queries
    MappedFile q_file;
    if (!q_file.open_file(queries_path)) {
        std::cerr << "Could not open " << queries_path << "\n";
        return 1;
    }

    FlatTermDict dict;
    dict.init(1 << 18);

    std::vector<std::string> id_to_term;
    auto get_or_insert_term = [&](const std::string& term) -> uint32_t {
        uint64_t h = fnv1a(term.data(), term.size());
        int32_t found = dict.lookup(h, term.data(), term.size());
        if (found >= 0) return found;
        uint32_t id = id_to_term.size();
        dict.insert(term, id);
        id_to_term.push_back(term);
        return id;
    };

    std::vector<Query> queries;
    const char* q_ptr = q_file.data;
    const char* q_end = q_file.data + q_file.size;

    while (q_ptr < q_end) {
        const char* line_start = q_ptr;
        while (q_ptr < q_end && *q_ptr != '\n') ++q_ptr;
        const char* line_end = q_ptr;
        if (q_ptr < q_end && *q_ptr == '\n') ++q_ptr;
        if (line_start == line_end) continue;

        std::string_view line(line_start, line_end - line_start);
        Query q;
        if (line.starts_with("BOOL ")) {
            q.type = QueryType::BOOL_Q;
            size_t pos = 5;
            bool is_term = true;
            while (pos < line.size()) {
                size_t next_sp = line.find(' ', pos);
                if (next_sp == std::string_view::npos) next_sp = line.size();
                std::string word(line.substr(pos, next_sp - pos));
                if (is_term) {
                    q.term_ids.push_back(get_or_insert_term(word));
                    is_term = false;
                } else {
                    if (word == "AND") q.ops.push_back(OpType::AND);
                    else if (word == "OR") q.ops.push_back(OpType::OR);
                    else if (word == "NOT") q.ops.push_back(OpType::NOT);
                    is_term = true;
                }
                pos = next_sp + 1;
            }
            queries.push_back(std::move(q));
        } else if (line.starts_with("PHRASE \"")) {
            q.type = QueryType::PHRASE_Q;
            size_t pos = 8;
            size_t end_quote = line.rfind('"');
            if (end_quote != std::string_view::npos && end_quote >= pos) {
                std::string_view inside = line.substr(pos, end_quote - pos);
                size_t p = 0;
                while (p < inside.size()) {
                    size_t next_sp = inside.find(' ', p);
                    if (next_sp == std::string_view::npos) next_sp = inside.size();
                    std::string word(inside.substr(p, next_sp - p));
                    q.phrase_strings.push_back(word);
                    q.term_ids.push_back(get_or_insert_term(word));
                    p = next_sp + 1;
                }
            }
            queries.push_back(std::move(q));
        }
    }

    uint32_t num_terms = id_to_term.size();

    // 2. Map docs.txt and index
    MappedFile d_file;
    if (!d_file.open_file(docs_path)) {
        std::cerr << "Could not open " << docs_path << "\n";
        return 1;
    }

    std::vector<DocSlice> docs;
    docs.reserve(300000);

    const char* d_ptr = d_file.data;
    const char* d_end = d_file.data + d_file.size;

    while (d_ptr < d_end) {
        const char* line_start = d_ptr;
        while (d_ptr < d_end && *d_ptr != '\n') ++d_ptr;
        const char* line_end = d_ptr;
        if (d_ptr < d_end && *d_ptr == '\n') ++d_ptr;

        const char* tab_ptr = line_start;
        while (tab_ptr < line_end && *tab_ptr != '\t') ++tab_ptr;
        if (tab_ptr >= line_end) continue;

        const char* text_start = tab_ptr + 1;
        uint32_t text_len = line_end - text_start;
        docs.push_back({text_start, text_len});
    }

    size_t num_docs = docs.size();
    constexpr size_t NUM_THREADS = 6;
    size_t workers = std::max(size_t(1), std::min(NUM_THREADS, num_docs));

    // Each thread indexes a slice of docs
    std::vector<std::vector<std::vector<uint32_t>>> thread_postings(workers);
    for (size_t w = 0; w < workers; ++w) {
        thread_postings[w].resize(num_terms);
    }

    auto doc_worker = [&](size_t wid) {
        size_t d_begin = wid * num_docs / workers;
        size_t d_end = (wid + 1) * num_docs / workers;

        std::vector<uint32_t> last_seen(num_terms, UINT32_MAX);
        auto& my_postings = thread_postings[wid];

        for (size_t doc_id = d_begin; doc_id < d_end; ++doc_id) {
            DocSlice slice = docs[doc_id];
            const char* p = slice.text;
            const char* pend = slice.text + slice.len;

            while (p < pend) {
                char c = *p;
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
                    const char* tstart = p;
                    uint64_t h = FNV_OFFSET_BASIS;
                    while (p < pend) {
                        char ch = *p;
                        if (ch >= 'A' && ch <= 'Z') ch += ('a' - 'A');
                        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
                            h = (h ^ uint8_t(ch)) * FNV_PRIME;
                            ++p;
                        } else break;
                    }
                    uint32_t len = p - tstart;
                    int32_t tid = dict.lookup(h == 0 ? 1 : h, tstart, len);
                    if (tid >= 0) {
                        if (last_seen[tid] != doc_id) {
                            last_seen[tid] = doc_id;
                            my_postings[tid].push_back(doc_id);
                        }
                    }
                } else {
                    ++p;
                }
            }
        }
    };

    std::vector<std::thread> doc_threads;
    for (size_t w = 0; w < workers; ++w) {
        doc_threads.emplace_back(doc_worker, w);
    }
    for (auto& th : doc_threads) th.join();

    // Merge thread postings into global postings
    std::vector<std::vector<uint32_t>> postings(num_terms);
    for (uint32_t tid = 0; tid < num_terms; ++tid) {
        size_t total_sz = 0;
        for (size_t w = 0; w < workers; ++w) {
            total_sz += thread_postings[w][tid].size();
        }
        postings[tid].reserve(total_sz);
        for (size_t w = 0; w < workers; ++w) {
            postings[tid].insert(postings[tid].end(),
                                 thread_postings[w][tid].begin(),
                                 thread_postings[w][tid].end());
        }
    }
    std::vector<std::vector<std::vector<uint32_t>>>().swap(thread_postings);

    // 3. Parallel Query Evaluation
    size_t num_queries = queries.size();
    std::vector<std::string> query_results(num_queries);
    std::atomic<size_t> next_q{0};

    auto query_worker = [&]() {
        std::vector<TokenSpan> doc_tokens;
        doc_tokens.reserve(1024);
        std::string line_buf;
        line_buf.reserve(256);

        while (true) {
            size_t qid = next_q.fetch_add(64, std::memory_order_relaxed);
            if (qid >= num_queries) break;
            size_t qend = std::min(qid + 64, num_queries);

            for (size_t idx = qid; idx < qend; ++idx) {
                const auto& q = queries[idx];
                line_buf.clear();

                if (q.type == QueryType::BOOL_Q) {
                    if (q.term_ids.empty()) {
                        query_results[idx] = "0 0\n";
                        continue;
                    }
                    std::vector<uint32_t> res = postings[q.term_ids[0]];
                    for (size_t i = 0; i < q.ops.size(); ++i) {
                        OpType op = q.ops[i];
                        uint32_t next_tid = q.term_ids[i + 1];
                        const auto& next_p = postings[next_tid];
                        if (op == OpType::AND) res = and_op(res, next_p);
                        else if (op == OpType::OR) res = or_op(res, next_p);
                        else if (op == OpType::NOT) res = not_op(res, next_p);
                    }

                    if (res.empty()) {
                        query_results[idx] = "0 0\n";
                    } else {
                        uint64_t sum = 0;
                        for (uint32_t id : res) sum += id;
                        line_buf += std::to_string(res.size()) + " " + std::to_string(sum);
                        size_t limit = std::min((size_t)10, res.size());
                        for (size_t i = 0; i < limit; ++i) {
                            line_buf += " " + std::to_string(res[i]);
                        }
                        line_buf += "\n";
                        query_results[idx] = line_buf;
                    }
                } else {
                    // PHRASE
                    if (q.term_ids.empty()) {
                        query_results[idx] = "0 0\n";
                        continue;
                    }
                    bool missing = false;
                    size_t min_idx = 0;
                    size_t min_size = postings[q.term_ids[0]].size();
                    if (min_size == 0) missing = true;

                    for (size_t i = 1; i < q.term_ids.size(); ++i) {
                        size_t sz = postings[q.term_ids[i]].size();
                        if (sz == 0) { missing = true; break; }
                        if (sz < min_size) {
                            min_size = sz;
                            min_idx = i;
                        }
                    }

                    if (missing) {
                        query_results[idx] = "0 0\n";
                        continue;
                    }

                    std::vector<uint32_t> cand = postings[q.term_ids[min_idx]];
                    for (size_t i = 0; i < q.term_ids.size(); ++i) {
                        if (i == min_idx) continue;
                        cand = and_op(cand, postings[q.term_ids[i]]);
                        if (cand.empty()) break;
                    }

                    if (cand.empty()) {
                        query_results[idx] = "0 0\n";
                        continue;
                    }

                    size_t k = q.phrase_strings.size();
                    std::vector<uint32_t> phrase_matches;

                    for (uint32_t cid : cand) {
                        DocSlice slice = docs[cid];
                        doc_tokens.clear();
                        const char* p = slice.text;
                        const char* pend = slice.text + slice.len;

                        while (p < pend) {
                            char c = *p;
                            bool is_tok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
                            if (is_tok) {
                                const char* tstart = p;
                                while (p < pend) {
                                    char ch = *p;
                                    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')) ++p;
                                    else break;
                                }
                                doc_tokens.push_back({tstart, (uint32_t)(p - tstart)});
                            } else {
                                ++p;
                            }
                        }

                        if (doc_tokens.size() < k) continue;

                        bool matched = false;
                        for (size_t i = 0; i <= doc_tokens.size() - k; ++i) {
                            bool ok = true;
                            for (size_t j = 0; j < k; ++j) {
                                if (!match_token(doc_tokens[i + j], q.phrase_strings[j])) {
                                    ok = false;
                                    break;
                                }
                            }
                            if (ok) {
                                matched = true;
                                break;
                            }
                        }
                        if (matched) phrase_matches.push_back(cid);
                    }

                    if (phrase_matches.empty()) {
                        query_results[idx] = "0 0\n";
                    } else {
                        uint64_t sum = 0;
                        for (uint32_t id : phrase_matches) sum += id;
                        line_buf += std::to_string(phrase_matches.size()) + " " + std::to_string(sum);
                        size_t limit = std::min((size_t)10, phrase_matches.size());
                        for (size_t i = 0; i < limit; ++i) {
                            line_buf += " " + std::to_string(phrase_matches[i]);
                        }
                        line_buf += "\n";
                        query_results[idx] = line_buf;
                    }
                }
            }
        }
    };

    std::vector<std::thread> query_threads;
    for (size_t w = 0; w < workers; ++w) {
        query_threads.emplace_back(query_worker);
    }
    for (auto& th : query_threads) th.join();

    // Write output
    fs::create_directories(out_dir);
    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Could not open " << output_path << "\n";
        return 1;
    }
    for (const auto& line : query_results) {
        out.write(line.data(), line.size());
    }

    return out.good() ? 0 : 1;
}

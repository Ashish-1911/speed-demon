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
#include <chrono>
#include <array>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

alignas(64) static const uint8_t IS_ALNUM[256] = {
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1, 1,1,
    0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,
    0,0,0,0,0,0,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,
    0
};

alignas(64) static const std::array<char, 256> TO_LOWER_MAP = []{
    std::array<char, 256> map{};
    for (int i = 0; i < 256; ++i) {
        if (i >= 'A' && i <= 'Z') map[i] = (char)(i + ('a' - 'A'));
        else map[i] = (char)i;
    }
    return map;
}();

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
        madvise(ptr, size, MADV_WILLNEED | MADV_SEQUENTIAL);
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
        h = (h ^ uint8_t(TO_LOWER_MAP[(uint8_t)s[i]])) * FNV_PRIME;
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
                    if (TO_LOWER_MAP[(uint8_t)raw_str[i]] != stored[i]) {
                        match = false;
                        break;
                    }
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

inline void and_op(const uint32_t* a, size_t a_sz, const uint32_t* b, size_t b_sz, std::vector<uint32_t>& out) {
    out.clear();
    size_t i = 0, j = 0;
    while (i < a_sz && j < b_sz) {
        if (a[i] == b[j]) {
            out.push_back(a[i]);
            ++i; ++j;
        } else if (a[i] < b[j]) {
            ++i;
        } else {
            ++j;
        }
    }
}

inline void or_op(const uint32_t* a, size_t a_sz, const uint32_t* b, size_t b_sz, std::vector<uint32_t>& out) {
    out.clear();
    size_t i = 0, j = 0;
    while (i < a_sz && j < b_sz) {
        if (a[i] == b[j]) {
            out.push_back(a[i]);
            ++i; ++j;
        } else if (a[i] < b[j]) {
            out.push_back(a[i++]);
        } else {
            out.push_back(b[j++]);
        }
    }
    while (i < a_sz) out.push_back(a[i++]);
    while (j < b_sz) out.push_back(b[j++]);
}

inline void not_op(const uint32_t* a, size_t a_sz, const uint32_t* b, size_t b_sz, std::vector<uint32_t>& out) {
    out.clear();
    size_t i = 0, j = 0;
    while (i < a_sz && j < b_sz) {
        if (a[i] == b[j]) {
            ++i; ++j;
        } else if (a[i] < b[j]) {
            out.push_back(a[i++]);
        } else {
            ++j;
        }
    }
    while (i < a_sz) out.push_back(a[i++]);
}

struct TokenSpan {
    const char* ptr;
    uint32_t len;
};

inline bool match_token(TokenSpan span, const std::string& term) {
    if (span.len != term.size()) return false;
    for (size_t i = 0; i < span.len; ++i) {
        if (TO_LOWER_MAP[(uint8_t)span.ptr[i]] != term[i]) return false;
    }
    return true;
}

inline bool verify_phrase_doc(const DocSlice& slice, const std::vector<std::string>& phrase, std::vector<TokenSpan>& doc_tokens) {
    const size_t k = phrase.size();
    if (k == 0) return true;

    doc_tokens.clear();
    const char* p = slice.text;
    const char* pend = slice.text + slice.len;

    while (p < pend) {
        if (IS_ALNUM[(uint8_t)*p]) {
            const char* tstart = p;
            while (p < pend && IS_ALNUM[(uint8_t)*p]) ++p;
            doc_tokens.push_back({tstart, (uint32_t)(p - tstart)});
        } else {
            ++p;
        }
    }

    if (doc_tokens.size() < k) return false;

    for (size_t i = 0; i <= doc_tokens.size() - k; ++i) {
        bool ok = true;
        for (size_t j = 0; j < k; ++j) {
            if (!match_token(doc_tokens[i + j], phrase[j])) {
                ok = false;
                break;
            }
        }
        if (ok) return true; // Break immediately upon finding phrase!
    }
    return false;
}

inline char* write_u64(char* p, uint64_t val) {
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    char temp[24];
    int idx = 0;
    while (val > 0) {
        temp[idx++] = '0' + (val % 10);
        val /= 10;
    }
    while (idx > 0) {
        *p++ = temp[--idx];
    }
    return p;
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
    if (!q_file.open_file(queries_path)) return 1;

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
    queries.reserve(60000);
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

    // 2 & 3. Map docs.txt and parallel single-pass slice + index
    MappedFile d_file;
    if (!d_file.open_file(docs_path)) return 1;

    // Detect total_docs from the last line
    uint32_t total_docs = 0;
    if (d_file.size > 0) {
        const char* end_ptr = d_file.data + d_file.size - 1;
        while (end_ptr > d_file.data && *end_ptr == '\n') --end_ptr;
        const char* last_nl = end_ptr;
        while (last_nl > d_file.data && *(last_nl - 1) != '\n') --last_nl;
        uint32_t last_id = 0;
        while (last_nl < d_file.data + d_file.size && *last_nl >= '0' && *last_nl <= '9') {
            last_id = last_id * 10 + (*last_nl - '0');
            ++last_nl;
        }
        total_docs = last_id + 1;
    }

    std::vector<DocSlice> docs(total_docs);

    size_t workers = std::max(size_t(1), std::min(size_t(6), (size_t)total_docs));
    std::vector<size_t> chunk_offsets(workers + 1, 0);
    chunk_offsets[0] = 0;
    chunk_offsets[workers] = d_file.size;

    for (size_t i = 1; i < workers; ++i) {
        size_t rough = i * d_file.size / workers;
        const char* p = (const char*)memchr(d_file.data + rough, '\n', d_file.size - rough);
        chunk_offsets[i] = (p ? (p - d_file.data + 1) : d_file.size);
    }

    std::vector<std::vector<std::vector<uint32_t>>> thread_postings(workers);
    for (size_t w = 0; w < workers; ++w) {
        thread_postings[w].resize(num_terms);
    }

    auto doc_worker = [&](size_t wid) {
        const char* p = d_file.data + chunk_offsets[wid];
        const char* pend = d_file.data + chunk_offsets[wid + 1];

        std::vector<uint32_t> last_seen(num_terms, UINT32_MAX);
        auto& my_postings = thread_postings[wid];

        while (p < pend) {
            const char* lend = (const char*)memchr(p, '\n', pend - p);
            if (!lend) lend = pend;

            const char* tab = (const char*)memchr(p, '\t', lend - p);
            if (tab) {
                uint32_t doc_id = 0;
                for (const char* d = p; d < tab; ++d) {
                    doc_id = doc_id * 10 + (*d - '0');
                }

                const char* text_start = tab + 1;
                uint32_t text_len = lend - text_start;
                if (doc_id < total_docs) {
                    docs[doc_id] = {text_start, text_len};
                }

                const char* ptr = text_start;
                const char* ptend = lend;
                while (ptr < ptend) {
                    if (IS_ALNUM[(uint8_t)*ptr]) {
                        const char* tstart = ptr;
                        uint64_t h = FNV_OFFSET_BASIS;
                        while (ptr < ptend && IS_ALNUM[(uint8_t)*ptr]) {
                            h = (h ^ uint8_t(TO_LOWER_MAP[(uint8_t)*ptr])) * FNV_PRIME;
                            ++ptr;
                        }
                        uint32_t len = ptr - tstart;
                        int32_t tid = dict.lookup(h == 0 ? 1 : h, tstart, len);
                        if (tid >= 0) {
                            if (last_seen[tid] != doc_id) {
                                last_seen[tid] = doc_id;
                                my_postings[tid].push_back(doc_id);
                            }
                        }
                    } else {
                        ++ptr;
                    }
                }
            }
            p = (lend < pend) ? lend + 1 : pend;
        }
    };

    std::vector<std::thread> doc_threads;
    for (size_t w = 0; w < workers; ++w) {
        doc_threads.emplace_back(doc_worker, w);
    }
    for (auto& th : doc_threads) th.join();

    // 4. Build Flat CSR Postings
    std::vector<uint32_t> csr_head(num_terms + 1, 0);
    for (uint32_t tid = 0; tid < num_terms; ++tid) {
        uint32_t count = 0;
        for (size_t w = 0; w < workers; ++w) {
            count += thread_postings[w][tid].size();
        }
        csr_head[tid + 1] = csr_head[tid] + count;
    }
    uint32_t total_postings_entries = csr_head[num_terms];
    std::vector<uint32_t> csr_postings(total_postings_entries);

    auto copy_worker = [&](size_t wid) {
        size_t t_begin = wid * num_terms / workers;
        size_t t_end = (wid + 1) * num_terms / workers;
        for (size_t tid = t_begin; tid < t_end; ++tid) {
            uint32_t* dest = csr_postings.data() + csr_head[tid];
            for (size_t w = 0; w < workers; ++w) {
                const auto& vec = thread_postings[w][tid];
                if (!vec.empty()) {
                    memcpy(dest, vec.data(), vec.size() * sizeof(uint32_t));
                    dest += vec.size();
                }
            }
        }
    };

    std::vector<std::thread> copy_threads;
    for (size_t w = 0; w < workers; ++w) {
        copy_threads.emplace_back(copy_worker, w);
    }
    for (auto& th : copy_threads) th.join();

    std::vector<std::vector<std::vector<uint32_t>>>().swap(thread_postings);

    // 5. Query Evaluation against Flat CSR
    size_t num_queries = queries.size();
    std::vector<std::string> query_results(num_queries);
    std::atomic<size_t> next_q{0};

    auto query_worker = [&]() {
        std::vector<uint32_t> bufA;
        std::vector<uint32_t> bufB;
        bufA.reserve(4096);
        bufB.reserve(4096);

        std::vector<uint32_t> phrase_matches;
        phrase_matches.reserve(1024);

        std::vector<TokenSpan> doc_tokens;
        doc_tokens.reserve(1024);

        char line_buf[512];

        while (true) {
            size_t qid = next_q.fetch_add(128, std::memory_order_relaxed);
            if (qid >= num_queries) break;
            size_t qend = std::min(qid + 128, num_queries);

            for (size_t idx = qid; idx < qend; ++idx) {
                const auto& q = queries[idx];

                if (q.type == QueryType::BOOL_Q) {
                    if (q.term_ids.empty()) {
                        query_results[idx] = "0 0\n";
                        continue;
                    }

                    uint32_t t0 = q.term_ids[0];
                    const uint32_t* cur_ptr = csr_postings.data() + csr_head[t0];
                    size_t cur_sz = csr_head[t0 + 1] - csr_head[t0];
                    bool in_bufA = false;

                    for (size_t i = 0; i < q.ops.size(); ++i) {
                        OpType op = q.ops[i];
                        uint32_t next_tid = q.term_ids[i + 1];
                        const uint32_t* next_ptr = csr_postings.data() + csr_head[next_tid];
                        size_t next_sz = csr_head[next_tid + 1] - csr_head[next_tid];

                        if (!in_bufA) {
                            if (op == OpType::AND) and_op(cur_ptr, cur_sz, next_ptr, next_sz, bufA);
                            else if (op == OpType::OR) or_op(cur_ptr, cur_sz, next_ptr, next_sz, bufA);
                            else if (op == OpType::NOT) not_op(cur_ptr, cur_sz, next_ptr, next_sz, bufA);
                            cur_ptr = bufA.data();
                            cur_sz = bufA.size();
                            in_bufA = true;
                        } else {
                            if (op == OpType::AND) and_op(cur_ptr, cur_sz, next_ptr, next_sz, bufB);
                            else if (op == OpType::OR) or_op(cur_ptr, cur_sz, next_ptr, next_sz, bufB);
                            else if (op == OpType::NOT) not_op(cur_ptr, cur_sz, next_ptr, next_sz, bufB);
                            cur_ptr = bufB.data();
                            cur_sz = bufB.size();
                            in_bufA = false;
                        }
                    }

                    if (cur_sz == 0) {
                        query_results[idx] = "0 0\n";
                    } else {
                        uint64_t sum = 0;
                        for (size_t i = 0; i < cur_sz; ++i) sum += cur_ptr[i];
                        char* lp = line_buf;
                        lp = write_u64(lp, cur_sz);
                        *lp++ = ' ';
                        lp = write_u64(lp, sum);
                        size_t limit = std::min((size_t)10, cur_sz);
                        for (size_t i = 0; i < limit; ++i) {
                            *lp++ = ' ';
                            lp = write_u64(lp, cur_ptr[i]);
                        }
                        *lp++ = '\n';
                        query_results[idx].assign(line_buf, lp - line_buf);
                    }
                } else {
                    // PHRASE
                    if (q.term_ids.empty()) {
                        query_results[idx] = "0 0\n";
                        continue;
                    }

                    std::vector<std::pair<size_t, uint32_t>> term_sizes;
                    term_sizes.reserve(q.term_ids.size());
                    bool missing = false;
                    for (uint32_t tid : q.term_ids) {
                        size_t sz = csr_head[tid + 1] - csr_head[tid];
                        if (sz == 0) { missing = true; break; }
                        term_sizes.push_back({sz, tid});
                    }

                    if (missing) {
                        query_results[idx] = "0 0\n";
                        continue;
                    }

                    std::sort(term_sizes.begin(), term_sizes.end());

                    uint32_t min_tid = term_sizes[0].second;
                    const uint32_t* cand_ptr = csr_postings.data() + csr_head[min_tid];
                    size_t cand_sz = term_sizes[0].first;
                    bool in_bufA = false;

                    for (size_t i = 1; i < term_sizes.size(); ++i) {
                        uint32_t next_tid = term_sizes[i].second;
                        const uint32_t* next_ptr = csr_postings.data() + csr_head[next_tid];
                        size_t next_sz = term_sizes[i].first;

                        if (!in_bufA) {
                            and_op(cand_ptr, cand_sz, next_ptr, next_sz, bufA);
                            cand_ptr = bufA.data();
                            cand_sz = bufA.size();
                            in_bufA = true;
                        } else {
                            and_op(cand_ptr, cand_sz, next_ptr, next_sz, bufB);
                            cand_ptr = bufB.data();
                            cand_sz = bufB.size();
                            in_bufA = false;
                        }
                        if (cand_sz == 0) break;
                    }

                    if (cand_sz == 0) {
                        query_results[idx] = "0 0\n";
                        continue;
                    }

                    phrase_matches.clear();
                    for (size_t i = 0; i < cand_sz; ++i) {
                        uint32_t cid = cand_ptr[i];
                        if (cid < total_docs && verify_phrase_doc(docs[cid], q.phrase_strings, doc_tokens)) {
                            phrase_matches.push_back(cid);
                        }
                    }

                    if (phrase_matches.empty()) {
                        query_results[idx] = "0 0\n";
                    } else {
                        uint64_t sum = 0;
                        for (uint32_t id : phrase_matches) sum += id;
                        char* lp = line_buf;
                        lp = write_u64(lp, phrase_matches.size());
                        *lp++ = ' ';
                        lp = write_u64(lp, sum);
                        size_t limit = std::min((size_t)10, phrase_matches.size());
                        for (size_t i = 0; i < limit; ++i) {
                            *lp++ = ' ';
                            lp = write_u64(lp, phrase_matches[i]);
                        }
                        *lp++ = '\n';
                        query_results[idx].assign(line_buf, lp - line_buf);
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

    // 6. Write output in a single block
    fs::create_directories(out_dir);

    size_t total_out_sz = 0;
    for (const auto& line : query_results) total_out_sz += line.size();

    std::string out_buf;
    out_buf.resize(total_out_sz);
    char* out_ptr = out_buf.data();
    for (const auto& line : query_results) {
        memcpy(out_ptr, line.data(), line.size());
        out_ptr += line.size();
    }

    int out_fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd >= 0) {
        size_t written = 0;
        while (written < total_out_sz) {
            ssize_t res = write(out_fd, out_buf.data() + written, total_out_sz - written);
            if (res <= 0) break;
            written += res;
        }
        close(out_fd);
    }

    return 0;
}

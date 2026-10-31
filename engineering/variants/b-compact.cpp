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
    uint32_t id_length = 0;
    uint32_t term_str_offset = 0;
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
        table[idx] = {h, id | (uint32_t(term.size()) << 24), str_offset};
    }

    inline int32_t lookup(uint64_t h, const char* raw_str, uint32_t len) const {
        uint32_t idx = h & mask;
        while (true) {
            uint64_t slot_h = table[idx].hash;
            if (slot_h == 0) return -1;
            if (slot_h == h && (table[idx].id_length >> 24) == len) {
                const char* stored = term_storage.data() + table[idx].term_str_offset;
                bool match = true;
                for (uint32_t i = 0; i < len; ++i) {
                    if (TO_LOWER_MAP[(uint8_t)raw_str[i]] != stored[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) return table[idx].id_length & 0xffffff;
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
    if (a_sz > b_sz) { std::swap(a, b); std::swap(a_sz, b_sz); }
    if (a_sz == 0) return;
    // Sparse intersections should not scan an entire common-term posting list.
    if (b_sz / a_sz >= 16) {
        size_t j = 0;
        for (size_t i = 0; i < a_sz && j < b_sz; ++i) {
            size_t step = 1;
            while (step < b_sz - j && b[j + step] < a[i]) step *= 2;
            size_t end = j + std::min(step, b_sz - j);
            j = std::lower_bound(b + j, b + end, a[i]) - b;
            if (j < b_sz && b[j] == a[i]) out.push_back(a[i]);
        }
        return;
    }
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

struct CandidatePair {
    uint32_t doc_id;
    uint32_t q_idx;
};

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

    MappedFile d_file;
    if (!d_file.open_file(docs_path)) return 1;

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
    for (size_t w = 0; w < workers; ++w) thread_postings[w].resize(num_terms);

    std::vector<std::thread> doc_threads;
    for (size_t w = 0; w < workers; ++w) {
        doc_threads.emplace_back([&, w]() {
            const char* p = d_file.data + chunk_offsets[w];
            const char* pend = d_file.data + chunk_offsets[w + 1];
            std::vector<uint32_t> last_seen(num_terms, UINT32_MAX);
            auto& my_post = thread_postings[w];

            while (p < pend) {
                const char* lend = (const char*)memchr(p, '\n', pend - p);
                if (!lend) lend = pend;
                const char* tab = (const char*)memchr(p, '\t', lend - p);
                if (tab) {
                    uint32_t doc_id = 0;
                    for (const char* d = p; d < tab; ++d) doc_id = doc_id * 10 + (*d - '0');
                    const char* text_start = tab + 1;
                    uint32_t text_len = lend - text_start;
                    if (doc_id < total_docs) docs[doc_id] = {text_start, text_len};

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
                            if (tid >= 0 && last_seen[tid] != doc_id) {
                                last_seen[tid] = doc_id;
                                my_post[tid].push_back(doc_id);
                            }
                        } else ++ptr;
                    }
                }
                p = (lend < pend) ? lend + 1 : pend;
            }
        });
    }
    for (auto& th : doc_threads) th.join();

    std::vector<uint32_t> csr_head(num_terms + 1, 0);
    for (uint32_t tid = 0; tid < num_terms; ++tid) {
        uint32_t count = 0;
        for (size_t w = 0; w < workers; ++w) count += thread_postings[w][tid].size();
        csr_head[tid + 1] = csr_head[tid] + count;
    }
    uint32_t total_postings = csr_head[num_terms];
    std::vector<uint32_t> csr_postings(total_postings);

    std::vector<std::thread> copy_threads;
    for (size_t w = 0; w < workers; ++w) {
        copy_threads.emplace_back([&, w]() {
            size_t t_begin = w * num_terms / workers;
            size_t t_end = (w + 1) * num_terms / workers;
            for (size_t tid = t_begin; tid < t_end; ++tid) {
                uint32_t* dest = csr_postings.data() + csr_head[tid];
                for (size_t k = 0; k < workers; ++k) {
                    const auto& vec = thread_postings[k][tid];
                    if (!vec.empty()) {
                        memcpy(dest, vec.data(), vec.size() * sizeof(uint32_t));
                        dest += vec.size();
                    }
                }
            }
        });
    }
    for (auto& th : copy_threads) th.join();
    std::vector<std::vector<std::vector<uint32_t>>>().swap(thread_postings);

    size_t num_queries = queries.size();
    std::vector<std::string> query_results(num_queries);

    // 1. Parallel Boolean Evaluation on 12 threads
    std::atomic<size_t> next_bool_q{0};
    auto bool_worker = [&]() {
        std::vector<uint32_t> bufA;
        std::vector<uint32_t> bufB;
        bufA.reserve(4096);
        bufB.reserve(4096);
        char line_buf[512];

        while (true) {
            size_t idx = next_bool_q.fetch_add(128, std::memory_order_relaxed);
            if (idx >= num_queries) break;
            size_t qend = std::min(idx + 128, num_queries);

            for (size_t i = idx; i < qend; ++i) {
                const auto& q = queries[i];
                if (q.type != QueryType::BOOL_Q) continue;

                if (q.term_ids.empty()) {
                    query_results[i] = "0 0\n";
                    continue;
                }

                uint32_t t0 = q.term_ids[0];
                const uint32_t* cur_ptr = csr_postings.data() + csr_head[t0];
                size_t cur_sz = csr_head[t0 + 1] - csr_head[t0];
                bool in_bufA = false;

                for (size_t op_i = 0; op_i < q.ops.size(); ++op_i) {
                    OpType op = q.ops[op_i];
                    uint32_t next_tid = q.term_ids[op_i + 1];
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
                    query_results[i] = "0 0\n";
                } else {
                    uint64_t sum = 0;
                    for (size_t k = 0; k < cur_sz; ++k) sum += cur_ptr[k];
                    char* lp = line_buf;
                    lp = write_u64(lp, cur_sz);
                    *lp++ = ' ';
                    lp = write_u64(lp, sum);
                    size_t limit = std::min((size_t)10, cur_sz);
                    for (size_t k = 0; k < limit; ++k) {
                        *lp++ = ' ';
                        lp = write_u64(lp, cur_ptr[k]);
                    }
                    *lp++ = '\n';
                    query_results[i].assign(line_buf, lp - line_buf);
                }
            }
        }
    };

    std::vector<std::thread> bool_threads;
    for (size_t w = 0; w < 12; ++w) bool_threads.emplace_back(bool_worker);
    for (auto& th : bool_threads) th.join();

    // 2. Document-Centric Batch Phrase Verification
    std::vector<std::vector<CandidatePair>> thread_pairs(12);
    std::atomic<size_t> next_phrase_q{0};
    auto phrase_pair_gen = [&](int wid) {
        auto& my_pairs = thread_pairs[wid];
        my_pairs.reserve(30000);
        std::vector<uint32_t> bufA;
        std::vector<uint32_t> bufB;
        bufA.reserve(4096);
        bufB.reserve(4096);

        while (true) {
            size_t idx = next_phrase_q.fetch_add(128, std::memory_order_relaxed);
            if (idx >= num_queries) break;
            size_t qend = std::min(idx + 128, num_queries);

            for (size_t i = idx; i < qend; ++i) {
                const auto& q = queries[i];
                if (q.type != QueryType::PHRASE_Q) continue;

                if (q.term_ids.empty()) {
                    query_results[i] = "0 0\n";
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
                    query_results[i] = "0 0\n";
                    continue;
                }

                std::sort(term_sizes.begin(), term_sizes.end());

                uint32_t min_tid = term_sizes[0].second;
                const uint32_t* cand_ptr = csr_postings.data() + csr_head[min_tid];
                size_t cand_sz = term_sizes[0].first;
                bool in_bufA = false;

                for (size_t t_i = 1; t_i < term_sizes.size(); ++t_i) {
                    uint32_t next_tid = term_sizes[t_i].second;
                    const uint32_t* next_ptr = csr_postings.data() + csr_head[next_tid];
                    size_t next_sz = term_sizes[t_i].first;

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
                    query_results[i] = "0 0\n";
                    continue;
                }

                for (size_t c_i = 0; c_i < cand_sz; ++c_i) {
                    my_pairs.push_back({cand_ptr[c_i], (uint32_t)i});
                }
            }
        }
    };

    std::vector<std::thread> gen_threads;
    for (int w = 0; w < 12; ++w) gen_threads.emplace_back(phrase_pair_gen, w);
    for (auto& th : gen_threads) th.join();

    size_t total_pairs = 0;
    for (int w = 0; w < 12; ++w) total_pairs += thread_pairs[w].size();

    std::vector<CandidatePair> all_pairs(total_pairs);
    size_t cur_pos = 0;
    for (int w = 0; w < 12; ++w) {
        memcpy(all_pairs.data() + cur_pos, thread_pairs[w].data(), thread_pairs[w].size() * sizeof(CandidatePair));
        cur_pos += thread_pairs[w].size();
        std::vector<CandidatePair>().swap(thread_pairs[w]);
    }

    std::vector<uint32_t> doc_pair_count(total_docs + 1, 0);
    for (const auto& cp : all_pairs) doc_pair_count[cp.doc_id + 1]++;
    for (uint32_t d = 0; d < total_docs; ++d) doc_pair_count[d + 1] += doc_pair_count[d];

    std::vector<CandidatePair> sorted_pairs(total_pairs);
    std::vector<uint32_t> write_pos = doc_pair_count;
    for (const auto& cp : all_pairs) {
        sorted_pairs[write_pos[cp.doc_id]++] = cp;
    }
    std::vector<CandidatePair>().swap(all_pairs);

    std::vector<uint32_t> cand_docs;
    cand_docs.reserve(total_docs / 2);
    for (uint32_t d = 0; d < total_docs; ++d) {
        if (doc_pair_count[d + 1] > doc_pair_count[d]) cand_docs.push_back(d);
    }

    std::atomic<size_t> next_cand_idx{0};
    size_t num_cand_docs = cand_docs.size();

    struct MatchRecord {
        uint32_t q_idx;
        uint32_t doc_id;
    };
    std::vector<std::vector<MatchRecord>> thread_match_records(12);

    auto doc_verify_worker = [&](int wid) {
        auto& my_matches = thread_match_records[wid];
        my_matches.reserve(4096);
        std::vector<TokenSpan> doc_tokens;
        doc_tokens.reserve(1024);

        while (true) {
            size_t c_idx = next_cand_idx.fetch_add(64, std::memory_order_relaxed);
            if (c_idx >= num_cand_docs) break;
            size_t cend = std::min(c_idx + 64, num_cand_docs);

            for (size_t ci = c_idx; ci < cend; ++ci) {
                uint32_t doc_id = cand_docs[ci];
                uint32_t p_start = doc_pair_count[doc_id];
                uint32_t p_end = doc_pair_count[doc_id + 1];

                doc_tokens.clear();
                const char* p = docs[doc_id].text;
                const char* pend = docs[doc_id].text + docs[doc_id].len;
                while (p < pend) {
                    if (IS_ALNUM[(uint8_t)*p]) {
                        const char* tstart = p;
                        while (p < pend && IS_ALNUM[(uint8_t)*p]) ++p;
                        doc_tokens.push_back({tstart, (uint32_t)(p - tstart)});
                    } else ++p;
                }

                for (uint32_t pi = p_start; pi < p_end; ++pi) {
                    uint32_t q_idx = sorted_pairs[pi].q_idx;
                    const auto& q = queries[q_idx];
                    const size_t k = q.phrase_strings.size();

                    if (doc_tokens.size() >= k) {
                        bool ok = false;
                        for (size_t pos = 0; pos <= doc_tokens.size() - k; ++pos) {
                            bool match = true;
                            for (size_t j = 0; j < k; ++j) {
                                if (!match_token(doc_tokens[pos + j], q.phrase_strings[j])) {
                                    match = false;
                                    break;
                                }
                            }
                            if (match) { ok = true; break; }
                        }
                        if (ok) {
                            my_matches.push_back({q_idx, doc_id});
                        }
                    }
                }
            }
        }
    };

    std::vector<std::thread> verify_threads;
    for (int w = 0; w < 12; ++w) verify_threads.emplace_back(doc_verify_worker, w);
    for (auto& th : verify_threads) th.join();

    std::vector<std::vector<uint32_t>> query_doc_matches(num_queries);
    for (int w = 0; w < 12; ++w) {
        for (const auto& m : thread_match_records[w]) {
            query_doc_matches[m.q_idx].push_back(m.doc_id);
        }
    }

    for (size_t i = 0; i < num_queries; ++i) {
        if (queries[i].type != QueryType::PHRASE_Q) continue;
        auto& matches = query_doc_matches[i];
        if (matches.empty()) {
            query_results[i] = "0 0\n";
        } else {
            std::sort(matches.begin(), matches.end());
            uint64_t sum = 0;
            for (uint32_t id : matches) sum += id;
            char line_buf[512];
            char* lp = line_buf;
            lp = write_u64(lp, matches.size());
            *lp++ = ' ';
            lp = write_u64(lp, sum);
            size_t limit = std::min((size_t)10, matches.size());
            for (size_t k = 0; k < limit; ++k) {
                *lp++ = ' ';
                lp = write_u64(lp, matches[k]);
            }
            *lp++ = '\n';
            query_results[i].assign(line_buf, lp - line_buf);
        }
    }

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

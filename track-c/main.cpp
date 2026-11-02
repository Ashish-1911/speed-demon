// Speed Demon 2026 Track C — Compact Dictionary with Separated Metadata
#include <algorithm>
#include <array>
#include <barrier>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <immintrin.h>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

struct InputMap {
  int fd = -1;
  size_t size = 0;
  const char *ptr = nullptr;
  explicit InputMap(const std::string &path) {
    fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
      return;
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size < 0)
      return;
    size = size_t(st.st_size);
    if (!size)
      return;
    void *p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p != MAP_FAILED) {
      ptr = static_cast<const char *>(p);
      madvise(p, size, MADV_WILLNEED | MADV_SEQUENTIAL);
    }
  }
  ~InputMap() {
    if (ptr)
      munmap(const_cast<char *>(ptr), size);
    if (fd >= 0)
      close(fd);
  }
  bool good() const { return fd >= 0 && (size == 0 || ptr != nullptr); }
};

static inline uint64_t fnv1a(const char *data, size_t len) {
  uint64_t h = 14695981039346656037ULL;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint8_t>(data[i]);
    h *= 1099511628211ULL;
  }
  return h ? h : 1;
}

struct NameEntry {
  const char *ptr;
  uint32_t len;
  uint64_t mask[4];
};

struct Partition {
  alignas(64) std::atomic_flag lock = ATOMIC_FLAG_INIT;
  static constexpr size_t CAP = 256;
  static constexpr size_t MASK = CAP - 1;
  uint64_t hashes[CAP]{};
  uint16_t entry_indices[CAP]{};
  std::vector<NameEntry> entries;
  std::vector<NameEntry> overflow;

  inline void insert_or_update(const char *p, uint32_t len, uint64_t h,
                               uint16_t c) {
    while (lock.test_and_set(std::memory_order_acquire)) {
      _mm_pause();
    }
    size_t idx = h & MASK;
    for (size_t step = 0; step < CAP; ++step) {
      uint64_t slot_h = hashes[idx];
      if (slot_h == 0) {
        uint16_t e_idx = static_cast<uint16_t>(entries.size());
        hashes[idx] = h;
        entry_indices[idx] = e_idx;
        NameEntry e{p, len, {0, 0, 0, 0}};
        e.mask[c >> 6] |= (1ULL << (c & 63));
        entries.push_back(e);
        lock.clear(std::memory_order_release);
        return;
      }
      if (slot_h == h) {
        uint16_t e_idx = entry_indices[idx];
        NameEntry &e = entries[e_idx];
        if (e.len == len && (len == 0 || std::memcmp(e.ptr, p, len) == 0)) {
          e.mask[c >> 6] |= (1ULL << (c & 63));
          lock.clear(std::memory_order_release);
          return;
        }
      }
      idx = (idx + 1) & MASK;
    }
    for (NameEntry &e : overflow) {
      if (e.len == len && (len == 0 || std::memcmp(e.ptr, p, len) == 0)) {
        e.mask[c >> 6] |= (1ULL << (c & 63));
        lock.clear(std::memory_order_release);
        return;
      }
    }
    NameEntry e{p, len, {0, 0, 0, 0}};
    e.mask[c >> 6] |= (1ULL << (c & 63));
    overflow.push_back(e);
    lock.clear(std::memory_order_release);
  }
};

static inline uint64_t number(const char *&p, const char *end) {
  uint64_t x = 0;
  while (p < end && *p >= '0' && *p <= '9')
    x = x * 10 + uint64_t(*p++ - '0');
  return x;
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

  inline void add(uint64_t id, int64_t amount, size_t nbytes, size_t note) {
    if (!count)
      min = max = amount;
    else {
      if (amount < min)
        min = amount;
      if (amount > max)
        max = amount;
    }
    ++count;
    id_sum += static_cast<int64_t>(id);
    sum += amount;
    name_bytes += nbytes;
    note_bytes += note;
  }

  void merge(const Aggregates &o) {
    if (!o.count)
      return;
    if (!count) {
      min = o.min;
      max = o.max;
    } else {
      if (o.min < min)
        min = o.min;
      if (o.max > max)
        max = o.max;
    }
    count += o.count;
    id_sum += o.id_sum;
    sum += o.sum;
    name_bytes += o.name_bytes;
    note_bytes += o.note_bytes;
  }
};

static inline std::string money(int64_t value) {
  bool neg = value < 0;
  uint64_t x = neg ? uint64_t(-(value + 1)) + 1 : uint64_t(value);
  std::string s = neg ? "-" : "";
  s += std::to_string(x / 100);
  s += '.';
  if (x % 100 < 10)
    s += '0';
  s += std::to_string(x % 100);
  return s;
}

static std::vector<const char *>
find_chunk_boundaries(const char *data, size_t size, int num_threads) {
  const char *p = data;
  const char *end = data + size;

  if (end - p >= 3 && uint8_t(p[0]) == 0xef && uint8_t(p[1]) == 0xbb &&
      uint8_t(p[2]) == 0xbf)
    p += 3;
  while (p < end && *p != '\n')
    ++p;
  if (p < end)
    ++p;

  const char *base = p;
  std::vector<const char *> bounds(num_threads + 1);
  bounds[0] = base;
  bounds[num_threads] = end;

  if (num_threads <= 1 || size_t(end - base) < size_t(num_threads * 128)) {
    size_t chunk_size = size_t(end - base) / size_t(num_threads);
    uint64_t total_quotes = 0;
    int target = 1;
    size_t next_target = chunk_size;
    while (p < end && target < num_threads) {
      char b = *p++;
      if (b == '"')
        total_quotes++;
      else if (b == '\n') {
        if (size_t(p - base) >= next_target && (total_quotes & 1) == 0) {
          bounds[target++] = p;
          next_target = target * chunk_size;
        }
      }
    }
    while (target < num_threads)
      bounds[target++] = end;
    return bounds;
  }

  size_t effective_size = size_t(end - base);
  size_t chunk_size = effective_size / size_t(num_threads);

  std::vector<const char *> slice_start(num_threads);
  std::vector<const char *> slice_end(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    slice_start[i] = base + size_t(i) * chunk_size;
    slice_end[i] =
        (i == num_threads - 1) ? end : (base + size_t(i + 1) * chunk_size);
  }

  std::vector<uint64_t> slice_quotes(num_threads, 0);
  std::vector<uint64_t> prefix_quotes(num_threads, 0);
  std::barrier sync_point(num_threads);

  std::vector<std::thread> workers;
  workers.reserve(num_threads);
  for (int tid = 0; tid < num_threads; ++tid) {
    workers.emplace_back([&, tid]() {
      const char *sp = slice_start[tid];
      const char *sp_end = slice_end[tid];
      uint64_t quotes = 0;
      __m256i q = _mm256_set1_epi8('"');
      while (sp + 32 <= sp_end) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(sp));
        quotes +=
            _mm_popcnt_u32(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, q)));
        sp += 32;
      }
      while (sp < sp_end) {
        if (*sp++ == '"')
          ++quotes;
      }
      slice_quotes[tid] = quotes;

      sync_point.arrive_and_wait();

      if (tid == 0) {
        prefix_quotes[0] = 0;
        for (int i = 1; i < num_threads; ++i)
          prefix_quotes[i] = prefix_quotes[i - 1] + slice_quotes[i - 1];
      }

      sync_point.arrive_and_wait();

      if (tid > 0) {
        const char *scan_p = slice_start[tid];
        uint64_t total_q = prefix_quotes[tid];
        while (scan_p < end) {
          char b = *scan_p++;
          if (b == '"')
            ++total_q;
          else if (b == '\n') {
            if ((total_q & 1) == 0) {
              bounds[tid] = scan_p;
              break;
            }
          }
        }
        if (scan_p >= end)
          bounds[tid] = end;
      }
    });
  }
  for (auto &w : workers)
    w.join();
  return bounds;
}

int main(int argc, char **argv) {
  std::string data_dir = "/data", out_dir = "/out";
  int num_threads = int(std::thread::hardware_concurrency());
  if (num_threads <= 0)
    num_threads = 6;
  if (num_threads > 12)
    num_threads = 12;

  for (int i = 1; i < argc; ++i) {
    std::string_view a(argv[i]);
    if (a == "--data" && i + 1 < argc)
      data_dir = argv[++i];
    else if (a == "--out" && i + 1 < argc)
      out_dir = argv[++i];
    else if (a == "--threads" && i + 1 < argc)
      num_threads = std::stoi(argv[++i]);
  }

  constexpr uint16_t ABSENT = 0xffff;
  std::vector<uint16_t> region(1000000, ABSENT);
  std::vector<std::string> codes;
  {
    InputMap rmap((fs::path(data_dir) / "regions.csv").string());
    if (!rmap.good() || !rmap.ptr) {
      std::cerr << "Cannot map regions.csv\n";
      return 1;
    }
    const char *p = rmap.ptr;
    const char *end = p + rmap.size;
    while (p < end && *p != '\n')
      ++p;
    if (p < end)
      ++p;

    std::vector<uint16_t> code_to_id(17576, ABSENT);
    codes.reserve(256);

    while (p < end) {
      if (*p == '\r' || *p == '\n') {
        ++p;
        continue;
      }
      uint32_t id = 0;
      while (p < end && *p >= '0' && *p <= '9')
        id = id * 10 + (*p++ - '0');
      if (p < end && *p == ',')
        ++p;
      const char *c_start = p;
      while (p < end && *p != '\r' && *p != '\n')
        ++p;
      size_t clen = size_t(p - c_start);
      if (p < end && *p == '\r')
        ++p;
      if (p < end && *p == '\n')
        ++p;

      if (clen == 3) {
        int key = (c_start[0] - 'A') * 676 + (c_start[1] - 'A') * 26 +
                  (c_start[2] - 'A');
        uint16_t cid = code_to_id[key];
        if (cid == ABSENT) {
          cid = uint16_t(codes.size());
          codes.emplace_back(c_start, 3);
          code_to_id[key] = cid;
        }
        if (id >= region.size())
          region.resize(std::max(size_t(id + 1), region.size() * 2), ABSENT);
        region[id] = cid;
      }
    }
  }
  uint16_t unmatched = uint16_t(codes.size());
  codes.emplace_back("_UNMATCHED");

  InputMap file((fs::path(data_dir) / "records.csv").string());
  if (!file.good() || !file.ptr) {
    std::cerr << "Cannot map records.csv\n";
    return 1;
  }

  auto bounds = find_chunk_boundaries(file.ptr, file.size, num_threads);

  std::vector<std::vector<Aggregates>> worker_aggs(
      num_threads, std::vector<Aggregates>(codes.size()));
  std::vector<Partition> partitions(1024);

  std::vector<std::thread> workers;
  workers.reserve(num_threads);
  for (int tid = 0; tid < num_threads; ++tid) {
    workers.emplace_back([&, tid]() {
      const char *p = bounds[tid];
      const char *end = bounds[tid + 1];
      auto &aggs = worker_aggs[tid];
      const uint16_t *region_data = region.data();
      const size_t region_size = region.size();

      while (p < end) {
        if (end - p >= 3 && uint8_t(p[0]) == 0xef && uint8_t(p[1]) == 0xbb &&
            uint8_t(p[2]) == 0xbf)
          p += 3;
        if (p == end)
          break;
        uint64_t id = number(p, end);
        if (p == end || *p++ != ',')
          break;

        const char *name_ptr = nullptr;
        size_t name_raw_len = 0;
        size_t name_len = 0;
        if (*p == '"') {
          ++p;
          const char *name_start = p;
          size_t escaped_quotes = 0;
          while (p < end) {
            const char *q =
                static_cast<const char *>(std::memchr(p, '"', end - p));
            if (!q) {
              p = end;
              break;
            }
            if (q + 1 < end && q[1] == '"') {
              ++escaped_quotes;
              p = q + 2;
            } else {
              p = q;
              break;
            }
          }
          if (p >= end)
            break;
          name_raw_len = size_t(p - name_start);
          name_len = name_raw_len - escaped_quotes;
          name_ptr = name_start;
          ++p;
          if (p == end || *p++ != ',')
            break;
        } else {
          if (*p != ',')
            break;
          ++p;
          name_ptr = nullptr;
          name_raw_len = 0;
          name_len = 0;
        }

        size_t note_bytes = 0;
        if (*p == '"') {
          ++p;
          while (p < end) {
            const char *q =
                static_cast<const char *>(std::memchr(p, '"', end - p));
            if (!q) {
              p = end;
              break;
            }
            note_bytes += size_t(q - p);
            if (q + 1 < end && q[1] == '"') {
              ++note_bytes;
              p = q + 2;
            } else {
              p = q + 1;
              break;
            }
          }
        } else {
          const char *comma =
              static_cast<const char *>(std::memchr(p, ',', end - p));
          if (!comma)
            break;
          note_bytes = size_t(comma - p);
          p = comma;
        }
        if (p == end || *p++ != ',')
          break;

        uint64_t r_id = number(p, end);
        if (p == end || *p++ != ',')
          break;

        bool negative = false;
        if (p < end && *p == '-') {
          negative = true;
          ++p;
        }
        uint64_t whole = number(p, end);
        if (p == end || *p++ != '.')
          break;
        if (end - p < 2)
          break;
        const int64_t cents =
            int64_t(whole * 100 + 10 * (p[0] - '0') + (p[1] - '0')) *
            (negative ? -1 : 1);
        p += 2;
        if (p < end && *p == '\r')
          ++p;
        if (p < end && *p == '\n')
          ++p;

        uint16_t c = r_id < region_size ? region_data[size_t(r_id)] : unmatched;
        if (c == 0xffff)
          c = unmatched;

        aggs[c].add(id, cents, name_len, note_bytes);

        uint64_t h = fnv1a(name_ptr, name_raw_len);
        uint16_t part = uint16_t((h >> 54) & 1023);
        partitions[part].insert_or_update(name_ptr, uint32_t(name_raw_len), h,
                                          c);
      }
    });
  }
  for (auto &w : workers)
    w.join();

  std::vector<Aggregates> final_aggs(codes.size());
  for (int i = 0; i < num_threads; ++i) {
    for (size_t c = 0; c < codes.size(); ++c) {
      final_aggs[c].merge(worker_aggs[i][c]);
    }
  }

  for (const auto &part : partitions) {
    for (const auto &e : part.entries) {
      for (int w = 0; w < 4; ++w) {
        uint64_t mask = e.mask[w];
        while (mask) {
          int b = __builtin_ctzll(mask);
          size_t c = (w << 6) | b;
          if (c < final_aggs.size()) {
            final_aggs[c].names_distinct++;
          }
          mask &= mask - 1;
        }
      }
    }
    for (const auto &e : part.overflow) {
      for (int w = 0; w < 4; ++w) {
        uint64_t mask = e.mask[w];
        while (mask) {
          int b = __builtin_ctzll(mask);
          size_t c = (w << 6) | b;
          if (c < final_aggs.size()) {
            final_aggs[c].names_distinct++;
          }
          mask &= mask - 1;
        }
      }
    }
  }

  std::vector<size_t> order;
  for (size_t i = 0; i < final_aggs.size(); ++i)
    if (final_aggs[i].count)
      order.push_back(i);
  std::sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return codes[a] < codes[b]; });

  fs::create_directories(out_dir);
  std::string out_str;
  out_str.reserve(65536);
  for (size_t c : order) {
    const auto &a = final_aggs[c];
    out_str += codes[c];
    out_str += ' ';
    out_str += std::to_string(a.count);
    out_str += ' ';
    out_str += std::to_string(a.id_sum);
    out_str += ' ';
    out_str += money(a.sum);
    out_str += ' ';
    out_str += money(a.min);
    out_str += ' ';
    out_str += money(a.max);
    out_str += ' ';
    out_str += std::to_string(a.names_distinct);
    out_str += ' ';
    out_str += std::to_string(a.name_bytes);
    out_str += ' ';
    out_str += std::to_string(a.note_bytes);
    out_str += '\n';
  }

  int out_fd = open((fs::path(out_dir) / "aggregates.txt").c_str(),
                    O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (out_fd >= 0) {
    ssize_t written = write(out_fd, out_str.data(), out_str.size());
    (void)written;
    close(out_fd);
  }
  return 0;
}

"""Flat parsed records, counted disjoint scatter, one contiguous latency arena."""
from astra import ROOT,TMP,compile,baseline_source
s=baseline_source('a').replace('#include <array>','#include <array>\n#include <span>\n#include <cerrno>')
s=s.replace('    std::vector<uint32_t> latencies;','    uint32_t count = 0;',1)
s=s.replace('    LocalEpDict dict;','    struct Record { uint32_t group, latency; };\n    std::vector<Record> records;\n    LocalEpDict dict;',1)
s=s.replace('        auto& table = wd.table;','        auto& table = wd.table;\n        wd.records.reserve(size_t(end - p) / 40 + 1);',1)
s=s.replace('            grp.latencies.push_back(latency);','            ++grp.count;\n            wd.records.push_back({uint32_t(h * MAX_EP + local_eid), latency});',1)
marker='    // Parallelize merging, percentile calculation, and output formatting'
prep='''    // Compute each worker's disjoint destination spans, then scatter once.
    size_t groups = size_t(HOURS) * num_global_ep;
    std::vector<uint32_t> offsets(groups + 1), errors(groups);
    for (size_t w = 0; w < workers; ++w)
        for (int h = 0; h < HOURS; ++h)
            for (size_t loc = 0; loc < worker_ep_map[w].size(); ++loc) {
                const auto& g = worker_data[w].table[size_t(h) * MAX_EP + loc];
                size_t global = size_t(h) * num_global_ep + worker_ep_map[w][loc];
                offsets[global + 1] += g.count;
                errors[global] += g.errors;
            }
    for (size_t g = 0; g < groups; ++g) offsets[g + 1] += offsets[g];
    std::vector<uint32_t> values(offsets.back()), cursor = offsets;
    for (size_t w = 0; w < workers; ++w)
        for (int h = 0; h < HOURS; ++h)
            for (size_t loc = 0; loc < worker_ep_map[w].size(); ++loc) {
                auto& g = worker_data[w].table[size_t(h) * MAX_EP + loc];
                size_t global = size_t(h) * num_global_ep + worker_ep_map[w][loc];
                uint32_t count = g.count;
                g.count = cursor[global];
                cursor[global] += count;
            }
    threads.clear();
    for (size_t w = 0; w < workers; ++w) threads.emplace_back([&,w] {
        auto& wd = worker_data[w];
        for (const auto& r : wd.records) values[wd.table[r.group].count++] = r.latency;
        std::vector<WorkerData::Record>().swap(wd.records);
    });
    for (auto& th : threads) th.join();
    for (auto& wd : worker_data) std::vector<LocalGroup>().swap(wd.table);
    std::vector<uint32_t>().swap(cursor);

'''
s=s.replace(marker,prep+marker)
start=s.index('    struct MergedGroup {');end=s.index('    auto process_worker',start);s=s[:start]+s[end:]
start=s.index('        int num_hours =');end=s.index('        // Format lines',start);s=s[:start]+s[end:]
s=s.replace('            int local_h = h - h_start;\n','')
s=s.replace('''                auto& g = local_merged[size_t(local_h) * num_global_ep + e];
                auto& v = g.latencies;''','''                size_t group = size_t(h) * num_global_ep + e;
                std::span<uint32_t> v(values.data() + offsets[group], offsets[group + 1] - offsets[group]);''')
s=s.replace('uint64_t(g.errors)','uint64_t(errors[group])').replace('append_u32(out_ptr, g.errors)','append_u32(out_ptr, errors[group])')
s=s.replace('                std::vector<uint32_t>().swap(v);\n','')
s=s.replace('out_ptr + 256 > out_end','size_t(out_end - out_ptr) < 256')
s=s.replace('''            ssize_t written = write(out_fd, thread_out[w].data(), thread_out_len[w]);
            (void)written;''','''            size_t offset = 0;
            while (offset < thread_out_len[w]) {
                ssize_t written = write(out_fd, thread_out[w].data() + offset, thread_out_len[w] - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) { close(out_fd); return 1; }
                offset += size_t(written);
            }''')
assert 'local_merged' not in s and 'g.latencies' not in s
p=TMP/'a-flat.cpp';p.write_text(s);compile('a','a-flat',p)

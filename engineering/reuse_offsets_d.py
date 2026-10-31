"""Reuse dead degree-count arrays as per-worker CSR scatter cursors."""
from campaign import EVIDENCE,ROOT,TMP,compile
s=(ROOT/'engineering/variants/d-sparse.cpp').read_text()
marker='    std::vector<Edge> edges(m);'
prep='''    // Convert counts to disjoint write cursors before any scatter can start.
    // Each node belongs to one preparation worker; each scatter owns one row.
    std::vector<std::thread> offset_threads;
    for (int wid = 0; wid < parse_workers; ++wid) {
        offset_threads.emplace_back([&, wid]() {
            uint32_t begin = uint64_t(wid) * n / parse_workers;
            uint32_t end = uint64_t(wid + 1) * n / parse_workers;
            for (uint32_t u = begin; u < end; ++u) {
                uint32_t f = head[u], r = r_head[u];
                for (int w = 0; w < parse_workers; ++w) {
                    uint32_t df = worker_deg_f[w][u + 1], dr = worker_deg_r[w][u + 1];
                    worker_deg_f[w][u + 1] = f;
                    worker_deg_r[w][u + 1] = r;
                    f += df; r += dr;
                }
            }
        });
    }
    for (auto& th : offset_threads) th.join();

'''
s=s.replace(marker,prep+marker)
start=s.index('            std::vector<uint32_t> wf(n);'); end=s.index('            const auto& we',start)
s=s[:start]+'''            auto& wf = worker_deg_f[wid];
            auto& wr = worker_deg_r[wid];
'''+s[end:]
s=s.replace('edges[wf[e.u]++]','edges[wf[e.u + 1]++]').replace('r_edges[wr[e.v]++]','r_edges[wr[e.v + 1]++]')
path=TMP/'d-reuse.cpp'; path.write_text(s); compile('d','d-reuse',path)

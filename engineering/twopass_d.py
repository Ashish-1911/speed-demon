"""Test eliminating raw-edge storage after sparse states expose construction RSS."""
from campaign import ROOT,TMP,compile
s=(ROOT/'engineering/variants/d-reuse.cpp').read_text()
start=s.index('struct RawEdge {');end=s.index('struct Edge {',start);s=s[:start]+s[end:]
s=s.replace('    std::vector<std::vector<RawEdge>> worker_edges(parse_workers);\n','')
s=s.replace('            worker_edges[wid].reserve((m / parse_workers) + 32768);\n','')
s=s.replace('            auto& edges_out = worker_edges[wid];\n','')
s=s.replace('                uint32_t w = parse_u32_fast(p);\n','',1)
s=s.replace('                edges_out.push_back({u, v, w});\n','')
start=s.index('            const auto& we = worker_edges[wid];');end=s.index('        });',start)
s=s[:start]+'''            const char* p = chunk_ptrs[wid];
            const char* end = chunk_ptrs[wid + 1];
            while (p < end) {
                while (p < end && *p <= ' ') ++p;
                if (p == end) break;
                uint32_t u = parse_u32_fast(p); ++p;
                uint32_t v = parse_u32_fast(p); ++p;
                uint32_t w = parse_u32_fast(p); ++p;
                edges[wf[u + 1]++] = {v,w};
                r_edges[wr[v + 1]++] = {u,w};
            }
'''+s[end:]
s=s.replace('    std::vector<std::vector<RawEdge>>().swap(worker_edges);\n','')
path=TMP/'d-twopass.cpp';path.write_text(s);compile('d','d-twopass',path)

"""Contract only protected-endpoint-free, simple two-neighbor bidirectional chains."""
from astra import ROOT,TMP,compile,baseline_source
s=baseline_source('d')
marker='    std::vector<int64_t> results(queries.size(), -1);'
prep=r'''
    // A removed vertex has exactly two distinct neighbors in both directions.
    // Protect all query endpoints. Nonnegative detours cannot improve a path;
    // replacing each maximal chain by its directional sum is therefore exact.
    std::vector<uint8_t> removed(n, 0), protected_node(n, 0);
    for (auto [u,v] : queries) protected_node[u] = protected_node[v] = 1;
    uint32_t removed_count = 0;
    for (uint32_t u = 0; u < n; ++u) {
        if (protected_node[u] || head[u+1]-head[u] != 2 || r_head[u+1]-r_head[u] != 2) continue;
        uint32_t a=edges[head[u]].v, b=edges[head[u]+1].v;
        uint32_t c=r_edges[r_head[u]].v, d=r_edges[r_head[u]+1].v;
        if (a != b && a != u && b != u && ((a==c && b==d)||(a==d && b==c))) {
            removed[u] = 1; ++removed_count;
        }
    }
    struct WideEdge { uint64_t w; uint32_t v; };
    std::vector<uint32_t> compact_head(n+1), compact_rhead(n+1);
    for (uint32_t u=0;u<n;++u) {
        compact_head[u+1]=compact_head[u]+(removed[u]?0:head[u+1]-head[u]);
        compact_rhead[u+1]=compact_rhead[u]+(removed[u]?0:r_head[u+1]-r_head[u]);
    }
    std::vector<WideEdge> compact_edges(compact_head.back()), compact_redges(compact_rhead.back());
    std::vector<std::thread> contraction_threads;
    for (int wid=0;wid<parse_workers;++wid) contraction_threads.emplace_back([&,wid] {
        uint32_t begin=uint64_t(wid)*n/parse_workers, end=uint64_t(wid+1)*n/parse_workers;
        for (uint32_t u=begin;u<end;++u) if (!removed[u]) {
            for (int direction=0;direction<2;++direction) {
                const auto& h=direction?r_head:head;
                const auto& es=direction?r_edges:edges;
                const auto& ch=direction?compact_rhead:compact_head;
                auto& out=direction?compact_redges:compact_edges;
                uint32_t offset=ch[u];
                for (uint32_t i=h[u];i<h[u+1];++i) {
                    uint32_t prev=u, v=es[i].v;
                    uint64_t distance=es[i].w;
                    while (removed[v]) {
                        uint32_t e=h[v]+(es[h[v]].v==prev);
                        distance+=es[e].w;
                        prev=v; v=es[e].v;
                    }
                    out[offset++]={distance,v};
                }
            }
        }
    });
    for (auto& th:contraction_threads) th.join();
    std::cerr << "contracted " << removed_count << "/" << n << " vertices; edges " << m << " -> " << compact_edges.size() << "\n";
    std::vector<Edge>().swap(edges); std::vector<Edge>().swap(r_edges);
    head.swap(compact_head);r_head.swap(compact_rhead);
    std::vector<uint32_t>().swap(compact_head);std::vector<uint32_t>().swap(compact_rhead);
    std::vector<uint8_t>().swap(removed);std::vector<uint8_t>().swap(protected_node);

'''
s=s.replace(marker,prep+marker)
start=s.index(marker)
s=s[:start]+s[start:].replace('edges[e]', 'compact_edges[e]').replace('r_compact_edges[e]', 'compact_redges[e]')
p=TMP/'d-contract.cpp';p.write_text(s);compile('d','d-contract',p)

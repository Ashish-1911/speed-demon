"""Radix-partition edges by vertex range so CSR counts/scatters stay cache-local."""
from astra import ROOT,TMP,compile,baseline_source
s=baseline_source('d')
start=s.index('    // Single-pass:');end=s.index('    InputMap q_map',start)
s=s[:start]+r'''
    std::vector<std::vector<RawEdge>> worker_edges(parse_workers);
    std::vector<std::thread> parse_threads;
    for (int wid=0;wid<parse_workers;++wid) parse_threads.emplace_back([&,wid] {
        auto& out=worker_edges[wid]; out.reserve(m/parse_workers+32768);
        const char* p=chunk_ptrs[wid];const char* end=chunk_ptrs[wid+1];
        while (p<end) {
            while (p<end && *p<=' ') ++p;
            if (p==end) break;
            uint32_t u=parse_u32_fast(p);++p;
            uint32_t v=parse_u32_fast(p);++p;
            uint32_t w=parse_u32_fast(p);++p;
            out.push_back({u,v,w});
        }
    });
    for(auto& t:parse_threads)t.join();
    std::vector<uint32_t> head(n+1),r_head(n+1);
    std::vector<Edge> edges(m),r_edges(m);
    // Coarse partitioning turns random full-graph cursor updates into small local
    // arrays. Each bucket owns disjoint vertex and edge ranges: no scatter atomics.
    constexpr uint32_t SHIFT=13, WIDTH=1u<<SHIFT;
    const uint32_t buckets=(n+WIDTH-1)/WIDTH;
    std::vector<RawEdge> partitioned(m);
    auto build=[&](bool reverse,std::vector<uint32_t>& h,std::vector<Edge>& target) {
        std::vector<std::vector<uint32_t>> counts(parse_workers,std::vector<uint32_t>(buckets));
        std::vector<std::thread> threads;
        for(int wid=0;wid<parse_workers;++wid)threads.emplace_back([&,wid] {
            for(const auto& e:worker_edges[wid]) ++counts[wid][(reverse?e.v:e.u)>>SHIFT];
        });
        for(auto& t:threads)t.join();threads.clear();
        std::vector<uint32_t> begin(buckets+1);
        for(uint32_t b=0;b<buckets;++b) {
            uint32_t off=begin[b];
            for(int wid=0;wid<parse_workers;++wid) {
                uint32_t size=counts[wid][b];counts[wid][b]=off;off+=size;
            }
            begin[b+1]=off;
        }
        for(int wid=0;wid<parse_workers;++wid)threads.emplace_back([&,wid] {
            for(const auto& e:worker_edges[wid]) partitioned[counts[wid][(reverse?e.v:e.u)>>SHIFT]++]=e;
        });
        for(auto& t:threads)t.join();threads.clear();
        std::atomic<uint32_t> next{0};
        for(int wid=0;wid<parse_workers;++wid)threads.emplace_back([&] {
            std::vector<uint32_t> cursor(WIDTH);
            while(true) {
                uint32_t b=next.fetch_add(1,std::memory_order_relaxed);if(b>=buckets)break;
                uint32_t lo=b*WIDTH,hi=std::min(n,lo+WIDTH);
                std::fill(cursor.begin(),cursor.end(),0);
                for(uint32_t i=begin[b];i<begin[b+1];++i) {
                    const auto& e=partitioned[i];++cursor[(reverse?e.v:e.u)-lo];
                }
                uint32_t off=begin[b];
                for(uint32_t u=lo;u<hi;++u) {
                    uint32_t size=cursor[u-lo];h[u]=off;cursor[u-lo]=off;off+=size;
                }
                for(uint32_t i=begin[b];i<begin[b+1];++i) {
                    const auto& e=partitioned[i];
                    uint32_t u=reverse?e.v:e.u,v=reverse?e.u:e.v;
                    target[cursor[u-lo]++]={v,e.w};
                }
            }
        });
        for(auto& t:threads)t.join();h[n]=m;
    };
    build(false,head,edges);build(true,r_head,r_edges);
    std::vector<RawEdge>().swap(partitioned);
    std::vector<std::vector<RawEdge>>().swap(worker_edges);

'''+s[end:]
p=TMP/'d-bucket.cpp';p.write_text(s);compile('d','d-bucket',p)

"""Bounded SPSC dispatch to exclusive dictionary partition owners."""
from astra import ROOT,TMP,compile,baseline_source
s=baseline_source('c').replace('#include <array>','#include <array>\n#include <memory>')
s=s.replace('struct Partition {\n    alignas(64) std::atomic_flag lock = ATOMIC_FLAG_INIT;', '// Each partition is accessed only by its assigned owner until all owners join.\nstruct alignas(64) Partition {')
s=s.replace('''        while (lock.test_and_set(std::memory_order_acquire)) {
            _mm_pause();
        }
''','')
s=s.replace('                lock.clear(std::memory_order_release);\n','').replace('        lock.clear(std::memory_order_release);\n','')
queue=r'''
// Input mappings outlive the consumers; names need no allocated byte copies.
struct NameRef { uint64_t hash; const char* ptr; uint32_t len; uint16_t country; };
// One producer and one consumer per queue. Release/acquire transfers a whole batch;
// a producer cannot reuse its ring slot until the consumer releases it.
struct NameQueue {
    static constexpr unsigned SIZE=256, BLOCKS=4;
    struct Batch {
        alignas(64) std::atomic<bool> ready{false};
        unsigned count=0;
        NameRef names[SIZE];
    };
    std::array<Batch,BLOCKS> batches;
    alignas(64) unsigned write_index=0, count=0;
    alignas(64) unsigned read_index=0;
    void add(NameRef item) {
        auto& batch=batches[write_index%BLOCKS];
        if(count==0)while(batch.ready.load(std::memory_order_acquire))_mm_pause();
        batch.names[count++]=item;
        if(count==SIZE)flush();
    }
    void flush() {
        if(!count)return;
        auto& batch=batches[write_index%BLOCKS];
        batch.count=count;batch.ready.store(true,std::memory_order_release);
        ++write_index;count=0;
    }
};

'''
s=s.replace('int main(',queue+'int main(',1)
s=s.replace('    constexpr uint16_t ABSENT', '    int owner_count=std::max(1,num_threads/2);\n    num_threads=std::max(1,num_threads-owner_count);\n    constexpr uint16_t ABSENT',1)
marker='    std::vector<std::thread> workers;'
setup=r'''
    auto queues=std::make_unique<NameQueue[]>(num_threads*owner_count);
    auto done=std::make_unique<std::atomic<bool>[]>(num_threads);
    std::vector<std::thread> owners;
    for(int owner=0;owner<owner_count;++owner)owners.emplace_back([&,owner] {
        while(true) {
            bool finished=true;
            for(int producer=0;producer<num_threads;++producer) {
                auto& q=queues[producer*owner_count+owner];
                auto& batch=q.batches[q.read_index%NameQueue::BLOCKS];
                // Acquire completion before checking the queue: final publication precedes it.
                bool producer_done=done[producer].load(std::memory_order_acquire);
                if(batch.ready.load(std::memory_order_acquire)) {
                    for(unsigned i=0;i<batch.count;++i) {
                        const auto& x=batch.names[i];
                        partitions[(x.hash>>54)&1023].insert_or_update(x.ptr,x.len,x.hash,x.country);
                    }
                    batch.ready.store(false,std::memory_order_release);++q.read_index;
                    finished=false;
                } else if(!producer_done)finished=false;
            }
            if(finished)break;
            _mm_pause();
        }
    });
'''
s=s.replace(marker,setup+marker)
s=s.replace('                partitions[part].insert_or_update(name_ptr, uint32_t(name_raw_len), h, c);','                queues[tid*owner_count+part%owner_count].add({h,name_ptr,uint32_t(name_raw_len),c});')
marker='''        });
    }
    for (auto& w : workers) w.join();'''
assert marker in s
s=s.replace(marker,'''            for(int owner=0;owner<owner_count;++owner)queues[tid*owner_count+owner].flush();
            done[tid].store(true,std::memory_order_release);
        });
    }
    for (auto& w : workers) w.join();
    for (auto& owner : owners) owner.join();''',1)
p=TMP/'c-owned.cpp';p.write_text(s);compile('c','c-owned',p)

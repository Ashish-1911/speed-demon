// A measured byte-reading floor, not a substitute for the real parsing work.
#include <array>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <immintrin.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>
int main(int argc,char**argv) {
    int fd=open(argv[1],O_RDONLY);struct stat st{};fstat(fd,&st);
    size_t size=st.st_size;
    const char* data=(const char*)mmap(nullptr,size,PROT_READ,MAP_PRIVATE,fd,0);
    for(unsigned workers: {1u,6u})for(int rep=0;rep<5;++rep) {
        std::array<uint64_t,6> counts{};std::vector<std::thread> threads;
        auto start=std::chrono::steady_clock::now();
        for(unsigned w=0;w<workers;++w)threads.emplace_back([&,w] {
            size_t pos=size*w/workers,end=size*(w+1)/workers;uint64_t count=0;
            auto lf=_mm256_set1_epi8('\n');
            for(;end-pos>=32;pos+=32) count+=__builtin_popcount(unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i*)(data+pos)),lf))));
            for(;pos<end;++pos)count+=data[pos]=='\n';counts[w]=count;
        });
        for(auto& t:threads)t.join();uint64_t count=0;for(auto x:counts)count+=x;
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<workers<<" "<<rep<<" "<<size<<" "<<seconds<<" "<<count<<"\n";
    }
    munmap((void*)data,size);close(fd);
}

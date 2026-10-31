// Standalone invariants for the production monotone queue and sparse state.
#define main submission_main
#include "../track-d/main.cpp"
#undef main
#include <cassert>
#include <queue>
#include <random>

int main() {
    QueryState state;
    state.clear();
    for (uint32_t i = 0; i < 100000; ++i) state.get(i).f = uint64_t(i) * i;
    for (uint32_t i = 0; i < 100000; ++i) {
        assert(state.get(i).f == uint64_t(i) * i);
        assert(state.get(i).b == UINT64_MAX);
    }
    state.epoch = UINT32_MAX;
    state.clear();
    assert(state.epoch == 1);
    for (uint32_t i = 0; i < 100000; ++i) assert(state.get(i).f == UINT64_MAX);

    RadixHeap<uint32_t> queue;
    std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>> reference;
    std::mt19937_64 random(20261031);
    uint64_t last = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        uint64_t value = last + random() % 100000;
        queue.push(value, i);
        reference.push(value);
        if (i % 3 == 0) {
            last = queue.pop().priority;
            assert(last == reference.top());
            reference.pop();
        }
    }
    while (!reference.empty()) {
        assert(queue.pop().priority == reference.top());
        reference.pop();
    }
    queue.clear();
    queue.push(UINT64_MAX, 1);
    queue.push(uint64_t(1) << 63, 2);
    queue.push(0, 3);
    assert(queue.pop().priority == 0);
    assert(queue.pop().priority == uint64_t(1) << 63);
    assert(queue.pop().priority == UINT64_MAX);
    assert(queue.empty());
}

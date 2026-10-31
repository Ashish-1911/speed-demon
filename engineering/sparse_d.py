"""Construct an isolated sparse-state Track D experiment from the radix candidate."""
from campaign import EVIDENCE,ROOT,TMP,compile
# Historical recipe: requires the preceding candidate source; see evidence and git history.
s=(ROOT/'engineering/variants/d-radix.cpp').read_text()
state=r'''
struct QueryState {
    struct Value { uint32_t node = 0, stamp = 0; uint64_t f, b; };
    std::vector<Value> slots = std::vector<Value>(1024);
    uint32_t epoch = 0;
    size_t used = 0;
    void clear() {
        used = 0;
        if (++epoch == 0) {
            for (auto& s : slots) s.stamp = 0;
            epoch = 1;
        }
    }
    Value& get(uint32_t node) {
        if ((used + 1) * 2 > slots.size()) {
            auto old = std::move(slots);
            slots = std::vector<Value>(old.size() * 2);
            used = 0;
            for (auto& s : old) if (s.stamp == epoch) get(s.node) = s;
        }
        size_t i = (node * 2654435761u) & (slots.size() - 1);
        while (slots[i].stamp == epoch && slots[i].node != node)
            i = (i + 1) & (slots.size() - 1);
        if (slots[i].stamp != epoch) {
            slots[i] = {node, epoch, UINT64_MAX, UINT64_MAX};
            ++used;
        }
        return slots[i];
    }
};

'''
s=s.replace('int main(',state+'int main(',1)
start=s.index('        std::vector<uint64_t> dist_f')
end=s.index('        RadixHeap',start)
s=s[:start]+'        QueryState state;\n\n'+s[end:]
start=s.index('            if (++current_epoch')
end=s.index('            pq_f.clear()',start)
s=s[:start]+'''            state.clear();
            state.get(s).f = 0;
            state.get(t).b = 0;

'''+s[end:]
s=s.replace('                    uint16_t ef = epoch_fb[u] >> 16;\n                    if (ef != current_epoch || pq_f.top().priority > dist_f[u]) {','                    if (pq_f.top().priority > state.get(u).f) {')
s=s.replace('                    uint16_t eb = epoch_fb[u] & 0xFFFF;\n                    if (eb != current_epoch || pq_b.top().priority > dist_b[u]) {','                    if (pq_b.top().priority > state.get(u).b) {')
s=s.replace('''                        uint32_t ep = epoch_fb[v];
                        uint16_t ef = ep >> 16;
                        uint16_t eb = ep & 0xFFFF;
''','''                        auto& st = state.get(v);
''')
s=s.replace('if (ef != current_epoch || nd < dist_f[v])','if (nd < st.f)').replace('if (eb != current_epoch || nd < dist_b[v])','if (nd < st.b)')
s=s.replace('dist_f[v] = nd;','st.f = nd;').replace('dist_b[v] = nd;','st.b = nd;')
s=s.replace('                            epoch_fb[v] = (ep & 0x0000FFFF) | (uint32_t(current_epoch) << 16);\n','').replace('                            epoch_fb[v] = (ep & 0xFFFF0000) | uint32_t(current_epoch);\n','')
s=s.replace('eb == current_epoch && nd + dist_b[v] < best','st.b != UINT64_MAX && nd + st.b < best').replace('ef == current_epoch && dist_f[v] + nd < best','st.f != UINT64_MAX && st.f + nd < best')
s=s.replace('best = nd + dist_b[v];','best = nd + st.b;').replace('best = dist_f[v] + nd;','best = st.f + nd;')
assert 'epoch_fb' not in s and 'dist_f' not in s
path=TMP/'d-sparse.cpp'; path.write_text(s); compile('d','d-sparse',path)

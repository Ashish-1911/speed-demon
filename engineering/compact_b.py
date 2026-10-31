"""Isolated 16-byte term dictionary experiment, retaining exact byte comparisons."""
from campaign import EVIDENCE,ROOT,TMP,compile
# Historical recipe: requires the preceding candidate source; see evidence and git history.
s=(ROOT/'engineering/variants/b-gallop.cpp').read_text()
s=s.replace('    uint32_t term_id = UINT32_MAX;','    uint32_t id_length = 0;')
s=s.replace('    uint16_t term_len = 0;\n','')
s=s.replace('table[idx] = {h, id, str_offset, (uint16_t)term.size()};','table[idx] = {h, id | (uint32_t(term.size()) << 24), str_offset};')
s=s.replace('table[idx].term_len == len','(table[idx].id_length >> 24) == len')
s=s.replace('return table[idx].term_id;','return table[idx].id_length & 0xffffff;')
path=TMP/'b-compact.cpp'; path.write_text(s); compile('b','b-compact',path)

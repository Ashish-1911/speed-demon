from astra import TMP,compile,compare,EVIDENCE
import json,statistics
s=(TMP/'c-owned.cpp').read_text()
for owners in [3,4]:
 p=TMP/f'c-owned{owners}.cpp';p.write_text(s.replace('int owner_count=std::max(1,num_threads/2);',f'int owner_count=std::min({owners},std::max(1,num_threads/2));'));compile('c',f'c-owned{owners}',p)
for owners in [3,4]: compare('c',f'c-owned{owners}','c-owned')

"""Byte-scan floor and instrumented D work, kept outside the submission."""
import subprocess,json
from astra import ROOT,TMP,EVIDENCE,compile,run
rows=[]
for t,name in [('a','requests.log'),('c','records.csv'),('d','graph.txt')]:
    result=subprocess.check_output([str(TMP/'scan'),str(TMP/f'data/{t}_full/{name}')],text=True)
    rows.append(dict(track=t,scan_columns=['workers','repeat','bytes','seconds','newline_count'],scan=result))
    print(t,result,flush=True)
s=(TMP/'d-bucket.cpp').read_text()
s=s.replace('    auto worker = [&]() {','    std::atomic<uint64_t> expansions{0}, scans{0}, stale{0};\n    auto worker = [&]() {\n        uint64_t ex=0, sc=0, st=0;')
s=s.replace('if (idx >= queries.size()) return;','if (idx >= queries.size()) { expansions+=ex; scans+=sc; stale+=st; return; }')
s=s.replace('auto [d, u] = pq_f.pop();','auto [d, u] = pq_f.pop(); ++ex; sc+=head[u+1]-head[u];')
s=s.replace('auto [d, u] = pq_b.pop();','auto [d, u] = pq_b.pop(); ++ex; sc+=r_head[u+1]-r_head[u];')
s=s.replace('                        pq_f.pop();','                        pq_f.pop(); ++st;').replace('                        pq_b.pop();','                        pq_b.pop(); ++st;')
s=s.replace('    fs::create_directories(out_dir);','    std::cerr<<"expansions="<<expansions<<" edge_scans="<<scans<<" stale_pops="<<stale<<"\\n";\n    fs::create_directories(out_dir);')
p=TMP/'d-work.cpp';p.write_text(s);compile('d','d-work',p)
rows.append(dict(track='d',instrumented=run('d','d-work')))
print(rows[-1],flush=True)
(EVIDENCE/'work-floor.json').write_text(json.dumps(rows,indent=2))

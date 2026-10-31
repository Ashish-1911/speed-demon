"""Reproducible local experiments; invoke under WSL, never in a scored track."""
import hashlib, json, pathlib, statistics, subprocess, time, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "engineering/evidence"
EVIDENCE.mkdir(parents=True, exist_ok=True)
BASE = pathlib.Path('/mnt/d/hackathons/Speed Demon')
TMP = pathlib.Path('/tmp/sd-campaign')
TMP.mkdir(exist_ok=True)
OUTPUT = dict(a='summary.txt', b='results.txt', c='aggregates.txt', d='distances.txt')
def compile(track, name, source=None, flags=None):
    if source is None and name.endswith('-base'):
        source = TMP / f'{track}-baseline.cpp'
        source.write_bytes(subprocess.check_output(['git','-C',str(ROOT),'show',f'a3ab590:track-{track}/main.cpp']))
    source = source or ROOT / f'track-{track}/main.cpp'
    subprocess.run(['g++', '-std=c++20', '-O3', '-flto', '-march=haswell', '-pthread', *(flags or []), str(source), '-o', str(TMP/name)], check=True)
def run(track, name, dataset='full', args=()):
    out = TMP / f'out-{name}-{dataset}'
    out.mkdir(exist_ok=True)
    start = time.perf_counter()
    p = subprocess.run(['/usr/bin/time', '-f', 'RSS_KB=%M', str(TMP/name), '--data', str(TMP/f'data/{track}_{dataset}'), '--out', str(out), *args], capture_output=True, text=True, check=True)
    return dict(seconds=time.perf_counter()-start, stderr=p.stderr, sha256=hashlib.sha256((out/OUTPUT[track]).read_bytes()).hexdigest())
def profile():
    markers = {
      'a': [('    // Collect and sort global endpoints','parse'), ('    // Parallelize merging','dictionary'), ('    // Fast write','reduce_format')],
      'b': [('    uint32_t num_terms','queries'), ('    std::vector<uint32_t> csr_head','tokenize_index'), ('    size_t num_queries','csr'), ('    // 2. Document-Centric','boolean'), ('    size_t total_pairs','phrase_candidates'), ('    std::atomic<size_t> next_cand_idx','bucket'), ('    std::vector<std::vector<uint32_t>> query_doc_matches','phrase_verify')],
      'c': [('    auto bounds =','regions'), ('    std::vector<std::vector<Aggregates>>','boundaries'), ('    std::vector<Aggregates> final_aggs','parse_aggregate')],
      'd': [('    // Parallel degree sum','parse'), ('    std::vector<Edge> edges(m)','prefix'), ('    // Release all temporary','scatter'), ('    InputMap q_map','release'), ('    fs::create_directories(out_dir)','query')]
    }
    for track in 'abcd':
        source=(ROOT/f'track-{track}/main.cpp').read_text()
        source='#include <chrono>\n'+source
        start=source.index('{',source.index('int main('))+1
        source=source[:start]+'''\n    auto last=std::chrono::steady_clock::now();
    auto stamp=[&](const char* label) { auto now=std::chrono::steady_clock::now(); std::cerr << label << " " << std::chrono::duration<double,std::milli>(now-last).count() << " ms\\n"; last=now; };\n'''+source[start:]
        for marker,label in markers[track]:
            assert marker in source, marker
            source=source.replace(marker,f'    stamp("{label}");\n'+marker,1)
        path=TMP/f'{track}-profile.cpp'; path.write_text(source)
        compile(track,f'{track}-base')
        compile(track,f'{track}-profile',path)
        for rep in range(3):
            r=run(track,f'{track}-profile'); print(track,rep,json.dumps(r),flush=True)
if __name__=='__main__':
    if sys.argv[1] in ['compare','compare-built']:
        t,name,base=sys.argv[2:]
        if sys.argv[1]=='compare': compile(t,name)
        rows=[]
        for rep in range(7):
            for binary in ([base,name] if rep%2==0 else [name,base]):
                row=dict(name=binary,rep=rep,**run(t,binary)); rows.append(row); print(json.dumps(row),flush=True)
        (EVIDENCE/f'{name}.json').write_text(json.dumps(rows,indent=2))
    if sys.argv[1]=='profile': profile()
    if sys.argv[1]=='b-intersect':
        compile('b','b-gallop')
        rows=[]
        for rep in range(7):
            for name in (['b-base','b-gallop'] if rep%2==0 else ['b-gallop','b-base']):
                row=dict(name=name,rep=rep,**run('b',name)); rows.append(row); print(json.dumps(row),flush=True)
        (EVIDENCE/'b-intersect.json').write_text(json.dumps(rows,indent=2))
    if sys.argv[1]=='d-workers':
        rows=[]
        for rep in range(5):
            for count in ([12,6,8,4] if rep%2==0 else [4,8,6,12]):
                row=dict(threads=count,rep=rep,**run('d','d-base',args=['--threads',str(count)])); rows.append(row); print(json.dumps(row),flush=True)
        (EVIDENCE/'d-workers.json').write_text(json.dumps(rows,indent=2))

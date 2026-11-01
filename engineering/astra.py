"""Three-track architectural campaign, baseline f153bef; WSL only."""
import pathlib,subprocess,json,time,statistics,hashlib,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE=pathlib.Path('/mnt/d/hackathons/Speed Demon')
TMP=pathlib.Path('/home/mumukshu/sd-astra'); TMP.mkdir(exist_ok=True)
EVIDENCE=ROOT/'engineering/evidence/astra'; EVIDENCE.mkdir(exist_ok=True)
OUT={'a':'summary.txt','c':'aggregates.txt','d':'distances.txt'}

def baseline_source(t):
    assert t in OUT
    return subprocess.check_output(['git','-C',str(BASE),'show',f'f153bef:track-{t}/main.cpp']).decode()
def compile(t,name,source=None,extra=()):
    assert t in OUT
    source=source or ROOT/f'track-{t}/main.cpp'
    subprocess.run(['g++','-std=c++20','-O3','-flto','-march=haswell','-pthread',*extra,str(source),'-o',str(TMP/name)],check=True)
def run(t,name,dataset='full',args=()):
    out=TMP/f'out-{name}-{dataset}';out.mkdir(exist_ok=True)
    start=time.perf_counter()
    p=subprocess.run(['/usr/bin/time','-f','RSS_KiB=%M',str(TMP/name),'--data',str(TMP/f'data/{t}_{dataset}'),'--out',str(out),*args],capture_output=True,text=True,timeout=180,check=True)
    return dict(seconds=time.perf_counter()-start,stderr=p.stderr,sha256=hashlib.sha256((out/OUT[t]).read_bytes()).hexdigest())
def compare(t,name,base):
    for _ in range(2):
        run(t,base); run(t,name)
    rows=[]
    for rep in range(7):
        for binary in ([base,name] if rep%2==0 else [name,base]):
            r=dict(binary=binary,rep=rep,**run(t,binary));rows.append(r);print(json.dumps(r),flush=True)
    assert len(set(r['sha256'] for r in rows))==1
    (EVIDENCE/f'{name}.json').write_text(json.dumps(rows,indent=2))
def profile():
    markers={'a':[('    // Collect and sort global endpoints','parse'),('    // Parallelize merging','dictionary'),('    // Fast write','reduce_format')],
             'c':[('    auto bounds =','regions'),('    std::vector<std::vector<Aggregates>>','boundaries'),('    std::vector<Aggregates> final_aggs','parse_aggregate')],
             'd':[('    // Parallel degree sum','parse'),('    std::vector<Edge> edges(m)','prefix_cursors'),('    // Release all temporary','scatter'),('    InputMap q_map','release'),('    fs::create_directories(out_dir)','queries')]}
    rows=[]
    for t in OUT:
        compile(t,f'{t}-base')
        s=(ROOT/f'track-{t}/main.cpp').read_text(); s='#include <chrono>\n'+s
        start=s.index('{',s.index('int main('))+1
        s=s[:start]+'''\n auto last=std::chrono::steady_clock::now();
 auto stamp=[&](const char* label){auto now=std::chrono::steady_clock::now();std::cerr<<label<<" "<<std::chrono::duration<double,std::milli>(now-last).count()<<" ms\\n";last=now;};\n'''+s[start:]
        for marker,label in markers[t]:
            assert marker in s,marker
            s=s.replace(marker,f'    stamp("{label}");\n'+marker,1)
        path=TMP/f'{t}-profile.cpp';path.write_text(s);compile(t,f'{t}-profile',path)
        for rep in range(3):
            row=dict(track=t,rep=rep,**run(t,f'{t}-profile'));rows.append(row);print(json.dumps(row),flush=True)
    (EVIDENCE/'profile.json').write_text(json.dumps(rows,indent=2))
if __name__=='__main__':
    if sys.argv[1]=='profile': profile()
    elif sys.argv[1]=='compare': compare(*sys.argv[2:])

import subprocess,json,os,hashlib
from astra import ROOT,TMP,EVIDENCE,OUT
from astra_verify import CANDIDATE
rows=[]
cases={'a':['tiny','sample','empty'],'c':['tiny','sample','partition-overflow','empty-records'],'d':['tiny','sample','random0','empty-edges','bucket-boundary','epoch-wrap']}
for kind,flags in [('asan-ubsan',['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']),('tsan',['-fsanitize=thread','-fno-pie','-no-pie'])]:
    for t in 'acd':
        binary=TMP/f'{t}-{kind}'
        subprocess.run(['g++','-std=c++20','-O1','-g','-march=haswell','-pthread',*flags,str(TMP/f'{CANDIDATE[t]}.cpp'),'-o',str(binary)],check=True)
        for ds in cases[t]:
            out=TMP/f'san-{t}-{kind}-{ds}';out.mkdir(exist_ok=True)
            p=subprocess.run([str(binary),'--data',str(TMP/f'data/{t}_{ds}'),'--out',str(out)],capture_output=True,text=True,timeout=180,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1','TSAN_OPTIONS':'halt_on_error=1'})
            row=dict(track=t,kind=kind,dataset=ds,returncode=p.returncode,stderr=p.stderr);rows.append(row)
            (EVIDENCE/'sanitizers.json').write_text(json.dumps(rows,indent=2))
            print(json.dumps(row),flush=True)
            if p.returncode:
                if kind=='tsan' and ('unexpected memory mapping' in p.stderr or 'ADDR_NO_RANDOMIZE' in p.stderr):break
                raise RuntimeError(row)
            expected=(TMP/f'out-{CANDIDATE[t]}-{ds}'/OUT[t]).read_bytes()
            assert (out/OUT[t]).read_bytes()==expected
s=(ROOT/'engineering/d_structures.cpp').read_text().replace('"../track-d/main.cpp"','"d-bucket.cpp"')
(TMP/'d-structures.cpp').write_text(s)
subprocess.run(['g++','-std=c++20','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-pie','-no-pie',str(TMP/'d-structures.cpp'),'-o',str(TMP/'d-structures')],check=True)
subprocess.run([str(TMP/'d-structures')],check=True)
print('D sparse growth, epoch wrap, radix boundaries PASS',flush=True)

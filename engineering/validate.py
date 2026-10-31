"""Byte-exact reference regression, with persisted hashes and process outcomes."""
import importlib.util, json, pathlib, subprocess, sys, hashlib
from campaign import EVIDENCE,ROOT, BASE, TMP, OUTPUT, run, compile
MODULE=dict(a='a_ingest',b='b_index',c='c_join',d='d_traverse')
def reference(t, data, out):
    out.mkdir(exist_ok=True,parents=True)
    subprocess.run(['python3',str(BASE/f'official/reference/{MODULE[t]}.py'),'--data',str(data),'--out',str(out)],check=True)
    return hashlib.sha256((out/OUTPUT[t]).read_bytes()).hexdigest()
def validate(t,name):
    rows=[]
    for ds in ['tiny','sample','seed42','seed99','full']:
        if ds=='full':
            cal=json.loads((BASE/'official/harness/calibration-seed1.json').read_text())
            expected=cal['tracks'][t]['output_sha256'][0]
        else: expected=reference(t,TMP/f'data/{t}_{ds}',TMP/f'ref-{t}-{ds}')
        r=run(t,name,ds); r.update(track=t,dataset=ds,expected=expected,passed=r['sha256']==expected)
        rows.append(r); print(json.dumps(r),flush=True); assert r['passed']
    (EVIDENCE/f'verification-{name}.json').write_text(json.dumps(rows,indent=2))
def fresh():
    rows=[]
    for t in 'abcd':
        name=f'{t}-release'; compile(t,name)
        validate(t,name)
        data=TMP/f'data/{t}_fresh20261031'
        subprocess.run(['python3',str(BASE/f'official/generators/{MODULE[t]}.py'),'--seed','20261031','--scale','full','--out',str(data)],check=True)
        expected=reference(t,data,TMP/f'ref-{t}-fresh20261031')
        for repeat in range(2):
            r=run(t,name,'fresh20261031'); r.update(track=t,dataset='full-seed20261031',expected=expected,passed=r['sha256']==expected)
            rows.append(r); print(json.dumps(r),flush=True); assert r['passed']
        (EVIDENCE/'verification-fresh.json').write_text(json.dumps(rows,indent=2))
if __name__=='__main__':
    if sys.argv[1]=='fresh': fresh()
    else: validate(*sys.argv[1:])

import subprocess,json,hashlib,sys
from astra import ROOT,BASE,TMP,EVIDENCE,OUT,run
MODULE={'a':'a_ingest','c':'c_join','d':'d_traverse'}
CANDIDATE={'a':'a-flat','c':'c-owned','d':'d-bucket'}
def reference(t,ds):
    out=TMP/f'reference-{t}-{ds}';out.mkdir(exist_ok=True)
    subprocess.run(['python3',str(BASE/f'official/reference/{MODULE[t]}.py'),'--data',str(TMP/f'data/{t}_{ds}'),'--out',str(out)],check=True)
    return hashlib.sha256((out/OUT[t]).read_bytes()).hexdigest()
def validate(t,name,datasets):
    rows=[]
    for ds in datasets:
        expected=(json.loads((BASE/'official/harness/calibration-seed1.json').read_text())['tracks'][t]['output_sha256'][0] if ds=='full' else reference(t,ds))
        for repeat in range(2):
            r=run(t,name,ds);r.update(track=t,dataset=ds,expected=expected,passed=r['sha256']==expected);assert r['passed'],r
            rows.append(r);print(json.dumps(r),flush=True)
        (EVIDENCE/f'verify-{name}-{datasets[-1]}.json').write_text(json.dumps(rows,indent=2))
if __name__=='__main__':
    for t in 'acd':
        if sys.argv[1]=='fresh':
            ds='fresh20261101';subprocess.run(['python3',str(BASE/f'official/generators/{MODULE[t]}.py'),'--seed','20261101','--scale','full','--out',str(TMP/f'data/{t}_{ds}')],check=True)
            validate(t,CANDIDATE[t],[ds])
        else: validate(t,CANDIDATE[t],['tiny','sample','seed42','seed99','full'])

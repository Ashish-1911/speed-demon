import json,hashlib
from astra import TMP,EVIDENCE,OUT,run
from astra_verify import CANDIDATE
rows=[]
for t in 'acd':
    expected={}
    for f in EVIDENCE.glob(f'verify-{CANDIDATE[t]}-*.json'):
        for r in json.loads(f.read_text()):expected[r['dataset']]=r['expected']
    for r in json.loads((EVIDENCE/'verification-adversarial.json').read_text()):
        if r['track']==t:expected[r['case']]=r['expected']
    for ds,sha in expected.items():
        for rep in range(2):
            r=run(t,f'{t}-selected',ds);assert r['sha256']==sha,(t,ds,r,sha)
            rows.append(dict(track=t,dataset=ds,repeat=rep,expected=sha,**r))
        print(t,ds,'PASS',flush=True)
(EVIDENCE/'final-verification.json').write_text(json.dumps(rows,indent=2))

import json
from campaign import EVIDENCE,ROOT,TMP,run,compile
from validate import validate
rows=[]
for t in 'bd':
    compile(t,f'{t}-release'); validate(t,f'{t}-release')
    expected=next(r['expected'] for r in json.loads((EVIDENCE/'verification-fresh.json').read_text()) if r['track']==t)
    for repeat in range(2):
        r=run(t,f'{t}-release','fresh20261031'); assert r['sha256']==expected
        rows.append(dict(track=t,repeat=repeat,expected=expected,**r)); print(t,'full fresh PASS',flush=True)
(EVIDENCE/'verification-second-pass.json').write_text(json.dumps(rows,indent=2))

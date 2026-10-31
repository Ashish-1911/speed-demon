"""Targeted correctness tests for the optimized paths, using independent references."""
import random, subprocess, json, hashlib
from campaign import EVIDENCE,ROOT, BASE, TMP, OUTPUT, compile, run
from validate import reference

rows=[]
def check(t,label,files,args=()):
    data=TMP/f'data/{t}_{label}'; data.mkdir(parents=True,exist_ok=True)
    for name,content in files.items(): (data/name).write_bytes(content)
    expected=reference(t,data,TMP/f'ref-{t}-{label}')
    r=run(t,f'{t}-release',label,args); assert r['sha256']==expected,(label,r,expected)
    rows.append(dict(track=t,case=label,expected=expected,**r)); print(t,label,'PASS',flush=True)

for t in 'abcd': compile(t,f'{t}-release')
check('a','empty',{'requests.log':b''})
random.seed(312026)
for seed in range(12):
    n=31; edges=[(random.randrange(n),random.randrange(n),random.choice([0,1,2,17,4294967295])) for _ in range(100)]
    graph=f'{n} {len(edges)}\n'+''.join(f'{u} {v} {w}\n' for u,v,w in edges)
    queries=''.join(f'{u} {v}\n' for u in range(n) for v in range(n))
    check('d',f'random{seed}',{'graph.txt':graph.encode(),'queries.txt':queries.encode()})
check('d','epoch-wrap',{'graph.txt':b'4 4\n0 1 4294967295\n1 2 4294967295\n2 3 0\n3 0 1\n','queries.txt':b'0 3\n3 2\n'*35000},['--threads','1'])

docs=[]
for i in range(5000):
    words=['common','COMMON','alpha']
    if i%17==0: words += ['rare','rare','beta']
    if i%37==0: words += ['gamma','beta']
    if i%5==0: words += ['007','Z\u00fcrich']
    docs.append(f'{i}\t'+','.join(words)+'\n')
queries=['PHRASE "rare rare beta"','PHRASE "common alpha"','PHRASE "z rich"']
terms=['common','rare','beta','gamma','missing','007','alpha','rich']
for _ in range(3000):
    q='BOOL '+random.choice(terms)
    for _ in range(random.randrange(5)): q+=' '+random.choice(['AND','OR','NOT'])+' '+random.choice(terms)
    queries.append(q)
check('b','skewed-sets',{'docs.txt':''.join(docs).encode(),'queries.txt':('\n'.join(queries)+'\n').encode()})
queries=['BOOL '+' OR '.join('term'+str(i*6+j) for j in range(6)) for i in range(50000)]
check('b','dictionary-growth',{'docs.txt':b'0\tterm0 term299999\n1\tterm150000\n','queries.txt':('\n'.join(queries)+'\n').encode()})

# Same partition and colliding hashes must compare bytes and terminate on overflow.
def fnv(s):
    h=14695981039346656037
    for c in s: h=((h^c)*1099511628211)&((1<<64)-1)
    return h or 1
names=[]; i=0
while len(names)<350:
    x=i; suffix=''
    for _ in range(6): suffix+=chr(65+x%26); x//=26
    name=('Name '+suffix).encode(); i+=1
    if fnv(name)>>54==42: names.append(name)
records=[b'\xef\xbb\xbfid,name,note,region_id,amount\r\n']
for i,name in enumerate(names*3):
    records.append(str(i+1).encode()+b',"'+name+b'","said ""no""\nnext",'+str(i%3+1).encode()+b',-0.01\r\n')
check('c','partition-overflow',{'regions.csv':b'region_id,country\n1,AAA\n2,BBB\n','records.csv':b''.join(records)})
(EVIDENCE/'verification-adversarial.json').write_text(json.dumps(rows,indent=2))

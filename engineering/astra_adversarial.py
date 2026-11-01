"""Independent reference comparisons for A/C/D architectural edge cases."""
import random,json,subprocess
from astra import ROOT,TMP,EVIDENCE,run
from astra_verify import reference,CANDIDATE
rows=[]
def check(t,label,files,args=()):
    data=TMP/f'data/{t}_{label}';data.mkdir(parents=True,exist_ok=True)
    for name,content in files.items(): (data/name).write_bytes(content)
    expected=reference(t,label)
    for _ in range(3):
        r=run(t,CANDIDATE[t],label,args);assert r['sha256']==expected,(label,r,expected)
        rows.append(dict(track=t,case=label,expected=expected,**r))
    print(t,label,'PASS',flush=True)
check('a','empty',{'requests.log':b''})
random.seed(312026)
logs=[]
for group,n in enumerate([1,2,3,4,19,20,21,99,100,101,257,4097]):
    ep='/'+'a'*124 if group==0 else '/rank'+str(group)
    for i in range(n):
        logs.append(f'2026-11-01T00:00:00.000000Z GET {ep} {[200,499,500,599][i%4]} {[0,1,4294967295,17,255][i%5]}\n')
random.shuffle(logs)
check('a','rank-boundaries',{'requests.log':''.join(logs).encode()})
for seed in range(12):
    n=31;edges=[(random.randrange(n),random.randrange(n),random.choice([0,1,2,17,4294967295])) for _ in range(100)]
    graph=f'{n} {len(edges)}\n'+''.join(f'{u} {v} {w}\n' for u,v,w in edges)
    queries=''.join(f'{u} {v}\n' for u in range(n) for v in range(n))
    check('d',f'random{seed}',{'graph.txt':graph.encode(),'queries.txt':queries.encode()})
check('d','epoch-wrap',{'graph.txt':b'4 4\n0 1 4294967295\n1 2 4294967295\n2 3 0\n3 0 1\n','queries.txt':b'0 3\n3 2\n'*35000},['--threads','1'])
check('d','empty-edges',{'graph.txt':b'8193 0\n','queries.txt':b'0 8192\n8192 8192\n8191 8192\n'})
edges=[(i,8192,0) for i in range(8193)]+[(8192,i,4294967295) for i in range(8193)]+[(0,8192,1)]*100
check('d','bucket-boundary',{'graph.txt':(f'8193 {len(edges)}\n'+''.join(f'{u} {v} {w}\n' for u,v,w in edges)).encode(),'queries.txt':b'0 8191\n8191 0\n8192 0\n0 0\n'})
def fnv(s):
    h=14695981039346656037
    for c in s:h=((h^c)*1099511628211)&((1<<64)-1)
    return h or 1
names=[];i=0
while len(names)<350:
    x=i;suffix=''
    for _ in range(6):suffix+=chr(65+x%26);x//=26
    name=('Name '+suffix).encode();i+=1
    if fnv(name)>>54==42:names.append(name)
records=[b'\xef\xbb\xbfid,name,note,region_id,amount\r\n']
for i,name in enumerate(names*100):
    records.append(str(i+1).encode()+b',"'+name+b'","said ""no""\nnext",'+str(i%3+1).encode()+b',-0.01\r\n')
check('c','partition-overflow',{'regions.csv':b'region_id,country\n1,AAA\n2,BBB\n','records.csv':b''.join(records)})
check('c','empty-records',{'regions.csv':b'region_id,country\n1,AAA\n','records.csv':b'id,name,note,region_id,amount\n'})
(EVIDENCE/'verification-adversarial.json').write_text(json.dumps(rows,indent=2))

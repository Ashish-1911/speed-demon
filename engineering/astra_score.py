import json,statistics,math
from astra import EVIDENCE,BASE
refs={'a':(32.047276,227983360,56),'c':(14.444059,367435776,108),'d':(72.692655,892915712,43)}
board=json.loads((BASE/'.out/astra-board.json').read_text())
team=next(x for x in board['teams'] if x['team']=='Kujo Jotaro')
(EVIDENCE/'official-baseline.json').write_text(json.dumps(dict(generated_at=board['generated_at'],team=team),indent=2))
memory=json.loads((EVIDENCE/'release-memory.json').read_text())
sizes=json.loads((EVIDENCE/'static-axes.json').read_text())
rows=[]
def k(t,T,M,S):
    tr,mr,sr=refs[t]
    return (tr/T)**.5*min(50,mr/M)**.2*min(1,sr/S)**.15
for t in 'acd':
    times=json.loads((EVIDENCE/f'{t}-selected.json').read_text());row={'track':t}
    for version,suffix in [('baseline','production'),('candidate','selected')]:
        ts=[r['seconds'] for r in times if r['binary']==f'{t}-{suffix}']
        ms=[r['memory_peak'] for r in memory if r['binary']==f'{t}-{suffix}']
        T=statistics.median(ts);M=statistics.median(ms);S=next(r['S'] for r in sizes if r['track']==t and r['version']==version)
        K=k(t,T,M,S)
        row[version]=dict(T=T,T_min=min(ts),T_max=max(ts),M=M,S=S,D=1,K=K,points=50*math.log10(K))
    base=row['baseline'];new=row['candidate'];official=team['tracks'][t]['best']
    projected_T=official['T_s']*new['T']/base['T'];projected_M=official['M_bytes']*new['M']/base['M']
    projected_K=k(t,projected_T,projected_M,new['S'])
    row['projection_only']=dict(T=projected_T,M=projected_M,K=projected_K,points=50*math.log10(projected_K))
    row['local_K_ratio']=new['K']/base['K'];rows.append(row);print(json.dumps(row),flush=True)
result=dict(method='Seven alternating process times; independent three-run cgroup peaks; native WSL, not official. Projections apply local ratios to best official baseline.',rows=rows,baseline_portfolio=math.prod(team['tracks'][t]['best']['points'] for t in 'acd')**(1/3),projected_portfolio=math.prod(r['projection_only']['points'] for r in rows)**(1/3))
(EVIDENCE/'complete-K.json').write_text(json.dumps(result,indent=2));print(result['baseline_portfolio'],result['projected_portfolio'])

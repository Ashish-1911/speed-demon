"""Read-only official feed snapshot; distinguish best from latest per track."""
import urllib.request,json,math,datetime
from astra import EVIDENCE
url='https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json'
request=urllib.request.Request(url,headers={'User-Agent':'SpeedDemon-engineering/1.0','Cache-Control':'no-cache'})
b=json.loads(urllib.request.urlopen(request,timeout=30).read())
def score(team):
    points=[v['best']['points'] for v in team['tracks'].values() if v.get('registered') and v.get('best') and v['best'].get('ok')]
    return math.prod(sorted(points,reverse=True)[:3])**(1/3) if len(points)>=3 else 0
ranked=sorted(b['teams'],key=score,reverse=True)
team=next(x for x in ranked if x['team']=='Kujo Jotaro')
result=dict(generated_at=b['generated_at'],rank=ranked.index(team)+1,portfolio=score(team),top5=[dict(team=x['team'],portfolio=score(x)) for x in ranked[:5]],team=team)
stamp=b['generated_at'].replace(':','').replace('-','')
(EVIDENCE/f'official-{stamp}.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))

import subprocess,json
from astra import ROOT,BASE,TMP,EVIDENCE
from astra_verify import CANDIDATE

exec((ROOT/'engineering/get_static_tools.py').read_text().replace("'/tmp/sd-campaign/tools'", "'/home/mumukshu/sd-astra/tools'").replace('mkdir(exist_ok=True)','mkdir(parents=True,exist_ok=True)'))
rows=[]
for version in ['baseline','candidate']:
    for t in 'acd':
        dest=TMP/f'static-{version}-{t}';dest.mkdir(exist_ok=True)
        source=(subprocess.check_output(['git','-C',str(BASE),'show',f'f153bef:track-{t}/main.cpp']) if version=='baseline' else (TMP/f'{CANDIDATE[t]}.cpp').read_bytes())
        formatted=subprocess.run([str(TMP/'tools/clang-format'),'--style=LLVM','--assume-filename=main.cpp'],input=source,capture_output=True,check=True).stdout
        (dest/'main.cpp').write_bytes(formatted)
        (dest/'Dockerfile').write_bytes((ROOT/f'track-{t}/Dockerfile').read_bytes())
        stats=json.loads(subprocess.check_output([str(TMP/'tools/scc'),'--no-config','--format','json',str(dest)]))
        rows.append(dict(version=version,track=t,S=sum(x['Code'] for x in stats),D=1))
        print(rows[-1],flush=True)
(EVIDENCE/'static-axes.json').write_text(json.dumps(rows,indent=2))

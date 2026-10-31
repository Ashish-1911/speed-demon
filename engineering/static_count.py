import subprocess,json,pathlib
from campaign import EVIDENCE,ROOT,BASE,TMP
rows=[]
for version,root in [('baseline',BASE),('candidate',ROOT)]:
    for t in 'abcd':
        dest=TMP/f'static-{version}-{t}'; dest.mkdir(exist_ok=True)
        source=(subprocess.check_output(['git','-C',str(ROOT),'show',f'a3ab590:track-{t}/main.cpp'])
                if version=='baseline' else (root/f'track-{t}/main.cpp').read_bytes())
        formatted=subprocess.run([str(TMP/'tools/clang-format'),'--style=LLVM','--assume-filename=main.cpp'],input=source,capture_output=True,check=True).stdout
        (dest/'main.cpp').write_bytes(formatted)
        (dest/'Dockerfile').write_bytes((root/f'track-{t}/Dockerfile').read_bytes())
        stats=json.loads(subprocess.check_output([str(TMP/'tools/scc'),'--no-config','--format','json',str(dest)]))
        count=sum(x['Code'] for x in stats)
        rows.append(dict(version=version,track=t,S=count,D=1)); print(rows[-1])
(EVIDENCE/'static-axes.json').write_text(json.dumps(rows,indent=2))

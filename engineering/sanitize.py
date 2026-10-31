import subprocess,json
from campaign import EVIDENCE,ROOT,TMP,run
rows=[]
for mode in ['address,undefined','thread']:
    for t in 'abcd':
        name=t+'-'+mode.replace(',','-')
        subprocess.run(['g++','-std=c++20','-O1','-g','-march=haswell','-pthread','-fno-omit-frame-pointer','-fno-pie','-no-pie','-fsanitize='+mode,str(ROOT/f'track-{t}/main.cpp'),'-o',str(TMP/name)],check=True)
        try:
            r=run(t,name,'tiny'); rows.append(dict(track=t,mode=mode,**r)); print(t,mode,r['stderr'],flush=True)
        except subprocess.CalledProcessError as e:
            rows.append(dict(track=t,mode=mode,error=e.stderr,returncode=e.returncode)); print(t,mode,e.stderr,flush=True)
        (EVIDENCE/'sanitizers.json').write_text(json.dumps(rows,indent=2))

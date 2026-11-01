"""Native WSL cgroup-v2 high-water marks with input resident before each group.
These are local measurements, not official Docker or bare-metal scores.
Run as root; only creates/removes its own empty cgroups.
"""
import subprocess,json,pathlib,time,hashlib,sys
from astra import TMP,EVIDENCE,OUT
from astra_verify import CANDIDATE
parent=pathlib.Path('/sys/fs/cgroup/sd-astra-measure');parent.mkdir(exist_ok=True)
(parent/'cgroup.subtree_control').write_text('+memory')
rows=[]
release='--release' in sys.argv
for t in 'acd':
    data=TMP/f'data/{t}_full'
    for f in data.iterdir():
        with f.open('rb') as stream:
            while stream.read(8<<20):pass
    for rep in range(3):
        for binary in ([f'{t}-production',f'{t}-selected'] if release else [f'{t}-base',CANDIDATE[t]]):
            cg=parent/f'{binary}-{rep}';cg.mkdir(exist_ok=True)
            out=TMP/f'memory-{binary}';out.mkdir(exist_ok=True)
            def enter(): (cg/'cgroup.procs').write_text(str(__import__('os').getpid()))
            start=time.perf_counter()
            subprocess.run([str(TMP/binary),'--data',str(data),'--out',str(out)],check=True,preexec_fn=enter)
            row=dict(track=t,binary=binary,rep=rep,seconds=time.perf_counter()-start,memory_peak=int((cg/'memory.peak').read_text()),sha256=hashlib.sha256((out/OUT[t]).read_bytes()).hexdigest())
            rows.append(row);cg.rmdir();print(json.dumps(row),flush=True)
(EVIDENCE/('release-memory.json' if release else 'cgroup-memory.json')).write_text(json.dumps(rows,indent=2))
parent.rmdir()

"""Reproduce each Dockerfile's compiler flags for final native comparisons."""
import subprocess
from astra import ROOT,TMP,baseline_source,compare
for t in 'acd':
    flags=(ROOT/f'track-{t}/Dockerfile').read_text().split('RUN g++ ',1)[1].split(' main.cpp',1)[0].split()
    base=TMP/f'{t}-baseline-source.cpp';base.write_text(baseline_source(t))
    for name,source in [(f'{t}-production',base),(f'{t}-selected',ROOT/f'track-{t}/main.cpp')]:
        subprocess.run(['g++',*flags,str(source),'-o',str(TMP/name)],check=True)
    compare(t,f'{t}-selected',f'{t}-production')

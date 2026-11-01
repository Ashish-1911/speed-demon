"""Copy selected, independently tested candidates into the isolated worktree."""
import subprocess
from astra import ROOT,TMP
from astra_verify import CANDIDATE
for t,name in CANDIDATE.items():
    source=(TMP/f'{name}.cpp').read_bytes()
    formatted=subprocess.run([str(TMP/'tools/clang-format'),'--style={BasedOnStyle: LLVM, IndentWidth: 4, ColumnLimit: 110}','--assume-filename=main.cpp'],input=source,capture_output=True,check=True).stdout
    (ROOT/f'track-{t}/main.cpp').write_bytes(formatted)

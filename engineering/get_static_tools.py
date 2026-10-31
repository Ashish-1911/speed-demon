import urllib.request,hashlib,zipfile,tarfile,io,pathlib
root=pathlib.Path('/tmp/sd-campaign/tools'); root.mkdir(exist_ok=True)
items=[('https://github.com/boyter/scc/releases/download/v4.1.0/scc_Linux_x86_64.tar.gz','c7328436d3027f4357d3d7853f7dc3ac2bbcb4ca08f1adad91a27c593884079b','scc'),('https://files.pythonhosted.org/packages/11/d1/658d3da4dcbca98620956868c7cf6f4ba19fccecbdfc30cc055e19c9d4a7/clang_format-23.1.3-py2.py3-none-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl','58bf1804216501669832a39f2415e14284337438a062ff564729792adec664ac','clang-format')]
for url,sha,name in items:
 try:
  data=urllib.request.urlopen(url,timeout=30).read(); assert hashlib.sha256(data).hexdigest()==sha
  if name=='scc': binary=tarfile.open(fileobj=io.BytesIO(data)).extractfile('scc').read()
  else: binary=zipfile.ZipFile(io.BytesIO(data)).read('clang_format/data/bin/clang-format')
  p=root/name; p.write_bytes(binary); p.chmod(0o755); print(name,'verified')
 except Exception as e: print(name,type(e).__name__,str(e))

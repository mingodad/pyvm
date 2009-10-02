from pyc import compileFile

def compile (file, cfile=None, dfile=None, doraise=False, incompat=False, kw={}):
	if not incompat:
		compileFile (file, showmarks=0, dynlocals=0, rrot3=1, output=cfile, **kw)
	else:
		compileFile (file, showmarks=0, dynlocals=0, rrot3=1, marshal_builtin=True, arch='pyvm', renames=1, output=cfile, pyvm=True, **kw)

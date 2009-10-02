/*
 *  Essential builtin methods and module imports
 * 
 *  Copyright (c) 2006 Stelios Xanthakis
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) version 3 of the License.
 */

#define	INTARG(X) IntObj.fcheckedcast (argv [X].o)->i

REFPTR devnull;

/*
 * BuiltinCallables and FuncWrappers
 *
 * Normally, in OOP sense we should be using virtual functions to polymoph
 * for different builtins. On the other hand we'd have an entire virtual table
 * for each builtin. So we'll use plain good old C pointer to function callbacks.
 *
 * There is no support for named arguments for builtins and consequently,
 * we can't pass keyword arguments to them. (that should be done in python)
 */

//#define TRACE_BUILTIN

extern void NoDefaults (int) noreturn;

//*** noarg functions

void FuncWrapperObj_noarg.call (REFPTR ret, REFPTR argv[], int argc)
{
#ifdef	TRACE_BUILTIN
	pprint ("1.Calling:", name.o);
#endif
	if_unlikely (argc)
		NoDefaults (argc);
	no_arg_func FUNC = f;
	ret = FUNC ();
#ifdef	TRACE_BUILTIN
	pprint ("  Leaving:", name.o);
#endif
}

//*** fixed argcount functions

void FuncWrapperObj_fargc.call (REFPTR ret, REFPTR argv[], int argc)
{
#ifdef	TRACE_BUILTIN
	pprint ("2.Calling:", name.o);
#endif
	if_unlikely (argc != argcount)
		NoDefaults (argc);
	fixed_arg_func FUNC = f;
	ret = FUNC (argv + 1);
#ifdef	TRACE_BUILTIN
	pprint ("  Leaving.:", name.o);
#endif
}

// **** var argcount functions -- no kwargs

void FuncWrapperObj_vargc.call (REFPTR ret, REFPTR argv[], int argc)
{
#ifdef	TRACE_BUILTIN
	pprint ("3.Calling:", name.o);
#endif
	if_unlikely (argc < minarg || argc > maxarg)
		NoDefaults (argc);
	var_arg_func FUNC = f;
	ret = FUNC (argv + 1, argc);
#ifdef	TRACE_BUILTIN
	pprint ("  Leaving:", name.o);
#endif
}

// **** inf argcount functions -- no kwargs

void FuncWrapperObj_iargc.call (REFPTR ret, REFPTR argv[], int argc)
{
#ifdef	TRACE_BUILTIN
	pprint ("4.Calling:", name.o);
#endif
	if_unlikely (argc < minarg)
		NoDefaults (argc);
	var_arg_func FUNC = f;
	ret = FUNC (argv + 1, argc);
#ifdef	TRACE_BUILTIN
	pprint ("  Leaving:", name.o);
#endif
}


/////////////////////////////////////////////////////

/*
 * Here we also put some builtins that are interested in inlining
 * core pyvm functions
 */

/* ----* __builtins__.range() *---- */

modsection __object__ *range_builtin (REFPTR argv [], int args)
{
	int from, to, step = 1, n;
	if (args == 1) {
		from = 0;
		to = INTARG (0);
	} else {
		from = INTARG (0);
		to = INTARG (1);
		if (args == 3)
			if (!(step = INTARG (2)))
				RaiseValueError ("zero step in range()");
	}
	if ((n = (to - from) / step) < 0)
		RaiseValueError ("negate step in range()");
	ListObj *L = new ListObj __sizector (n);
	L->len = n;
	if (from > -INTTAB_MIN && from < INTTAB_MAX && to > -INTTAB_MIN && to < INTTAB_MAX)
		for (unsigned int i = 0; i < (unsigned int) n; i++)
			L->__inititem (i, &SmallInts [postfix (from + INTTAB_MIN, from += step)]);
	else
		for (unsigned int i = 0; i < (unsigned int) n; i++)
			L->__inititem (i, newIntObj (postfix (from, from += step)));
	return L;
}

/* ----* __builtins__.xrange() *---- */

static class xRangeObj : iteratorBase
{
	int i, from, step, to;
   public:
modsection	xRangeObj (int, int, int);
	__object__ *xnext ();
	void print ();
};

void xRangeObj.print ()
{
	print_out (STRL ("xRangeObj ("), from, _CHAR(' '), to, _CHAR(' '), step, _CHAR(' '),
		   i, _CHAR(')'));
}

xRangeObj.xRangeObj (int _from, int _to, int _step)
{
	iteratorBase.ctor (&None);
	i = from = _from;
	to = _to;
	step = _step;
}

__object__ *xRangeObj.xnext ()
{
	if_unlikely (step > 0 ? i >= to : i <= to) 
		RaiseStopIteration ();
	return newIntObj (postfix (i, i += step));
}

modsection __object__ *xrange_builtin (REFPTR argv [], int argc)
{
	if (argc == 1)
		return new xRangeObj (0, INTARG (0), 1);
	else if (argc == 2)
		return new xRangeObj (INTARG (0), INTARG (1), 1);
	else if (argc == 3)
		return new xRangeObj (INTARG (0), INTARG (1), INTARG (2));
	else	RaiseValueError ("xrange () takes 1..3 arguments");
}


/* ----* string.split() *---- */

__object__ *split (const char *str, int len, const char *sep, int maxsplit)
{
	ListObj *L = new ListObj __sizector (4);
	unsigned int i = 0, j = 0, ns;
	if (!sep) {
#define	ISWS(X) (X == ' ' || (X <= '\r' && X >= '\t'))
		for (i = 0; i < len; i++)
			if (!ISWS (str [i])) break;
		for (ns = 0; ns < maxsplit && i < len; ns++) {
			for (j = i + 1; j < len; j++)
				if_unlikely (ISWS (str [j])) break;
			L->append (new StringObj binctor (str + i, j - i));
			for (i = j + 1; i < len; i++)
				if (!ISWS (str [i])) break;
		}
		if (ns == maxsplit && i < len)
			L->append (new StringObj binctor (str + i, len - i));
	} else if (sep [1] == 0) {
		char sp = sep [0];
		if_likely (maxsplit == $INT_MAX)
			for (i = 0; i <= len; i = j + 1) {
				char *p = memchr (str + i, sp, len - i);
				j = p ? p - str : len;
				L->append (new StringObj binctor (str + i, j - i));
			}
		else
			for (ns = i = 0; ns < maxsplit && i <= len; i = j + 1, ns++) {
				for (j = i; j < len && str [j] != sp; j++)
					continue;
				L->append (new StringObj binctor (str + i, j - i));
			}
		if (j < len)
			L->append (new StringObj binctor (str + j + 1, len - (j + 1)));
	} else {
		int sl = strlen (sep);
		for (ns = i = 0; ns < maxsplit && i < len; i = j + sl, ns++) {
			for (j = i; j < len && memcmp (str + j, sep, sl); j++)
				continue;
			L->append (new StringObj binctor (str + i, j - i));
		}
		if (j < len)
			L->append (new StringObj binctor (str + j + sl, len - (j + sl)));
	}
	return L;
}

modsection __object__ *split_string (REFPTR argv[], int argc)
{
	StringObj *s = StringObj.fcheckedcast (argv [0].o);
	if (argc == 1)
		return split (s->str, s->len, 0, $INT_MAX);
	int maxsep = argc == 3 ? IntObj.fcheckedcast (argv [2].o)->i ?: $INT_MAX : $INT_MAX;
	char *sep = argv [1].o == &None ? 0 : StringObj.fcheckedcast (argv [1].o)->str;
	return split (s->str, s->len, sep, maxsep);
}

modsection static __object__ *join_string (REFPTR argv[])
{
	return StringObj.fcheckedcast (argv [0].o)->join (argv [1].o);
}

#ifdef HAVE_ISPLIT
static class isplitObj : iteratorBase
{
	REFPTR s1;
	char *str, *sep;
	int len, i;
   public:
	isplitObj (StringObj*, char*);
	__object__ *xnext ();
	void print ();
};

static __object__ *isplit_string (REFPTR argv[], int argc)
{
	StringObj *s = StringObj.fcheckedcast (argv [0].o);
	if (argc == 1)
		return new isplitObj (s, 0);
	StringObj *sep = StringObj.fcheckedcast (argv [1].o);
	if_unlikely (sep->len != 1)
		RaiseNotImplemented ("isplit separator must be of length 1");
	return new isplitObj (s, sep->str);
}

void isplitObj.print ()
{
	print_out (STRL ("isplit"));
}

isplitObj.isplitObj (StringObj *_s1, char *_sep)
{
	s1.ctor (_s1);
	str = _s1->str;
	len = _s1->len;
	sep = _sep;
	i = 0;
}

__object__ *isplitObj.xnext ()
{
	unsigned int j;
	__object__ *ret;
	if (!sep) {
#define	ISWS(X) (X == ' ' || (X <= '\r' && X >= '\t'))
		for (; i < len; i++)
			if (!ISWS (str [i])) break;
		if (i < len) {
			for (j = i + 1; j < len; j++)
				if_unlikely (ISWS (str [j])) break;
			ret = new StringObj binctor (str + i, j - i);
			i = j + 1;
			return ret;
		}
	} else {
		if (i <= len) {
			char *p = memchr (str + i, sep [0], len - i);
			j = p ? p - str : len;
			ret = new StringObj binctor (str + i, j - i);
			i = j + 1;
			return ret;
		}
	}
	RaiseStopIteration ();
}
#endif

//////////////////////////////////////////////////////

/* --* some StringObj, ListObj, DictObj attributes *-- */

extern __object__ *translate_string (REFPTR[], int);
extern __object__ *isspace_string (REFPTR[]);
extern __object__ *isalpha_string (REFPTR[]);
extern __object__ *isdigit_string (REFPTR[]);
extern __object__ *isvarname_string (REFPTR[]);
extern __object__ *endswith_string (REFPTR[], int);
extern __object__ *partition_string (REFPTR[]);
extern __object__ *break_string (REFPTR[]);
extern __object__ *rpartition_string (REFPTR[]);
extern __object__ *rfind_string (REFPTR[], int);
extern __object__ *rindex_string (REFPTR[], int);
extern __object__ *replace_string (REFPTR[], int);
extern __object__ *maketrans_string (REFPTR[]);
extern __object__ *strip_string (REFPTR[], int);
extern __object__ *lower_string (REFPTR[]);
extern __object__ *upper_string (REFPTR[]);
extern __object__ *count_string (REFPTR[], int);
extern __object__ *expandtabs_string (REFPTR[], int);

static __object__ *append_ListObj (REFPTR argv[])
{
	argv [0].as_list->append (argv [1].o);
	return &None;
}

static __object__ *insert_ListObj (REFPTR argv[])
{
	argv [0].as_list->insert (IntObj.checkedcast (argv [1].o)->i, argv [2].o);
	return argv [0].o;
}

static __object__ *pop_ListObj (REFPTR argv[], int argc)
{
	/** XXX: this is special.
	*** We should avoid incdec reference and put directly to retval
	**/
	return argv [0].as_list->pop (argc == 2 ? IntObj.fcheckedcast (argv [1].o)->i : -1);
}

static __object__ *pops_ListObj (REFPTR argv[])
{
	return argv [0].as_list->pops (IntObj.fcheckedcast (argv [1].o)->i);
}

static __object__ *extend_ListObj (REFPTR argv[])
{
	argv [0].as_list->extend (argv [1].o);
	return &None;
}

static __object__ *remove_ListObj (REFPTR argv[])
{
	argv [0].as_list->remove (argv [1].o);
	return &None;
}

static __object__ *sort_ListObj (REFPTR argv[], int argc)	/* cold */
{
	if_likely (argc == 1)
		argv [0].as_list->sort ();
	else
		argv [0].as_list->sort_cmp (argv [1].o);
	return argv [0].o;
}

static __object__ *reverse_ListObj (REFPTR argv[])
{
	argv [0].as_list->reverse ();
	return argv [0].o;
}

static __object__ *_fix__ListObj (REFPTR argv[])
{
	argv [0].as_list->fix ();
	return argv [0].o;
}

static __object__ *index_ListObj (REFPTR argv[], int argc)
{
	int from = 0, to = $INT_MAX;
	if (argc > 2) {
		from = IntObj.fcheckedcast (argv [2].o)->i;
		if (argc > 3)
			to = IntObj.fcheckedcast (argv [3].o)->i;
	}
	int idx = argv [0].as_list->index (argv [1].o, from, to);
	if_unlikely (idx == -1)
		RaiseValueError_li ();
	return newIntObj (idx);
}

static __object__ *count_ListObj (REFPTR argv[])
{
	int i = 0, s = 0, j;
	REFPTR *data = argv [0].as_list->data;
	int L = argv [0].as_list->len;
	while ((j = argv [1]->__find_in (data + i, L - i)) != -1)
		++s, i += j + 1;
	return newIntObj (s);
}

static __object__ *get_DictObj (REFPTR argv[], int argc)
{
	return argv [0].as_dict->xgetitem_noraise (argv [1].o) ?: argc == 3 ? argv [2].o : &None;
}

static __object__ *pop_DictObj (REFPTR argv[], int argc)
{
	__object__ *v = argv [0].as_dict->pop (argv [1].o);
	if (v) return v;
	if (argc == 3)
		return argv [2].o;
	RaiseKeyError (argv [1].o);
}

static __object__ *key_DictObj (REFPTR argv[])
{
	__object__ *v = argv [0].as_dict->key (argv [1].o);
	if (v) return v;
	RaiseKeyError (argv [1].o);
}

static __object__ *count_inc_DictObj (REFPTR argv[])
{
	/* xx: avoid double hash */
	DictObj *D = argv [0].as_dict;
	if (!D->contains (argv [1].o))
		D->xsetitem (argv [1].o, IntObj_1);
	else {
		int i = IntObj.fcheckedcast (D->xgetitem (argv [1].o))->i;
		D->xsetitem (argv [1].o, newIntObj (i + 1));
	}
	return &None;
}

static __object__ *count_dec_DictObj (REFPTR argv[])
{
	DictObj *D = argv [0].as_dict;
	int i = IntObj.fcheckedcast (D->xgetitem (argv [1].o))->i;
	if (i == 1) D->xdelitem (argv [1].o);
	else D->xsetitem (argv [1].o, newIntObj (i - 1));
	return &None;
}

static __object__ *gather_DictObj (REFPTR argv[])
{
	DictObj *D = argv [0].as_dict;
	__object__ *key = argv [1].o;
	if (!D->contains (key))
		D->xsetitem (key, (__object__*) (new ListObj (argv [2].o)));
	else ListObj.checkedcast (D->xgetitem (key))->append (argv [2].o);
	return &None;
}

static __object__ *clear_DictObj (REFPTR argv[])
{
	DictObj.cast (argv [0].o)->D.clear ();
	return &None;
}

static __object__ *__setitem___DictObj (REFPTR argv[])
{
	DictObj.checkedcast (argv [0].o)->xsetitem (argv [1].o, argv [2].o);
	return &None;
}

static __object__ *__iter___DictObj (REFPTR argv[])
{
	return DictObj.checkedcast (argv [0].o)->iter ();
}

static __object__ *copy_DictObj (REFPTR argv[])
{
	return DictObj.type_call (argv, 1);
}

static __object__ *keys_DictObj (REFPTR argv[])
{
	return argv [0].as_dict->keys ();
}

static __object__ *items_DictObj (REFPTR argv[])
{
	return argv [0].as_dict->items ();
}

static __object__ *values_DictObj (REFPTR argv[])
{
	return argv [0].as_dict->values ();
}

static __object__ *iteritems_DictObj (REFPTR argv[])
{
	return argv [0].as_dict->iteritems ();
}

static __object__ *has_key_DictObj (REFPTR argv[])
{
	return argv [0].as_dict->contains (argv [1].o) ? &TrueObj : &FalseObj;
}

static __object__ *setdefault_DictObj (REFPTR argv [], int argc)
{
	// default argument is other argument!
	return argv [0].as_dict->setdefault (argv [1].o, argc == 3 ? argv [2].o : argv [1].o);
}

static __object__ *itervalues_DictObj (REFPTR argv[])
{
	return argv [0].as_dict->itervalues ();
}

static __object__ *update_DictObj (REFPTR argv[])
{
	argv [0].as_dict->update (DictObj.checkedcast (argv [1].o));
	return argv [0].o;
}

modsection static __object__ *startswith_StringObj (REFPTR argv[], int argc)
{
	StringObj *s = argv [0].as_string, *prefix = StringObj.fcheckedcast (argv [1].o);
	int start = argc > 2 ? IntObj.fcheckedcast (argv [2].o)->i : 0;
	return start + prefix->len <= s->len && !memcmp (s->str + start, prefix->str, prefix->len)
		? &TrueObj : &FalseObj;
}

modsection static __object__ *rstrip_StringObj (REFPTR argv[], int argc)
{
	StringObj *s = argv [0].as_string;
	char *str = s->str;
	int i;

	if (argc == 1)
		for (i = s->len - 1; i >= 0 && ISWS (str [i]); i--);
	else {
		StringObj *chars = StringObj.fcheckedcast (argv [1].o);
		const void *a = chars->str;
		const int l = chars->len;
		for (i = s->len - 1; i >= 0 && memchr (a, str [i], l); i--)
			continue;
	}
	if_unlikely (i == s->len - 1)
		return s;
	return new StringObj binctor (str, i + 1);
}

modsection static __object__ *lstrip_StringObj (REFPTR argv[], int argc)
{
	StringObj *s = argv [0].as_string;
	char *str = s->str;
	int i, j = s->len;

	if (argc == 1)
		for (i = 0; i < j && ISWS (str [i]); i++);
	else {
		StringObj *chars = StringObj.fcheckedcast (argv [1].o);
		const void *a = chars->str;
		const int l = chars->len;
		for (i = 0; i < j && memchr (a, str [i], l); i++)
			continue;
	}
	return new StringObj (str + i, j - i);
}

/* fastsearch from Python 2.5 (effbot, "need for speed" sprint) */
/* This function is something like strstr() only, it works with
 * string sizes and not NULL termination. Also it is significantly
 * faster than strstr, especially with bigger needlesizes.  */

const char *memstr (register const char *haystack, const char *needle, int haysize, int nsize)
{
	int w = haysize - nsize;
	int mlast = nsize - 1;
	int skip = mlast - 1;
	int i, j;
#if 0
	// definitelly for 64bits
	unsigned long long mask;
#else
	unsigned long mask;
#endif
#define BITS ((sizeof mask * 8) - 1)

	if_unlikely (!nsize)
		return haystack;

	for (mask = i = 0; i < mlast; i++) {
		mask |= 1 << (needle [i] & BITS);
		if (needle [i] == needle [mlast])
			skip = mlast - i - 1;
	}

	mask |= 1 << (needle [mlast] & BITS);

	for (i = 0; i <= w; i++)
		if (haystack [i + nsize - 1] == needle [nsize - 1]) {
			for (j = 0; j < mlast; j++)
				if (haystack [i + j] != needle [j])
					break;
			if (j == mlast)
				return haystack + i;
			if (!(mask & (1 << (haystack [i+nsize] & BITS))))
				i += nsize;
			else
				i += skip;
		} else if (!(mask & (1 << (haystack [i+nsize] & BITS))))
			i += nsize;

	return 0;
}

modsection static int _find_StringObj (REFPTR argv[], int argc)
{
	StringObj *s = argv [0].as_string, *sub = StringObj.fcheckedcast (argv [1].o);
	int start, end = s->len;

	if (argc > 2) {
		start = IntObj.fcheckedcast (argv [2].o)->i;
		if (argc > 3) {
			end = IntObj.fcheckedcast (argv [3].o)->i;
			if (end < 0) {
				end += s->len;
				if (end < 0) end = 0;
			} else if (end > s->len) end = s->len;
		}
	} else start = 0;

	int idx = -1;
	if_likely (start < end)
		if (sub->len == 1) {
			char *p = memchr (s->str + start, sub->str [0], end - start);
			if (p) idx = p - s->str;
		} else {
			const char *p = memstr (s->str + start, sub->str, s->len - start, sub->len);
			if (p) idx = p - s->str;
		}
	else;

	return idx;
}

modsection static __object__ *find_string (REFPTR argv[], int argc)
{
	return newIntObj (_find_StringObj (argv, argc));
}

modsection static __object__ *index_string (REFPTR argv[], int argc)
{
	int i = _find_StringObj (argv, argc);
	if (i != -1)
		return newIntObj (i);
	RaiseValueErrorSubstring ();
}

///////////////////////////////////////////////////////////

/* --* *-- */

char **Program_argv, *Program_name;
int Program_argc;
REFPTR modpath;

__object__ *import_pyc (char *fnm, __object__ *mnam)
{
	__object__ *pfnm = new StringObj (fnm);	/* is already interned? */

	ModuleObj *module = new ModuleObj (pfnm, mnam);
	sys_modules.as_dict->xsetitem (mnam, (__object__*) module);
	PyCodeObj *c = PyCodeObj.checkedcast (load_compiled (fnm, (__object__*)module));

	PyFuncObj *F = new PyFuncObj ((__object__*) c, module->__dict__.as_dict, module->__dict__.as_dict);

	/* This is special.  We return a dictionary and boot_pyvm
	 * must detect that CtxSw.vm is set, in other words, it must
	 * run some bytecode to fill it
	 */
	F->call (devnull, &devnull, 0);

	module->setattr (Interns.__name__, mnam );
	module->setattr (Interns.__builtins__, __builtins__.as_ns->__dict__.o);
	module->setattr (Interns.__file__, pfnm);

	return module;
}

extern bool fix_modpath ();
extern bool Bootstrapping;

extern void import_hardcoded (REFPTR);
#define AUTO_COMPILE true
extern int import_find_file (const char*, char[], bool=AUTO_COMPILE);
extern int import_submodule (const char*, char[], const char*, bool=true);
extern char *compileFile (const char*);

void init_modules ()
{
	sys_modules = new DictObj;
	sys_modules.as_dict->GC_ROOT ();
	import_hardcoded (sys_modules);
}

static __object__ *_import_module (__object__ *mod, int depth=0)
{
	/* protect against infinite recursion */
	if_unlikely (depth > 20)
		RaiseImportError (mod);

	if_likely (((long) sys_modules.as_dict->contains (mod))) {
		__object__ *M = sys_modules.as_dict->xgetitem (mod);
		/* somebody added something that isn't a module to sys.modules?
		   gets punished now.  */
		return ModuleObj.isinstance (M) ? M : RaiseImportError (mod);
	}

	char path [1024], *pp, *name = StringObj.cast (mod)->str;

	/*
	 * dotted import. When we see "Foo.Bar", "Foo" must be already imported!
	 * Then we look for "Bar" in "Foo"'s directory.
	 * If Foo hasn't been imported we must execute "import Foo" internally
	 */
	REFPTR subm;
	if ((pp = strchr (name, '.'))) {
	again:
		subm = new StringObj binctor (name, pp - name);
		if (sys_modules.as_dict->contains (subm.o)) {
			if (strchr (pp+1, '.')) {
				pp = strchr (pp+1, '.');
				goto again;
			}
			__object__ *M = sys_modules.as_dict->xgetitem (subm.o);
			switch (import_submodule (name, path, ModuleObj.cast (M)->pyc_path.as_string->str)) {
				case 1:
					return import_pyc (path, mod);
				case 2:
					if ((pp = compileFile (path)))
						return import_pyc (pp, mod);
				default:
					RaiseImportError (mod);
			}
		} else {
			CtxSw.vm = 0;
			REFPTR BASE = new StringObj binctor (name, pp-name);
			REFPTR M = import_module (BASE.o, 0);
			if (CtxSw.vm) {
				preempt_pyvm (CtxSw.vm);
				CtxSw.vm = 0;
			}
			return _import_module (mod, depth+1);	/* XXX: recursion danger? */
		}
	}


	/*
	 * If the compiled file's timestamp is the same as the source file's
	 * timestamp we are OK (case 1).
	 * If not we'll have to compile the source with pyc compiler.
	 * If the pyc compiler is not available we'll just skip the timestamp
	 * checks and try to load the compiled files anyway.
	 *  This trick makes the initial bootstrap possible.
	 */
	switch (import_find_file (name, path)) {
		case 3: {
			REFPTR SUBP = new StringObj (path);
			return _import_module (SUBP.o, depth+1);
		}
		case 2:
			if (mod == Interns.pyc) {
				fprintf (stderr, "Bootstrap error. Please bootstrap properly\n");
				exit (1);
			}

			if ((pp = compileFile (path)))
				return import_pyc (pp, mod);

			if (import_find_file (name, path, false) != 1)
		default:
				RaiseImportError (mod);
		case 1:
			return import_pyc (path, mod);
	}
}

/* why isn't 'fallthrough' commented properly as it should ??? */

REFPTR sys_modules;

__object__ *import_module (__object__ *mod, bool fromform)
{
//pprint ("import:", mod);
//pprint ("import:", fromform);
	__object__ *mod_ns = _import_module (mod);
	if (!fromform && strchr (StringObj.cast (mod)->str, '.')) {
		/*
		 * In "from Foo.Bar .." we return Bar's namespace and
		 * this ain't needed.
		 *
		 * When we say "import Foo.Bar", we get the
		 * namespace 'Foo', in other words the globals of
		 * the __init__.py at the directory Foo, but we
		 * execute the module initialization of Foo/Bar.py
		 *
		 * If 'Foo' has been imported already we are OK.
		 * We just return Foo's namespace, Bar's vm_context
		 * and add an attribute 'Bar' with value Bar's namespace
		 * to Foo's namespace.
		 *
		 * If not we are in more trouble.  We have to execute
		 * both Foo/__init__.py and Foo/Bar.py and do the thing...
		 *
		 */
		char tmp [500];
		strcpy (tmp, StringObj.cast (mod)->str);
		*strrchr (tmp, '.') = 0;
		REFPTR par = new StringObj (tmp);
		REFPTR chld = new StringObj (tmp + strlen (tmp) + 1);
		if (sys_modules.as_dict->contains (par.o)) {
			__object__ *par_ns = sys_modules.as_dict->xgetitem (par.o);
			par_ns->setattr (chld.o, mod_ns);
			mod_ns = par_ns;
		} else RaiseNotImplemented ("Cannot import Foo.Bar if Foo hasn't been imported before");
	}
	return mod_ns;
}

void import_star (DictObj *g, __object__ *n)
{
	DictObj *D = ModuleObj.checkedcast (n)->__dict__.as_dict;
	__object__ *o;

	if (D->contains (Interns.__all__)) {
		o = D->xgetitem (Interns.__all__);
		if (ListObj.typecheck (o)) {
			ListObj *L = ListObj.cast (o);
			for (dictEntry *d = 0; (d = D->__iterfast (d));)
			if (StringObj.isinstance (d->key.o) && L->contains (d->key.o))
				g->xsetitem (d->key.o, d->val.o);
		} else goto no___all__;
	} else no___all__:
		for (dictEntry *d = 0; (d = D->__iterfast (d));) 
			if (StringObj.isinstance (d->key.o) && d->key.as_string->str [0] != '_')
				g->xsetitem (d->key.o, d->val.o);
}


//////////////////////////// initialize //////////////////////////////
extern __object__ *hexlify (REFPTR[]);

static const method_attribute string_methods [] = {
	{"startswith",	"string.startswith",	SETARGC (2, 4), startswith_StringObj},
	{"sw",		"string.startswith",	SETARGC (2, 4), startswith_StringObj},
	{"rstrip",	"string.rstrip",	SETARGC (1, 2), rstrip_StringObj},
	{"lstrip",	"string.lstrip",	SETARGC (1, 2), lstrip_StringObj},
	{"find",	"string.find",		SETARGC (2, 4), find_string},
	{"split",	"string.split",		SETARGC (1, 3), split_string},
#ifdef HAVE_ISPLIT
	{"isplit",	"string.isplit",	SETARGC (1, 2), isplit_string},
#endif
	{"join",	"string.join",		SETARGC (2, 2), join_string},
	{"replace",	"string.replace",	SETARGC (3, 4), replace_string},
	{"strip",	"string.strip",		SETARGC (1, 2), strip_string},
	{"rfind",	"string.rfind",		SETARGC (2, 4), rfind_string},
	{"endswith",	"string.endswith",	SETARGC (2, 4), endswith_string},
	{"partition",	"string.partition",	SETARGC (2, 2), partition_string},
	{"rpartition",	"string.rpartition",	SETARGC (2, 2), rpartition_string},
	{"ew",		"string.endswith",	SETARGC (2, 4), endswith_string},
	{"lower",	"string.lower",		SETARGC (1, 1), lower_string},
	{"upper",	"string.upper",		SETARGC (1, 1), upper_string},
	{"count",	"string.count",		SETARGC (2, 4), count_string},
	{"Break",	"string.Break",		SETARGC (2, 2), break_string},
	{"translate",	"string.translate",	SETARGC (2, 3), translate_string},
	{"isspace",	"string.isspace",	SETARGC (1, 1), isspace_string},
	{"isalpha",	"string.isalpha",	SETARGC (1, 1), isalpha_string},
	{"isdigit",	"string.isdigit",	SETARGC (1, 1), isdigit_string},
	{"isvarname",	"string.isvarname",	SETARGC (1, 1), isvarname_string},
	{"rindex",	"string.rindex",	SETARGC (2, 4), rindex_string},
	{"index",	"string.rindex",	SETARGC (2, 4), index_string},
	{"expandtabs",	"string.expandtabs",	SETARGC (1, 2), expandtabs_string},
	{"hexlify",	"string.hexlify",	SETARGC (1, 1), hexlify},
	MENDITEM
};

static const method_attribute list_methods [] = {
	{"append",	"list.append",	SETARGC (2, 2), append_ListObj},
	{"extend",	"list.extend",	SETARGC (2, 2), extend_ListObj},
	{"remove",	"list.remove",	SETARGC (2, 2), remove_ListObj},
	{"pop",		"list.pop",	SETARGC (1, 2), pop_ListObj},
	{"index",	"list.index",	SETARGC (2, 4), index_ListObj},
	{"count",	"list.count",	SETARGC (2, 2), count_ListObj},
	{"insert",	"list.insert",	SETARGC (3, 3), insert_ListObj},
	{"pops",	"list.pops",	SETARGC (2, 2), pops_ListObj},
	{"sort",	"list.sort",	SETARGC (1, 2), sort_ListObj},
	{"reverse",	"list.reverse",	SETARGC (1, 1), reverse_ListObj},
	{"_fix_",	"list._fix_",	SETARGC (1, 1), _fix__ListObj},
	MENDITEM
};

static const method_attribute tuple_methods [] = {
	{"count",	"list.count", SETARGC (2, 2), count_ListObj},
	{"index",	"list.index", SETARGC (2, 4), index_ListObj},
	MENDITEM
};

static const method_attribute dict_methods [] = {
	{"get",		"dict.get",		SETARGC (2, 3), get_DictObj},
	{"setdefault",	"dict.setdefault",	SETARGC (1, 3), setdefault_DictObj},
	{"has_key",	"dict.has_key",		SETARGC (2, 2), has_key_DictObj},
	{"keys",	"dict.keys",		SETARGC (1, 1), keys_DictObj},
	{"items",	"dict.items",		SETARGC (1, 1), items_DictObj},
	{"values",	"dict.values",		SETARGC (1, 1), values_DictObj},
	{"iteritems",	"dict.iteritems",	SETARGC (1, 1), iteritems_DictObj},
	{"itervalues",	"dict.itervalues",	SETARGC (1, 1), itervalues_DictObj},
	{"update",	"dict.update",		SETARGC (2, 2), update_DictObj},
	{"clear",	"dict.clear",		SETARGC (1, 1), clear_DictObj},
	{"__setitem__",	"dict.__setitem__",	SETARGC (3, 3), __setitem___DictObj},
	{"__iter__",	"dict.__iter__",	SETARGC (1, 1), __iter___DictObj},
	{"copy",	"dict.copy",		SETARGC (1, 1), copy_DictObj},
	{"pop",		"dict.pop",		SETARGC (2, 3), pop_DictObj},
	{"key",		"dict.key",		SETARGC (2, 2), key_DictObj},
	{"count_inc",	"dict.count_inc",	SETARGC (2, 2), count_inc_DictObj},
	{"count_dec",	"dict.count_dec",	SETARGC (2, 2), count_dec_DictObj},
	{"gather",	"dict.gather",		SETARGC (3, 3), gather_DictObj},
	MENDITEM
};

DictObj StringMethods __noinit (), ListMethods __noinit (), DictMethods __noinit (),
	TupleMethods __noinit (), SetMethods __noinit ();

static	slowcold class InitAttribs : InitObj {
	int priority = INIT_ATTR;
	void todo ()
	{
		StringMethods.ctor (string_methods);
		ListMethods.ctor (list_methods);
		TupleMethods.ctor (tuple_methods);
		DictMethods.ctor (dict_methods);
		SetMethods.ctor (set_methods);	/* in set.h.c+ */
	}
};

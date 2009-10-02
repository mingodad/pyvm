/*
 *  Main interpreter loop
 * 
 *  Copyright (c) 2006, 2007, 2008 Stelios Xanthakis
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) version 3 of the License.
 */

#include "py_opcodes.h"
#include "config.h"

/*
 * control the debugging messages, also see VMPARAMS.h
 */

//#define PRETTY_TRACE
//#define SIMPLE_TRACE
//	#define STACKPRINT
#ifdef TRACEVM
#define STACKPRINT
#ifndef SIMPLE_TRACE
#define SIMPLE_TRACE
#endif
#endif

// traceback levels. the right way to get tracebacks is to turn the
// vm_context into a real __object__. However doing this just to be
// able to get tracebacks may not worth it. Need tests.

// level 0: no tracebacks. the object is always None
// level 1: informative fast. Can report the line/file of where the exception
//	    happened but not the callers.
// level 2: full inaccurate. For each exception generate a full traceback.
//	    However, if the Program Counter is at the same location as the
//	    last traceback, use the cached traceback. It's possible that
//	    the second call will have a different caller and thus the bt
//	    will be wrong. (however, in the case of ERROR, we are mostly ok)
// level 3: full slow. For each exception we generate a full traceback string.

/* defined in dynlib.h */
//#define TRACEBACK_LEVEL 2

/* protos */

extern void print_whereami (vm_context *);
extern __object__ *traceback_str (vm_context*, int);
extern void unpack_other (__object__*, REFPTR[], int);
extern __object__ *__exec___builtin (REFPTR[], int);
extern int DebugVM;
extern void segv (int);
extern void sigpipe (int);
extern void IMPORT_GUARDED (__object__*);
extern void IMPORT_GUARDED_FROM (__object__*, __object__*);
extern void py_sched_yield ();
extern void build_something (int, exec_stack);
extern void restartTimer ();

/*
 * the __builtins__ dictionary is common and global. Getting this from the
 * current module is allowed from the applications. But internally the bytecode
 * uses this global
 */
REFPTR __builtins__;

/*
 * The graveyard list for __del__ zombies
 */
static REFPTR Graveyard;

/* allocator for vm contexts */

NEW_ALLOCATOR (vm_context)

/*
 * stack. These functions are as fast as macros (tested) 
 */

static inline exec_stack.exec_stack ()
{
}

static inline exec_stack.exec_stack (REFPTR *s, int ss)
{
	maxTOS = ss; STACKTOP = STACK = s;
	for (unsigned int i = 0; i < ss; i++)
		STACK [i].ctor ();
}

static inline void exec_stack.copy_from (exec_stack S)
{
#ifdef	STACKPRINT
	*this = S;
#else
	STACKTOP = S.STACKTOP;
#endif
}

static inline void exec_stack.push (__object__*x)
{
	*STACKTOP++ = x;
}

static inline void exec_stack.push_perm (__object__*x)
{
	STACKTOP++->__copyobj (x);
}

static inline void exec_stack.pushnone ()
{
	STACKTOP++->__copyobj (&None);
}

static inline void exec_stack.pre_push ()
{
	STACKTOP++;
}

static inline void exec_stack.settop (__object__*x)
{
	STACKTOP [-1] = x;
}

static inline void exec_stack.overtop (__object__ *x)
{
	STACKTOP [0] = x;
}

static inline void exec_stack.settop_overint (__object__ *x)
{
	STACKTOP [-1].ctordtorint (x);
}

static inline void exec_stack.settop_perm (__object__ *x)
{
	STACKTOP [-1].__copyobj (x);
}

static inline REFPTR &exec_stack.xpop ()
{
	return --STACKTOP;
}

static inline REFPTR &exec_stack.xpop2 ()
{
	STACKTOP -= 2;
	return STACKTOP;
}

static inline void exec_stack.push_free (__object__ *x)
{
	*STACKTOP++ += x;
}

static inline void exec_stack.dup_top ()
{
	STACKTOP [0] += STACKTOP [-1].o;
	++STACKTOP;
}

static inline void exec_stack.dup_top2 ()
{
	STACKTOP [0] += STACKTOP [-2].o;
	STACKTOP [1] += STACKTOP [-1].o;
	STACKTOP += 2;
}

static inline void exec_stack.dup_top3 ()
{
	STACKTOP [0] = STACKTOP [-3].o;
	STACKTOP [1] = STACKTOP [-2].o;
	STACKTOP [2] = STACKTOP [-1].o;
	STACKTOP += 3;
}

static inline void exec_stack.rot_two ()
{
	STACKTOP [-2].__swap (STACKTOP [-1]);
}

static inline void exec_stack.rot_three ()
{
	/* A(top) B C  --->  B(top) C A */
	register __object__ *tmp = STACKTOP [-1].o;
	STACKTOP [-1].o = STACKTOP [-2].o;
	STACKTOP [-2].o = STACKTOP [-3].o;
	STACKTOP [-3].o = tmp;
}

static inline void exec_stack.reverse_rot_three ()
{
	/* A(top) B C  --->  C(top) A B */
	register __object__ *tmp = STACKTOP [-3].o;
	STACKTOP [-3].o = STACKTOP [-2].o;
	STACKTOP [-2].o = STACKTOP [-1].o;
	STACKTOP [-1].o = tmp;
}

static inline void exec_stack.rot_swap1 ()
{
	/* A(top) B C -> C(top) B A */
	STACKTOP [-3].__swap (STACKTOP [-1]);
}

static inline void exec_stack.pop_top ()
{
	--STACKTOP;
}

static inline void exec_stack.pop_top (int n)
{
	STACKTOP -= n;
}

static inline REFPTR &exec_stack.top ()
{
	return STACKTOP [-1];
}

static inline REFPTR &exec_stack.second ()
{
	return STACKTOP [-2];
}

static inline REFPTR &exec_stack.third ()
{
	return STACKTOP [-3];
}

static inline REFPTR &exec_stack.nth (int n)
{
	return STACKTOP [- n - 1];
}

static inline REFPTR *exec_stack.n_addr (int n)
{
	return STACKTOP -= n;
}

/*static inline*/ exec_stack.~exec_stack ()
{
	for (int i = maxTOS - 1; i >= 0; i--)
		if_unlikely (STACK [i].o != &None)
			STACK [i].dtor ();
	seg_free (STACK);
}

static inline void exec_stack.clean ()
{
	for (int i = maxTOS - 1; i >= 0; i--)
		if_unlikely (STACK [i].o != &None)
			STACK [i].__copyobj (&None);
	STACKTOP = STACK;
}

#ifdef STACKPRINT
static void exec_stack.print_stack ()
{
	REFPTR *v;
	pprint (COLS"---- stackdump ", (void*)STACKTOP, "----"COLE);
	for (v = STACK; v < STACKTOP; v++)
		pprint ("STACK[", v-STACK, "]=", v->o);
}

static void exec_stack.print_stack_all ()
{
	REFPTR *v = STACK;
	pprint (COLS"---- stackdump", (void*)STACKTOP, "----", maxTOS, COLE);
	for (int i = 0; i < maxTOS; i++)
		pprint ("STACK[", i, "]=", v[i].o, "(", v[i]->refcnt, ")", (&v [i]==STACKTOP?"*":" "));
}
#endif

/*
 * profiler support
 */
bool profile_on = false;

#ifdef PPROFILE
#define DEBUG_PROF 0
static void profile_enter (vm_context *caller, vm_context *callee)
{
	tick_t now = get_cpu_ticks ();
	/* prepare callee */
	callee->cpu = callee->cpux = 0;
	callee->cpust = now;
	if (DEBUG_PROF && profile_on) fprintf (stderr, "ENTER cpu=%llu (%s)\n", callee->cpu>>12,
			callee->get_code ()->name.as_string->str);
	/* accumulate/reset caller */
	if (caller) {
		caller->cpu += now - caller->cpust;
		caller->cpux += now - caller->cpust;
		caller->cpust = 0;
	}
}
static void profile_leave (vm_context *callee, vm_context *caller=0)
{
	if (!caller)
		caller = callee->caller;
	tick_t now = get_cpu_ticks ();
	/* time spent in lifetime of this vm context */
	tick_t spent = now - callee->cpust;
	callee->cpux += spent;
	callee->cpu += spent;
	if (DEBUG_PROF && profile_on) fprintf (stderr, "LEAVE %llu (%s)\n", callee->cpu>>12,
		 callee->get_code ()->name.as_string->str);
	/* accumulate to function statistics */
	if (callee->FUNC.o->sigbit & 131072 && profile_on) {
		/* we use the `cloned` member to figure if a function is called recursively.
		 * this is not entirely correct as a function may be used in two different
		 * threads: `cloned` means that there are two simultaneous uses of a function.
		 * It seems that it's better to do this and place the start/stop calls in
		 * good spots. The right thing is to walk the call chain and see if the
		 * function is really recursively called upper in the chain.
		 */
		if (!callee->cloned)
			callee->FUNC.as_func->cputime += callee->cpu;
		callee->FUNC.as_func->cputimex += callee->cpux;
	}
	/* accumulate to caller */
	caller->cpu += callee->cpu;
	/* zero use for this call site */
	callee->cpux = callee->cpu = 0;
	/* rest caller's profile counter */
	caller->cpust = now;
}
static void profile_stop (vm_context *vm)
{
	tick_t now = get_cpu_ticks ();
	vm->cpux += now - vm->cpust;
	vm->cpu += now - vm->cpust;
}
static void profile_start (vm_context *vm)
{
	vm->cpust = get_cpu_ticks ();
}
#define PROFILE_LEAVE(...) profile_leave (__VA_ARGS__);
#define PROFILE_ENTER(...) profile_enter (__VA_ARGS__);
#define PROFILE_STOP_TIMING profile_stop(pvm);
#define PROFILE_START_TIMING  profile_start(pvm);
#else
#define PROFILE_LEAVE(...) {}
#define PROFILE_ENTER(...) {}
#define PROFILE_STOP_TIMING
#define PROFILE_START_TIMING
#endif

/*
 *
 * PyFuncObj, parsing arguments, MakeFunction/MakeClosure, PyGeneratorFuncObj.call
 *
 */

template parse_args_function(CLASS, HAS_KW, HAS_VA, HAS_DF) {
	// the values HAS_KW, HAS_VA, HAS_DF can be compile-time
	// constants which means that their 'if' block will be
	// totally eliminated by the compiler. Otoh, we can
	// define any of those to be a variable and have the
	// generic behavior for some cases.
void     CLASS.parse_args (REFPTR fl[], const REFPTR argvv[], int argc, int kargc)
{
	/* argvv[] may be the stack or it may be unrefcounted temporary storage
	 * from exp_call or from callBoundMethod, or both.
	 *  So **it doesn't** make sense to dtor() argvv[] elements
	 *  and it's **not** certain that if you put something in argvv[]
	 *  it will be dtored!  Not to mention that it's const....
	 */

	unsigned int i, j = argcount, k, l;
	DictObj *kwd = kwd;

	if (HAS_KW)
		inititem_fastlocal (fl, j + (HAS_VA?1:0), (__object__*) (kwd = new DictObj));

	if_unlikely (!HAS_VA && argc > j)
		RaiseTooManyArgs ();

	// fill positional.
	k = constant (HAS_VA) ? HAS_VA ? min (argc, j) : argc : min (argc, j);
	for (i = 0; i < k; i++)
		inititem_fastlocal (fl, i, argvv [i].o);

	// too many?
	if (HAS_VA) {
		register Tuplen *t;
		if (argc > j) {
			t = new Tuplen __sizector (k = argc - j);
			for (l = 0; l < k; l++)
				t->__xinititem__ (l, argvv [i++].o);
		} else t = NILTuple;
		inititem_fastlocal (fl, j, (__object__*) t);
	}

	// now 'i' points to the first unfilled slot
	if_likely (!kargc) {
		// fill remaining with defaults
		if (HAS_DF) {
			if_unlikely (i < ndflt)
				RaiseTooFewArgs ();
			for (; i < j; i++)
				inititem_fastlocal (fl, i, ((PyFuncObj_dflt*)this)
						->default_args.as_tuplen->__xgetitem__ (i));
		}
		else if_unlikely (i < j)
			RaiseTooFewArgs ();
	} else {
		// do keyword args
		/* We don't check for fewer arguments! If less are passes they will
		   be unbound local objects and may give an error when used (although,
		   comparisons against UnboundLocal work!)
		*/
		int offs = i;
		if (HAS_KW) for (i = 0; i < kargc; i += 2) {
			int pos = codeobj.as_code->lookup_argname (offs, argvv [argc + i].o);
			if (pos == -1) {
				kwd->xsetitem (argvv [argc + i].o, argvv [argc + i + 1].o);
				continue;
			}
			if_unlikely (getitem_fastlocal (fl, pos) != &UnboundLocal)
				RaiseValueError ("kwarg already has value by positional");
			inititem_fastlocal (fl, pos, argvv [argc + i + 1].o);
		} else for (i = 0; i < kargc; i += 2) {
			int pos = codeobj.as_code->lookup_argname (offs, argvv [argc + i].o);
			if_unlikely (pos == -1) 
				RaiseTypeError ("unexpected keyword argument");
			if_unlikely (getitem_fastlocal (fl, pos) != &UnboundLocal)
				RaiseValueError ("kwarg already has value by positional");
			inititem_fastlocal (fl, pos, argvv [argc + i + 1].o);
		}

		// fill remaining with defaults
		if (HAS_DF) for (i = max (argc, ndflt); i < j; i++)
			if (getitem_fastlocal (fl, i) == &UnboundLocal)
				inititem_fastlocal (fl, i, ((PyFuncObj_dflt*)this)->
						default_args.as_tuplen->__xgetitem__ (i));
	}
}
}

static class PyFuncObj_VARS : PyFuncObj {
	void parse_args (REFPTR[], const REFPTR[], int, int);
};

static class PyFuncObj_dflt : PyFuncObj
{
	REFPTR default_args;
	void parse_args (REFPTR[], const REFPTR[], int, int);
trv	void traverse ();
cln	void __clean ();
   public:
	PyFuncObj_dflt (__object__*, DictObj*, REFPTR*, int) noinline;
};

static class PyFuncObj_dflt_VARS : PyFuncObj_dflt {
	void parse_args (REFPTR[], const REFPTR[], int, int);
};

parse_args_function (PyFuncObj, 0, 0, 0)
parse_args_function (PyFuncObj_VARS, has_kw, has_va, 0)
parse_args_function (PyFuncObj_dflt, 0, 0, 1)
parse_args_function (PyFuncObj_dflt_VARS, has_kw, has_va, 1)

/*
 * PyFuncObj.init.  Called by MAKE_FUNCTION/MAKE_CLOSURE
 */

slow static void PyFuncObj.init (__object__ *x, DictObj *g, DictObj *l = 0) noinline coldfunc;

slow static void PyFuncObj.init (__object__ *x, DictObj *g, DictObj *l)
{
	REFPTR *fastlocals, *mstack;
	block_data *LOOPS;

	reentring = 0;
	PyCodeObj *co = PyCodeObj.cast (x);
	if_unlikely (co->nloops == -1)
		co->prepare_bytecode ();
	mstack = (REFPTR*) seg_alloc (co->nframe);
	has_va = co->flags & CO_VARARGS;
	has_kw = co->flags & CO_VARKEYWORDS;
	codeobj.ctor (x);
	GLOBALS.ctor (g);
	closures.ctor ();
#ifdef	PPROFILE
	cputime = cputimex = 0;
#endif

	/* Locals: There are three kinds of functions.
	**	1) Normal functions. Don't have locals unless we request them with locals()
	**	2) Module scope. Locals==Globals by implementation.
	**	3) exec-code, class declarations, etc.  IOW code objects that use
	**	   STORE_NAME or DELETE_NAME. These do get a brand new dictionary for locals.
	*/
	if (!l) {
		LOCALS.ctor (co->pyvm_flags ? new DictObj : &None);	
		theglobal = false;
	} else {
		LOCALS.ctor (l);
		theglobal = l == g;
	}

	new_fastlocals (fastlocals = (void*)mstack + co->off1,
			 nfast = co->nlocals - co->nclosure /*co->varnames.as_tuplen->len*/);

	LOOPS = (void*) mstack + co->off2;
	cache_ctx.LOOPS = LOOPS;
	cache_ctx.cloned = 0;
	cache_ctx.fastlocals = fastlocals;
	cache_ctx.globals = GLOBALS.as_dict;
	cache_ctx.S.ctor (mstack, co->stacksize);
	cache_ctx.FUNC.ctor ();
	/* Fetch everything from co. Good for cache */
	argcount = co->argcount;
	stacksize = co->stacksize;
	nframe = co->nframe;
	off1 = co->off1;
	off2 = co->off2;
	__container__.ctor();
#ifdef	DIRECT_THREADING
	if_unlikely (!co->lcode) {
		co->relocate_bytecode ();
		co->code = &None;
	}
	lcode = co->lcode;
#endif
}

PyFuncObj.PyFuncObj (__object__ *x, DictObj *g, DictObj *l) coldfunc
{
	init (x, g, l);
}

PyGeneratorFuncObj.PyGeneratorFuncObj (__object__ *f)
{
	__container__.ctor ();
	GTOR.ctor (f);
}

void PyGeneratorFuncObj.call (REFPTR ret, REFPTR argv[], int argc)
{
	GTOR.as_func->call (ret, argv, argc);
	ret = new PyGeneratorObj (CtxSw.vm);
}

__object__ *MakeFunction (__object__ *x, DictObj *g, REFPTR *argv, int argc) noinline coldfunc;
__object__ *MakeFunction (__object__ *x, DictObj *g, REFPTR *argv, int argc)
{
	int f = PyCodeObj.checkedcast (x)->flags;
	__object__ *rt;

	if (argc)
		rt = f & (CO_VARKEYWORDS|CO_VARARGS) ?
		(PyFuncObj*) new PyFuncObj_dflt_VARS (x, g, argv, argc) :
		(PyFuncObj*) new PyFuncObj_dflt (x, g, argv, argc);
	else
		rt = f & (CO_VARKEYWORDS|CO_VARARGS) ?
		(PyFuncObj*) new PyFuncObj_VARS (x, g) :
		new PyFuncObj (x, g);

	if (f & CO_GENERATOR)
		rt = new PyGeneratorFuncObj (rt);
	return rt;
}

__object__ *MakeClosure (__object__ *x, DictObj *g, REFPTR *argv, int argc) noinline coldfunc;
__object__ *MakeClosure (__object__ *x, DictObj *g, REFPTR *argv, int argc)
{
	PyCodeObj *co = PyCodeObj.cast (x);
	__object__ *p = MakeFunction (x, g, argv, argc);
	PyFuncObj *F = PyGeneratorFuncObj.isinstance (p) ? PyGeneratorFuncObj.cast (p)->GTOR.as_func
			: PyFuncObj.cast (p);
	F->closures.ctor (new Tuplen mvrefarray (argv + argc, co->nclosure));
	if (co->self_closure != -1)
		F->closures.as_tuplen->__xinititem__ (co->self_closure, p);
	memcpy (F->cache_ctx.fastlocals + F->nfast, F->closures.as_tuplen->data,
		 co->nclosure * sizeof (REFPTR));
	return p;
}

PyFuncObj_dflt.PyFuncObj_dflt (__object__ *x, DictObj *g, REFPTR *argv, int argc) coldfunc
{
	int i;
	Tuplen *t;

	init (x, g);
	t = new Tuplen __sizector (codeobj.as_code->argcount);
	ndflt = codeobj.as_code->argcount - argc;
	default_args.ctor (t);
	for (i = 0; i < ndflt; i++)
		t->__xputitem__ (i, &UnboundLocal);
	while (argc--)
		t->__xinititem__ (i++, argv++->o);
}

/*
 * Calling PyFunctions
 */
static inline void *smemcpy (register REFPTR* __restrict dest, register REFPTR* __restrict source, int n)
{
#if 1
	while (n--)
		*dest++ = *source++;
	return dest;
#else
	n *= sizeof (REFPTR);
	return memcpy (dest, source, n) + n;
#endif
}

static void exp_call (REFPTR, REFPTR[], int, int) noinline;
static void exp_call (REFPTR ret, REFPTR argv[], int args, int t)
{
	/* Insert *args tuple and **kwargs dict the sequence of
	 * arguments. A missing optimization is when args or kwargs
	 * have bazillions of arguments and the receiving function
	 * recreates *args / **kwargs from the long sequence of arguments.
	 * We consider that such cases are not found in function calls
	 * where speed matters...  And this is true; most critical calls
	 * are plain calls that use CALL_FUNCTION.
	 * Also, the case CALL_FUNCTION_VAR is more optimized.
	 * CALL_FUNCTION_KW/CALL_FUNCTION_VARKW are considered calls with
	 * a higher cost.
	 */ 
	int argc = args & 255;
	int kargc = (args >> 7) & 254;
	int o = argc + kargc;
	int nargc = argc, nkargc = kargc;
	TupleObj *T = 0;
	DictObj *D = 0;

	if (t != 1) {
		T = TupleObj.checkedcast (argv [++o].o);
		nargc += T->len;
	}
	if (t >= 1) {
		D = DictObj.checkedcast (argv [++o].o);
		nkargc += D->len () * 2;
	}
	REFPTR nargv [nargc + nkargc + 2], *p;

	nargv [0].ctor ();
	p = smemcpy (nargv + 1, argv + 1, argc);
	if_likely ((long)T)
		p = smemcpy (p, T->data, T->len);
	if_unlikely (nkargc) {
		p = smemcpy (p, argv + 1 + argc, kargc);
		if (D)
			for (dictEntry *d = 0; (d = D->__iterfast (d));) {
				p++->o = d->key.o;
				p++->o = d->val.o;
			}
	}
	p->ctor ();
	argv [0]->call (ret, nargv, nargc + (nkargc << 7));
}

/* -*-*-*-*- vm machine code -*-*-*-*- */

#ifdef DIRECT_THREADING
static const int *DToffsets;
static const void *DTbase0;
static void *XX_FOR_ITER, *XX_YIELD_VALUE;

/*const*/ inline void *XXLATE (byte cmd)
{
	return (void*) (DTbase0 + DToffsets [cmd]);
}

void PyCodeObj.relocate_bytecode () coldfunc
{
	lcode = relocate_bytecode ((byte*) code.as_string->str, code.as_string->len,
				   (byte*) lnotab.as_string->str + lno_offset, lno_len);
	inline_consts ();
}

slow void **relocate_bytecode (register const byte*, int, byte* =0,int=0) noinline;
#endif

static inline void release_NG (vm_context *v)
{
	if (!(v->get_code ()->flags & CO_GENERATOR))
		v->release_ctx ();
}

/*
 * unwind vm context until inside a try-except or try-finally block
 */
static vm_context *unwind_vm (vm_context *v)
{
	int i;
	block_data *b;

	for (;;) {
#ifdef	TRACEVM
		pprint ("Unwind to:", v->FUNC.o);
#endif
		for (i = v->LTOS - 1, b = v->LOOPS; i >= 0; i--)
			if (b [i].setup_type >= TYPEEXC) {
				v->LTOS = i + 1;
				return v;
			}
		PROFILE_LEAVE (v)
		v = postfix (v->caller, release_NG (v));
	}
}

void vm_context.release_ctx ()
{
	int nfast = PyFuncObj.cast (FUNC.o)->nfast;
	if (!cloned) {
		reset_fastlocals (fastlocals, nfast);
		S.clean ();
		PyFuncObj.cast (FUNC.o)->reentring = 0;
		FUNC.setNone ();
	} else {
		destroy_fastlocals (fastlocals, nfast);
		delete this;
	}
}

ContextSwitchObj CtxSw;

void ContextSwitchObj.setretval (REFPTR *p)
{
	vm->retval = p;
}

void PyFuncObj.call (REFPTR ret, REFPTR argv [], int args)
{
	/* PyFunctions, ie. functions that execute a bytecode block,
	 * return a context. The context is executed by pyvm to
	 * produce the value. This is stackless.
	 */
#ifdef	TRACEVM
	if (DebugVM&&1) {
	pprint (COLS"____Disassembly_____"COLE);
	codeobj.as_code->disassemble ();
	pprint (COLS"~~~~~~~~~~~~~~~~~~~~"COLE);
	}
#endif
	register vm_context *v;
	if_likely (!reentring) {
		v = &cache_ctx;
		reentring = 1;
	} else {
		v = new vm_context;
		REFPTR *stackarr = (REFPTR*) seg_alloc (nframe);
		v->S.ctor (stackarr, stacksize);
		new_fastlocals (v->fastlocals = (void*)stackarr + off1, nfast);
		if_unlikely (closures.o != &None)
			memcpy (v->fastlocals + nfast, closures.as_tuplen->data,
				 codeobj.as_code->nclosure * sizeof (REFPTR));
		v->LOOPS = (void*) stackarr + off2;
		v->cloned = 1;
		v->globals = cache_ctx.globals;
	}

	/* *-*-*-*-*-*-*-*-*-*-*-*-*-*-* */
	int argc = args & 255;
	int kargc = (args >> 7) & 254;
	parse_args (v->fastlocals, argv + 1, argc, kargc);
	/* *-*-*-*-*-*-*-*-*-*-*-*-*-*-* */

	v->LTOS = 0;
#ifdef	DIRECT_THREADING
	v->WPC = lcode;
#else
	v->fbcd = v->bcd = codeobj.as_code->code.as_string->str;
#endif
	v->FUNC.ctor (this);
	CtxSw.vm = v;
	ret.__copyobj (&CtxSw);
}

static inline bool IntOrBool (__object__ *o)
{
	return IntObj.isinstance (o) || BoolObj.isinstance (o);
}

#define VF_NUMFLAGS (VF_NUMERIC|VF_INTEGER|VF_FLOAT)

enum NUMOPS { _OPMUL, _OPDIV, _OPADD, _OPSUB };
static __object__ *do_inplace (REFPTR, REFPTR, NUMOPS) noinline;
static __object__ *do_inplace (REFPTR N1, REFPTR N2, NUMOPS op)
{
	double f1 = FloatObj.isinstance (N1.o) ? N1.as_double->f : (double) N1.as_int->i;
	double f2 = FloatObj.isinstance (N2.o) ? N2.as_double->f : (double) N2.as_int->i;
	switch (op) {
		case _OPADD: f1 += f2;
		ncase _OPSUB: f1 -= f2;
		ncase _OPMUL: f1 *= f2;
		ncase _OPDIV: f1 /= f2;
	}
	return new FloatObj (f1);
}

/*
 * MACROS for inlining arithmetics in the main interpreter loop
 */
#define if_NUMBER_ARITHMETIC(_op_, _opn_) \
		register int ff = v1->vf_flags & VF_NUMFLAGS & v2->vf_flags;\
		if_likely (ff) {\
			__object__ *n;\
			if_likely (ff & VF_INTEGER)\
				n = newIntObj (v1.as_int->i _op_ v2.as_int->i);\
			else n = do_inplace (v1, v2, _opn_);\
			STK.settop (n);\
		}
#define if_INPLACE_ARITHMETIC(_op_, _opn_) \
		register int ff = op1->vf_flags & VF_NUMFLAGS & op2->vf_flags;\
		if_likely (ff) {\
			if_likely (ff & VF_INTEGER)\
				op1 = newIntObj (op1.as_int->i _op_ op2.as_int->i);\
			else op1 = do_inplace (op1, op2, _opn_);\
		}
#define if_INT_ARITHMETIC(_op_) \
	if (IntObj.isinstance (v1.o) && IntObj.isinstance (v2.o)) \
		STK.settop_overint (newIntObj (IntObj.cast (v1.o)->i _op_ IntObj.cast (v2.o)->i));
#define if_INPLACE_INT_ARITHMETIC(_op_) \
	if (IntObj.isinstance (v1.o) && IntObj.isinstance (v2.o)) \
		v1 = newIntObj (v1.as_int->i _op_ v2.as_int->i);
// shifts <<, >> between ints are done unsigned
#define if_UINT_ARITHMETIC(_op_) \
	if (v1->vf_flags & VF_INTEGER & v2->vf_flags) \
		STK.settop (newIntObj ((uint) IntObj.cast (v1.o)->i _op_\
			 (uint) IntObj.cast (v2.o)->i));
#define if_INPLACE_UINT_ARITHMETIC(_op_) \
	if (v1->vf_flags & VF_INTEGER & v2->vf_flags) \
		v1 = newIntObj ((uint) v1.as_int->i _op_ (uint) v2.as_int->i);

// 
//################################ running context ######################################
//

static bool doGC_ASAP;

#define restrictPTR __restrict
static	vm_context *pvm;
static	exec_stack STK;
static	REFPTR *Fastlocals;
static	int LTOS;
static	DictObj *globals;
static	block_data * restrictPTR LOOPS;
static	__object__ *_ext, *_exv;
static	REFPTR _tb;
#define	SET_CONTEXT_COMMON \
	STK.copy_from (pvm->S);\
	LTOS = pvm->LTOS;\
	globals = pvm->globals;\
	Fastlocals = pvm->fastlocals;\
	LOOPS = pvm->LOOPS;
#define SAVE_CONTEXT_COMMON \
	pvm->S.copy_from (STK);\
	pvm->LTOS = LTOS;

#ifdef	DIRECT_THREADING
static	void ** restrictPTR WPC;
#define	SET_CONTEXT \
	SET_CONTEXT_COMMON \
	WPC = pvm->WPC;
#define	SAVE_CONTEXT \
	SAVE_CONTEXT_COMMON \
	pvm->WPC = WPC;
#define AFTER_FOR_ITER WPC [-2] == XX_FOR_ITER
#define AFTER_YIELD_VALUE(X) X [-1] == XX_YIELD_VALUE
#define SETRACE WPC

#else	/* ! DIRECT_THREADING */
static	byte cmd;
static	byte * bcd;
static	const byte * fbcd;
#define	SET_CONTEXT \
	SET_CONTEXT_COMMON \
	bcd = pvm->bcd;\
	fbcd = pvm->fbcd;
#define	SAVE_CONTEXT \
	SAVE_CONTEXT_COMMON \
	pvm->bcd = bcd;\
	pvm->fbcd = fbcd;
#define AFTER_FOR_ITER bcd [-3] == FOR_ITER
#define AFTER_YIELD_VALUE(X) X [-1] == YIELD_VALUE
#define SETRACE bcd
#endif

#define SET_TASK \
	pvm = RC->vm;\
	PROFILE_START_TIMING
#define SAVE_TASK \
	RC->vm = pvm;\
	PROFILE_STOP_TIMING
//#######################################################################################
#ifndef DIRECT_THREADING
static inline __object__ *co_name (int n)
{
	return pvm->get_code ()->names.as_tuplen->__xgetitem__ (n);
}
#endif
/*******************************************************************************************
	This is boot_pyvm, the main interpreter loop.

	boot_pyvm is stackless, meaning that function calls are implemented
	with context switches inside this function.

	boot_pyvm also does the micro-threads switching which are too done with
	context switches.  checked every JUMP_ABSOLUTE opcode.

	the vm parameters are global so if the current thread blocks another thread
	can enter boot_pyvm and find the context data there.

	Argument (context*):
		0x0: First time initialization
		0x1: Take over mode.  Starts running the context of the RC task
		other: Boot from the given context (preempt)

	The return value can be:
		 2: Detected tasks that returned from blocking and are waiting to take over
		 1: No tasks to run.  Tasks are expected to unblock (or deadlocked)
		 0: Met END_THREAD opcode
		-1: Met END_VM opcode
		 *: Return from preemption.  Value useless

	The fact that pyvm is stackless does not mean that boot_pyvm cannot be reentered.
	We generally prefer not to break the stackless principle and we try to bring everything
	down to the bytecode level.  However this is not always possible.  For example
	the __cmp__ method which may be burried deep in C code.
	It's possible to call boot_pyvm recursively with *one* condition: the thread that
	enters boot_pyvm must be the one that will return from it.
	To ensure that, the current task is declared "preemptive" and the scheduler is
	not allowed to switch micro-threads (avoiding END_THREAD), or to return because
	it detected threads that are waiting to take over.  We can only return with
	END_VM.  Blocking calls do not block the vm!

	The code works equally well with or without DIRECT_THREADING.
	DIRECT_THREADING is the fast case and non-DIRECT_THREADING is easy to debug.
*******************************************************************************************/
int boot_pyvm (vm_context *ivm) nothrow;

int boot_pyvm (vm_context *ivm)
{
	// temps
	REFPTR *arg0;
	__object__ *xx, *nm, *oo1, *oo2;
	unsigned long int arg;
	bool rez;
	char fromform=0; //xxx no init

	// clear this flag
	DoSched &= ~3;

	if (ivm == TAKE_OVER) {
		if (!RC) return 1;
		SET_TASK
	}
#ifdef	DIRECT_THREADING
	else if_unlikely (!ivm) {
		goto export_tables;
	done:	return 0;
	}
#endif
	else {
		pvm = ivm;
		PROFILE_ENTER (0, pvm)
	}

	/* *-*-*-*-*-*-*-*-*-*-*-*-*-* */
		SET_CONTEXT
	/* *-*-*-*-*-*-*-*-*-*-*-*-*-* */
	
#ifdef	TRACEVM
	if (DebugVM)
		pprint ("BOOT PYVM from Func:", pvm->FUNC.o, " in task", RC->ID);
#endif

#define xcase if (0) case

	/* This is the global interpreter setjmp exception handler and all interrupts
	** raised end up here.  They are converted to Py Exceptions and bytecode Raise
	** takes over.  __on_throw__ is a lwc thingy which is like "if (!setjmp(...))"
	** so initially this entire block is skipped.
	*/
	__on_throw__ (Interrupt *I)
	{
		if (I->id == STOP_ITERATION) {
			goto (AFTER_FOR_ITER) ? stop_iterationx : raise_stop_iteration;
		} else if (RAISE_CONTEXTS && I->id == CONTEXT_SWITCH) {
			goto switch_context;
		} else if_unlikely (I->id == RE_RAISE) {
			goto raise0;
		} else {
			__object__ *E = I->interrupt2exception ();
			xx = E;
			nm = I->obj.o;
			if (I->id == IMPORT_ERROR) {
				xx = DynExceptions.ImportError;
#if				TRACEBACK_LEVEL == 1
				if (pvm->FUNC.as_func->codeobj.as_code->filename.o == &None) {
					_tb = pvm->caller->FUNC.o;
					PyFuncObj.cast (_tb.o)->exc_loc = pvm->caller->SETRACE;
					goto notb;
				}
#endif
			} else if (0) raise_no_attr: xx = DynExceptions.AttributeError;
			else if (0) raise_name_error: xx = DynExceptions.NameError;
			else if (0) {
			raise_unpack_error:
				xx = DynExceptions.ValueError;
				extern __object__ *MSG_TMVTU;
				nm = MSG_TMVTU;
			}
#if			TRACEBACK_LEVEL == 1
			_tb = pvm->FUNC.o;
			PyFuncObj.cast (_tb.o)->exc_loc = SETRACE;
		notb:
#elif			TRACEBACK_LEVEL >= 2
			_tb = traceback_str (pvm, TRACEBACK_LEVEL);
#endif
			_ext = xx;
			_exv = nm;
			goto raise0;
		}
	}

	/* vm control macros, depending on DIRECT_THREADING */

#ifdef	DIRECT_THREADING
#define NEXTCMD goto **WPC++;
#define NOSTEP --WPC;
#define GETARGI arg = (unsigned long int) *WPC++;
#define GETARGV GETARGI
#define GETPARG arg = (unsigned long int) WPC [-1];
#define GETOBJA *(__object__**)WPC++
#define SKIPARG WPC++;
#define JUMPREL WPC = (INSTR) arg;
#define JUMPABS JUMPREL
#define JARGREL WPC = (INSTR) *WPC;
#define JARGABS WPC = (INSTR) *WPC;
#define JARGAB1 WPC = (INSTR) WPC [1];
#define JARGRE1 WPC = (INSTR) WPC [1];
#define SKIPCMD WPC += 2;
#define SKIPONE WPC++;
#define SAVEJMP (INSTR) arg
#define LOADJMP WPC
#define CALLVKW arg >> 30
#define CO_NAME GETOBJA
		NEXTCMD		/* ---> enter loop */
		switch (0/*cmd*/) : base0, offsets[] {
#else	/* switch demultiplexing */
#define NOSTEP --bcd;
#define NEXTCMD break;
#define GETARGV arg = (bcd += 2, (bcd[-1]<<8)+bcd[-2]);
#define GETPARG arg = (bcd [-1]<<8) + bcd [-2];
#define GETARGI
#define SKIPARG 
#define JUMPREL bcd += arg;
#define JUMPABS bcd = fbcd + arg;
#define JARGREL JUMPREL
#define JARGABS JUMPABS
#define JARGAB1 bcd = fbcd + *(short*) &bcd [1];
#define JARGRE1 bcd += *(short*) &bcd [1] + 3;
#define SKIPCMD bcd += 3;
#define SKIPONE bcd++;
#define SAVEJMP bcd + arg
#define LOADJMP bcd
#define CALLVKW	cmd - CALL_FUNCTION_VAR
#define CO_NAME co_name (arg)
	for (;;) {
		cmd = *bcd++;
#ifdef	TRACEVM
		if (DebugVM&&1) {
			pprint ("\t", __enumstr__ (py_bytecode, cmd), ":");
			pprint ("\tPC=", bcd - fbcd, " TOS=", (STK.STACKTOP - STK.STACK));
			STK.print_stack ();
		}
#endif

		if (cmd >= STORE_NAME)
			GETARGV;

		switch (cmd) {
		default: pprint ("Unknown cmd:", __enumstr__ (py_bytecode, cmd), " ", cmd);
			RaiseNotImplemented ("Nope");
#endif	// .................................... main loop .........................................

		case JUMP_IF_FALSE:
			GETARGI
			xx = STK.top ().o;
			if (BoolObj.isinstance (xx) ? xx == &FalseObj : !xx->Bool ())
				JUMPREL;
			NEXTCMD
		case JUMP_ABSOLUTE:
			JARGABS
			if_unlikely (!!DoSched)
				goto scheduler;
			NEXTCMD
		case LIST_APPEND_JUMP:
			JARGAB1
		case LIST_APPEND:
		{
			REFPTR *o = &STK.top ();
			ListObj *L = STK.xpop2 ().as_list;
			L->append_mvref (*o);
			NEXTCMD
		}
		case DUP_TOP:
			STK.dup_top ();
			NEXTCMD
		case ROT_THREE:
			STK.rot_three ();
			NEXTCMD
		case ROT_TWO:
			STK.rot_two ();
			NEXTCMD
		case DUP_TOPX:;
			GETARGI
			if_likely (arg == 2) STK.dup_top2 ();
			else STK.dup_top3 ();
			NEXTCMD
		case POP_TOP:
			STK.pop_top ();
			NEXTCMD
		case RROT_THREE:
			STK.reverse_rot_three ();
			SKIPONE
			SKIPONE
			NEXTCMD
		case ROT_SWAP1:
			STK.rot_swap1 ();
			SKIPONE
			NEXTCMD
		case LOAD_CONST:
#ifndef		DIRECT_THREADING
			STK.push_free (pvm->FUNC.as_func->codeobj.as_code->get_const (arg));
#else
			STK.push_free (GETOBJA);
#endif
			NEXTCMD
#ifdef		DIRECT_THREADING
		case LOAD_CONST_PERMANENT:
			STK.push_perm (GETOBJA);
			NEXTCMD
#endif
		case STORE_FAST:
			GETARGI
			setitem_mvref_fastlocal (arg, STK.xpop ());
			NEXTCMD
		case LOAD_FAST:
			GETARGI
			STK.push_free (getitem_fastlocal (Fastlocals, arg));
			NEXTCMD
		case STORELOAD_FAST:
			GETARGI
			setitem_fastlocal (arg, STK.top ().o);
			SKIPCMD
			NEXTCMD
		case DUPSTORE_FAST:
			SKIPONE
			GETARGV
			setitem_fastlocal (arg, STK.top ().o);
			NEXTCMD
		case STORE_NAME:
			pvm->FUNC.as_func->LOCALS.as_dict->xsetitem_str (CO_NAME, STK.xpop ().o);
			NEXTCMD
		case DELETE_FAST:
			GETARGI
			// ??? this is what it does ???
			setitem_fastlocal (arg, &None);
			NEXTCMD
		case LOAD_ATTR:
			nm = STK.top ()->getattr (CO_NAME);
			if_likely (nm != 0) {
				STK.settop (nm);
				NEXTCMD
			}
		noattr:
#ifndef		DIRECT_THREADING
			nm = co_name (arg);
#else
			nm = (__object__*)WPC[-1];
#endif
			goto raise_no_attr;
		case LOAD_FAST_ATTR:
		{
			GETARGI
			__object__ *b = getitem_fastlocal (Fastlocals, arg);
			SKIPONE
#ifndef		DIRECT_THREADING
			GETARGV
#endif
		xcase SELF_ATTR:
			b = getitem_fastlocal (Fastlocals, 0);

			nm = b->getattr (CO_NAME);
			if_likely (nm != 0) {
				STK.push (nm);
				NEXTCMD
			}
			goto noattr;
		}
#define HAVE_SELF_ATTRW
#ifdef		HAVE_SELF_ATTRW
		case SELF_ATTRW:
		{
			__object__ *b = getitem_fastlocal (Fastlocals, 0);
			nm = STK.xpop ().o;
			b->setattr (CO_NAME, nm);
			NEXTCMD
		}
#endif
		case FOR_ITER:
		{
			SKIPARG
			__object__ *Nobj = STK.top ().o;
			STK.pre_push ();
			if_unlikely (PyGeneratorObj.isinstance (Nobj)) {
				CtxSw.vm = PyGeneratorObj.cast (Nobj)->vm;
				goto switch_context;
			}
			Nobj = Nobj->xnext ();
			if_unlikely (Nobj == &CtxSw)
				goto switch_context;
			STK.settop (Nobj);
			NEXTCMD
		stop_iterationx:
			GETPARG
#ifdef			TRACEVM
			if (DebugVM) pprint ("stop iteration in:", pvm->FUNC.o);
#endif
			STK.pop_top (2);
			JUMPREL
			NEXTCMD
		}
		case SETUP_LOOP:
			GETARGI
			LOOPS [LTOS].setup_type = TYPEBRK;
		   stp:
			LOOPS [LTOS].stacktop = (void*) STK.STACKTOP;
			LOOPS [LTOS++].addr = SAVEJMP;
			NEXTCMD
		case POP_BLOCK:
			--LTOS;
			NEXTCMD
		case BREAK_LOOP:
		break_loop:
			while (unlikely (LOOPS [--LTOS].setup_type != TYPEBRK))
				if (LOOPS [LTOS].setup_type == TYPEFIN) {
					STK.STACKTOP = LOOPS [LTOS].stacktop;
					STK.push (IntObj_1);
					LOADJMP = LOOPS [LTOS].addr;
					NEXTCMD
				}
			STK.STACKTOP = LOOPS [LTOS].stacktop;
			LOADJMP = LOOPS [LTOS].addr;
			NEXTCMD
		case LOAD_GLOBAL:
		{
			nm = CO_NAME;
		load_global_instead:;
			__object__ *var = globals->xgetitem_str (nm);
			if_unlikely (!var) {
				var = __builtins__.as_ns->getattr (nm);
				if_unlikely (!var) goto raise_name_error;
			}
			STK.push (var);
			NEXTCMD
		}
		case LOAD_NAME:
		{
			nm = CO_NAME;
			if_likely (pvm->FUNC.as_func->theglobal)
				goto load_global_instead;
			__object__ *var = pvm->FUNC.as_func->LOCALS.as_dict->xgetitem_str (nm);
			if (!var)
				goto load_global_instead;
			STK.push (var);
			NEXTCMD
		}
		case CALL_FUNCTION:
		{
			GETARGI
			arg0 = STK.n_addr ((arg & 255) + ((arg >> 7) & 254)) - 1;
			/*** we pass 2*kargs ***/
			arg0->o->call (*arg0, arg0, arg);
		funcreturn:
			if (&CtxSw == arg0->o) {
		switch_context:
				CtxSw.vm->retval = &STK.top ();
				SAVE_CONTEXT
				CtxSw.vm->caller = pvm;
				PROFILE_ENTER (pvm, CtxSw.vm)
				pvm = CtxSw.vm;
#ifdef	SIMPLE_TRACE
				if (DebugVM)
					pprint (" call-switching to",(void*)pvm,pvm->get_code ()->name.o);
#endif
#ifdef	PRETTY_TRACE
				pprint ("calling", pvm->get_code ()->name.o);
#endif
				SET_CONTEXT
			}
			NEXTCMD
		}
		case UNPACK2:
			nm = STK.xpop ().o;
			if_likely (nm->vf_flags & VF_FUNPACK)
				if_likely (TupleObj.cast (nm)->len == 2)
					TupleObj.cast (nm)->unpack2 (STK.STACKTOP);
				else goto raise_unpack_error;
			else unpack_other (nm, STK.STACKTOP, 2);
			STK.STACKTOP += 2;
			NEXTCMD
		case UNPACK_SEQUENCE:
			GETARGI
			nm = STK.xpop ().o;
			if_likely (nm->vf_flags & VF_FUNPACK)
				if_likely (arg == TupleObj.cast (nm)->len) {
					REFPTR p = nm;
					p.as_tuple->unpack (STK.STACKTOP);
				} else goto raise_unpack_error;
			else unpack_other (nm, STK.STACKTOP, arg);
			STK.STACKTOP += arg;
			NEXTCMD

		case BUILD_LIST:
		{
			GETARGI
			REFPTR *pargv = STK.n_addr (arg);
			STK.push (new ListObj mvrefarray (pargv, arg));
			NEXTCMD
		}
		case BUILD_TUPLE:
		{
			GETARGI
			REFPTR *pargv = STK.n_addr (arg);
			STK.push (newTuple_mvrefarray (pargv, arg));
			NEXTCMD
		}
		case BUILD_MAP:
			GETARGI
			STK.push (new DictObj);
			NEXTCMD

		case SLICE_2:
		{
			REFPTR &idx = STK.top ();
			__object__ *sq = STK.xpop2 ().o;
			IntObj.fenforcetype (idx.o);
			STK.push (sq->xgetslice (0, idx.as_int->i));
			NEXTCMD
		}
		case SLICE_1:
		{
			REFPTR &idx = STK.top ();
			__object__ *sq = STK.xpop2 ().o;
			IntObj.fenforcetype (idx.o);
			STK.push (sq->xgetslice (idx.as_int->i, INT_MAX));
			NEXTCMD
		}
		case SLICE_0:
		{
			__object__ *sq = STK.xpop ().o;
			STK.push (sq->xgetslice (0, INT_MAX));
			NEXTCMD
		}
		case SLICE_3:
		{
			REFPTR &op0 = STK.top (), &op1 = STK.second (), &op2 = STK.third ();
			STK.pop_top (3);
			STK.push (op2->xgetslice (IntObj.fcheckedcast (op1.o)->i,
				  IntObj.fcheckedcast (op0.o)->i));
			NEXTCMD
		}
		case DELETE_SUBSCR:
		{
			REFPTR &op0 = STK.top (), &op1 = STK.xpop2 ();
			op1->xdelitem (op0.o);
			NEXTCMD
		}
		case STORE_SUBSCR:
		{
			REFPTR &op0 = STK.top (), &op1 = STK.second (), &op2 = STK.third ();
			STK.pop_top (3);
			//op1->setitem_mvref (&op0, &op2);
			op1->xsetitem (op0.o, op2.o);
			NEXTCMD
		}
		case STORE_ATTR:
		{
			register REFPTR &op0 = STK.top (), &op1 = STK.xpop2 ();
			op0->setattr (CO_NAME, op1.o);
			NEXTCMD
		}
		case BINARY_SUBSCR:
		{
			REFPTR &idx = STK.xpop ();
			REFPTR &obj = STK.top ();
			/* If we are in RAISE_CONTEXT, the stack MUST be
			 * pre-pushed for this to work. We can't do it as
			 * 	top(),  xpop2(),  push()
			 * In the case of another exception the stack is
			 * re-adjusted at POP_BLOCK.
			 */
			STK.settop (obj->xgetitem (idx.o));
			NEXTCMD
		}
		case INPLACE_ADD:
		{
			REFPTR &op2 = STK.xpop ();
			REFPTR &op1 = STK.top ();
			if_INPLACE_ARITHMETIC(+, _OPADD)
			else op1 = op1->concat (op2.o);
			NEXTCMD
		}
		case JFP_COMPARE: {
			GETARGI
			REFPTR &op0 = STK.top (), &op = STK.xpop2 ();
			if (compare_op (op0, op, arg)) {
				SKIPCMD SKIPONE
			} else	JARGRE1
			NEXTCMD
		}
		case JFP_COMPARE_EQ:
			GETARGI
			oo1 = STK.top ().o;
			oo2 = STK.xpop2 ().o;
			if (IntObj.isinstance (oo1) && IntObj.isinstance (oo2))
				rez = IntObj.cast (oo1)->i == IntObj.cast (oo2)->i;
			else if (StringObj.isinstance (oo1) && StringObj.isinstance (oo2))
				rez = (*StringObj.cast (oo1)).cmp_EQ_same (oo2);
			else rez = oo2->cmp_EQ (oo1);
			if (rez ^ arg) {
				SKIPCMD SKIPONE
			} else	JARGRE1
			NEXTCMD
		case JFP_COMPARE_GR:
		case JFP_COMPARE_LEQ:
				oo2 = STK.top ().o;
				oo1 = STK.xpop2 ().o;
		xcase JFP_COMPARE_LE: {
		case JFP_COMPARE_GRQ:
				oo1 = STK.top ().o;
				oo2 = STK.xpop2 ().o;
			}
			GETARGI
			if_likely (oo1->vf_flags & VF_NUMERIC & oo2->vf_flags)
				rez = IntObj.isinstance (oo1) ? IntObj.isinstance (oo2) ?
					IntObj.cast (oo2)->i < IntObj.cast (oo1)->i :
					FloatObj.cast (oo2)->f < (float) IntObj.cast (oo1)->i :
				      IntObj.isinstance (oo2) ?
					(float) IntObj.cast (oo2)->i < FloatObj.cast (oo1)->f :
					FloatObj.cast (oo2)->f < FloatObj.cast (oo1)->f;
			else rez = oo1->cmp_GEN (oo2) > 0;
			if (rez ^ arg) {
				SKIPCMD SKIPONE
			} else	JARGRE1
			NEXTCMD
		case JFP_COMPARE_IN:
			GETARGI
			oo1 = STK.top ().o;
			oo2 = STK.xpop2 ().o;
			if (!!oo1->contains (oo2) ^ arg) {
				SKIPCMD SKIPONE
			} else	JARGRE1
			NEXTCMD
		case JFP_COMPARE_IS:
			GETARGI
			oo1 = STK.top ().o;
			oo2 = STK.xpop2 ().o;
			if ((oo1 == oo2) ^ arg) {
				SKIPCMD SKIPONE
			} else	JARGRE1
			NEXTCMD
		case BINARY_ADD:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_NUMBER_ARITHMETIC(+, _OPADD)
			else STK.settop (v1->binary_add (v2.o));
			NEXTCMD
		}
		case BINARY_SUBTRACT:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_NUMBER_ARITHMETIC(-, _OPSUB)
			else STK.settop (do_binary_sub (v1.o, v2.o));
			NEXTCMD
		}
		case BINARY_MULTIPLY:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_NUMBER_ARITHMETIC(*, _OPMUL)
			else STK.settop (v1->binary_mul (v2.o));
			NEXTCMD
		}
		case BINARY_FLOOR_DIVIDE:
		case BINARY_DIVIDE:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_NUMBER_ARITHMETIC(/, _OPDIV)
			else STK.settop (do_binary_div (v1.o, v2.o));
			NEXTCMD
		}
		case BINARY_MODULO:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_INT_ARITHMETIC(%)
			else STK.settop (v1->binary_modulo (v2.o));
			NEXTCMD
		}
		case UNARY_NOT:
			xx = STK.top ().o;
			if (BoolObj.isinstance (xx))
				STK.STACKTOP [-1].o = xx == &TrueObj ? &FalseObj : &TrueObj;
			else	STK.settop_perm (xx->Bool () ? &FalseObj : &TrueObj);
			NEXTCMD
		case UNARY_NEGATIVE:
			xx = STK.top ().o;
			if (IntObj.isinstance (xx))
				STK.settop (newIntObj (-IntObj.cast (xx)->i));
			else if (FloatObj.isinstance (xx))
				STK.settop (new FloatObj (-FloatObj.cast (xx)->f));
			else STK.settop (do_unary_neg (xx));
			NEXTCMD
		case STORE_GLOBAL:
			globals->xsetitem (CO_NAME, STK.xpop ().o);
			// xsetitem_str?
			NEXTCMD
		case CALL_FUNCTION_KW:
		case CALL_FUNCTION_VAR_KW:
		case CALL_FUNCTION_VAR:
		{
			GETARGI
			int args = arg & 255, kargs = (arg >> 7) & 254, t = CALLVKW;
			arg0 = STK.n_addr (args + kargs + 1 + ((t&2)>>1)) - 1;
			exp_call (*arg0, arg0, arg, t);
			goto funcreturn;
		}
		case JUMP_IF_TRUE:
			GETARGI
			xx = STK.top ().o;
			if (BoolObj.isinstance (xx) ? xx == &TrueObj : xx->Bool ())
				JUMPREL;
			NEXTCMD
		case PRINT_ITEM:
			prepare_stdout (0);
			print_out (STK.xpop ().o);
			NEXTCMD
		case PRINT_NEWLINE:
			prepare_stdout ('\n');
			NEXTCMD
		case RETURN_VALUE:
		return_value:
			pvm->retval->__copyobj (STK.top ().preserve ());
		return_value2:
#ifdef	SIMPLE_TRACE
			if (DebugVM)
				pprint (" returning value from:", (void*)pvm, pvm->get_code ()->name.o,
					" back to:", (void*)pvm->caller, pvm->caller->FUNC.o);
#endif
			PROFILE_LEAVE (pvm)
			pvm = postfix (pvm->caller, pvm->release_ctx ());
			SET_CONTEXT
			NEXTCMD
		case YIELD_VALUE:
#ifdef	SIMPLE_TRACE
			if (DebugVM)
				pprint (" yielding value from:", pvm->FUNC.o,
					" down to:", pvm->caller->FUNC.o);
#endif
			*pvm->retval = STK.xpop ().o;
			SAVE_CONTEXT
			PROFILE_LEAVE (pvm, pvm->caller)
			pvm = pvm->caller;
			SET_CONTEXT
			NEXTCMD
		case INPLACE_SUBTRACT:
		{
			REFPTR &op2 = STK.xpop ();
			REFPTR &op1 = STK.top ();
			if_INPLACE_ARITHMETIC(-, _OPSUB)
			else op1 = op1->inplace_sub (op2.o);
			NEXTCMD
		}
		case LOAD_LOCALS:
			STK.push (pvm->FastToLocals ());
			NEXTCMD
		case BUILD_CLASS:;
			extern __object__ *BUILD_CLASS_func (REFPTR[]);
			STK.push (BUILD_CLASS_func (STK.n_addr (3)));
			NEXTCMD
		case DELETE_NAME:
			nm = CO_NAME;
			pvm->FUNC.as_func->LOCALS.as_dict->xdelitem (nm);
			NEXTCMD
		case RETURN_GENERATOR:
			NOSTEP
			SAVE_CONTEXT
			PROFILE_LEAVE (pvm)
			pvm = pvm->caller;
			SET_CONTEXT
			goto raise_stop_iteration;
		case GET_ITER:
			xx = STK.top ()->iter ();
			STK.settop (xx);
			NEXTCMD
		case STORE_SLICE_3:
		{
			REFPTR &op0 = STK.top (), &op1 = STK.second (), &op2 = STK.third ();
			STK.pop_top (3);
			op2->xsetslice (IntObj.fcheckedcast (op1.o)->i,
					IntObj.fcheckedcast (op0.o)->i, STK.xpop ().o);
			NEXTCMD
		}
		case STORE_SLICE_0:
		{
			REFPTR &op = STK.xpop ();
			op->xsetslice (0, $INT_MAX, STK.xpop ().o);
			NEXTCMD
		}
		case STORE_SLICE_1:
		{
			REFPTR &op0 = STK.top (), &op = STK.xpop2 ();
			op->xsetslice (IntObj.fcheckedcast (op0.o)->i, $INT_MAX, STK.xpop ().o);
			NEXTCMD
		}
		case COMPARE_OP:
		{
			GETARGI
			REFPTR &v1 = STK.xpop (), &v2 = STK.top ();
			STK.settop_perm (compare_op (v1, v2, arg) ? &TrueObj : &FalseObj);
			NEXTCMD
		}
		case SETUP_EXCEPT:
			GETARGI
			LOOPS [LTOS].setup_type = TYPEEXC;
			goto stp;
		case SETUP_FINALLY:
			GETARGI
			LOOPS [LTOS].setup_type = TYPEFIN;
			goto stp;
		case JUMP_ABSOLUTE_EXC:
			JARGABS
			--LTOS;
			NEXTCMD
		case INPLACE_MULTIPLY:
		{
			REFPTR &op2 = STK.xpop ();
			REFPTR &op1 = STK.top ();
			if_INPLACE_ARITHMETIC(*, _OPMUL)
			else RaiseNotImplemented ("INPLACE_MULTIPLY");
			NEXTCMD
		}
		case RETURN_VALUE_FNL:
			STK.push (IntObj_0);
			for (int i = LTOS - 1; i >= 0; i--)
				if (LOOPS [i].setup_type == TYPEFIN) {
					LTOS = i;
					LOADJMP = LOOPS [LTOS].addr;
					break;
				}
			NEXTCMD
		case RAISE_VARARGS:;
			GETARGI
			if_likely (arg) {
				/* Dpreserve? */
				if (arg == 1) {
					_ext = STK.xpop ().Dpreserve ();
					_exv = &None;
					if (DynInstanceObj.isinstance (_ext)) {
						_exv = _ext;
						_ext = DynInstanceObj.cast (_ext)->__class__.o;
					}
				} else {
					_exv = STK.xpop ().preserve ();
					_ext = STK.xpop ().preserve ();
				}
#if	TRACEBACK_LEVEL == 1
				_tb = pvm->FUNC.o;
				PyFuncObj.cast (_tb.o)->exc_loc = SETRACE;
#elif	TRACEBACK_LEVEL >= 2
				_tb = traceback_str (pvm, TRACEBACK_LEVEL);
#endif
			} else {
				REFPTR *prm = unwind_to_hb ()->stacktop;
				_ext = prm [2].o;
				_exv = prm [1].o;
				_tb = prm [0].o;
			}
		raise0:
#ifdef	SIMPLE_TRACE
			if (DebugVM) {
				pprint ("----------------------------------------------------");
				pprint ("Raising started from:", pvm->get_code ()->name.o);
				pprint (_ext, _exv, _tb.o);
				whereami ();
			}
#endif
#if 0
			pprint ("Raising started from:", pvm->get_code ()->name.o);
			pprint (_ext, _exv, _tb.o);
			whereami ();
#endif
			if (_ext == DynExceptions.StopIteration)
				goto raise_stop_iteration;

			/* Look for TYPEEXC, TYPEFIN */
			for (int i = LTOS - 1; i >= 0; i--)
				if (LOOPS [i].setup_type >= TYPEEXC) {
					LTOS = i + 1;
					goto ex_ok;
				}
			++_ext->refcnt, ++_exv->refcnt;
			PROFILE_LEAVE (pvm)
			pvm = unwind_vm (postfix (pvm->caller, release_NG (pvm)));
			--_ext->refcnt, --_exv->refcnt;
			SET_CONTEXT
		ex_ok:
			LOOPS [LTOS - 1].setup_type = TYPE_HB;
			STK.STACKTOP = LOOPS [LTOS - 1].stacktop;
#if 0
			STK.push (&None);
#else
			STK.push (_tb.o);
#endif
			STK.push (_exv);
			STK.push (_ext);
			LOADJMP = LOOPS [LTOS - 1].addr;
			LOOPS [LTOS - 1].setup_type = TYPE_HB;
			NEXTCMD
		raise_stop_iteration:
			do {
				if_likely (AFTER_FOR_ITER)
					goto stop_iterationx;
				for (int i = LTOS - 1; i >= 0; i--)
					if (LOOPS [i].setup_type >= TYPEEXC) {
						LTOS = i + 1;
						_ext = DynExceptions.StopIteration;
						_exv = &None;
						goto ex_ok;
					}
				pvm = postfix (pvm->caller, release_NG (pvm));
				SET_CONTEXT
			} while (1);
		case END_FINALLY2:;
			/* Some explanation for the confused reader:
			 * If you think about it, END_FINALLY for try/except and
			 * try/finally is different. This is for the try/finally
			 * case. TOS tells it what to do. In the case of exception,
			 * TOS contains the exception. Otherwise, something we've
			 * arranged
			 */
			xx = STK.xpop ().o;
			if (IntObj.isinstance (xx)) {
				arg = IntObj.cast (xx)->i;
				if (!arg) goto return_value;
				if (arg == 1) goto break_loop;
				goto continue_loop;
			}
			if (xx != &None) {
				STK.pop_top (2);
				goto raise0;
			}
			NEXTCMD
		case END_FINALLY:
			/* XXXX: we must --LTOS? */
			STK.pop_top (3);
			goto raise0;
		case BINARY_LSHIFT:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_UINT_ARITHMETIC(<<)
			else STK.settop (do_binary_lsh (v1.o, v2.o));
			NEXTCMD
		}
		case BINARY_AND:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_UINT_ARITHMETIC(&)
			else STK.settop (do_binary_and (v1.o, v2.o));
			NEXTCMD
		}
		case BINARY_OR:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_UINT_ARITHMETIC(|)
			else STK.settop (do_binary_or (v1.o, v2.o));
			NEXTCMD
		}

		/***** below this point opcodes are used (very) rarely ****/
		case INPLACE_DIVIDE:
		{
			REFPTR &op2 = STK.xpop ();
			REFPTR &op1 = STK.top ();
			if_INPLACE_ARITHMETIC(/, _OPDIV)
			else STK.settop (do_inplace_div (op1.o, op2.o));
			NEXTCMD
		}
		case BINARY_POWER:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if (v2->vf_flags & VF_NUMERIC & v1->vf_flags)
				STK.settop (Num_Power (v1.o, v2.o));
			else RaiseNotImplemented ("BINARY_POWER");
			NEXTCMD
		}
		case INPLACE_OR:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_INPLACE_UINT_ARITHMETIC(|)
			else RaiseNotImplemented ("INPLACE_OR");
			NEXTCMD
		}
		case BINARY_XOR:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_UINT_ARITHMETIC(^)
			else STK.settop (do_binary_xor (v1.o, v2.o));
			NEXTCMD
		}
		case BINARY_RSHIFT:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_UINT_ARITHMETIC(>>)
			else STK.settop (do_binary_rsh (v1.o, v2.o));
			NEXTCMD
		}
		case INPLACE_RSHIFT:
		{
			REFPTR &v2 = STK.xpop (), &v1 = STK.top ();
			if_INPLACE_UINT_ARITHMETIC(>>)
			else RaiseNotImplemented ("INPLACE_RSHIFT");
			NEXTCMD
		}
		case MAKE_FUNCTION:
			GETARGI
			if (arg) {
				__object__ *co = STK.top ().o;
				STK.push (MakeFunction (co, globals, STK.n_addr (arg + 1), arg));
			} else
				STK.push (MakeFunction (STK.xpop ().o, globals));
			NEXTCMD
		case DELETE_ATTR:
			STK.xpop ()->delattr (CO_NAME);
			NEXTCMD

		/* XXXX: only lists support delslice, so we can avoid the
		 * virtuality and join all these in one case */
		case DELETE_SLICE_1:
		{
			REFPTR &idx = STK.top ();
			__object__ *sq = STK.xpop2 ().o;
			IntObj.fenforcetype (idx.o);
			sq->xdelslice (idx.as_int->i, INT_MAX);
			NEXTCMD
		}
		case DELETE_SLICE_3: {
			REFPTR &op0 = STK.top (), &op1 = STK.second (), &op2 = STK.third ();
			STK.pop_top (3);
			op2->xdelslice (op1.as_int->i, op0.as_int->i);
			NEXTCMD;
		}
		case UNARY_INVERT:
			xx = STK.top ().o;
			if (IntObj.isinstance (xx))
				STK.settop (newIntObj (~IntObj.cast (xx)->i));
			else RaiseNotImplemented ("UNARY_INVERT");
			NEXTCMD
		case EXEC_STMT:
		{
			/* really broken. We prefer eval() */
			REFPTR execargs [] = { STK.third ().o, STK.second ().o, STK.top ().o };
			(void) STK.xpop2 ();
			__exec___builtin (execargs, 3);
			goto switch_context;
		}
		case CONTINUE_LOOP:
			GETARGI
			do if_unlikely (LOOPS [--LTOS].setup_type == TYPEFIN) {
				STK.STACKTOP = LOOPS [LTOS].stacktop;
				STK.push (new IntObj (arg));
				LOADJMP = LOOPS [LTOS].addr;
				NEXTCMD
			continue_loop:;
			} while (LOOPS [LTOS - 1].setup_type != TYPEBRK);
			STK.STACKTOP = LOOPS [LTOS].stacktop;
			JUMPABS
			NEXTCMD
		case RETURN_MODULE:
#ifdef	OPTMIZE_MODULES
			globals->D.reorder_opt (pvm->FUNC.as_func->codeobj.as_code->name.o);
#endif
			/* Do not return 'None' over the module */
			goto return_value2;

		case IMPORT_NAME:
			fromform = STK.top ().o != &None;
			IMPORT_GUARDED (CO_NAME);	/* should pass from-form here.. */
			goto switch_context;
		case _IMPORT_NAME:; 
			GETARGI
			/* In case of a pyc module, we'll have to execute bytecode.
			 * import_module will set CtxSw.vm to the vm_context
			 * This is very special, but modules are treated as if they
			 * returned their globals()
			 */
			__object__ *modnam = STK.xpop ().o;
			CtxSw.vm = 0;
			STK.push (import_module (modnam, fromform));	/* .. and find it here */
			if (CtxSw.vm)
				goto switch_context;
			NEXTCMD

		case IMPORT_FROM:
			nm = STK.top ().o;
			nm = nm->getattr (xx = CO_NAME);
			if_likely (nm != 0) {
				STK.push (nm);
				NEXTCMD
			}
			IMPORT_GUARDED_FROM (STK.top ().o, xx);
			fromform=1;
			STK.pre_push ();
			goto switch_context;

		case IMPORT_STAR:
			nm = STK.xpop ().o;
			/*** must go in locals really ***/
			import_star (globals, nm);
			NEXTCMD
		scheduler: {
			/* Reached when somebody sets DoSched.
			 * In here to proceed with NEXTCMD
			 */
			None.inf ();
			int sched_flags = DoSched;
			DoSched &= SCHED_KILL;
			/* clean __del__ methods */
			if_unlikely (sched_flags & SCHED_DEL) {
				if (Graveyard.as_list->len) {
					run_graveyard ();
					NEXTCMD
				}
				if (!(sched_flags & ~SCHED_DEL))
					goto back_to_vm;
			}
			/* queued sigint */
			if_unlikely (sched_flags & SCHED_KILL) {
				if (!RC->ID)
					RaiseSystemExit (&None);
				DoSched |= SCHED_KILL;
			}
			/* maybe time for a garbage collection */
			if (doGC_ASAP || sched_flags & SCHED_PERIODIC) {
				maybe_do_GC ();
			}
			/* back from hibernation, restart timer */
			if (sched_flags & SCHED_HIBERNATE) {
				restartTimer ();
			}
			/* maybe threads waiting on GIL */
			if (have_pending ()) {
				if_unlikely (RC->preemptive)
					goto sched_yield;
				SAVE_CONTEXT
				SAVE_TASK
				return 2;
			}
			/* switch to other task, if any */
			if (RF == RC && RC == RL) back_to_vm: {
				NEXTCMD
			}
			if_unlikely (RC->preemptive) sched_yield: {
				py_sched_yield ();
				NEXTCMD
			}
			SAVE_CONTEXT
			SAVE_TASK
			if (RC->next) RC->next->make_current ();
			else RF->make_current ();
		}
		resume_thread:
#ifdef	TRACEVM
			pprint ("switching ctx from:(", pvm->FUNC.o, " to:", RC->vm->FUNC.o,
				"(", RC->ID, ")");
#endif
			SET_TASK
			SET_CONTEXT
			// thread interruption
			if_unlikely (RC->INTERRUPT.o != &None) {
				_ext = RC->INTERRUPT.o;
				_exv = &None;
#if	TRACEBACK_LEVEL == 1
				_tb = pvm->FUNC.o;
				PyFuncObj.cast (_tb.o)->exc_loc = SETRACE;
#elif	TRACEBACK_LEVEL >= 2
				_tb = traceback_str (pvm, TRACEBACK_LEVEL);
#endif
				RC->INTERRUPT.o = &None;
				goto raise0;
			}
			NEXTCMD
		case BLOCK_THREAD:
			GETARGI
			SAVE_CONTEXT
			SAVE_TASK
			if_unlikely (!RC->preemptive)
				maybe_do_GC ();
			RC->move_blocked ();
			if (RC) goto resume_thread;
			return 1;
		case MAKE_CLOSURE:
			GETARGI
			xx = STK.top ().o;
			STK.push (MakeClosure (xx, globals, STK.n_addr (arg + 1 +
						 PyCodeObj.cast (xx)->nclosure), arg));
		case NO_OP:
			NEXTCMD
		case END_THREAD:
			NOSTEP
			if (Graveyard.as_list->len)
				run_graveyard ();
			delete RC;
			pvm->release_ctx ();
			if (!RC) return 1;
			goto resume_thread;
		case END_VM:
			NOSTEP
			return -1;
#ifndef	DIRECT_THREADING
		case JUMP_FORWARD:
			JARGREL
			NEXTCMD
		case JUMP_FORWARD_EXC:
			JARGREL
			--LTOS;
			NEXTCMD
#endif
		case STOP_CODE:
			RaiseNotImplemented ("Unsupported feature");
#ifdef	DIRECT_THREADING
		default:
			whereami ();
			RaiseNotImplemented ("BAD OPCODE");
#endif
		}
#ifdef	DIRECT_THREADING
export_tables:
	/* gcc can't make address tables global */
	DTbase0 = base0;
	DToffsets = offsets;
	XX_FOR_ITER = XXLATE (FOR_ITER);
	XX_YIELD_VALUE = XXLATE (YIELD_VALUE);
	goto done;
#else
	}
#endif
}




/* walk the function stack, and for each function walk its
 * Block-Setup stack, looking for the innermost active 
 * exception handling block.  This whole deal is done to
 *	1) implement sys.exc_info
 *	2) Do re-raise (raise without arguments)
 *	3) Basically be able to do:
 *		try: raise A
 *		except:
 *			try: raise B
 *			except: pass
 *			raise	# THIS ONE MUST RAISE AN 'A'!
*/
static block_data *unwind_to_hb ()
{
	register block_data *bd;
	register int i;

	for (i = LTOS - 1, bd = LOOPS; i >= 0; i--)
		if_likely (bd [i].setup_type == TYPE_HB)
			return &bd [i];

	vm_context *v = pvm->caller;

	do for (i = v->LTOS - 1, bd = v->LOOPS; i >= 0; i--)
		if (bd [i].setup_type == TYPE_HB)
			return &bd [i];
	while ((v = v->caller));

	/* not reached. We have installed a pseudo-handler at the BIOS */
	return 0;
}

/*
 * sys.exc_info()
 * This is special in pyvm because the exception is stored on
 * the stack of the function with the exception handler.
 * If that has been destroyed then sys.exc_info will use the
 * innermost such block. If none exists it will use the exception
 * handling block of the "boot_ctx" which gives None, None, None
 */
__object__ *pyvm_exc_info ()
{
	block_data *B = unwind_to_hb ();
	REFPTR *prm = B->stacktop;
	Tuplen *r = new Tuplen __sizector (3);
	r->__xinititem__ (0, prm [2].o);
	r->__xinititem__ (1, prm [1].o);
#if TRACEBACK_LEVEL == 0
	r->__xinititem__ (2, &None);
#elif TRACEBACK_LEVEL == 1
	r->__xinititem__ (2, prm [0].as_func->codeobj.as_code->LineInfo (prm [0].as_func->exc_loc));
#elif TRACEBACK_LEVEL >= 2
	r->__xinititem__ (2, prm [0].o);
#endif
	return r;
}

/*
 * when we have to use iterators that stop with StopIteration in C,
 *  use this routine at the else-clause of the try-statement.
 */

static inline bool last_exc_stopiteration ()
{
	return _ext == DynExceptions.StopIteration;
}
void catch_stop_iteration (Interrupt *I)
{
	if (I->id == RE_RAISE && last_exc_stopiteration ())
		;
	else if (I->id != STOP_ITERATION)
		throw I;
}
void catch_no_attribute (Interrupt*) noinline;
void catch_no_attribute (Interrupt *I)
{
	if (I->id == RE_RAISE && _ext == DynExceptions.AttributeError)
		;
	else if (I->id != NO_ATTRIBUTE)
		throw I;
}


DictObj *globalocals;

/*******************************************************************

	  preemptive boot
 
	  called by __cmp__, __getitem__, etc...

******************************************************************** */

__object__ *preempt_pyvm (vm_context *vm)
{
	REFPTR xx;
	vm->retval = &xx;
	vm->caller = preempt_ctx;
	preempt_link pl;

	pl.v = pvm;
	pl.outer = RC->pctx;
	RC->pctx = &pl;
	++RC->preemptive;
	SAVE_CONTEXT
	vm_context *ctx = pvm;
	PROFILE_STOP_TIMING
#ifdef	PPROFILE
	preempt_ctx->cpu = 0;
#endif
	/*+*+*+*+*+*+*/
	boot_pyvm (vm);
	/*+*+*+*+*+*+*/
	pvm = ctx;
#ifdef	PPROFILE
	pvm->cpu += preempt_ctx->cpu;
#endif
	PROFILE_START_TIMING
	SET_CONTEXT
	--RC->preemptive;
	RC->pctx = pl.outer;

	if_likely (Preempt_ExceptionBlock.setup_type == TYPEEXC)
		return xx.Dpreserve ();

	/* exception */
	REFPTR *prm = preempt_ctx->S.STACK;
	_ext = prm [2].o;
	_exv = prm [1].o;
	_tb = prm [0].o;
	Preempt_ExceptionBlock.setup_type = TYPEEXC;

	ReRaise ();
}

static vm_context *preempt_ctx, *boot_ctx;

/***************************************************************

		Automatic garbage collection

GC is triggered if:
1) 1 second has ellapsed since the last GC
2) The memory held by the segmented allocator is 6.4MB more
   than the memory the segmented allocator held at the time
   of the previous GC

And no less than the 10th call to maybe_do_gc ()
xxx: 10th depends on the periodic timer. currently the periodic
 timer beeps every 2ms, so automatic gc is never invoked sooner
 than 20ms. but that should be ajusted to a time base.
***************************************************************/

static unsigned long last_GC_time;
static long last_GC_mem;
static int GC_nth;
#if 1
#define GC_EVERY_NTH 10		/* every nth invocation of maybe_do_gc () */
#define GC_EVERY_TIME 5		/* seconds since last gc */
#define GC_EVERY_MEM 200	/* memory increase since last gc */
#else // GC ASAP
#define GC_EVERY_NTH 1
#define GC_EVERY_TIME 1
#define GC_EVERY_MEM 1
#endif

static void maybe_do_GC () noinline;

slow void reset_GC_params ()
{
	last_GC_time = time (0);
	last_GC_mem = seg_n_segments ();
	GC_nth = 0;
}

static slow void maybe_do_GC ()
{
	long now, nseg;
	once {
		last_GC_time = time (0);
		last_GC_mem = seg_n_segments ();
		return;
	}

	/* avoid frequent expensive calls to time() */
	if (!doGC_ASAP && GC_nth++ < GC_EVERY_NTH)
		return;

	/* if must do as soon as possible but still in preemption, postpone */
	if (doGC_ASAP && RC->preemptive)
		return;

	if (!doGC_ASAP) {
		now = time (0);
		nseg = seg_n_segments ();
	}

	if (doGC_ASAP || now - last_GC_time >= GC_EVERY_TIME || nseg - last_GC_mem > GC_EVERY_MEM) {
		if (RC->preemptive) {
			doGC_ASAP = 1;
			return;
		}

		PROFILE_STOP_TIMING
		/*!*!*!*!*!*!*!*!*!*!*/
		weakref_collect ();
		GC_collect (_GC_LIST);
		/*!*!*!*!*!*!*!*!*!*!*/
		PROFILE_START_TIMING

		doGC_ASAP = false;
	}
}


/*******************************************************************

	__del__ method zombie cleanup

	instances with __del__ methods that are about to be
	destroyed, are placed in the graveyard list and become
	zombies.  When run_graveyard() is called, it invokes
	the __del__ method and after that if the instance has
	a refcnt of 1, it is killed for good.  IOW, __del__ may
	resurrect a zombie.  Depends on the hit points, alignment
	and level.

*******************************************************************/

static void ListObj.clean_zombies ()
{
	int len = len, i;
	REFPTR *data = data;

	for (i = 0; i < len; i++)
		if_likely (DynMethodObj.isinstance (data [i].o))
			if_likely (data [i].as_meth->__self__->refcnt == 1) {
				/* direct delete for instance */
				delete *data [i].as_meth->__self__.as_inst;
				data [i].as_meth->__self__.ctor ();
			}
}

static void run_graveyard () noinline;
extern PyFuncObj *graveFunc;

static void run_graveyard ()
{
	/* better run through the graveyard */
	REFPTR TL __copyctor (Graveyard.o);
	Graveyard.ctor (new ListObj ());
	(*graveFunc).call (devnull, &TL - 1, 1);	/* may not throw */

	/* we know that the graveyard func catches all exceptions,
	   that it may never raise and that it always returns None  */
	vm_context *vm = CtxSw.vm;
	vm->retval = &devnull;
	vm->caller = preempt_ctx;
	++RC->preemptive;
	SAVE_CONTEXT
	vm_context *ctx = pvm;
	boot_pyvm (vm);
	pvm = ctx;
	SET_CONTEXT
	--RC->preemptive;

	TL.as_list->clean_zombies ();
}

/**************************************************************

	code taken out of boot_pyvm as noinline

**************************************************************/

static bool compare_exceptions (__object__*, __object__*) noinline;
static bool obj_compare_op (REFPTR, REFPTR, unsigned int) noinline;

static bool compare_exceptions (__object__ * v1, __object__ * v2)
{
	if (DynInstanceObj.isinstance (v2))
		v2 = DynInstanceObj.cast (v2)->__class__.o;
	if (v1 == v2)
		return true;
	if (!DynClassObj.isinstance (v2))
		return false;
	if (TupleObj.typecheck (v1)) {
		REFPTR *lst = TupleObj.cast (v1)->data;
		for (int i = 0; i < TupleObj.cast (v1)->len; i++)
			if (lst [i].o == v2 || DynClassObj.cast (v2)->isparentclass (lst [i].o))
				return true;
		return false;
	}
	return DynClassObj.cast (v2)->isparentclass (v1);
}


static bool obj_compare_op (REFPTR v1, REFPTR v2, unsigned int arg)
{
	/* XXX: broken? Please test for overloaded classes */
	switch (arg) {
#define		COMPARE2(op1, op2, cfunc) return 0 op2 v2->cfunc (v1.o);
#define		COMPARE(op, cfunc) COMPARE2 (op, op, cfunc)
		case 2: COMPARE2(==, !=, cmp_EQ)	// cmp_EQ != 0
		case 6: return  v1->contains (v2.o);
		case 7: return !v1->contains (v2.o);
		case 3: COMPARE2(!=, ==, cmp_EQ)	// cmp_EQ == 0
		case 0: COMPARE(>,  cmp_GEN)
		case 1: COMPARE(>=, cmp_GEN)
		case 4: COMPARE(<,  cmp_GEN)
		case 5: COMPARE(<=, cmp_GEN)
		case 8: return v1.o == v2.o;
		case 9: return v1.o != v2.o;
		case 10: return compare_exceptions (v1.o, v2.o);
		case 11: /* isinstance operator */
			if (DynClassObj.isinstance (v1.o)) {
			 	if (DynInstanceObj.isinstance (v2.o)) {
					__object__ *cls = v2.as_inst->__class__.o;
					return cls == v1.o;
				} else if (NamedListObj.isinstance (v2.o)) {
					__object__ *cls = NamedListObj.cast (v2.o)->__class__.o;
					return cls == v1.o;
				}
			}
			return false;
		case 12: /* istype operator */
			return v1.o == v2->TypeOf ();
		case 13:
			return v1.o != v2->TypeOf ();
#ifndef	OPTIMIZEVM
		default: pprint ("Comparison not implemented ", arg, NL); exit (0);
#endif
	}
}

static inline bool compare_op (REFPTR v1, REFPTR v2, unsigned int arg)
{
	if (IntObj.isinstance (v1.o) && IntObj.isinstance (v2.o) && arg < 6) {
		register int i2 = v2.as_int->i, i1 = v1.as_int->i;
		switch (arg) {
			 case 0: return i2 <  i1;
			ncase 1: return i2 <= i1;
			ncase 2: return i2 == i1;
			ncase 3: return i2 != i1;
			ncase 4: return i2 >  i1;
			ncase 5: return i2 >= i1;
		}
	}
	return obj_compare_op (v1, v2, arg);
}

/* -----------------* misc *-------------------- */

PyGeneratorObj.~PyGeneratorObj ()
{
	vm->release_ctx ();
}

slow void *get_boot_ctx ()
{
	return boot_ctx;
}

static	REFPTR except0 [3];

///////////////////////// cold stuff which has to access statics //////////////////////

#include "pymodules.c+.h"
#include "pythreads.c+.h"

slow void ContextSwitchObj.print () coldfunc
{
	if (vm)
		print_out (STRL (COLS"<*context switch of "COLE), vm->FUNC.o, STRL (COLS" *>"COLE));
}

modsection __object__ *globals_builtin ()
{
	return pvm->globals;
}

__object__ *vm_context.FastToLocals ()
{
	if (FUNC.as_func->LOCALS.o != &None || FUNC.as_func->theglobal)
		return FUNC.as_func->LOCALS.o;
	if (FUNC.as_func->LOCALS.o == &None)
		return FUNC.as_func->FastToLocals (this);
	RaiseNotImplemented ("FastToLocals ()");
}

__object__ *locals_builtin ()
{
	return pvm->FastToLocals ();
}

modsection __object__ *fLOAD_NAME (StringObj *S) coldfunc;
modsection __object__ *fLOAD_NAME (StringObj *S)
{
	__object__ *var = 0;
	if (pvm->FUNC.as_func->LOCALS.o != &None)
		var = pvm->FUNC.as_func->LOCALS.as_dict->xgetitem_str (S);
	if_unlikely (!var) {
		var = globals->xgetitem_str (S);
		if (!var) {
			var = __builtins__.as_ns->getattr (S);
			if_unlikely (!var) RaiseNameError (S);
		}
	}
	return var;
}

/*
 * returns the Module of the current running context; from this we can
 * find the absolute path to the pyc file that contains the current running
 * code and this can be used by import to locate submodules in packages.
 */
__object__ *get_current_module () coldfunc;
__object__ *get_current_module ()
{
	if_unlikely (!pvm->caller || pvm->caller->get_code ()->filename.o == &None)
		return &None;
	return pvm->caller->get_code ()->module.o;
}

#ifndef OPTIMIZEVM
#ifdef STACKPRINT
void printstack ()
{
	pvm->S.print_stack_all ();
}
#endif
#endif
#if 1

void whereami ()
{
	print_whereami (pvm);
}
#endif

void flushPC ()
{
#ifdef DIRECT_THREADING
	pvm->WPC = WPC;
#endif
}

/*
 * Faryield hack needs these (XXXX: fix PROFILER for it)
 */
void do_far_yield (vm_context *acc)
{
	SAVE_CONTEXT
	pvm = acc;
	SET_CONTEXT
	SKIPONE		// pop_top
	SKIPCMD		// load_const
	SKIPONE		// return_value
	(void) STK.xpop ();
}

void switch_caller (vm_context *cc)
{
	SAVE_CONTEXT
	pvm = cc;
	SET_CONTEXT
}

vm_context *get_pvm ()
{
	return pvm;
}

#ifdef FEATURE_SETITER
/*
 * set_iter
 */
void set_iter (__object__ *o)
{
	int i;
	for (i = STK.STACKTOP - STK.STACK - 2; i >=0; i--)
		if (STK.STACK [i]->vf_flags & VF_SETITER) {
			STK.STACK [i]->setiter (o);
			return;
		}
	RaiseNotImplemented ("No SETITER");
}
#endif

//////////////////////////// initialization /////////////////////////////////

static block_data Preempt_ExceptionBlock, Boot_ExceptionBlock;

const void *boot1, *pree;

static inline void init_prectx () coldfunc;

static inline void init_prectx ()
{
	/*
	 * boot_ctx and preempt_ctx
	 *
	 * these are special vm contexts which have only one opcode, END_VM.
	 * END_VM does not advance the program counter and this is always fixed.
	 *
	 * preempt_ctx has a pseudo exception catching block, it catches all
	 * exceptions and points to END_VM as the handler address.
	 *
	 * boot_ctx on the other hand, has a pseudo exception handling block.
	 * if somebody tries to get sys.exc_info and there is no inner active
	 * exception handler, boot_ctx will give "something".
	 * No exceptions must escape down to boot_ctx.
	 */
static	char XXX [16];
static	vm_context VMC, BOOT;

	boot1 = boot_ctx = &BOOT;
	pree = preempt_ctx = &VMC;
	boot_ctx->caller = preempt_ctx->caller = 0;
	preempt_ctx->FUNC.ctor (&None);
	boot_ctx->FUNC.ctor (&None);
	preempt_ctx->S.ctor ((REFPTR*) XXX, 4);
	boot_ctx->S.ctor ((REFPTR*) XXX, 4);

#ifdef	DIRECT_THREADING
static	void *code;
	code = XXLATE (END_VM);
	Boot_ExceptionBlock.addr = Preempt_ExceptionBlock.addr =
	preempt_ctx->WPC = boot_ctx->WPC = &code;
#else
static	char code [] = { END_VM, 0, 0 };
	Boot_ExceptionBlock.addr = Preempt_ExceptionBlock.addr =
	preempt_ctx->bcd = preempt_ctx->fbcd = 
	boot_ctx->bcd = boot_ctx->fbcd = code;
#endif

	Preempt_ExceptionBlock.stacktop = preempt_ctx->S.STACKTOP;
	Boot_ExceptionBlock.stacktop = except0;
	Preempt_ExceptionBlock.setup_type = TYPEEXC;
	Boot_ExceptionBlock.setup_type = TYPE_HB;
	preempt_ctx->LOOPS = &Preempt_ExceptionBlock;
	boot_ctx->LOOPS = &Boot_ExceptionBlock;
	boot_ctx->LTOS = preempt_ctx->LTOS = 1;
}

static slowcold class InitVM : InitObj {
	int priority = INIT_VM;
	void todo ()
	{
#ifdef	DIRECT_THREADING
		boot_pyvm (0);
#endif
		init_prectx ();
		Graveyard.ctor (new ListObj);
		except0 [0].ctor ();
		except0 [1].ctor ();
		except0 [2].ctor ();
	}
};

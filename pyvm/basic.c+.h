/*
 *  Basic routines for the dynamic library
 * 
 *  Copyright (c) 2006 Stelios Xanthakis
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) version 3 of the License.
 */

/* not exceptional exceptions */

Interrupt StopIteration (STOP_ITERATION);
static Interrupt _ReRaise (RE_RAISE), DoContext (CONTEXT_SWITCH);

__object__ *RaiseStopIteration ()
{
	throw &StopIteration;
}

__object__ *ReRaise ()
{
	throw &_ReRaise;
}

__object__ *RaiseContext ()
{
	throw &DoContext;
}

///////////////////////////////////////////////////////////////////////

int __object__.__find_in (REFPTR arr[], int l)
{
	for (int i = 0; i < l; i++)
		if (cmp_EQ (arr [i].o))
			return i;
	return -1;
}

__object__ *__object__.get_type_method (__object__ *k)
{
	__object__ *o;

	if_likely ((o = type_methods->D.hothit (k)) != 0)
		return new DynMethodObj (this, o);
	if_likely ((o = type_methods->xgetitem_str (k)) != 0)
		return new DynMethodObj (this, o);
	if (k == Interns.__class__)
		return (__object__*)(void*) &type;
	if (k == Interns.__dict__)
		return (__object__*) type_methods;
	if ((o = NOMETHODS.xgetitem_str (k)) != 0)
		return new DynMethodObj (this, o);

	return 0;
}

__object__ *__object__.getattr (__object__ *k)
{
	return get_type_method (k) ?: RaiseNoAttribute (k);
}

bool __object__.hasattr (__object__ *k)
{
	/* Not very efficient but we avoid big code duplication.
	 * This is not used for instance/class objects. They override
	 * hasattr. This is the default case when we want to check
	 * hasattr of other objects, for example hasattr (func, func_code)
	 */
	if (type_methods->xgetitem_str (k))
		return true;
	try k = getattr (k);
	else k = 0;
	if (k) {
		REFPTR F = k;
		return true;
	}
	return false;
}

/* ----* containers *---- */

__container__ *GCFirst;
REFPTR _GC_LIST;

static inline void __container__.__add_to_GC ()
{
	if_likely ((next = GCFirst) != 0) GCFirst->prev = this;
	GCFirst = this;
	prev = 0;
}

__container__.__container__ ()
{
	__object__.ctor ();
	__add_to_GC ();
	sticky = 0;
}

static inline void __container__.__remove_from_GC ()
{
	if (next) next->prev = prev;
	if_likely (prev != 0) prev->next = next;
	else GCFirst = next;
}

__container__.~__container__ ()
{
	__remove_from_GC ();
}

/* ----* permanent *---- */

__permanent__.__permanent__ ()
{
	inf ();
}

/* -----* None *----- */

NoneObj None;

long NoneObj.hash ()
{
	return 0;
}

/* -----* Type Object *----- */
TypeObj IntTypeObj	ctor (IntObj._v_p_t_r_);
TypeObj BugTypeObj	ctor (0);
TypeObj NoneTypeObj	ctor (NoneObj._v_p_t_r_);
TypeObj ListTypeObj	ctor (ListObj._v_p_t_r_);
TypeObj DictTypeObj	ctor (DictObj._v_p_t_r_);
TypeObj TypeTypeObj	ctor (TypeObj._v_p_t_r_);
TypeObj BoolTypeObj	ctor (BoolObj._v_p_t_r_);
TypeObj FloatTypeObj	ctor (FloatObj._v_p_t_r_);
TypeObj TupleTypeObj	ctor (TupleObj._v_p_t_r_);
TypeObj ClassTypeObj	ctor (DynClassObj._v_p_t_r_);
TypeObj BoundTypeObj	ctor (DynMethodObj._v_p_t_r_);
TypeObj PyCodeTypeObj	ctor (PyCodeObj._v_p_t_r_);
TypeObj PyFuncTypeObj	ctor (PyFuncObj._v_p_t_r_);
TypeObj StringTypeObj	ctor (StringObj._v_p_t_r_);
TypeObj InstanceTypeObj	ctor (DynInstanceObj._v_p_t_r_);
TypeObj NamedListTypeObj ctor (NamedListObj._v_p_t_r_);
TypeObj IteratorTypeObj	ctor (iteratorBase._v_p_t_r_);
TypeObj NamespaceTypeObj ctor (NamespaceObj._v_p_t_r_);
TypeObj BuiltinFuncTypeObj ctor (BuiltinCallableBase._v_p_t_r_);

/* XXXX - pyvm */

TypeObj.TypeObj (const void *tp, method_proxy *p)
{
	__permanent__.ctor ();
	meth = p;
	memcpy ((void*)&typeptr, &tp, sizeof (void*));
}

void TypeObj.call (REFPTR retval, REFPTR argv[], int argc)
{
	retval = TYPE2VPTR (typeptr)->type_call (argv + 1, argc);
}

/* ----* iteratorBase basics *----- */

iteratorBase.iteratorBase (__object__ *o)
{
	__container__.ctor ();
	obj.ctor (o);
}

PyFuncObj *listFunc;

__object__ *iteratorBase.to_list ()
{
	int j = len ();
	if (j == -1) {
		/*** Hybrid. If xnext returns a context do it with
		 *** the listFunc function in bytecode.
		 ***/
		/*** XXX: BUG: if generator is empty, it's borken */
		ListObj *L = new ListObj __sizector (8);
		volatile bool needbc = false;
		try (Interrupt *I) {
			__object__ *o = xnext ();
			if (o == &CtxSw) {
		 		L->append (preempt_pyvm (CtxSw.vm));
				needbc = true;
			} else {
				L->append (o);
				for (;;) L->append (xnext ());
			}
		} else catch_stop_iteration (I);
		if (!needbc) 
			return L;
		REFPTR argv [] = { &None, this, L };
		(*listFunc).call (argv [0], argv, 2);
		preempt_pyvm (CtxSw.vm);
		return argv [2].Dpreserve ();
	}
	ListObj *L = new ListObj __sizector (j);
	for (int i = 0; i < j; i++)
		L->__inititem (i, xnext ());
	L->len = j;
	return L;
}

/* ----------* sequence basics *---------- */

int lenseqObj.abs_index (int i)
{
	if (i < 0) {
		if_unlikely ((i += (*this).len ()) < 0) RaiseListIndexOutOfRange ();
	} else if_unlikely (i >= (*this).len ()) RaiseListIndexOutOfRange ();
	return i;
}

__object__ *lenseqObj.xgetitem (int i)
{
	return __xgetitem__ (abs_index (i));
}

__object__ *lenseqObj.xgetitem (__object__ *o)
{
	return xgetitem (IntObj.fcheckedcast (o)->i);
}

void mutableseqObj.xsetitem (int i, __object__ *o)
{
	__xsetitem__ (abs_index (i), o);
}

void mutableseqObj.xsetitem (__object__ *o, __object__ *d)
{
	xsetitem (IntObj.fcheckedcast (o)->i, d);
}

void mutableseqObj.xdelitem (int i)
{
	__xdelitem__ (abs_index (i));
}

void mutableseqObj.xdelitem (__object__ *o)
{
	xdelitem (IntObj.fcheckedcast (o)->i);
}

__object__ *lenseqObj.xgetslice (int from, int to)
{
	int start = absl_index (from), end = absl_index (to);
	if ((end -= start) < 0)
		end = 0;
	return __xgetslice__ (start, end);
}

void mutableseqObj.xdelslice (int from, int to)
{
	int start = absl_index (from), end = absl_index (to);
	if ((end -= start) < 0)
		end = 0;
	__xdelslice__ (start, end);
}

void mutableseqObj.xsetslice (int from, int to, __object__ *o)
{
	int start = absl_index (from), end = absl_index (to);
	if ((end -= start) < 0)
		end = 0;
	if_likely (o != this)
		if_likely (o->vf_flags | VF_ISEQ)
			(*this).__xsetslice__ (start, end, o);
		else if (o->vf_flags | VF_ITER) {
			REFPTR x = ((iteratorBase*) o)->to_list ();
			(*this).__xsetslice__ (start, end, x.o);
		} else {
			REFPTR x = ((iteratorBase *) o->iter ())->to_list ();
			(*this).__xsetslice__ (start, end, x.o);
		}
	else {
		REFPTR dup = (*(ListObj*) o).__xgetslice__ (0, ((ListObj*) o)->len);
		(*this).__xsetslice__ (start, end, dup.o);
	}
}

int container_sequence.cmp_GEN_same (__object__ *o)
{
	container_sequence *s = (container_sequence*) o;
	return len - s->len ?: s - this;
}

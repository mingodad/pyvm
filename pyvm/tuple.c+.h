/*
 *  Implementation of dynamic tuple objects
 * 
 *  Copyright (c) 2006 Stelios Xanthakis
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) version 3 of the License.
 */

/* >>>>>>>>>>>>>> iterator <<<<<<<<<<<<<< */

static class TupleIterObj : iteratorBase, seg_allocd
{
	/* Because tuple is immutable to untrusted users,
	 * we can grab len/data and iterate over them
	 */
	int i, len;
	REFPTR *tupledata;
	int len ()	{ return len; }
   public:
	TupleIterObj (Tuplen *x);
	__object__ *xnext ();
};

static void TupleIterObj.bctor (__object__ *o)
{
	iteratorBase.ctor (o);
	i = 0;
}

TupleIterObj.TupleIterObj (Tuplen *x)
{
	bctor (x);
	len = x->len;
	tupledata = x->data;
}

__object__ *TupleIterObj.xnext ()
{
	return unlikely (i == len) ? RaiseStopIteration () : tupledata [i++].o;
}

/* ---------* *--------- */

int Array_Base.cmp_GEN_same (__object__ *T)
{
	TupleObj *X = TupleObj.cast (T);
//	_CLASS_ *X = _CLASS_.cast (T);
	int i, L = min (len, X->len), j;
	REFPTR *data = data, *data2 = X->data;
	for (i = 0; i < L; i++)
		if ((j = data [i]->cmp_GEN (data2 [i].o)))
			return j;
	return len - X->len;
}

bool TupleObj.contains (__object__ *o)
{
	return o->__find_in (data, len) != -1;
}

ListObj *TupleObj.to_list ()
{
	return new ListObj refctor (data, len);
}

///////////////////////////////////////

/////////////////////////////////////////

Array_Base.Array_Base (__object__ *o [...])
{
	__container__.ctor ();
	data = (REFPTR*) seg_alloc (nz (len = oc) * sizeof *data);
	for (int i = 0; i < oc; i++)
		data [i].ctor (ov [i]);
}


void Array_Base.refctor (REFPTR o [...])
{
	__container__.ctor ();
	data = (REFPTR*) seg_alloc (nz (len = oc) * sizeof *data);
	memcpy (data, ov, oc * sizeof *data);
	while (oc)
		data [--oc].incref ();
}

void Array_Base.mvrefarray (REFPTR o [...])
{
	__container__.ctor ();
	data = (REFPTR*) seg_alloc (nz (len = oc) * sizeof *data);
	for (int i = 0; i < oc; i++)
		data [i].__copyctor (ov [i].preserve ());
}

void Array_Base.mvrefctor (REFPTR *arr, int l)
{
	__container__.ctor ();
	data = arr;
	len = l;
}

void Array_Base.__sizector (int s)
{
	__container__.ctor ();
	data = (REFPTR*) seg_alloc ((len = s) * sizeof *data);
}

void Array_Base.concat_ctor (TupleObj T1, TupleObj T2) noinline
{
	len = T1.len + T2.len;
	REFPTR *data = data, *d1;

	int i = 0, j = T1.len;
	d1 = T1.data;
	while (j--)
		data [i++].ctor (d1++->o);
	j = T2.len;
	d1 = T2.data;
	while (j--)
		data [i++].ctor (d1++->o);
}

void Tuplen.concat_ctor (TupleObj T1, TupleObj T2)
{
	__sizector (T1.len + T2.len);
	Array_Base.concat_ctor (T1, T2);
}

void Tuplen.NoneCtor (int s)
{
	/* This is a special constructor which should be used if
	 * there is the possibility that an exception will occur while
	 * initializing the tuple with __xinititem__(). We put None in
	 * all the slots so a destructor on a partly initialized tuple
	 * will decref real objects
	 */
#ifdef	TUPLEVIEW
	tview = 0;
#endif
	__container__.ctor ();
	data = (REFPTR*) seg_alloc ((len = s) * sizeof *data);
	for (int i = 0; i < s; i++)
		data [i].ctor ();
}

__object__ *Tuplen.iter ()
{
	return new TupleIterObj (this);
}

long Tuplen.hash ()
{
	/* Same as python */
	register long x, y;
	register int len = len;
	register REFPTR *data = data;
	register long mult = 1000003L;
	x = 0x345678L;
	while (--len >= 0) {
		y = data++->o->hash ();
		x = (x ^ y) * mult;
		mult += 82520L + len + len;
	}
	x += 97531L;
	return x;
}

void TupleObj.unpack (REFPTR x[])
{
	unsigned int i = len;
	int j = 0;
	REFPTR *data = data;
	while (i--)
		x [j++] += data [i].o;
	x [j] = data [0].o;
}

void TupleObj.unpack2 (REFPTR x[])
{
	x [1] += data [0].o;
	x [0] = data [1].o;
}

int TupleObj.index (__object__ *k, int from, int to)
{
	/*** XXX in python this is supposed to raise ValueError? ***/
	from = absl_index (from);
	to = absl_index (to);
	if_likely (from < to) {
		int i = k->__find_in (data + from, to - from);
		return i == -1 ? -1 : from + i;
	}
	return -1;
}

int Tuplen.__find (__object__ *o)
{
	int i, len = len;
	REFPTR *data = data;

	for (i = 0; i < len; i++)
		if (data [i].o == o)
			return i;
	return -1;
}

#ifdef TUPLEVIEW
void Tuplen.viewctor (Tuplen *R, REFPTR *d, int l)
{
	__container__.ctor ();
	(*(REFPTR*)&tview).ctor (R);
	data = d;
	len = l;
}
#endif

__object__ *Tuplen.__xgetslice__ (int start, int length)
{
#ifdef	TUPLEVIEW
	return new Tuplen viewctor (this, &data [start], length);
#else
	return newTuple_ref (&data [start], length);
#endif
}

__object__ *Tuplen.binary_add (__object__ *t)
{
	Tuplen *T = Tuplen.checkedcast (t);
	if_unlikely (!len && !T->len)
		return NILTuple;
	return new Tuplen concat_ctor (this, T);
}

Tuplen.~Tuplen ()
{
#ifdef	DEBUG_RELEASE
	print ("freeing:", (__object__*) this, NL);
#endif

#ifdef	TUPLEVIEW
	if_unlikely ((int)tview) {
		(*(REFPTR*)&tview).dtor ();
		return;
	}
#endif

	REFPTR *data = data;
	int len = len;
	for (int i = len - 1; i >= 0; i--)
		data [i].dtor ();
	seg_free (data, len * sizeof *data);
}

//////////////////////////////////////////////

TupleObj *newTuple_ref (REFPTR parg [...])
{
	return new Tuplen refctor (pargv, pargc);
}

TupleObj *newTuple_mvrefarray (REFPTR parg [...])
{
	return new Tuplen mvrefarray (pargv, pargc);
}

TupleObj *newTuple (__object__ *parg [...])
{
	return new Tuplen (pargv, pargc);
}

Tuplen *NILTuple;

static slow class InitNILTup : InitObj {
	int priority = INIT_STRUCTS;
	void todo ()
	{
		NILTuple = new Tuplen __sizector (1);
		NILTuple->len = 0;
		NILTuple->inf ();
		CtxSw.GC_ROOT ();
	}
};

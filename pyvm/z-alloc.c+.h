/*
 *  Incremental allocator
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) version 3 of the License.
 */

/*
 * single-list, never-free allocator
 * this is the fastest possible dynamic memory provider.
 * used for things like IntObj, FloatObj, StringObj, etc
 * aquired memory is never freed but it is re-used.
 * moreover, if the objects have a virtual table, it is
 * set once for all objects of a block when the block is
 * created -- if that makes any difference...
 */

/*
 * This source uses lots of lwc magic. See generated C
 * to figure out
 */

#define	NP 16384 / sizeof (_type)
class ALLOCATOR
{
typedef	_type = 0;	/* pure typedef */
	_type pool [NP];
modular	_type *ffree;
auto	ALLOCATOR ();
auto modular	_type* alloc ();
auto modular	void dealloc (_type);
static inline
auto modular	int is_allocated (void*);
};

int ALLOCATOR.is_allocated (void *v)
{
	_type *p;
	for (p = ffree; p; p = p->fnext)
		if (v == p)
			return 0;
	return 1;
}

ALLOCATOR.ALLOCATOR ()
{
	int i;
	for (i = 0; i < NP - 1; i++)
		pool [i].fnext = &pool [i + 1];
	pool [i].fnext = 0;
	ffree = &pool [0];
}

_type *ALLOCATOR.alloc ()
{
	if (unlikely (!ffree))
		new _CLASS_;
	return postfix (ffree, ffree = ffree->fnext);
}

void ALLOCATOR.dealloc (_type x)
{
	x.fnext = ffree;
	ffree = &x;
}

#define NEW_ALLOCATOR(TYPE) \
	static inline class TYPE ## _allocator : ALLOCATOR {\
		typedef	TYPE _type;\
	};\
	\
	TYPE *TYPE.operator new ()\
	{\
		return TYPE ## _allocator.alloc ();\
	}\
	\
	void TYPE.operator delete ()\
	{\
		TYPE ## _allocator.dealloc (this);\
	}

template class personal_allocator
{
	_CLASS_ *operator new ();
	void operator delete ();
};

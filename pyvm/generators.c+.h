/*
 *  Implementation of generator-functions object
 * 
 *  Copyright (c) 2006 Stelios Xanthakis
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) version 3 of the License.
 */

bool PyGeneratorObj.contains (__object__ *o)
{
	/* can RAISE_CONTEXT */
extern	PyFuncObj *in_generatorFunc;
	REFPTR argv [] = { &None, o, this };
	(*in_generatorFunc).call (argv [0], argv, 2);
	return preempt_pyvm (CtxSw.vm) == &TrueObj;
}

static __object__ *bltin_next (REFPTR argv[])
{
	return argv [0]->xnext ();
}

static __object__ *bltin_gnext (REFPTR argv[])
{
	/* As long as contexts are not objects we have this bug:
	 * the context returned by this function may overwrite the generator.
	 * When a generator is destroyed it clears its context. And then we crash.
	 * This can be done in:
	 *	def gen (x): yield x
	 *	gen(1).next()
	 * This is a hack to store the generator somewhere for the short window
	 * until the context has been executed.  We place it in the stack above the
	 * current top.  Of course that too can crash if the top is at the end of
	 * the stack.
	 * For now, pyvm allocates an extra stack slot anyway.
	 */
	STK.overtop (argv [0].o);

	return PyGeneratorObj.cast (argv [0].o)->xnext ();
}

bool vm_context.after_yield ()
{
#ifdef	DIRECT_THREADING
	return (AFTER_YIELD_VALUE (WPC));
#else
	return (AFTER_YIELD_VALUE (bcd));
#endif
}

/*
 * 'unyield()'.  unyield will make a generator re-yield the last value
 * that was yielded the next time it's called.
 * If no value has been yielded yet, it will start from the start.
 * If StopIteration has been reached, it will stay there.
 * XXX: what about raise?
 *
 * unyield is useful to peek into generators
 */
static inline void PyGeneratorObj.unyield ()
{
#ifdef	DIRECT_THREADING
	if (AFTER_YIELD_VALUE (vm->WPC)) {
		--vm->WPC;
#else
	if (AFTER_YIELD_VALUE (vm->bcd)) {
		--vm->bcd;
#endif
		vm->S.pre_push ();
	}
}

static __object__ *bltin_unyield (REFPTR argv[])
{
	PyGeneratorObj.cast (argv [0].o)->unyield ();
	return &None;
}

__object__ *PyGeneratorObj.getattr (__object__ *o)
{
	if (o == Interns.next)
		return new DynMethodObj (this, gnext_attr);
	if (o == Interns.unyield)
		return new DynMethodObj (this, unyield_attr);
	RaiseNoAttribute (o);
}

/*
 *  VM opcodes
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) version 3 of the License.
 */

#ifndef PY_OPCODES
#define PY_OPCODES
enum CO_FLAGS {
	CO_OPTIMIZED =		0x0001,
	CO_NEWLOCALS =		0x0002,
	CO_VARARGS =		0x0004,
	CO_VARKEYWORDS =	0x0008,
	CO_NESTED =		0x0010,
	CO_GENERATOR =		0x0020,
	CO_NOFREE =		0x0040,
	CO_GENERATOR_ALLOWED =  0x1000,
	CO_FUTURE_DIVISION =	0x2000
};

enum py_compare {
	PY_LT = 0,
	PY_LE = 1,
	PY_EQ = 2,
	PY_NE = 3,
	PY_GT = 4,
	PY_GE = 5
};

enum py_bytecode {
	STOP_CODE   = 0,
	POP_TOP     = 1,
	ROT_TWO     = 2,
	ROT_THREE   = 3,
	DUP_TOP     = 4,
	ROT_FOUR    = 5,
	RROT_THREE    = 6,		//> new
	DUPSTORE_FAST = 7,		//> new
	END_FINALLY2  = 8,		//> new
	END_VM        = 9,		//> new
	UNARY_POSITIVE   = 10,
	UNARY_NEGATIVE   = 11,
	UNARY_NOT        = 12,
	UNARY_CONVERT    = 13,
	RETURN_GENERATOR = 14,		//> new
	UNARY_INVERT     = 15,
	LIST_APPEND_JUMP = 16,		//> new
	ROT_SWAP1        = 17,		//> new
	LIST_APPEND      = 18,
	BINARY_POWER    = 19,
	BINARY_MULTIPLY = 20,
	BINARY_DIVIDE   = 21,
	BINARY_MODULO   = 22,
	BINARY_ADD      = 23,
	BINARY_SUBTRACT = 24,
	BINARY_SUBSCR   = 25,
	BINARY_FLOOR_DIVIDE  = 26,
	BINARY_TRUE_DIVIDE   = 27,
	INPLACE_FLOOR_DIVIDE = 28,
	INPLACE_TRUE_DIVIDE  = 29,
	SLICE_0 = 30,
	SLICE_1 = 31,
	SLICE_2 = 32,
	SLICE_3 = 33,
	UNPACK2 = 34,

	RETURN_MODULE    = 35,		//>new
	RETURN_VALUE_FNL = 36,		//> new
	NO_OP            = 37,		//> new

	STORE_SLICE_0 = 40,
	STORE_SLICE_1 = 41,
	STORE_SLICE_2 = 42,
	STORE_SLICE_3 = 43,

	DELETE_SLICE_0 = 50,
	DELETE_SLICE_1 = 51,
	DELETE_SLICE_2 = 52,
	DELETE_SLICE_3 = 53,

	INPLACE_ADD      = 55,
	INPLACE_SUBTRACT = 56,
	INPLACE_MULTIPLY = 57,
	INPLACE_DIVIDE   = 58,
	INPLACE_MODULO   = 59,
	STORE_SUBSCR     = 60,
	DELETE_SUBSCR    = 61,
	BINARY_LSHIFT    = 62,
	BINARY_RSHIFT    = 63,
	BINARY_AND       = 64,
	BINARY_XOR       = 65,
	BINARY_OR        = 66,
	INPLACE_POWER    = 67,
	GET_ITER         = 68,

	PRINT_EXPR       = 70,
	PRINT_ITEM       = 71,
	PRINT_NEWLINE    = 72,
	PRINT_ITEM_TO    = 73,
	PRINT_NEWLINE_TO = 74,
	INPLACE_LSHIFT   = 75,
	INPLACE_RSHIFT   = 76,
	INPLACE_AND      = 77,
	INPLACE_XOR      = 78,
	INPLACE_OR       = 79,
	BREAK_LOOP       = 80,
	WITH_CLEANUP = 81,	 //* available
	LOAD_LOCALS  = 82,
	RETURN_VALUE = 83,
	IMPORT_STAR  = 84,
	EXEC_STMT    = 85,
	YIELD_VALUE  = 86,
	POP_BLOCK    = 87,
	END_FINALLY  = 88,
	BUILD_CLASS  = 89,

	//// with argument ////
	STORE_NAME      = 90,  
	DELETE_NAME     = 91,
	UNPACK_SEQUENCE = 92, 
	FOR_ITER        = 93,
	STORE_ATTR      = 95,     
	DELETE_ATTR     = 96,   
	STORE_GLOBAL    = 97, 
	DELETE_GLOBAL   = 98,
	DUP_TOPX        = 99,    
	LOAD_CONST      = 100, 
	LOAD_NAME       = 101, 
	BUILD_TUPLE     = 102,
	BUILD_LIST      = 103,
	BUILD_MAP       = 104,
	LOAD_ATTR       = 105,
	COMPARE_OP      = 106,
	IMPORT_NAME     = 107,
	IMPORT_FROM     = 108,
	LOAD_CONST_PERMANENT = 109,	//> new
	JUMP_FORWARD  = 110, 
	JUMP_IF_FALSE = 111,
	JUMP_IF_TRUE  = 112, 
	JUMP_ABSOLUTE = 113,

	LOAD_GLOBAL   = 116, 

	CONTINUE_LOOP = 119, 
	SETUP_LOOP    = 120,  
	SETUP_EXCEPT  = 121,  
	SETUP_FINALLY = 122, 
	LOAD_FAST     = 124, 
	STORE_FAST    = 125, 
	DELETE_FAST   = 126, 

	RAISE_VARARGS = 130,
	CALL_FUNCTION = 131, 
	MAKE_FUNCTION = 132,  
	BUILD_SLICE   = 133,     
	MAKE_CLOSURE  = 134,
	LOAD_CLOSURE  = 135,
	LOAD_DEREF    = 136,
	STORE_DEREF   = 137,

	CALL_FUNCTION_VAR    = 140,
	CALL_FUNCTION_KW     = 141,
	CALL_FUNCTION_VAR_KW = 142, 
	EXTENDED_ARG = 143,
	// new ones
	SELF_ATTR = 144,
	SELF_ATTRW = 145,
	// not available to the pyc compiler
	STORELOAD_FAST			= 146,
	FORITER_STOREFAST		= 147,
	COMPAREOP_JUMPFALSE_POP2	= 148,
	JUMP_FORWARD_EXC		= 149,
	JUMP_ABSOLUTE_EXC		= 150,
	END_THREAD 			= 151,		// actually argless...
	BLOCK_THREAD			= 152,		// argless
	_IMPORT_NAME			= 153,		// argless

	JFP_COMPARE_EQ	= 154,
	JFP_COMPARE_LE	= 155,
	JFP_COMPARE_LEQ	= 156,
	JFP_COMPARE_GR	= 157,
	JFP_COMPARE_GRQ	= 158,
	JFP_COMPARE_IN	= 159,
	JFP_COMPARE_IS	= 160,
	JFP_COMPARE     = 161,
	LOAD_FAST_ATTR	= 162,
	GFOR_ITER	= 163,
};

static inline int has_name (int x)
{
	return __inset__ (x, STORE_NAME, DELETE_NAME, STORE_ATTR, DELETE_ATTR, STORE_GLOBAL, DELETE_GLOBAL, LOAD_NAME, LOAD_ATTR, IMPORT_NAME, IMPORT_FROM, LOAD_GLOBAL);
}

static inline int has_jrel (int x)
{
	return __inset__ (x, FOR_ITER, JUMP_FORWARD, JUMP_IF_FALSE, JUMP_IF_TRUE, SETUP_LOOP, SETUP_EXCEPT, SETUP_FINALLY, JUMP_FORWARD_EXC, FORITER_STOREFAST);
}

static inline int has_jabs (int x)
{
	return x == JUMP_ABSOLUTE || x == CONTINUE_LOOP || x == JUMP_ABSOLUTE_EXC;
}

static inline int has_local (int x)
{
	return (x >= LOAD_FAST && x <= DELETE_FAST) || x == STORELOAD_FAST;
}

static inline int has_free (int x)
{
	return x >= LOAD_CLOSURE && x <= STORE_DEREF;
}

static inline int has_const (int x)
{
	return x == LOAD_CONST;
}
#endif

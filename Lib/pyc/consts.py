##  Compiler constants
##
##  This program is free software; you can redistribute it and/or modify
##  it under the terms of the GNU General Public License as published by
##  the Free Software Foundation; either version 2 of the License, or
##  (at your option) version 3 of the License.

# operation flags
__all__ = [
'OP_ASSIGN', 'OP_DELETE', 'OP_APPLY',
'CO_OPTIMIZED', 'CO_NEWLOCALS', 'CO_VARARGS', 'CO_VARKEYWORDS', 'CO_NESTED', 'CO_GENERATOR',
'CO_GENERATOR_ALLOWED', 'CO_FUTURE_DIVISION',
'SCOPE_GLOBAL', 'SCOPE_FUNC', 'SCOPE_CLASS', 'SCOPE_GEXP',
]

OP_ASSIGN = 'OP_ASSIGN'
OP_DELETE = 'OP_DELETE'
OP_APPLY = 'OP_APPLY'

CO_OPTIMIZED = 0x0001
CO_NEWLOCALS = 0x0002
CO_VARARGS = 0x0004
CO_VARKEYWORDS = 0x0008
CO_NESTED = 0x0010
CO_GENERATOR = 0x0020
CO_GENERATOR_ALLOWED = 0x1000
CO_FUTURE_DIVISION = 0x2000

SCOPE_GLOBAL = 0
SCOPE_FUNC = 1
SCOPE_CLASS = 2
SCOPE_GEXP = 3

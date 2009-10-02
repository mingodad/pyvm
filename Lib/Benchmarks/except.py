#!/usr/local/bin/python
# http://www.bagley.org/~doug/shootout/

#DEJAVU
'''
{
'NAME':"Exceptions",
'DESC':"Exceptions from computer language shootout",
'GROUP':'real-bench',
'CMPOUT':1,
'ARGS':"100",
'BARGS':"250000"
}
'''

import sys

HI = 0
LO = 0


class Hi_exception:
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return `self.value`


class Lo_exception:
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return `self.value`


def some_function(num):
    try:
        hi_function(num)
    except:
        raise "We shouldn't get here (%s)" % sys.exc_info()[0]


def hi_function(num):
    global HI
    try:
        lo_function(num)
    except Hi_exception, ex:
        HI += 1
        #print 'Hi_exception occurred, value:', ex.value


def lo_function(num):
    global LO
    try:
        blowup(num)
    except Lo_exception, ex:
        LO += 1
        #print 'Lo_exception occurred, value:', ex.value


def blowup(num): 
    raise (((num & 1) and Lo_exception) or Hi_exception)(num)

def main():
    global LO, HI
    NUM = int(sys.argv[1])
    if NUM < 1:
        NUM = 1
    for i in xrange(NUM-1,-1,-1):
        some_function(i)
    print "Exceptions: HI=%d / LO=%d" % (HI, LO)


main()

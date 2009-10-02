
#!/usr/bin/python -OO
# The Computer Language Shootout Benchmarks
# http://shootout.alioth.debian.org/
#
# contributed by Kevin Carson

import sys


#DEJAVU
'''
{
'NAME':"binary-tree",
'DESC':"Binary-tree from computer language shootout",
'GROUP':'real-bench',
'CMPOUT':1,
'ARGS':"3",
'BARGS':"10 9 8 7 7"
}
'''

class treeNode :
    def __init__(self, left, right, item) :
        self.left = left
        self.right = right
        self.item = item


    def ItemCheck(self) :
        if self.left == None :
            return self.item
        else :
            return self.item + self.left.ItemCheck() - self.right.ItemCheck()


def BottomUpTree(item, depth) :
    if depth > 0 :
        item_item = 2 * item
        depth -= 1
        return treeNode(
            BottomUpTree(item_item - 1, depth),
            BottomUpTree(item_item, depth),
            item
        )
    else :
        return treeNode(None, None, item)


def main(N) :

    minDepth = 4

    if (minDepth + 2) > N :
        maxDepth = minDepth + 2
    else :
        maxDepth = N

    stretchDepth = maxDepth + 1

    stretchTree = BottomUpTree(0, stretchDepth)
    print "stretch tree of depth %d\t  check: %d" \
        % (stretchDepth, stretchTree.ItemCheck())
    stretchTree = None

    longLivedTree = BottomUpTree(0, maxDepth)

    for depth in xrange(minDepth, maxDepth + 1, 2) :
        iterations = 2**(maxDepth - depth + minDepth)
        check = 0

        for i in xrange(1, iterations + 1) :
            tempTree = BottomUpTree(i, depth)
            check += tempTree.ItemCheck()

            tempTree = BottomUpTree(-i, depth)
            check += tempTree.ItemCheck()

        print "%d\t  trees of depth %d\t  check: %d" \
            % (iterations * 2, depth, check)

    print "long lived tree of depth %d\t  check: %d" \
        % (maxDepth, longLivedTree.ItemCheck())


for i in sys.argv [1:]:
    main (int (i))

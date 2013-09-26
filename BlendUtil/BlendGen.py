#!/usr/bin/env python3

from struct import pack as sPack, unpack as sUnpack, error as sError

print('hello')

class P:
    def __init__(self):
        self.l = []

    def append(self, s):
        self.l.append(s)
        
    def merge(self, otherP):
        self.l.extend(otherP.l)
    
    def getBytes(self):
        return b"".join(self.l)

def mkInt32(p, i):
    p.append(sPack('<i', i))

def mkMatrix4x4(p, m):
    assert len(m) == 16
    p.append(sPack('<16f', *m))

def mkLendel(p, data):
    p.append(sPack('<i%ds' % (len(data)), len(data), data))

def mkSect(p, name, data):
    p.append(sPack('<iii%ds%ds' % (len(name), len(data)),
        4+4+4+len(name)+len(data), len(name), len(data), name, data))

def mkNodeName(p, lName):
    pName = P()
    for n in lName:
        mkLendel(pName, n)
    mkSect(p, b"NODENAME", pName.getBytes())

def mkNodeChild(p, lChild):
    pChild = P()
    for c in lChild:
        mkInt32(pChild, c)
    mkSect(p, b"NODECHILD", pChild.getBytes())

def mkNodeMatrix(p, lMtx):
    pMtx = P()
    for m in lMtx:
        mkMatrix4x4(pMtx, m)
    mkSect(p, b"NODEMATRIX", pMtx.getBytes())
    
def run():
    p = P()
    mkNodeName(p, [b'Hello', b'Abcd', b'ExampleName'])
    mkNodeChild(p, [-1, 0, 1])
    mIdentity = [1.0, 0.0, 0.0, 0.0,
                 0.0, 1.0, 0.0, 0.0,
                 0.0, 0.0, 1.0, 0.0,
                 0.0, 0.0, 0.0, 1.0]
    mkNodeMatrix(p, [mIdentity, mIdentity, mIdentity])
    mkSect(p, b"HELLO", b"datadata")
    print(p.l)

if __name__ == '__main__':
    run()

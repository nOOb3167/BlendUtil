#!/usr/bin/env python3

from struct import pack as sPack, unpack as sUnpack, error as sError

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

def mkFloat(p, f):
    p.append(sPack('<f', f))

def mkMatrix4x4(p, m):
    assert len(m) == 16
    p.append(sPack('<16f', *m))

def mkLendel(p, data):
    p.append(sPack('<i%ds' % (len(data)), len(data), data))

def mkSect(p, name, data):
    p.append(sPack('<iii%ds%ds' % (len(name), len(data)),
        4+4+4+len(name)+len(data), len(name), len(data), name, data))

def mkLenDelSec(p, bSecName, lStr):
    pW = P()
    for n in lStr:
        mkLendel(pW, n)
    mkSect(p, bSecName, pW.getBytes())

def mkIntSec(p, bSecName, lInt):
    pW = P()
    for n in lInt:
        mkInt32(pW, n)
    mkSect(p, bSecName, pW.getBytes())

def mkListFloatSec(p, bSecName, llFloat):
    pW = P()
    for m in llFloat:
        pX = P()
        for n in m:
            mkFloat(pX, n)
        mkLendel(pW, pX.getBytes())
    mkSect(p, bSecName, pW.getBytes())

def mkMatrixSec(p, bSecName, lMtx):
    pW = P()
    for n in lMtx:
        mkMatrix4x4(pW, n)
    mkSect(p, bSecName, pW.getBytes())

def run():
    p = P()

    mIdentity = [1.0, 0.0, 0.0, 0.0,
                 0.0, 1.0, 0.0, 0.0,
                 0.0, 0.0, 1.0, 0.0,
                 0.0, 0.0, 0.0, 1.0]

    mkLenDelSec(p, b"NODENAME", [b'Hello', b'Abcd', b'ExampleName'])
    mkIntSec(p, b"NODECHILD", [-1, 0, 1])
    mkMatrixSec(p, b"NODEMATRIX", [mIdentity, mIdentity, mIdentity])

    mkIntSec(p, b"NODEMESH", [0, -1, -1])

    mkLenDelSec(p, b"BONENAME", [b'Hello'])
    mkIntSec(p, b"BONECHILD", [-1])
    mkMatrixSec(p, b"BONEMATRIX", [mIdentity])

    mkLenDelSec(p, b"MESHNAME", [b'Mesh0'])
    mkListFloatSec(p, b"MESHVERT",
        [[0.0, 0.0, 0.0,
         1.0, 0.0, 0.0,
         1.0, 1.0, 0.0,]])

    mkSect(p, b"HELLO", b"datadata")

    print(p.l)

    return p

if __name__ == '__main__':
    p = run()

    import sys
    if len(sys.argv) == 1:
        print("### NOT OUTPUTTING TO FILE ###")
    if len(sys.argv) == 2:
        fname = str(sys.argv[1])
        assert fname.endswith('.dat')
        with open(fname, 'wb') as f:
            f.write(p.getBytes())

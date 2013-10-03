#!/usr/bin/env python3

# Bones in blender are y-forward (From head to tail as in 3D display window)

def dbg():
    import pdb; pdb.set_trace()

try:
    import bpy
except ImportError:
    pass

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
    assert isinstance(i, int)
    p.append(sPack('<i', i))

def mkFloat(p, f):
    assert isinstance(f, float)
    p.append(sPack('<f', f))

def mkMatrix4x4(p, m):
    assert len(m) == 16 and all([isinstance(x, float) for x in m])
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

def mkListListPairIntFloatSec(p, bSecName, llpIF):
    pW = P()
    for meshID, m in enumerate(llpIF):
        for boneID, b in enumerate(m):
            pX = P()
            for pairIF in b:
                mkInt32(pX, pairIF[0])
                mkFloat(pX, pairIF[1])
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
    mkIntSec(p, b"NODEPARENT", [-1, 0, 1])
    mkMatrixSec(p, b"NODEMATRIX", [mIdentity, mIdentity, mIdentity])

    # FIXME: A particular Mesh should be assigned to only one node.
    #        BONEWEIGHT section is mapping '(Mesh, Bone) -> [(id, weight)]'.
    #        If Mesh 'sharing' between multiple nodes is enabled, with differing (id, weight) lists,
    #        an indexing pair such as '(Node /*Mesh implicit*/, Bone) -> [(id, weight)]' may be required.
    mkIntSec(p, b"NODEMESH", [0, -1, -1])

    mkLenDelSec(p, b"BONENAME", [b'Hello'])
    mkIntSec(p, b"BONEPARENT", [-1])
    mkMatrixSec(p, b"BONEMATRIX", [mIdentity])

    mkLenDelSec(p, b"MESHNAME", [b'Mesh0'])
    mkListFloatSec(p, b"MESHVERT",
        [[0.0, 0.0, 0.0,
         1.0, 0.0, 0.0,
         1.0, 1.0, 0.0,]])
    
    mkListListPairIntFloatSec(p, b"MESHBONEWEIGHT",
        [
            [ # Mesh 0
                [(0, 1.0), (2, 1.0)]
            ]
        ])
    
    mkMatrixSec(p, b"MESHROOTMATRIX", [mIdentity])
    
    mkIntSec(p, b"MESHBONECOUNT", [1])

    mkSect(p, b"HELLO", b"datadata")

    print(p.l)

    return p

def GetMeshArmature(oMesh):
    for m in oMesh.modifiers:
        if m.type == 'ARMATURE':
            assert m.object.type == 'ARMATURE'
            return m.object
    return None

def RecursiveParentRoot(rp):
    if not len(rp): return None
    return rp[-1]

def IsBoneHierarchySingleRoot(lBone):
    if not len(lBone): return True
    par = RecursiveParentRoot(lBone[0].parent_recursive)
    for b in lBone:
        if RecursiveParentRoot(b.parent_recursive) != par:
            return False
    return True

def GetBoneVertexGroupIdx(oMesh, bone):
    for g in oMesh.vertex_groups:
        if g.name == bone.name:
            return g.index
    return None
    
class BId:
    def __init__(self, oMesh, lBone, mapIdBone, mapBoneId, lVGIdx, mapVGIdxBoneId):
        self.oMesh = oMesh
        self.lBone = lBone
        self.mapIdBone = mapIdBone
        self.mapBoneId = mapBoneId
        self.lVGIdx = lVGIdx
        self.mapVGIdxBoneId = mapVGIdxBoneId

    @classmethod
    def MakeBoneId(klass, oMesh, lBone):
        """ Not super useful. Bone IDs gotten thus are local to mesh instead of global """
        mapIdBone, mapBoneId = BId.BoneGetMapId(lBone)
        
        lVGIdx = [GetBoneVertexGroupIdx(oMesh, x) for x in lBone]
        mapVGIdxBoneId = dict([(b, a) for a, b in enumerate(lVGIdx)])
        
        return BId(oMesh, lBone, mapBoneId, mapIdBone, lVGIdx, mapVGIdxBoneId)

    @classmethod
    def BoneGetMapId(klass, lBone):
        mapIdBone = dict(enumerate(lBone))
        mapBoneId = dict([(b, a) for a, b in mapIdBone.items()])
        
        return [mapIdBone, mapBoneId]
    
    @classmethod
    def BidGetIdParent(klass, lBid):
        lBone = [b for bid in lBid for b in bid.lBone]
        mapIdBone, mapBoneId = BId.BoneGetMapId(lBone)
        
        lIdParent = []
        
        for b in lBone:
            assert b in mapBoneId
            assert b.parent == None or b.parent in mapBoneId
            if b.parent == None:
                lIdParent.append(-1)
            else:
                lIdParent.append(mapBoneId[b.parent])
            
        return lIdParent
        
    
def GetWeights(bid):
    llIF = [[] for x in range(len(bid.lBone))]
    
    dMesh = bid.oMesh.data
    
    for vI, v in enumerate(dMesh.vertices):
        assert v.index == vI # Very surprising if not. Expecting the vertices array to be stored in sequential index order
        for g in v.groups:
            inflBoneId = bid.mapVGIdxBoneId[g.group]
            assert inflBoneId is not None
            llIF[inflBoneId].append([v.index, g.weight])

    return llIF

def GetVerts(bid):
    lVert = []

    dMesh = bid.oMesh.data
    
    for vI, v in enumerate(dMesh.vertices):
        assert v.index == vI
        lVert.append([v.co[0], v.co[1], v.co[2]])
    
    return lVert

def GetIndices(bid):
    lIdx = []
    
    dMesh = bid.oMesh.data
    
    # FIXME: Will be using tessfaces, force recalculation if dirty
    dMesh.update(calc_tessface=True)
    assert not dMesh.validate()
    
    for f in dMesh.tessfaces:
        assert len(f.vertices) == 3 or len(f.vertices) == 4
        if len(f.vertices) == 3:
            lIdx.extend([f.vertices[0], f.vertices[1], f.vertices[2]])
        if len(f.vertices) == 4:
            lIdx.extend([f.vertices[0], f.vertices[1], f.vertices[2]])
            lIdx.extend([f.vertices[2], f.vertices[3], f.vertices[0]])
            
    return lIdx

class AMesh:
    def __init__(self, bid, wts, vts, ics):
        self.bid = bid
        self.wts = wts
        self.vts = vts
        self.ics = ics

def ArmaMesh(oMesh):
    assert GetMeshArmature(oMesh) is not None
    oArm = GetMeshArmature(oMesh)
    dArm = oArm.data
    lBone = [x for x in dArm.bones]
    
    bid = BId.MakeBoneId(oMesh, lBone)
    
    wts = GetWeights(bid)
    vts = GetVerts(bid)
    ics = GetIndices(bid)
    
    #assert IsBoneHierarchySingleRoot(lBone)
    
    return AMesh(bid, wts, vts, ics)

def BlendMatToList(mat):
    l = [float(e) for vec in mat for e in vec]
    l = [(0.0 if abs(e) <= 0.0001 else e) for e in l]
    return l

def pm(m):
    for i in range(4):
        print(m[4*i+0], m[4*i+1], m[4*i+2], m[4*i+3])

def BlendRun():
    oMesh = [x for x in bpy.context.scene.objects if x.type == 'MESH']
    oArmaturedMesh = [x for x in oMesh if GetMeshArmature(x)]
    
    am = [ArmaMesh(m) for m in oArmaturedMesh]
    
    p = P()
    
    nodeName = [m.bid.oMesh.name for m in am]
    
    #FIXME: What should be:
    #       Export through all nodes, not just Armatured.
    #nodeParent = FIXME
    #nodeMatrix = [m.bid.oMesh.matrix_local for m in am]
    
    #FIXME: Instead:
    #       Export no hierarchy. World matrices and null parents.
    nodeParent = [-1 for m in am]
    nodeMatrix = [m.bid.oMesh.matrix_world for m in am]
    
    #FIXME: Every node is a mesh node (Traversing only ArmaMesh of oArmaturedMesh)
    nodeMesh = list(range(len(am)))
    
    boneName = [i.name for m in am for i in m.bid.lBone]
    boneParent = BId.BidGetIdParent([m.bid for m in am])
    boneMatrix = [i.matrix_local for m in am for i in m.bid.lBone]
    
    meshRootMatrix = [GetMeshArmature(m.bid.oMesh).matrix_world for m in am]
    meshBoneCount = [len(m.bid.lBone) for m in am]

if __name__ == '__main__':
    try:
        import bpy
        inBlend = True
    except ImportError:
        inBlend = False

    if inBlend:
        BlendRun()
    else:
        p = run()

        import sys
        if len(sys.argv) == 1:
            print("### NOT OUTPUTTING TO FILE ###")
        if len(sys.argv) == 2:
            fname = str(sys.argv[1])
            assert fname.endswith('.dat')
            with open(fname, 'wb') as f:
                f.write(p.getBytes())

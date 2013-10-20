#!/usr/bin/env python3

# Notepad++ Truncates Run Commands
# cmd /K ""C:\Users\Andrej\testM\Blender\blender-2.67b-windows32\blender.exe" -b "C:\Users\Andrej\Documents\Visual Studio 2012\Projects\BlendUtil\blendOneBone.blend" -P "$(FULL_CURRENT_PATH)" -- "tmpdata.dat" "

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

def mkListGenSec(p, bSecName, subSerializer, llSub):
    pW = P()
    for m in llSub:
        pX = P()
        for n in m:
            subSerializer(pX, n)
        mkLendel(pW, pX.getBytes())
    mkSect(p, bSecName, pW.getBytes())

def mkListFloatSec(p, bSecName, llFloat):
    return mkListGenSec(p, bSecName, mkFloat, llFloat)

def mkListIntSec(p, bSecName, llInt):
    return mkListGenSec(p, bSecName, mkInt32, llInt)

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
    
    mkListIntSec(p, b"MESHINDEX",
        [[0, 1, 2,
          3, 4, 5,
          6, 7, 8]])
    
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

def ListGetMapId(l):
    mapIdElt = dict(enumerate(l))
    mapEltId = dict([(b, a) for a, b in mapIdElt.items()])
    
    return [mapIdElt, mapEltId]

def ListGetIdParentBlenderSpecial(lObject): return ListGetIdParentBlenderSpecialCheck(lObject, lambda x: True)
def ListGetIdParentBlenderSpecialCheck(lObject, fChecker):
    """For Blender Objects (Used for Node, Bone)
         Assuming x in lObject:
         x.parent == None if no parent (Is a root)
                  else is another member of lObject
       Id is set to -1 in case of no parent"""
    mapIdElt, mapEltId = ListGetMapId(lObject)
    
    lIdParent = []
    
    for o in lObject:
        assert o in mapEltId
        assert o.parent == None or o.parent in mapEltId
        
        assert fChecker(o)
        
        if o.parent == None:
            lIdParent.append(-1)
        else:
            lIdParent.append(mapEltId[o.parent])
            
    return lIdParent
    
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
        return ListGetMapId(lBone)
    
    @classmethod
    def BidGetIdParent(klass, lBid):
        lBone = [b for bid in lBid for b in bid.lBone]
        # Had the "or x.parent_type == 'BONE'" check, but seems Bones do not have parent_type (Objects do)
        return ListGetIdParentBlenderSpecialCheck(lBone, lambda x: True if x.parent == None or True else False)

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

def GetVerts(bid): return dMeshGetVerts(bid.oMesh.data)
def dMeshGetVerts(dMesh):
    lVert = []
    
    for vI, v in enumerate(dMesh.vertices):
        assert v.index == vI
        lVert.extend([v.co[0], v.co[1], v.co[2]])
    
    return lVert

    
def GetIndices(bid): return dMeshGetIndices(bid.oMesh.data)
def dMeshGetIndices(dMesh):
    lIdx = []
    
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

class BaseMesh:
    @classmethod
    def NodeGetMapId(klass, lAllNode):
        """lAllNode: Combined MMesh, AMesh"""
        assert all([isinstance(x, MMesh) or isinstance(x, AMesh) for x in lAllNode])
        return ListGetMapId(lAllNode)
        
    @classmethod
    def NodeGetIdParent(klass, lAllNode):
        loMesh = [x.bid.oMesh for x in lAllNode]
        return ListGetIdParentBlenderSpecialCheck(loMesh, lambda x: True if x.parent == None or x.parent_type == 'OBJECT' else False)
    
class MMesh:
    def __init__(self, bid, vts, ics):
        self.bid = bid
        self.vts = vts
        self.ics = ics

def MeshMesh(oMesh):
    vts = dMeshGetVerts(oMesh.data)
    ics = dMeshGetIndices(oMesh.data)

    class DummyBid:
        def __init__(self, oMesh):
            self.oMesh = oMesh
    bid = DummyBid(oMesh)
    
    return MMesh(bid, vts, ics)

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

def _BlendMatToListUnused(mat):
    l = [float(e) for vec in mat for e in vec]
    l = [(0.0 if abs(e) <= 0.0001 else e) for e in l]
    return l

def BlendMatToListColumnMajor(mat):
    assert len(mat.col) == 4 and len(mat.row) == 4
    l = []
    for col in range(len(mat.col)):
        for row in range(len(mat.col)):
            l.append(mat[row][col])
    l = [(0.0 if abs(e) <= 0.0001 else e) for e in l]
    l = [float(e) for e in l]
    return l

def BlendMatToList(mat):
    return BlendMatToListColumnMajor(mat)

def BlendMatCheck():
    """Blender people are evil and change matrix indexing convention
       randomly and break every script in existence.
       Will attempt to make breakage attempts trip up checks and
       break in a controlled fashion instead.
       
       The indexing convention at the time of writing (Blender 2.67b) is
       Row major when converted to flat list like this:
         l = [elt for vec in mat for elt in vec]
       Mat[Row][Col] when indexed."""

    try:
        import mathutils
    except ImportError:
        assert 0
        
    from mathutils import Matrix, Vector
    
    def ZeroEq(a):
        epsilon = 0.001
        return True if abs(a) <= epsilon else False
    
    def MatrixEq(a, b):
        la, lb = [e for vec in a for e in vec], [e for vec in b for e in vec]
        assert len(a) == len(b)
        return True if all([ZeroEq(e[0] - e[1]) for e in zip(la, lb)]) else False
    
    def MakeZero():
        m = Matrix()
        m.identity()
        m[0][0], m[1][1], m[2][2], m[3][3] = 0.0, 0.0, 0.0, 0.0
        return m
    
    m = MakeZero()
    m.zero()
    assert MatrixEq(MakeZero(), m)
    
    m = MakeZero()
    assert all([ZeroEq(e) for vec in m for e in vec])
    
    m = MakeZero()
    m[1][0] = 42.0
    assert ZeroEq(m.row[1][0] - 42.0)
    
    m = MakeZero()
    m.row[1][0] = 42.0
    assert ZeroEq(m[1][0] - 42.0)
    
    m = MakeZero()
    m.col[2] = Vector([10.0, 11.0, 12.0, 13.0])
    l = [e for vec in m for e in vec]
    assert l[2] == 10.0 and l[6] == 11.0 and l[10] == 12.0 and l[14] == 13.0
    
    m = Matrix([[0.0, 4.0, 8.0, 12.0],
                [1.0, 5.0, 9.0, 13.0],
                [2.0, 6.0, 10.0, 14.0],
                [3.0, 7.0, 11.0, 15.0]])
    m = m.transposed()
    assert all([ZeroEq(e[0] - e[1]) for e in zip([e2 for vec in m for e2 in vec],
                                                 range(0, 16))])
    
    return True

def pm(m):
    for i in range(4):
        print(m[4*i+0], m[4*i+1], m[4*i+2], m[4*i+3])

def lConcat(la, lb):
    lc = la[:]; lc.extend(lb); return lc

def BytesFromStr(s):
    return bytes(s, encoding='UTF-8')
        
def BlendRun():
    assert BlendMatCheck()

    oMesh = [x for x in bpy.context.scene.objects if x.type == 'MESH']
    oArmaturedMesh = [x for x in oMesh if GetMeshArmature(x)]
    oMeshedMesh    = [x for x in oMesh if not GetMeshArmature(x)]
    assert len(set(lConcat(oMeshedMesh, oArmaturedMesh))) == len(oMeshedMesh) + len(oArmaturedMesh)
    
    am = [ArmaMesh(m) for m in oArmaturedMesh]
    mm = [MeshMesh(m) for m in oMeshedMesh]
    
    # Important: Concatenation order is AMesh, MMesh
    #              Full Bone information for the AMesh part
    #              No   Bone information for the MMesh part
    #            Ameshes get the 0-len(len(am)) IDs.
    #            Currently important at least in meshBoneCount etc.
    allM = lConcat(am, mm)
    
    nodeName = [m.bid.oMesh.name for m in allM]
    nodeParent = BaseMesh.NodeGetIdParent(allM)
    nodeMatrix = [m.bid.oMesh.matrix_local for m in allM]
            
    #FIXME: Every node is a mesh node.
    #       Now traversing MMesh and AMesh - Probably need to add
    #       traversal of regular Blender Objects besides meshes
    nodeMesh = list(range(len(allM)))
    
    boneName   = [i.name for m in am for i in m.bid.lBone]
    boneParent = BId.BidGetIdParent([m.bid for m in am])
    #FIXME/NOTE: Bone.matrix_local is in Armature space not local relative to parent bone
    #            Notice meshRootMatrix is filled with GetMeshArmature(m.bid.oMesh).matrix_world
    #            A Bone.matrix_local and an Armature.matrix_world can be combined for form Bone world space
    #            Since Bone.matrix_local is relative to same space for every Bone (The Armature space),
    #            can get true local by: (BoneParent.matrix_local)^-1 * BoneChild.matrix_local
    boneMatrix = [i.matrix_local for m in am for i in m.bid.lBone]
    
    #FIXME: Mesh name just using Node names
    meshName       = nodeName[:]
    meshBoneCount  = lConcat([len(m.bid.lBone) for m in am], [0     for m in mm])
    meshVert       = lConcat([m.vts for m in am],            [m.vts for m in mm])
    meshIndex      = lConcat([m.ics for m in am],            [m.ics for m in mm])
    meshRootMatrix = lConcat([GetMeshArmature(m.bid.oMesh).matrix_world for m in am],
                             [m.bid.oMesh.matrix_world for m in mm])
    
    meshBoneWeight = lConcat([m.wts for m in am],            [[]    for m in mm])
    
    p = P()
    
    mkLenDelSec(p, b"NODENAME", [BytesFromStr(i) for i in nodeName])
    mkIntSec(p, b"NODEPARENT", nodeParent)
    mkMatrixSec(p, b"NODEMATRIX", [BlendMatToList(m) for m in nodeMatrix])
    
    mkIntSec(p, b"NODEMESH", nodeMesh)
    
    mkLenDelSec(p, b"BONENAME", [BytesFromStr(i) for i in boneName])
    mkIntSec(p, b"BONEPARENT", boneParent)
    mkMatrixSec(p, b"BONEMATRIX", [BlendMatToList(m) for m in boneMatrix])
    
    mkLenDelSec(p, b"MESHNAME", [BytesFromStr(i) for i in meshName])
    mkIntSec(p, b"MESHBONECOUNT", meshBoneCount)
    mkListFloatSec(p, b"MESHVERT", meshVert)
    mkListIntSec(p, b"MESHINDEX", meshIndex)
    mkMatrixSec(p, b"MESHROOTMATRIX", [BlendMatToList(m) for m in meshRootMatrix])
    
    mkListListPairIntFloatSec(p, b"MESHBONEWEIGHT", meshBoneWeight)
    
    return p

if __name__ == '__main__':
    try:
        import bpy
        inBlend = True
    except ImportError:
        inBlend = False

    import sys
    import os

    if inBlend:
        p = BlendRun()
        
        # Paranoia length check
        assert len(sys.argv) == 7
        blendPath = str(sys.argv[2])
        outName   = str(sys.argv[6])
        assert blendPath.endswith(".blend") and os.path.exists(blendPath)
        assert outName.endswith('.dat')
        outPathFull = os.path.join(os.path.dirname(blendPath), outName)
        
        with open(outPathFull, 'wb') as f:
            f.write(p.getBytes())
    else:
        p = run()

        if len(sys.argv) == 1:
            print("### NOT OUTPUTTING TO FILE ###")
        if len(sys.argv) == 2:
            fname = str(sys.argv[1])
            assert fname.endswith('.dat')
            with open(fname, 'wb') as f:
                f.write(p.getBytes())

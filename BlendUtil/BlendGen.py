#!/usr/bin/env python3

# Notepad++ Truncates Run Commands
# cmd /K ""C:\Users\Andrej\testM\Blender\blender-2.67b-windows32\blender.exe" -b "C:\Users\Andrej\Documents\Visual Studio 2012\Projects\BlendUtil\blendOneBone.blend" -P "$(FULL_CURRENT_PATH)" -- "tmpdata.dat" "

# Bones in blender are y-forward (From head to tail as in 3D display window)

from pprint import pprint as pp
def pall(x):
    from pprint import pprint
    pprint(x.__dict__)
def dbg():
    import pdb; pdb.set_trace()

try:
    import bpy
except ImportError:
    pass

from struct import pack as sPack, unpack as sUnpack, error as sError
from collections import namedtuple

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

def mkPairIntSec(p, bSecName, lPairInt):
    pW = P()
    for pair in lPairInt:
        mkInt32(pW, pair[0])
        mkInt32(pW, pair[1])
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
    
def BlendMatToListColumnMajor(mat):
    assert len(mat.col) == 4 and len(mat.row) == 4
    l = []
    for col in range(len(mat.col)):
        for row in range(len(mat.row)):
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

def lFlatten(ll):
    return [e for l in ll for e in l]

def BytesFromStr(s):
    return bytes(s, encoding='UTF-8')

def lUniq(l, **kwargs):
    f = 'f' in kwargs and kwargs['f'] or (lambda x: x)
    
    r = []
    seen = set()
    for x in l:
        tmp = f(x)
        if tmp not in seen:
            r.append(x)
        seen.add(tmp)
    return r

def lUniqP(l, **kwargs):
    return len(l) == len(lUniq(l, **kwargs))

def lMap(*args, **kwargs):
    assert 'f' in kwargs
    return list(map(kwargs['f'], *args))

def lReduce(*args, **kwargs):
    assert 'f' in kwargs
    def fuckoff():
        global reduce
        try:
            from functools import reduce as fuckoff
            return fuckoff
        except ImportError:
            return reduce
    return list(fuckoff()(f, *args))

def lCountOne(l, e, **kwargs):
    f = 'f' in kwargs and kwargs['f'] or (lambda x: x)
    
    count = 0
    for x in enumerate(l):
        if f(x) == e:
            count += 1
    return count

def lCount(l, **kwargs):
    f = 'f' in kwargs and kwargs['f'] or (lambda x: x)

    seen = {}
    fs = [f(x) for x in l]
    for i, x in enumerate(l):
        if x not in seen:
            seen[fs[i]] = 0
        seen[fs[i]] += 1
    return [seen[fs[i]] for i in range(l)]

def lAppend(l, e):
    l2 = l[:]
    return lAppendI(l2, e)

def lAppendI(l, e):
    l.append(e)
    return l

def lAppendMultiI(lL, lV):
    for x in zip(lL, lV):
        lAppendI(x[0], x[1])

def lFilter(l, **kwargs):
    assert 'f' in kwargs
    return list(filter(kwargs['f'], l))

def lIndexNestedN(lplus, n):
    assert n
    nItem = []
    trail = []
    def help(node):
        if len(trail) == n:
            nItem.append(trail[:])
            return
        for i, l in enumerate(node):
            trail.append(i)
            help(l)
            trail.pop()
    help(lplus)
    return nItem

def lEmptyN(n):
    return [None for x in range(n)]

def lSequentialEquiv(l):
    return all(lMap(zip(range(len(l)),
                        l),
                    f=lambda x: x[0] == x[1]))

def lSequentialContain(l):
    return lSequentialEquiv(l.sorted())

def lUnzip(l):
    return zip(*l)

def dReversed(d):
    assert lUniqP(d.values())
    return {v : k for k, v in d.items()}

def SceneMeshSelectAll():
    return [x for x in bpy.context.scene.objects if x.type == 'MESH']

def MeshArmatureAll(oMesh):
    return [m.object for m in oMesh.modifiers if m.type == 'ARMATURE']

def dMeshGetVerts(dMesh):
    lVert = []
    
    for vI, v in enumerate(dMesh.vertices):
        assert v.index == vI
        lVert.extend([v.co[0], v.co[1], v.co[2]])
    
    return lVert
    
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
    
def GetWeights(oMesh, lMeshAllArmBoneId, lMeshAllArmBoneName):
    #FIXME: For the case of a mesh with no Bones
    
    def GetBoneVertexGroupIdx(oMesh, boneName):
        for g in oMesh.vertex_groups:
            if g.name == boneName:
                return g.index
        return None

    def MakeMapVGIdxBoneId(oMesh, lBoneId, lBoneName):
        lVGIdx = [GetBoneVertexGroupIdx(oMesh, name) for name in lBoneName]
        assert lUniqP([x for x in lVGIdx if x is not None])
        mapVGIdxBoneId = dict(zip(lVGIdx, lBoneId))
        if None in mapVGIdxBoneId: del mapVGIdxBoneId[None]
        return mapVGIdxBoneId
        
    llIF = [[] for x in range(len(oMesh.data.vertices))]
        
    mapVGIdxBoneId = MakeMapVGIdxBoneId(oMesh, lMeshAllArmBoneId, lMeshAllArmBoneName)
    
    assert lSequentialEquiv([v.index for v in oMesh.data.vertices])
    
    for i, v in enumerate(oMesh.data.vertices):
        for g in v.groups:
            inflBoneId = mapVGIdxBoneId[g.group]
            if inflBoneId is not None:
                llIF[i].append([inflBoneId, g.weight])
    
    return llIF

def Br3():
    class Data:
        for l in ''.split(): setattr(self, l, [])
            
    d = Data()
    
    TupMab  = namedtuple('Mab', ['M', 'A', 'B', 'oM', 'oA', 'oB', 'oPB'])
    TupMabM = namedtuple('MabM', ['M', 'oM'])
    TupMabA = namedtuple('MabA', ['A', 'oA'])
    TupMabB = namedtuple('MabB', ['A', 'B', 'oB', 'oPB'])

    TuptM = namedtuple('tM', ['id', 'M', 'oM'])
    TuptA = namedtuple('tA', ['id', 'A', 'oA'])
    TuptB = namedtuple('tB', ['id', 'A', 'B', 'oB', 'oPB'])
    
    TuptlMA = namedtuple('tlMA', ['idM', 'idA'])
    TuptlBA = namedtuple('tlBA', ['idB', 'idA'])
    
    TuptMParent = namedtuple('tMParent', ['id', 'idP'])
    TuptBParent = namedtuple('tBParent', ['id', 'idP'])
        
    lMab = []
    for m in SceneMeshSelectAll():
        for a in MeshArmatureAll(m):
            assert lUniqP(a.data.bones, f=lambda b: b.name)
            def _BlendPoseBoneToBone(pb):
                # Check PoseBone belongs to currently processed armature
                assert pb.id_data and pb.id_data.type == 'ARMATURE' and pb.id_data.name == a.name
                # Match PoseBone with associated Bone by name
                bone = lFilter(a.data.bones, f=lambda x: x.name == pb.name); assert len(bone) == 1
                return bone[0]
            for pb in a.pose.bones:
                lAppendI(lMab, TupMab(m.name, a.name, pb.name, m, a, _BlendPoseBoneToBone(pb), pb))
    
    def MabTrimM(): return lMap(lUniq(lMab, f=lambda x: x.M), f=lambda x: TupMabM(x.M, x.oM))
    def MabTrimA(): return lMap(lUniq(lMab, f=lambda x: x.A), f=lambda x: TupMabA(x.A, x.oA))
    def MabTrimB(): return lMap(lUniq(lMab, f=lambda x: (x.A, x.B)), f=lambda x: TupMabB(x.A, x.B, x.oB, x.oPB))
    lMabM = MabTrimM()
    lMabA = MabTrimA()
    lMabB = MabTrimB()
    
    tM = [TuptM(i, m.M, m.oM)      for i, m in enumerate(lMabM)]
    tA = [TuptA(i, m.A, m.oA)      for i, m in enumerate(lMabA)]
    tB = [TuptB(i, m.A, m.B, m.oB, m.oPB) for i, m in enumerate(lMabB)]

    def tinorder(t, attrPlus):
        lAttr = attrPlus if isinstance(attrPlus, list) else [attrPlus]
        for i, m in enumerate(t):
            compKey = tuple([getattr(m, attr) for attr in lAttr])
            if i > 0: assert old < compKey
            old = compKey
            yield m
    
    def GenQuery_tbl_attr_val(tbl, attr, val):
        return GenQueryComposite_tbl_lAttr_lVal(tbl, [attr], [val])
    def GenQueryComposite_tbl_lAttr_lVal(tbl, lAttr, lVal):
        lst = GenQueryCompositeL_tbl_lAttr_lVal(tbl, lAttr, lVal); assert len(lst); return lst[0]
    def GenQueryCompositeL_tbl_lAttr_lVal(tbl, lAttr, lVal):
        return [m for m in tbl if [getattr(m, attr) for attr in lAttr] == lVal]
        
    def GenUniq_tbl_attr(tbl, attr):
        return lUniq(tbl, lambda m: getattr(m, attr))        
    def GenUniqComposite_tbl_lAttr(tbl, lAttr):
        return lUniq(tbl, f=lambda m: tuple([getattr(m, attr) for attr in lAttr]))
    def GenSortComposite_tbl_lAttr(tbl, lAttr):
        return sorted(tbl, key=lambda m: tuple([getattr(m, attr) for attr in lAttr]))
    
    def Query_tM_M(M): return GenQuery_tbl_attr_val(tM, 'M', M)
    def Query_tA_id(id): return GenQuery_tbl_attr_val(tA, 'id', id)
    def Query_tA_A(A): return GenQuery_tbl_attr_val(tA, 'A', A)
    def Query_tB_id(id): return GenQuery_tbl_attr_val(tB, 'id', id)
    def Query_tB_B(B): return GenQuery_tbl_attr_val(tB, 'B', B)
    def Query_tB_AB(AB): return GenQueryComposite_tbl_lAttr_lVal(tB, ['A', 'B'], AB)
    def QueryL_tlMA_idM(idM): return GenQueryCompositeL_tbl_lAttr_lVal(tlMA, ['idM'], [idM])
    def QueryL_tlBA_idA(idA): return GenQueryCompositeL_tbl_lAttr_lVal(tlBA, ['idA'], [idA])
    def Query_tlBA_idB(idB): return GenQueryComposite_tbl_lAttr_lVal(tlBA, ['idB'], [idB])
    def Query_tAnim_Anim(anim): return GenQueryComposite_tbl_lAttr_lVal(tAnim, ['Anim'], [anim])
        
    def tUniq(tbl, attrPlus):
        lAttr = attrPlus if isinstance(attrPlus, list) else [attrPlus]
        return GenSortComposite_tbl_lAttr(GenUniqComposite_tbl_lAttr(tbl, lAttr), lAttr)
    def tUniqI(tbl, attrPlus):
        tbl[:] = tUniq(tbl, attrPlus)
        return tbl
        
    tlMA = [TuptlMA(Query_tM_M(m.M).id, Query_tA_A(m.A).id) for m in lMab]
    tlBA = [TuptlBA(Query_tB_AB([m.A, m.B]).id, Query_tA_A(m.A).id) for m in lMab]
    
    tMParent = [TuptMParent(m.id, Query_tM_M(m.oM.parent.name).id if m.oM.parent else -1)  for m in tM]
    tBParent = [TuptBParent(m.id, Query_tB_B(m.oB.parent.name).id if m.oB.parent else -1)  for m in tB]
    
    tUniqI(tM, 'id')
    tUniqI(tA, 'id')
    tUniqI(tB, 'id')
    tUniqI(tlMA, 'idM')
    tUniqI(tlBA, ['idA', 'idB'])
    tUniqI(tMParent, 'id')
    tUniqI(tBParent, 'id')
    
    meshName = [m.M for m in tinorder(tM, 'id')]
    meshParent = [m.idP for m in tinorder(tMParent, 'id')]
    meshMatrix = [m.oM.matrix_world for m in tinorder(tM, 'id')]
    
    armName   = [m.A for m in tinorder(tA, 'id')]
    armMatrix = [m.oA.matrix_world for m in tinorder(tA, 'id')]
    
    boneName = [m.B for m in tinorder(tB, 'id')]
    boneParent = [m.idP for m in tinorder(tBParent, 'id')]
    boneMatrix = [Query_tA_id(Query_tlBA_idB(m.id).idA).oA.matrix_world * m.oB.matrix_local for m in tinorder(tB, 'id')]
    
    meshVert  = [dMeshGetVerts(m.oM.data)   for m in tinorder(tM, 'id')]
    meshIndex = [dMeshGetIndices(m.oM.data) for m in tinorder(tM, 'id')]
        
    meshVertBoneWeight = []
    for m in tinorder(tM, 'id'):
        lMeshAllArmId = sorted([t.idA for t in QueryL_tlMA_idM(m.id)])
        lMeshAllArmBonetlBA = lFlatten([QueryL_tlBA_idA(a) for a in lMeshAllArmId])
        lMeshAllArmBoneId   = [t.idB for t in tinorder(lMeshAllArmBonetlBA, 'idB')]
        lMeshAllArmBoneName = [Query_tB_id(t.idB).B for t in tinorder(lMeshAllArmBonetlBA, 'idB')]
        assert lUniqP(lMeshAllArmBoneName) and lUniqP(lMeshAllArmBoneName)
        lAppendI(meshVertBoneWeight, GetWeights(m.oM, lMeshAllArmBoneId, lMeshAllArmBoneName))
    
    ######
    ######

    def SceneActionGetAll():
        return [x for x in bpy.data.actions]
    def SceneArmatureGetAll():
        return [x for x in bpy.data.armatures]

    # FIXME: Likely want only Armatures reachable from SceneMeshSelectAll
    oAct = SceneActionGetAll()
    oArm = SceneArmatureGetAll()
    
    assert len(oAct) == 1
    assert len(oArm) == 1
    
    def GetAnimByMatchName(oAct):
        def GetDataPathEltName(blenderDataPath):
            import re
            rq = re.search(r"""(?P<prefix>pose\.bones)
                              \["(?P<name>(\w|\s|\.)+)"\]
                              \.(?P<tag>\w+)""", blenderDataPath, re.VERBOSE)
            assert rq # FIXME: P<name> part format: Currently accepts chars, spaces and dots.
            r = rq.groupdict()
            assert len(r['name']) and r['tag'] in ['location', 'rotation_quaternion', 'scale']
            return r['name']
        
        ActInflu = namedtuple('ActInflu', ['animName', 'armName', 'lChanName'])
        ret = []
        for act in oAct:
            import re
            rq = re.search(r"""(?P<animName>\w+)_(?P<armName>\w+)
                               (?P<rest>(\..*)?)""", act.name, re.VERBOSE)
            assert rq       # FIXME: Actions with nonmatching names are maybe not meant to be exported
            r = rq.groupdict()
            assert len(r['animName']) and len(r['armName'])
            lChanNameAll = [GetDataPathEltName(fc.data_path) for fc in act.fcurves]
            lChanName    = lUniq(lChanNameAll)      # FCurves ending in .location .whatever got the same chan name
            lAppendI(ret, ActInflu(r['animName'], r['armName'], lChanName))
        return ret
    
    TuptAnim     = namedtuple('tAnim',     ['id', 'Anim'])
    TuptlAnimArmChan = namedtuple('tAnimChan', ['idAnim', 'idA', 'idB'])
    
    allAnim = GetAnimByMatchName(oAct)
    
    tAnim = [TuptAnim(i, m.animName) for i, m in enumerate(lUniq(allAnim, f=lambda x: x.animName))]
    tlAnimArmChan = [TuptlAnimArmChan(Query_tAnim_Anim(m.animName).id, Query_tA_A(m.armName).id, Query_tB_AB([m.armName, c]).id) for m in allAnim for c in m.lChanName]
    
    # FIXME: Blender global side effect
#    for t in tinorder(tA, 'id'):
#        assert t.oA.pose_position == 'REST'
#        a.pose_position = 'POSE'
        
    ######
    ######
    
    p = P()

    mkLenDelSec(p, b"MESHNAME", [BytesFromStr(i) for i in meshName])
    mkIntSec(p, b"MESHPARENT", meshParent)
    mkMatrixSec(p, b"MESHMATRIX", [BlendMatToList(m) for m in meshMatrix])    
    
    mkLenDelSec(p, b"BONENAME", [BytesFromStr(i) for i in boneName])
    mkIntSec(p, b"BONEPARENT", boneParent)
    mkMatrixSec(p, b"BONEMATRIX", [BlendMatToList(m) for m in boneMatrix])
    
    mkListFloatSec(p, b"MESHVERT", meshVert)
    mkListIntSec(p, b"MESHINDEX", meshIndex)
    mkListListPairIntFloatSec(p, b"MESHVERTBONEWEIGHT", meshVertBoneWeight)
    
    return p
        
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
        #Br2()
        #p = BlendRun()
        p = Br3()
        
        assert len(p.getBytes()) <= 1024 * 1024 # Arbitrary length limit
        
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

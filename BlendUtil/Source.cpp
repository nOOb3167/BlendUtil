#include <cstdlib>
#include <cstdio>
#include <cassert>
/* SCNd32 is 2013 only, thanks MSVC */
/* #include <inttypes.h> */
#include <cstdint>
#include <cctype> /* isspace */

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <numeric> /* ::std::accumulate */
#include <functional> /* ::std::function */

#include <exception>

/* warning C4018: signed/unsigned mismatch; warning C4996: fopen deprecated */
#pragma warning(disable : 4018 4996)

using namespace std;

#define BU_MAX_INFLUENCING_BONE 4
#define BU_MAX_TOTAL_BONE_PER_MESH 64

class ExcItemExist  : public exception {};
class ExcRecurseMax : public exception {};

class slice_str_t {};
class slice_reslice_abs_t {};
class slice_reslice_rel_t {};

#define DMAT_RMAJOR_ELT(m,r,c) ((m).d[4*(r)+(c)])
#define DMAT_CMAJOR_ELT(m,r,c) ((m).d[4*(c)+(r)])
#define DMAT_ELT(m,r,c) (DMAT_CMAJOR_ELT((m),(r),(c)))

struct DMat {
	float d[16];

	static DMat MakeIdentity() {
		DMat m;
		DMAT_ELT(m, 0, 0) = 1.0; DMAT_ELT(m, 0, 1) = 0.0; DMAT_ELT(m, 0, 2) = 0.0; DMAT_ELT(m, 0, 3) = 0.0;
		DMAT_ELT(m, 1, 0) = 0.0; DMAT_ELT(m, 1, 1) = 1.0; DMAT_ELT(m, 1, 2) = 0.0; DMAT_ELT(m, 1, 3) = 0.0;
		DMAT_ELT(m, 2, 0) = 0.0; DMAT_ELT(m, 2, 1) = 0.0; DMAT_ELT(m, 2, 2) = 1.0; DMAT_ELT(m, 2, 3) = 0.0;
		DMAT_ELT(m, 3, 0) = 0.0; DMAT_ELT(m, 3, 1) = 0.0; DMAT_ELT(m, 3, 2) = 0.0; DMAT_ELT(m, 3, 3) = 1.0;
		return m;
	}

	static DMat MakeFromVec(
		float v0, float v1, float v2, float v3,
		float v4, float v5, float v6, float v7,
		float v8, float v9, float v10, float v11,
		float v12, float v13, float v14, float v15)
	{
		DMat m;
		float v[] = {
			v0, v1, v2, v3,
			v4, v5, v6, v7,
			v8, v9, v10, v11,
			v12, v13, v14, v15,
		};
		assert(sizeof v / sizeof *v == 16);
		memcpy(m.d, v, 16 * sizeof(float));
		return Transpose(m);
	}

	static DMat Transpose(const DMat &a) {
		DMat m;
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				DMAT_ELT(m, i, j) = DMAT_ELT(a, j, i);
		return m;
	}

	static DMat Multiply(const DMat &lhs, const DMat &rhs) {
		DMat m;
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++) {
				DMAT_ELT(m, r, c) = 0.0;
				DMAT_ELT(m, r, c) += DMAT_ELT(lhs, r, 0) * DMAT_ELT(rhs, 0, c);
				DMAT_ELT(m, r, c) += DMAT_ELT(lhs, r, 1) * DMAT_ELT(rhs, 1, c);
				DMAT_ELT(m, r, c) += DMAT_ELT(lhs, r, 2) * DMAT_ELT(rhs, 2, c);
				DMAT_ELT(m, r, c) += DMAT_ELT(lhs, r, 3) * DMAT_ELT(rhs, 3, c);
			}
			return m;
	}

	static DMat InvertNs(const DMat &m) {
		DMat oM;
		int r = InvertEx(m, &oM);
		assert(r);
		return oM;
	}

	static bool InvertEx(const DMat &iMat, DMat *oMat) {
		/* This function accepts Column major order layout. */
		/* Remember Row/Column major does matter, as: $(A^T)^-1 = (A^-1)^T$.
		Wrong major order (Row major) is the same as inputting in $A^T$ instead of $A$ into the inversion function.
		The wanted result is $(A^T)^-1 = A^-1$, but by the above, will result in getting out $(A^-1)^T$.
		An extra transpose may be neccessary to bring $(A^-1)^T^T = A^-1$.*/
		const float *m = iMat.d;
		float *invOut  = oMat->d;

		float inv[16], det;
		int i;

		inv[0] = m[5]  * m[10] * m[15] - 
			m[5]  * m[11] * m[14] - 
			m[9]  * m[6]  * m[15] + 
			m[9]  * m[7]  * m[14] +
			m[13] * m[6]  * m[11] - 
			m[13] * m[7]  * m[10];

		inv[4] = -m[4]  * m[10] * m[15] + 
			m[4]  * m[11] * m[14] + 
			m[8]  * m[6]  * m[15] - 
			m[8]  * m[7]  * m[14] - 
			m[12] * m[6]  * m[11] + 
			m[12] * m[7]  * m[10];

		inv[8] = m[4]  * m[9] * m[15] - 
			m[4]  * m[11] * m[13] - 
			m[8]  * m[5] * m[15] + 
			m[8]  * m[7] * m[13] + 
			m[12] * m[5] * m[11] - 
			m[12] * m[7] * m[9];

		inv[12] = -m[4]  * m[9] * m[14] + 
			m[4]  * m[10] * m[13] +
			m[8]  * m[5] * m[14] - 
			m[8]  * m[6] * m[13] - 
			m[12] * m[5] * m[10] + 
			m[12] * m[6] * m[9];

		inv[1] = -m[1]  * m[10] * m[15] + 
			m[1]  * m[11] * m[14] + 
			m[9]  * m[2] * m[15] - 
			m[9]  * m[3] * m[14] - 
			m[13] * m[2] * m[11] + 
			m[13] * m[3] * m[10];

		inv[5] = m[0]  * m[10] * m[15] - 
			m[0]  * m[11] * m[14] - 
			m[8]  * m[2] * m[15] + 
			m[8]  * m[3] * m[14] + 
			m[12] * m[2] * m[11] - 
			m[12] * m[3] * m[10];

		inv[9] = -m[0]  * m[9] * m[15] + 
			m[0]  * m[11] * m[13] + 
			m[8]  * m[1] * m[15] - 
			m[8]  * m[3] * m[13] - 
			m[12] * m[1] * m[11] + 
			m[12] * m[3] * m[9];

		inv[13] = m[0]  * m[9] * m[14] - 
			m[0]  * m[10] * m[13] - 
			m[8]  * m[1] * m[14] + 
			m[8]  * m[2] * m[13] + 
			m[12] * m[1] * m[10] - 
			m[12] * m[2] * m[9];

		inv[2] = m[1]  * m[6] * m[15] - 
			m[1]  * m[7] * m[14] - 
			m[5]  * m[2] * m[15] + 
			m[5]  * m[3] * m[14] + 
			m[13] * m[2] * m[7] - 
			m[13] * m[3] * m[6];

		inv[6] = -m[0]  * m[6] * m[15] + 
			m[0]  * m[7] * m[14] + 
			m[4]  * m[2] * m[15] - 
			m[4]  * m[3] * m[14] - 
			m[12] * m[2] * m[7] + 
			m[12] * m[3] * m[6];

		inv[10] = m[0]  * m[5] * m[15] - 
			m[0]  * m[7] * m[13] - 
			m[4]  * m[1] * m[15] + 
			m[4]  * m[3] * m[13] + 
			m[12] * m[1] * m[7] - 
			m[12] * m[3] * m[5];

		inv[14] = -m[0]  * m[5] * m[14] + 
			m[0]  * m[6] * m[13] + 
			m[4]  * m[1] * m[14] - 
			m[4]  * m[2] * m[13] - 
			m[12] * m[1] * m[6] + 
			m[12] * m[2] * m[5];

		inv[3] = -m[1] * m[6] * m[11] + 
			m[1] * m[7] * m[10] + 
			m[5] * m[2] * m[11] - 
			m[5] * m[3] * m[10] - 
			m[9] * m[2] * m[7] + 
			m[9] * m[3] * m[6];

		inv[7] = m[0] * m[6] * m[11] - 
			m[0] * m[7] * m[10] - 
			m[4] * m[2] * m[11] + 
			m[4] * m[3] * m[10] + 
			m[8] * m[2] * m[7] - 
			m[8] * m[3] * m[6];

		inv[11] = -m[0] * m[5] * m[11] + 
			m[0] * m[7] * m[9] + 
			m[4] * m[1] * m[11] - 
			m[4] * m[3] * m[9] - 
			m[8] * m[1] * m[7] + 
			m[8] * m[3] * m[5];

		inv[15] = m[0] * m[5] * m[10] - 
			m[0] * m[6] * m[9] - 
			m[4] * m[1] * m[10] + 
			m[4] * m[2] * m[9] + 
			m[8] * m[1] * m[6] - 
			m[8] * m[2] * m[5];

		det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

		if (det == 0)
			return false;

		det = 1.0f / det;

		for (i = 0; i < 16; i++)
			invOut[i] = inv[i] * det;

		return true;
	}
};

struct DVec3 {
	float d[3];
};

bool ScaZero(float a) {
	const float delta = 0.001f;
	return (std::fabsf(a) < delta);
}

class Slice {
	int beg, end;
	shared_ptr<string> s;
public:
	Slice(slice_str_t _, const string &s) : s(new string(s)), beg(0), end(s.size()) {}
	Slice(slice_reslice_abs_t _, const Slice &other, int beg, int end) : s(other.s), beg(beg), end(end) {
		assert(beg >= other.beg && beg <= end && end <= other.end);
	}
	Slice(slice_reslice_rel_t _, const Slice &other, int beg, int end) : s(other.s), beg(other.beg + beg), end(other.beg + end) {
		assert(this->beg >= other.beg && this->beg <= this->end && this->end <= other.end);
	}

	bool Check() const {
		return beg >= 0 && beg <= end && end <= (long)s->size();
	}

	bool CheckRelRange(int rel) const {
		return rel >= 0 && rel <= size();
	}

	int size() const {
		assert(Check());
		return end - beg;
	}

	int BytesLeftRel(int rel) const {
		assert(CheckRelRange(rel));
		return size() - rel;
	}

	const char *CharPtrRel(int rel) const {
		assert(CheckRelRange(rel));
		return &s->data()[beg + rel];
	}
};

class Section {
public:
	string name;
	Slice  data;

	Section(const string &name, const Slice &data) : name(name), data(data) {}
};

class P {
	int p;
	Slice s;
public:
	P(const string &s) : p(0), s(slice_str_t(), s) {}
	P(const Slice &s) : p(0), s(s) {}
	P(const P &other) : p(other.p), s(other.s) {}
	~P() {}

	/* Cruft Start */
	void OptSkipWs() {
		P w(*this);
		while (w.BytesLeft() && isspace(*(w.s.CharPtrRel(w.p))))
			w.AdvanceN(1);
		*this = w;
	}

	bool OptAfterNextDelShallow(const string &del) {
		P w(*this);
		w.OptSkipWs();

		if (w.BytesLeft() >= del.size() && strncmp(del.c_str(), w.s.CharPtrRel(w.p), del.size()) == 0)
			return (w.AdvanceN(del.size()), *this = w, true);

		return false;
	}

	bool OptAfterNextDelDeep(const string &del) {
		P w(*this);
		int r;
		/* FIXME: One day find out what happens if BytesLeft() == 0 and del.size() == 0 */
		while (w.BytesLeft() >= del.size() && (r = strncmp(del.c_str(), w.s.CharPtrRel(w.p), del.size())) != 0)
			w.AdvanceN(1);
		if (r == 0)
			return (w.AdvanceN(del.size()), *this = w, true);
		return false;
	}

	string OptRawSpanTo(const P &rhs) {
		/* FIXME: All uncompliant */
		assert(s.CharPtrRel(p) <= rhs.s.CharPtrRel(rhs.p) && BytesLeft() >= rhs.s.CharPtrRel(rhs.p) - s.CharPtrRel(p));
		return string(s.CharPtrRel(p), rhs.s.CharPtrRel(rhs.p) - s.CharPtrRel(p));
	}

	string OptRawSpanToEnd() {
		P a(*this);
		assert(a.BytesLeft() >= 0);
		a.AdvanceN(a.BytesLeft());
		return OptRawSpanTo(a);
	}
	/* Cruft End */

	int BytesLeft() const {
		return s.BytesLeftRel(p);
	}

	bool CheckIntArbitraryLimit(int i) {
		return i == -1 || (i >= 0 && i <= 1024 * 1024);
	}

	void AdvanceN(int n) {
		assert(BytesLeft() >= n);
		p += n;
	}

	void AdvanceInt() {
		AdvanceN(4);
	}

	int ReadInt() {
		assert(BytesLeft() >= 4);

		/* SCNd8 : See n1256@7.8.1/4 */
		/* sscanf(s.CharPtrRel(p), "" SCNd32 "", &i32); */
		union { int32_t i; char c[4]; } uni;
		memcpy(&uni.c, s.CharPtrRel(p), 4);

		assert(CheckIntArbitraryLimit(uni.i));

		return (AdvanceInt(), uni.i);
	}

	float ReadFloat() {
		assert(BytesLeft() >= 4);

		/* FIXME: Yes the union thing */
		union { float f; char c[4]; } uni;
		memcpy(&uni.c, s.CharPtrRel(p), 4);

		return (AdvanceN(4), uni.f);
	}

	string ReadString(int n) {
		assert(BytesLeft() >= n);

		string ret(s.CharPtrRel(p), n);

		return (AdvanceN(n), ret);
	}

	string ReadLenDel() {
		P w(*this);

		assert(w.BytesLeft() >= 4);

		int    len  = w.ReadInt();
		assert(CheckIntArbitraryLimit(len));
		string data = w.ReadString(len);

		return (*this = w, data);
	}

	Section ReadSectionWeak() {
		P w(*this);

		assert(w.BytesLeft() >= 4+4+4);

		int lenTotal, lenName, lenData;

		lenTotal = w.ReadInt();
		lenName  = w.ReadInt();
		lenData  = w.ReadInt();

		assert(lenTotal == 4+4+4+lenName+lenData);

		string name, data;

		name = w.ReadString(lenName);
		data = w.ReadString(lenData);

		return (*this = w, Section(name, Slice(slice_str_t(), data)));
	}
};

class SectionData {
public:
	vector<string> meshName;
	vector<int>    meshParent;
	vector<DMat>   meshMatrix;

	vector<string> boneName;
	vector<int>    boneParent;
	vector<DMat>   boneMatrix;

	vector<vector<float> > meshVert;
	vector<vector<int> >   meshIndex;

	/* [Mesh0: [Vert0: id*BU_MAX_INFLUENCING_BONE ...] ...] */
	vector<vector<int> >   meshVertId;
	vector<vector<float> > meshVertWt;

	vector<vector<int> > meshChild;
	vector<vector<int> > boneChild;
};

class SectionDataEx : public SectionData {
public:
};

bool MultiRootReachabilityCheck(const vector<vector<int> > &child, const vector<int> &parent) {
	assert(child.size() == parent.size());

	vector<int> w(child.size(), 0);

	std::function<void(int)> rec;
	rec = [&rec, &child, &w](int state) {
		w[state] += 1;

		for (auto &i : child[state])
			rec(i);
	};

	for (int i = 0; i < parent.size(); i++)
		if (parent[i] == -1)
			rec(i);

	bool allReachable   = accumulate(w.begin(), w.end(), true, [](int a, int x) { return a && x; });
	bool allReachedOnce = accumulate(w.begin(), w.end(), true, [](int a, int x) { return a && (x == 1); });

	return allReachable && allReachedOnce;
}

void MatrixAccumulateWorld(const vector<DMat> &mLocal, const vector<vector<int> > &child, int state, const DMat &mInitial, vector<DMat> *oWorld) {
	assert(oWorld->size() == mLocal.size());

	std::function<void(int, const DMat &)> rec;
	rec = [&rec, &mLocal, &child, &oWorld](int state, const DMat &acc) {
		DMat newAcc = DMat::Multiply(acc, mLocal[state]);
		(*oWorld)[state] = newAcc;

		for (auto &i : child[state])
			rec(i, newAcc);
	};

	rec(state, mInitial);
}

void MultiRootMatrixAccumulateWorld(const vector<DMat> &mLocal, const vector<vector<int> > &child, const vector<int> &parent, const vector<DMat> root, vector<DMat> *oWorld) {
	assert(mLocal.size() == child.size() && mLocal.size() == parent.size() && mLocal.size() == oWorld->size());
	assert(MultiRootReachabilityCheck(child, parent));

	for (int i = 0; i < parent.size(); i++)
		if (parent[i] == -1)
			MatrixAccumulateWorld(mLocal, child, i, root[i], oWorld);
}

void MatrixMeshToBone(const vector<DMat> &meshWorldMatrix, const vector<DMat> &boneWorldMatrix, vector<vector<DMat> > *oWorld) {
	int numMesh = meshWorldMatrix.size();
	int numAllBone = boneWorldMatrix.size();

	assert(oWorld->size() == boneWorldMatrix.size());
	
	vector<vector<DMat>> ret;

	for (int m = 0; m < numMesh; m++) {
		vector<DMat> v;
		for (int b = 0; b < numAllBone; b++) {
			const DMat mMeshBone = DMat::Multiply(meshWorldMatrix[m], DMat::InvertNs(boneWorldMatrix[b]));
			v.push_back(mMeshBone);
		}
		ret.push_back(v);
	}

	*oWorld = ret;
}

class Parse {
public:
	static vector<Section> ReadSection(const P &inP) {
		vector<Section> sec;
		P w(inP);

		int bleft = w.BytesLeft() + 1;

		while (w.BytesLeft() < bleft && (bleft = w.BytesLeft()) != 0) {
			sec.push_back(Section(w.ReadSectionWeak()));
		}

		return sec;
	}

	static bool SectionExistByName(const vector<Section> &sec, const string &name) {
		for (auto &i : sec)
			if (i.name == name)
				return true;
		return false;
	}

	static Section SectionGetByName(const vector<Section> &sec, const string &name) {
		for (auto &i : sec)
			if (i.name == name)
				return i;
		throw ExcItemExist();
	}

	static SectionDataEx * MakeSectionDataEx(const P &inP) {
		vector<Section> sec = ReadSection(inP);

		SectionDataEx *sd = new SectionDataEx();
		FillSectionData(sec, sd);
		CheckSectionData(*sd);

		return sd;
	}

	static void FillSectionData(const vector<Section> &sec, SectionData *outSD) {
		FillLenDel(SectionGetByName(sec, "MESHNAME").data, &outSD->meshName);
		FillInt(SectionGetByName(sec, "MESHPARENT").data, &outSD->meshParent);
		FillMat(SectionGetByName(sec, "MESHMATRIX").data, &outSD->meshMatrix);

		FillLenDel(SectionGetByName(sec, "BONENAME").data, &outSD->boneName);
		FillInt(SectionGetByName(sec, "BONEPARENT").data, &outSD->boneParent);
		FillMat(SectionGetByName(sec, "BONEMATRIX").data, &outSD->boneMatrix);

		assert(outSD->boneName.size() <= BU_MAX_TOTAL_BONE_PER_MESH);

		{
			vector<string>         mVertChunks;
			vector<vector<float> > mVert;
			FillLenDel(SectionGetByName(sec, "MESHVERT").data, &mVertChunks);
			for (int i = 0; i < mVertChunks.size(); i++) {
				vector<float> v;
				FillFloat(Slice(slice_str_t(), mVertChunks[i]), &v);
				assert(v.size() % 3 == 0);
				mVert.push_back(v);
			}
			outSD->meshVert = mVert;
		}

		{
			vector<string>       mIndexChunks;
			vector<vector<int> > mIndex;
			FillLenDel(SectionGetByName(sec, "MESHINDEX").data, &mIndexChunks);
			for (int i = 0; i < mIndexChunks.size(); i++) {
				vector<int> v;
				FillInt(Slice(slice_str_t(), mIndexChunks[i]), &v);
				assert(v.size() % 3 == 0);
				mIndex.push_back(v);
			}
			outSD->meshIndex = mIndex;
		}

		{
			int numMesh = outSD->meshName.size();

			vector<vector<int> > mVBWeightId;
			vector<vector<float> > mVBWeightWt;

			vector<string> mBWChunks;
			FillLenDel(SectionGetByName(sec, "MESHVERTBONEWEIGHT").data, &mBWChunks);
			/* MESHVERTBONEWEIGHT stored as flat (MeshN x VertOfMeshN) -> [pairIdWt, ...]
			*  Accumulate-skip numVert[MeshN] entries to get to Mesh_{N+1} data. */
			assert(mBWChunks.size() == accumulate(outSD->meshVert.begin(), outSD->meshVert.end(), 0, [](int a, const vector<float> &x) { return a + mNumVertFromSize(x.size()); }));

			mVBWeightId = vector<vector<int> >(numMesh);
			mVBWeightWt = vector<vector<float> >(numMesh);
			for (int i = 0; i < numMesh; i++) {
				int numVert = mNumVertFromSize(outSD->meshVert[i].size());
				mVBWeightId[i] = vector<int>(BU_MAX_INFLUENCING_BONE * numVert);
				mVBWeightWt[i] = vector<float>(BU_MAX_INFLUENCING_BONE * numVert);
			}

			int currBaseIdx = 0;
			for (int m = 0; m < numMesh; m++) {
				int numVert = mNumVertFromSize(outSD->meshVert[m].size());
				for (int i = 0; i < numVert; i++) {
					vector<pair<int, float> > v;
					FillPairIntFloat(Slice(slice_str_t(), mBWChunks[currBaseIdx + i]), &v);

					vector<pair<int, float> > finals = v;

					/* Sort by Descending weight */
					sort(finals.begin(), finals.end(),
						[](const pair<int, float> &a, const pair<int, float> &b) {
							/* FIXME: Floating point comparison sync alert */
							return a.second > b.second;
					});

					/* Cut if have too many influencing bones, zero pad if too few */
					finals.resize(BU_MAX_INFLUENCING_BONE, make_pair(0, 0.0f));

					/* Normalize weights
					*  In Blender, weight painting produces weights in [0.0, 1.0] for individual Bone irregardless of other Bone weights.
					*  Thus painting multiple Bones produces multiple weights, each in [0.0, 1.0].
					*    - Weights have to sum to 1.0
					*    - influA having the same Blender weight as influB should result in having the same final weight
					*    - influA having a Blender weight 'n' times as high as InfluB should result in having 'n' times the final weight
					*  finalWeights = map(lambda x: x / sum(influWeights), influWeights) # Just a division by sum of influences
					*/
					float influWeightSum = accumulate(finals.begin(), finals.end(), 0.0f, [](float a, pair<int, float> x) { return a + x.second; });
					if (!ScaZero(influWeightSum))
						transform(finals.begin(), finals.end(), finals.begin(), [&influWeightSum](pair<int, float> x) { return make_pair(x.first, x.second / influWeightSum); });

					assert(BU_MAX_INFLUENCING_BONE == finals.size());
					for (int j = 0; j < BU_MAX_INFLUENCING_BONE; j++) {
						mVBWeightId[m][(BU_MAX_INFLUENCING_BONE * i) + j] = finals[j].first;
						mVBWeightWt[m][(BU_MAX_INFLUENCING_BONE * i) + j] = finals[j].second;
					}
				}
				currBaseIdx += numVert;
			}

			outSD->meshVertId = mVBWeightId;
			outSD->meshVertWt = mVBWeightWt;
		}

		FillChild(outSD->meshParent, &outSD->meshChild);
		FillChild(outSD->boneParent, &outSD->boneChild);
	}

	static void CheckSectionData(const SectionData &sd) {
		int numMesh = sd.meshName.size();
		int numBone = sd.boneName.size();

		/* No Mesh in the model? */
		assert(numMesh);
		assert(numMesh == sd.meshParent.size());
		assert(numMesh == sd.meshChild.size());
		/* Empty names? */
		for (auto &i : sd.meshName)
			assert(i.size());
		for (auto &i : sd.meshParent)
			assert(i == -1 || (i >= 0 && i < numMesh));
		assert(!IsCycle(sd.meshParent));
		assert(numMesh == sd.meshMatrix.size());

		assert(numBone);
		assert(numBone == sd.boneParent.size());
		assert(numBone == sd.boneChild.size());
		for (auto &i : sd.boneName)
			assert(i.size());
		for (auto &i : sd.boneParent)
			assert(i == -1 || (i >= 0 && i < numBone));
		assert(!IsCycle(sd.boneParent));
		assert(numBone == sd.boneMatrix.size());

		assert(numMesh == sd.meshVert.size());

		for (int i = 0; i < numMesh; i++) {
			int numVert = mNumVertFromSize(sd.meshVert[i].size());

			assert(sd.meshVertId[i].size() == BU_MAX_INFLUENCING_BONE * numVert);
			assert(sd.meshVertWt[i].size() == BU_MAX_INFLUENCING_BONE * numVert);

			for (auto &j : sd.meshVertId[i])
				assert(j >= 0 && j < numBone);
			for (auto &j : sd.meshVertWt[i])
				assert(j >= 0.0 && j <= 1.0);
		}
	}

	static void FillChild(const vector<int> &inParent, vector<vector<int> > *outSD) {
		vector<vector<int> > cAcc(inParent.size());

		for (int i = 0; i < inParent.size(); i++)
			if (inParent[i] != -1)
				cAcc[inParent[i]].push_back(i);
		*outSD = cAcc;	
	}

	static int mNumVertFromSize(int nFloats) {
		assert(nFloats % 3 == 0);
		return nFloats / 3;
	}

	static bool IsCycle(const vector<int> &parent) {
		vector<int> root = mListRootFromParent(parent);
		vector<vector<int> > child;
		FillChild(parent, &child);

		/* Worst case of a 'n' element hierarchy is a 'n' element chain. Therefore if depth reaches 'n+1' there has to be a cycle. */
		try {
			for (int i = 0; i < root.size(); i++)
				mCycleDfs(parent, child, parent.size() + 1, 0, root[i]);
		} catch(ExcRecurseMax &) {
			return true;
		}

		return false;
	}

	static vector<int> mListRootFromParent(const vector<int> &parent) {
		/* Find roots (-1 parent) */
		vector<int> root;
		for (int i = 0; i < parent.size(); i++)
			if (parent[i] == -1)
				root.push_back(i);
		return root;
	}

	static void mCycleDfs(const vector<int> &parent, const vector<vector<int> > &child, const int maxDepth, int depth, int visiting) {
		if (depth >= maxDepth)
			throw ExcRecurseMax();
		for (int i = 0; i < child[visiting].size(); i++)
			mCycleDfs(parent, child, maxDepth, depth + 1, child[visiting][i]);
	}

	static void FillInt(const Slice &sec, vector<int> *outVS) {
		int bleft;
		P w(sec);

		vector<int> vS;

		while ((bleft = w.BytesLeft()) != 0) {
			vS.push_back(w.ReadInt());
		}

		*outVS = vS;
	}

	static void FillFloat(const Slice &sec, vector<float> *outVS) {
		int bleft;
		P w(sec);

		vector<float> vS;

		while ((bleft = w.BytesLeft()) != 0) {
			vS.push_back(w.ReadFloat());
		}

		*outVS = vS;
	}

	static void FillPairIntFloat(const Slice &sec, vector<pair<int, float> > *outVS) {
		int bleft;
		P w(sec);

		vector<pair<int, float> > vS;

		while ((bleft = w.BytesLeft()) != 0) {
			/* FIXME: Used to be make_pair(w.ReadInt(), w.ReadFloat), but argument evaluation order is unspecified in C++ */
			int   i = w.ReadInt();
			float f = w.ReadFloat();
			vS.push_back(make_pair(i, f));
		}

		*outVS = vS;
	}


	static void FillLenDel(const Slice &sec, vector<string> *outVS) {
		int bleft;
		P w(sec);

		vector<string> vS;

		while ((bleft = w.BytesLeft()) != 0) {
			vS.push_back(w.ReadLenDel());
		}

		*outVS = vS;
	}

	static void FillVec3(const Slice &sec, vector<DVec3> *outVS) {
		int bleft;
		P w(sec);

		vector<DVec3> vS;

		while ((bleft = w.BytesLeft()) != 0) {
			DVec3 m;

			assert(w.BytesLeft() >= 3*4);

			for (int i = 0; i < 3; i++)
				m.d[i] = w.ReadFloat();

			vS.push_back(m);
		}

		*outVS = vS;
	}

	static void FillMat(const Slice &sec, vector<DMat> *outVS) {
		int bleft;
		P w(sec);

		vector<DMat> vS;

		while ((bleft = w.BytesLeft()) != 0) {
			DMat m;

			assert(w.BytesLeft() >= 16*4);

			for (int i = 0; i < 16; i++)
				m.d[i] = w.ReadFloat();

			vS.push_back(m);
		}

		*outVS = vS;
	}
};

P * MakePFromFile(const string &fname) {
	int r;
	char buf[1024];
	string acc;
	FILE *f;

	f = fopen(fname.c_str(), "rb");
	assert(f);

	while ((r = fread(buf, 1, 1024, f)))
		acc.append(buf, r);

	assert(!ferror(f));
	assert(feof(f));

	fclose(f);

	return new P(acc);
}

SectionDataEx * BlendUtilMakeSectionDataEx(const string &fName) {
	shared_ptr<P> p(MakePFromFile(fName.c_str()));
	SectionDataEx *sd = Parse::MakeSectionDataEx(*p);

	return sd;
}

void BlendUtilRun(void) {
	SectionDataEx *sd = BlendUtilMakeSectionDataEx("../tmpdata.dat");
}

#include <cstdlib>
#include <cstdio>
#include <cassert>
/* SCNd32 is 2013 only, thanks MSVC */
/* #include <inttypes.h> */
#include <cstdint>

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include <exception>

/* warning C4018: signed/unsigned mismatch; warning C4996: fopen deprecated */
#pragma warning(disable : 4018 4996)

using namespace std;

#define BU_MAX_INFLUENCING_BONE 4

class ExcItemExist  : public exception {};
class ExcRecurseMax : public exception {};

class slice_str_t {};
class slice_reslice_abs_t {};
class slice_reslice_rel_t {};

struct DMat {
	float d[16];
};

struct DVec3 {
	float d[3];
};

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
	vector<string> nodeName;
	vector<int>    nodeParent;
	vector<DMat>   nodeMatrix;

	vector<int> nodeMesh;

	vector<string> boneName;
	vector<int>    boneParent;
	vector<DMat>   boneMatrix;

	vector<string> meshName;
	vector<int>    meshBoneCount;
	vector<vector<float> > meshVert;
	vector<vector<int> >   meshIndex;
	vector<DMat>           meshRootMatrix;

	vector<vector<vector<pair<int, float> > > > meshBoneWeight;
};

class SectionDataEx : public SectionData {
public:
	vector<vector<int> > nodeChild;
	vector<vector<int> > boneChild;

	vector<vector<int> > meshBone;

	/* [Mesh0: [Vert0: id*BU_MAX_INFLUENCING_BONE ...] ...] */
	vector<vector<int> >   meshVertId;
	vector<vector<float> > meshVertWt;
};

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
		FillSectionDataExtra(sd);
		CheckSectionDataEx(*sd);

		return sd;
	}

	static void FillSectionData(const vector<Section> &sec, SectionData *outSD) {
		FillLenDel(SectionGetByName(sec, "NODENAME").data, &outSD->nodeName);
		FillInt(SectionGetByName(sec, "NODEPARENT").data, &outSD->nodeParent);
		FillMat(SectionGetByName(sec, "NODEMATRIX").data, &outSD->nodeMatrix);

		FillInt(SectionGetByName(sec, "NODEMESH").data, &outSD->nodeMesh);

		FillLenDel(SectionGetByName(sec, "BONENAME").data, &outSD->boneName);
		FillInt(SectionGetByName(sec, "BONEPARENT").data, &outSD->boneParent);
		FillMat(SectionGetByName(sec, "BONEMATRIX").data, &outSD->boneMatrix);

		FillLenDel(SectionGetByName(sec, "MESHNAME").data, &outSD->meshName);
		FillInt(SectionGetByName(sec, "MESHBONECOUNT").data, &outSD->meshBoneCount);

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

		FillMat(SectionGetByName(sec, "MESHROOTMATRIX").data, &outSD->meshRootMatrix);

		{
			int numMesh = outSD->meshName.size();
			int numBone = outSD->boneName.size();

			vector<string> mBWChunks;
			vector<vector<vector<pair<int, float> > > > mBWeight;

			FillLenDel(SectionGetByName(sec, "MESHBONEWEIGHT").data, &mBWChunks);
			assert(mBWChunks.size() == outSD->meshName.size() * outSD->boneName.size());

			mBWeight = vector<vector<vector<pair<int, float> > > >(numMesh);
			for (auto &i : mBWeight)
				i = vector<vector<pair<int, float> > >(numBone);

			for (int m = 0; m < numMesh; m++)
				for (int b = 0; b < numBone; b++) {
					vector<pair<int, float> > v;
					FillPairIntFloat(Slice(slice_str_t(), mBWChunks[(numMesh * m) + b]), &v);
					mBWeight[m][b] = v;
				}

				outSD->meshBoneWeight = mBWeight;
		}
	}

	static void FillSectionDataExtra(SectionDataEx *outSD) {
		FillChild(outSD->nodeParent, &outSD->nodeChild);
		FillChild(outSD->boneParent, &outSD->boneChild);

		{
			vector<vector<int> > meshBone(outSD->meshBoneCount.size());
			for (int i = 0; i < outSD->meshBoneCount.size(); i++)
				meshBone[i] = vector<int>(outSD->meshBoneCount[i]);

			int bCnt = 0;
			for (int i = 0; i < outSD->meshBoneCount.size(); i++)
				for (int j = 0; j < i; j++)
					meshBone[i].push_back(bCnt++);
			outSD->meshBone = meshBone;
		}

		FillMeshVertInfluence(outSD->meshVert, outSD->meshBoneWeight, &outSD->meshVertId, &outSD->meshVertWt);
	}

	static void CheckSectionData(const SectionData &sd) {
		int numNode = sd.nodeName.size();
		int numMesh = sd.meshName.size();
		int numBone = sd.boneName.size();

		/* No nodes in the model? */
		assert(numNode);
		assert(numNode == sd.nodeParent.size());
		/* Empty names? */
		for (auto &i : sd.nodeName)
			assert(i.size());
		for (auto &i : sd.nodeParent)
			assert(i == -1 || (i >= 0 && i < numNode));
		assert(!IsCycle(sd.nodeParent));
		assert(numNode == sd.nodeMatrix.size());

		assert(numNode == sd.nodeMesh.size());
		for (auto &i : sd.nodeMesh)
			assert(i == -1 || (i >= 0 && i < numMesh));
		{
			/* Check against duplicate mesh assignments. A particular Mesh should be assigned to only one node. */
			vector<int> tmpNodeMesh = sd.nodeMesh;
			auto it = remove(tmpNodeMesh.begin(), tmpNodeMesh.end(), -1);
			tmpNodeMesh.resize(distance(tmpNodeMesh.begin(), it));
			int sansNegative = tmpNodeMesh.size(); /* -1 elements are not Mesh assignment. Do not count them. */
			auto it2 = unique(tmpNodeMesh.begin(), tmpNodeMesh.end());
			tmpNodeMesh.resize(distance(tmpNodeMesh.begin(), it2));
			int sansDuplicate = tmpNodeMesh.size(); /* Maintaining size after duplicate removal means no multiple assignment occured. */
			assert(sansNegative == sansDuplicate);
		}

		assert(numBone);
		assert(numBone == sd.boneParent.size());
		for (auto &i : sd.boneName)
			assert(i.size());
		for (auto &i : sd.boneParent)
			assert(i == -1 || (i >= 0 && i < numBone));
		assert(!IsCycle(sd.boneParent));
		assert(numBone == sd.boneMatrix.size());

		assert(numMesh);
		assert(numMesh == sd.meshBoneCount.size());
		assert(numMesh == sd.meshVert.size());
	}

	static void CheckSectionDataEx(const SectionDataEx &sd) {

	}

	static void FillChild(const vector<int> &inParent, vector<vector<int> > *outSD) {
		vector<vector<int> > cAcc(inParent.size());

		for (int i = 0; i < inParent.size(); i++)
			if (inParent[i] != -1)
				cAcc[inParent[i]].push_back(i);
		*outSD = cAcc;	
	}

	static void FillMeshVertInfluence(
		const vector<vector<float> > &meshVert /* Vert counts */,
		const vector<vector<vector<pair<int, float> > > > &meshBoneWeight,
		vector<vector<int> > *oMeshVertId,
		vector<vector<float> > *oMeshVertWt)
	{
		int numMesh = meshBoneWeight.size();

		vector<vector<int> >   meshVertId(numMesh);
		vector<vector<float> > meshVertWt(numMesh);

		for (int i = 0; i < numMesh; i++)
			FillMeshVertInfluenceOne(meshVert[i], meshBoneWeight[i], &meshVertId[i], &meshVertWt[i]);

		*oMeshVertId = meshVertId;
		*oMeshVertWt = meshVertWt;
	}

	static void FillMeshVertInfluenceOne(
		const vector<float> &meshVert /* Vert count */,
		const vector<vector<pair<int, float> > > &boneWeight,
		vector<int> *oMeshVertId,
		vector<float> *oMeshVertWt)
	{
		int numVert = mNumVertFromSize(meshVert.size());
		int numBone = boneWeight.size();

		vector<int>   meshVertId(BU_MAX_INFLUENCING_BONE * numVert);
		vector<float> meshVertWt(BU_MAX_INFLUENCING_BONE * numVert);

		vector<int> curVisited(numBone, 0);

		/* Sort by Ascending id */
		vector<vector<pair<int, float> > > boneWeightSorted = boneWeight;
		for (auto &i : boneWeightSorted)
			sort(i.begin(), i.end(), [](pair<int, float> a, pair<int, float> b) { return a.first < b.first; });

		for (int i = 0; i < numVert; i++) {
			mInfluCounterAdvance(boneWeightSorted, i, &curVisited);
			vector<pair<int, float> > influ = mInfluGather(boneWeight, i, curVisited);

			assert(influ.size() == BU_MAX_INFLUENCING_BONE);
			for (int j = 0; j < influ.size(); j++) {
				meshVertId[(BU_MAX_INFLUENCING_BONE * i) + j] = influ[j].first;
				meshVertWt[(BU_MAX_INFLUENCING_BONE * i) + j] = influ[j].second;
			}
		}

		*oMeshVertId = meshVertId;
		*oMeshVertWt = meshVertWt;
	}

	static int mNumVertFromSize(int nFloats) {
		assert(nFloats % 3 == 0);
		return nFloats / 3;
	}

	static void mInfluCounterAdvance(const vector<vector<pair<int, float> > > &boneWeight, int state, vector<int> *ioCurVisited) {
		for (int i = 0; i < boneWeight.size(); i++) {
			int &curVisited   = (*ioCurVisited)[i];
			int  nextVisited  = curVisited + 1;
			int  finalVisited = boneWeight[i].size() - 1;

			if (curVisited < finalVisited && boneWeight[i][nextVisited].first == state)
				curVisited += 1;
		}
	}

	static vector<pair<int, float> > mInfluGather(const vector<vector<pair<int, float> > > &boneWeight, int state, const vector<int> &curVisited) {
		/* Remember: In the returned pair<int, float>, int is bone number.
		* In boneWeight, int is vertex id instead. */

		vector<int> boneCandidate;

		for (int i = 0; i < boneWeight.size(); i++)
			if (boneWeight[i][curVisited[i]].first == state)
				boneCandidate.push_back(i);

		/* Sort by Descending weight */
		sort(boneCandidate.begin(), boneCandidate.end(),
			[&boneWeight, &curVisited, &state](int a, int b) {
				assert(boneWeight[a][curVisited[a]].first == state && boneWeight[b][curVisited[b]].first == state);
				/* FIXME: Floating point comparison sync alert */
				return boneWeight[a][curVisited[a]].second > boneWeight[b][curVisited[b]].second;
		});

		vector<pair<int, float> > finals;

		for (auto &i : boneCandidate)
			finals.push_back(make_pair(i, boneWeight[i][curVisited[i]].second));

		/* Cut if have too many influencing bones, zero pad if too few */
		finals.resize(BU_MAX_INFLUENCING_BONE, make_pair(0, 0.0f));

		return finals;
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

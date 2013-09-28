#include <cstdlib>
#include <cstdio>
#include <cassert>
/* SCNd32 is 2013 only, thanks MSVC */
/* #include <inttypes.h> */
#include <cstdint>

#include <memory>
#include <string>
#include <vector>

#include <exception>

using namespace std;

// P : Slice
// Section : Name Slice

class ExcItemExist : public exception {};

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
	vector<DMat> nodeMatrix;

	vector<int> nodeMesh;

	vector<string> boneName;
	vector<int>    boneParent;
	vector<DMat> boneMatrix;

	vector<string> meshName;
	vector<vector<float> > meshVert;

	vector<vector<vector<pair<int, float> > > > meshBoneWeight;
};

class SectionDataEx : public SectionData {
	vector<vector<int> > nodeChild;
	vector<vector<int> > boneChild;
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

	static void ListSectionPostfix(const P &inP) {
		vector<Section> sec = ReadSection(inP);

		SectionData sd;
		FillSectionData(sec, &sd);
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

		{
			vector<string>         mVertChunks;
			vector<vector<float> > mVert;
			FillLenDel(SectionGetByName(sec, "MESHVERT").data, &mVertChunks);
			for (int i = 0; i < mVertChunks.size(); i++) {
				vector<float> v;
				FillFloat(Slice(slice_str_t(), mVertChunks[i]), &v);
				assert(v.size() % 3 == 0 && v.size() % 9 == 0);
				mVert.push_back(v);
			}
			outSD->meshVert = mVert;
		}

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

int main(int argc, char **argv) {

	shared_ptr<P> p(MakePFromFile("../tmpdata.dat"));

	Parse::ListSectionPostfix(*p);

	return EXIT_SUCCESS;
}

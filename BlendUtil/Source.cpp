#include <cstdlib>
#include <cstdio>
#include <cassert>
/* SCNd32 is 2013 only, thanks MSVC */
/* #include <inttypes.h> */
#include <cstdint>

#include <memory>
#include <string>
#include <vector>

using namespace std;

// P : Slice
// Section : Name Slice

class slice_str_t {};
class slice_reslice_abs_t {};
class slice_reslice_rel_t {};

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
		return beg >= 0 && beg <= end && end <= s->size();
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
	P(const P &other) : p(other.p), s(other.s) {}
	~P() {}

	int BytesLeft() const {
		return s.BytesLeftRel(p);
	}

	bool CheckIntArbitraryLimit(int i) {
		return i >= 0 && i <= 1024 * 1024;
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

		AdvanceInt();
		return uni.i;
	}

	string ReadString(int n) {
		assert(BytesLeft() >= n);

		string ret(s.CharPtrRel(p), n);

		AdvanceN(n);
		return ret;
	}

	Section ReadSectionWeak() {
		P w(*this);

		assert (w.BytesLeft() >= 4+4+4);

		int lenTotal, lenName, lenData;

		lenTotal = w.ReadInt();
		lenName  = w.ReadInt();
		lenData  = w.ReadInt();

		assert(lenTotal == 4+4+4+lenName+lenData);

		string name, data;

		name = w.ReadString(lenName);
		data = w.ReadString(lenData);

		*this = w;

		return Section(name, Slice(slice_str_t(), data));
	}
};

class Parse {
public:
	static void ListSectionPostfix(const P &inP) {
		P w(inP);

		vector<Section> sec;

		int bleft = w.BytesLeft() + 1;

		while (w.BytesLeft() < bleft && (bleft = w.BytesLeft()) != 0) {
			sec.push_back(Section(w.ReadSectionWeak()));
		}
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

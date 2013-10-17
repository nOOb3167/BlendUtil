#include <cstdlib>
#include <cassert>
#include <cctype> /* isspace */

#include <functional> /* std::function */
#include <algorithm>
#include <iostream>
#include <sstream>
#include <memory>
#include <vector>
#include <deque>
#include <map>
#include <set>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <oglplus/all.hpp>

#include <../BlendUtil/Source.cpp>

#define G_WIN_W 800
#define G_WIN_H 800

/* 640k should be enough for anyone - IIRC GL 3.0 Guarantees 1024 */
#define G_MAX_BONES_UNIFORM     30
#define G_MAX_BONES_INFLUENCING 4

#define EX_OGLPLUS_ERROR_WRAP_START()                      \
	try {
#define EX_OGLPLUS_ERROR_WRAP_MIDDLE()                     \
	} catch(oglplus::Error &e) {                           \
	  {                                                    \
	  if (dynamic_cast<oglplus::CompileError *>(&e))     \
	  std::cerr << ((oglplus::CompileError &)e).Log(); \
	  if (dynamic_cast<oglplus::LinkError *>(&e))        \
	  std::cerr << ((oglplus::LinkError &)e).Log();    \
	  OglGenErr(e);                                      \
	  }                                                    \
	  {
#define EX_OGLPLUS_ERROR_WRAP_END()                        \
	  }                                                    \
	}

using namespace oglplus;
using namespace std;

class Ctx : public oglplus::Context {};

namespace Cruft {

	string StrTrim(const string &s) {
		const char *str = s.c_str();
		while(*str && isspace(*str)) str++;
		const char *end = str + strlen(str);
		while(end > str && isspace(*(end-1))) end--;
		return string(str, end);
	}

	string ReadFile(const string &fname) {
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

		return acc;
	}

	map<string, string> ParseShdFromString(const string &acc) {
		map<string, string> mapNameShd;

		P p(acc);

		while (true) {
			string markS("======"), markE("@@@@@@");
			P pS(p);
			bool afterNextDelS = pS.OptAfterNextDelShallow(markS);
			P pE(pS);
			bool afterNextDelE = pE.OptAfterNextDelDeep(markE);

			if (! (afterNextDelS && afterNextDelE))
				break;

			string shdNameSpan(pS.OptRawSpanTo(pE));
			assert(shdNameSpan.size() >= markE.size() && equal(markE.begin(), markE.end(), shdNameSpan.rbegin()));
			string shdName(StrTrim(shdNameSpan.substr(0, shdNameSpan.size() - markE.size())));
			
			P pContentS(pE);
			P pContentE(pContentS);
			bool afterNextDelContent = pContentE.OptAfterNextDelDeep(markS);
			if (afterNextDelContent) {
				pContentE.AdvanceN(- (int)markS.size());
				mapNameShd[shdName] = StrTrim(pContentS.OptRawSpanTo(pContentE));
			} else {
				mapNameShd[shdName] = StrTrim(pContentS.OptRawSpanToEnd());
			}

			p = pContentE;
		}

		return mapNameShd;
	}

	map<string, string> ParseShdFromFile(const string &fname) {
		string acc(ReadFile(fname));
		return ParseShdFromString(acc);
	}

	int _dummy_sprintf(string *o, char format[], ...) {
		int r;
		char buf[20];

		va_list args;
		va_start(args, format);
		r = vsnprintf(buf, sizeof buf, format, args);
		va_end(args);

		if (r == -1 || r >= sizeof buf)
			return -1;

		*o = string(buf);

		return r;
	}

	string ConvertIntString(int x) {
		string s;
		int r = _dummy_sprintf(&s, "%d", x);
		assert(r != -1);
		return s;
	}

	oglplus::Mat4f DMatToOgl(const DMat &m) {
		return oglplus::Mat4f(
			DMAT_ELT(m, 0, 0), DMAT_ELT(m, 0, 1), DMAT_ELT(m, 0, 2), DMAT_ELT(m, 0, 3),
			DMAT_ELT(m, 1, 0), DMAT_ELT(m, 1, 1), DMAT_ELT(m, 1, 2), DMAT_ELT(m, 1, 3),
			DMAT_ELT(m, 2, 0), DMAT_ELT(m, 2, 1), DMAT_ELT(m, 2, 2), DMAT_ELT(m, 2, 3),
			DMAT_ELT(m, 3, 0), DMAT_ELT(m, 3, 1), DMAT_ELT(m, 3, 2), DMAT_ELT(m, 3, 3));
	}

	struct ExBase {
		int tick;
		ExBase() : tick(-1) {}
		virtual ~ExBase() {};
		virtual void Display() { tick++; };
	};

	class Shd {
		bool valid;
	public:
		Shd() : valid(false) {}
		void Invalidate() { valid = false; }
		bool IsValid() { return valid; }
	protected:
		void Validate() { valid = true; }
	};

	bool IsAttribActive(const Program &prog, const char *name) {
		VertexAttribSlot dummy;
		return oglplus::VertexAttribOps::QueryLocation(prog, name, dummy);
	}

	void OglGenErr(oglplus::Error &err) {
		std::cerr <<
			"Error (in " << err.GLSymbol() << ", " <<
			err.ClassName() << ": '" <<
			err.ObjectDescription() << "'): " <<
			err.what() <<
			" [" << err.File() << ":" << err.Line() << "] ";
		std::cerr << std::endl;
		auto i = err.Properties().begin(), e = err.Properties().end();
		for (auto &i : err.Properties())
			std::cerr << "<" << i.first << "='" << i.second << "'>" << std::endl;
		err.Cleanup();
	}

	void timerfunc(int msecTime) {
		glutPostRedisplay();
		glutTimerFunc(msecTime, timerfunc, msecTime);
	}

	template<typename ExType>
	void RunExample(int argc, char **argv) {
		glutInit(&argc, argv);
		glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
		glutInitWindowSize(G_WIN_W, G_WIN_H);
		glutInitWindowPosition(100, 100);
		glutInitContextVersion (3,3);
		glutInitContextProfile (GLUT_CORE_PROFILE);
		glewExperimental=TRUE;
		glutCreateWindow("OGLplus");
		assert (glewInit() == GLEW_OK);
		glGetError();

		static bool gFailed = false;

		static ExBase *gEx;
		EX_OGLPLUS_ERROR_WRAP_START();
		{
			gEx = new ExType();
		}
		EX_OGLPLUS_ERROR_WRAP_MIDDLE();
		{
			throw;
		}
		EX_OGLPLUS_ERROR_WRAP_END();

		/* Cannot pass exceptions through FreeGlut,
		have to workaround with error flags and FreeGlut API / Option flags. */

		auto dispfunc = []() {
			EX_OGLPLUS_ERROR_WRAP_START();
			{
				Ctx::ClearColor(0.2f, 0.2f, 0.2f, 0.0f);
				Ctx::Clear().ColorBuffer().DepthBuffer();
				gEx->Display();
				glutSwapBuffers();
			}
			EX_OGLPLUS_ERROR_WRAP_MIDDLE();
			{
				gFailed = true;
				glutLeaveMainLoop();
			}
			EX_OGLPLUS_ERROR_WRAP_END();
		};

		glutDisplayFunc(dispfunc);
		glutTimerFunc(33, timerfunc, 33);

		glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);

		glutMainLoop();

		if (gFailed)
			throw exception("Failed");
	}

};

namespace Md {

	using namespace Cruft;

	struct MdT {
		Mat4f ProjectionMatrix;
		Mat4f CameraMatrix;
		Mat4f ModelMatrix;

		MdT(const Mat4f &p, const Mat4f &c, const Mat4f &m) : ProjectionMatrix(p), CameraMatrix(c), ModelMatrix(m) {}
	};

	Program * ShaderTexSimple() {
		VertexShader vs; FragmentShader fs; Program *prog = new Program();

		string defS("#version 420\n");
		defS.append("#define MAX_BONES ");      defS.append(ConvertIntString(G_MAX_BONES_UNIFORM));     defS.append("\n");
		defS.append("#define MAX_BONES_INFL "); defS.append(ConvertIntString(G_MAX_BONES_INFLUENCING)); defS.append("\n");

		string vsSrc(defS);
		vsSrc.append(
			"uniform mat4 ProjectionMatrix, CameraMatrix, ModelMatrix;\n"
			"in vec4  Position;\n"
			"in vec2  TexCoord;\n"
			"out vec2 vTexCoord;\n"
			"\n"
			"uniform mat4 MeshMat;\n"
			"uniform mat4 BoneMat[64];\n"
			"in ivec4 BoneId;\n"
			"in  vec4 BoneWt;\n"
			"\n"
			"void main(void) {"
			"  vTexCoord = TexCoord;\n"
			""
			"  vec4 blendPos = vec4(0,0,0,0);\n"
			"  for (int i = 0;\n i < 4;\n ++i) {"
			"    blendPos += BlendWt[i] * (BoneMat[BoneId[i]] * Position);\n"
			"  }"
			""
			"  if (equal(BoneWt, vec4(0,0,0,0)))"
			"    gl_Position = ProjectionMatrix * CameraMatrix * ModelMatrix * MeshMat * Position;\n"
			"  else"
			"    gl_Position = ProjectionMatrix * CameraMatrix * ModelMatrix * blendPos;\n"
			"}"
			);
		string fsSrc(defS);
		fsSrc.append(
			"uniform sampler2D TexUnit;"
			"in vec2 vTexCoord;"
			"out vec4 fragColor;"
			"void main(void) {"
			"  vec4 t = texture(TexUnit, vTexCoord);"
			"  fragColor = vec4(t.rgb, 1.0);"
			"}"
			);

		vs.Source(vsSrc); fs.Source(fsSrc); vs.Compile(); fs.Compile(); prog->AttachShader(vs); prog->AttachShader(fs); prog->Link();
		return prog;
	}

	class ShdTexSimple : public Shd {
	public:

		class MdD {
		public:
			size_t triCnt;

			shared_ptr<Buffer> id;
			shared_ptr<Buffer> vt;

			MdD(const SectionDataEx &sde, int meshId) :
				triCnt(sde.meshIndex[meshId].size() / 3),
				id(new Buffer()),
				vt(new Buffer())
			{
				assert(sde.meshIndex[meshId].size() % 3 == 0);

				{
					id->Bind(oglplus::BufferOps::Target::Array);
					vector<GLuint> v; transform(sde.meshIndex[meshId].begin(), sde.meshIndex[meshId].end(), back_inserter(v), [](int i) { return i; });
					Buffer::Data(oglplus::BufferOps::Target::Array, v);
				}

				{
					vt->Bind(oglplus::BufferOps::Target::Array);
					vector<GLfloat> v; transform(sde.meshVert[meshId].begin(), sde.meshVert[meshId].end(), back_inserter(v), [](float i) { return i; });
					Buffer::Data(oglplus::BufferOps::Target::Array, v);
				}
			}
		};

		shared_ptr<Program> prog;
		shared_ptr<VertexArray> va;

		size_t triCnt;

		ShdTexSimple() :
			prog(shared_ptr<Program>(ShaderTexSimple())),
			va(new VertexArray()),
			triCnt(0) {}

		void Prime(const MdT &mt, const MdD &md) {
			triCnt = md.triCnt;

			va->Bind();

			md.id->Bind(oglplus::BufferOps::Target::ElementArray);

			md.vt->Bind(oglplus::BufferOps::Target::Array);
			(*prog|"Position").Setup(3, oglplus::DataType::Float).Enable();

			/* MdT */

			ProgramUniform<Mat4f>(*prog, "ProjectionMatrix") = mt.ProjectionMatrix;
			ProgramUniform<Mat4f>(*prog, "CameraMatrix") = mt.CameraMatrix;
			ProgramUniform<Mat4f>(*prog, "ModelMatrix") = mt.ModelMatrix;

			Validate();
		}

		void Draw() {
			assert(IsValid());

			prog->Use();
			Ctx::DrawElements(PrimitiveType::Triangles, triCnt * 3, oglplus::DataType::UnsignedInt);
			prog->UseNone();
		}

		void UnPrime() {
			Invalidate();

			va->Unbind();

			Buffer::Unbind(oglplus::BufferOps::Target::Array);
			Buffer::Unbind(oglplus::BufferOps::Target::ElementArray);
		}
	};

	struct Ex1 : public ExBase {
		Md::ShdTexSimple shd;
		shared_ptr<SectionDataEx> sde;
		shared_ptr<Md::MdT> mdt0, mdt1;
		shared_ptr<Md::ShdTexSimple::MdD> mdd0, mdd1;

		Ex1() {
			sde = shared_ptr<SectionDataEx>(BlendUtilMakeSectionDataEx("../tmpdata.dat"));

			assert(sde->meshName.size() == 2);
			mdd0 = shared_ptr<ShdTexSimple::MdD>(new ShdTexSimple::MdD(*sde, 0));
			mdd1 = shared_ptr<ShdTexSimple::MdD>(new ShdTexSimple::MdD(*sde, 1));
		}

		void Display() {
			ExBase::Display();

			vector<DMat> nodeWorldIdentityRoot(sde->nodeName.size(), DMat::MakeIdentity());
			vector<DMat> nodeWorldMatrix(sde->nodeName.size());
			vector<DMat> boneWorldMatrix(sde->boneName.size());
			vector<DMat> boneMeshToBoneMatrix(sde->boneName.size());

			MultiRootMatrixAccumulateWorld(sde->nodeMatrix, sde->nodeChild, sde->nodeParent, nodeWorldIdentityRoot, &nodeWorldMatrix);
			MultiRootMatrixAccumulateWorld(sde->boneMatrix, sde->boneChild, sde->boneParent, sde->meshRootMatrix, &boneWorldMatrix);
			MatrixMeshToBone(sde->meshBoneCount, nodeWorldMatrix, boneWorldMatrix, &boneMeshToBoneMatrix);

			DMat nm0 = DMat::Multiply(boneWorldMatrix[0], boneMeshToBoneMatrix[0]);

			mdt0 = shared_ptr<Md::MdT>(new Md::MdT(
				CamMatrixf::PerspectiveX(Degrees(90), GLfloat(G_WIN_W)/G_WIN_H, 1, 30),
				CamMatrixf::Orbiting(oglplus::Vec3f(0, 0, 0), 3, Degrees(float(tick * 5)), Degrees(15)),
				//DMatToOgl(nodeWorldMatrix[0])));
				DMatToOgl(nm0)));
			mdt1 = shared_ptr<Md::MdT>(new Md::MdT(
				CamMatrixf::PerspectiveX(Degrees(90), GLfloat(G_WIN_W)/G_WIN_H, 1, 30),
				CamMatrixf::Orbiting(oglplus::Vec3f(0, 0, 0), 3, Degrees(float(tick * 5)), Degrees(15)),
				DMatToOgl(nodeWorldMatrix[1])));

			shd.Prime(*mdt0, *mdd0);
			shd.Draw();
			shd.UnPrime();

			shd.Prime(*mdt1, *mdd1);
			shd.Draw();
			shd.UnPrime();
		}
	};

};

int main(int argc, char **argv) {
	using namespace Cruft;
	using namespace Md;

	RunExample<Ex1>(argc, argv);

	return EXIT_SUCCESS;
}

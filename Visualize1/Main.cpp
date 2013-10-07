#include <cstdlib>
#include <cassert>

#include <functional>
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

using namespace oglplus;
using namespace std;

class Ctx : public oglplus::Context {};

namespace Cruft {

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

		static ExBase *gEx = new ExType();
		static bool gFailed = false;

		/* Cannot pass exceptions through FreeGlut,
		have to workaround with error flags and FreeGlut API / Option flags. */

		auto dispfunc = []() {
			try {
				Ctx::ClearColor(0.2f, 0.2f, 0.2f, 0.0f);
				Ctx::Clear().ColorBuffer().DepthBuffer();
				gEx->Display();
				glutSwapBuffers();
			} catch(oglplus::Error &e) {
				if (dynamic_cast<oglplus::CompileError *>(&e))
					std::cerr << ((oglplus::CompileError &)e).Log();
				OglGenErr(e);

				gFailed = true;
				glutLeaveMainLoop();
			}
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
		VertexShader vs;
		FragmentShader fs;
		Program *prog = new Program();
		vs.Source(
			"#version 420\n\
			uniform mat4 ProjectionMatrix, CameraMatrix, ModelMatrix;\
			in vec4 Position;\
			in vec2 TexCoord;\
			out vec2 vTexCoord;\
			void main(void) {\
			vTexCoord = TexCoord;\
			gl_Position = ProjectionMatrix * CameraMatrix * ModelMatrix * Position;\
			}"
			);
		fs.Source(
			"#version 420\n\
			uniform sampler2D TexUnit;\
			in vec2 vTexCoord;\
			out vec4 fragColor;\
			void main(void) {\
			vec4 t = texture(TexUnit, vTexCoord);\
			fragColor = vec4(t.rgb, 1.0);\
			}"
			);
		vs.Compile();
		fs.Compile();
		prog->AttachShader(vs);
		prog->AttachShader(fs);
		prog->Link();
		return prog;
	}

	class ShdTexSimple : public Shd {
	public:
		shared_ptr<Program> prog;
		shared_ptr<VertexArray> va;

		size_t triCnt;

		ShdTexSimple() :
			prog(shared_ptr<Program>(ShaderTexSimple())),
			va(new VertexArray()),
			triCnt(0) {}

		void Prime(const MdT &mt) {
			//triCnt = md.triCnt;

			va->Bind();

			//md.id->Bind(oglplus::BufferOps::Target::ElementArray);

			//md.vt->Bind(oglplus::BufferOps::Target::Array);
			//(*prog|"Position").Setup(3, oglplus::DataType::Float).Enable();

			/* MdT */

			ProgramUniform<Mat4f>(*prog, "ProjectionMatrix") = mt.ProjectionMatrix;
			ProgramUniform<Mat4f>(*prog, "CameraMatrix") = mt.CameraMatrix;
			ProgramUniform<Mat4f>(*prog, "ModelMatrix") = mt.ModelMatrix;

			Validate();
		}

		void Draw() {
			assert(IsValid());

			prog->Use();
			Ctx::DrawArrays(PrimitiveType::Triangles, 0, triCnt * 3);
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
		shared_ptr<Md::MdT> mdt;

		Ex1() {}

		void Display() {
			ExBase::Display();

			mdt = shared_ptr<Md::MdT>(new Md::MdT(
				CamMatrixf::PerspectiveX(Degrees(90), GLfloat(G_WIN_W)/G_WIN_H, 1, 30),
				CamMatrixf::Orbiting(oglplus::Vec3f(0, 0, 0), 3, Degrees(float(tick * 5)), Degrees(15)),
				ModelMatrixf()));

			shd.Prime(*mdt);
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

#pragma once

#include <string>
#include <map>
#include <vector>
#include <stddef.h>


namespace eve::graphics
{

class Graphics;

// A GLSL shader
class Shader
{
public:
	enum ShaderType
	{
		eCompute,
        eVertex,
        eFragment,
        eGeometry,
        eTessCtrl,
        eTessEval
	};

	// Pointer to currently active Shader.
	static Shader *current;

	// Pointer to the default Shader.
	static Shader *defaultShaders[3];

	Shader();
	virtual ~Shader();

	int sendToVar(const std::string &name, const void *data, size_t size);
	int getFromVar(const std::string &name, void *data, size_t size);

	


}; // Shader

} // eve::graphics
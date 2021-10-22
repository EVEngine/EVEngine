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

}; // Shader

} // eve::graphics
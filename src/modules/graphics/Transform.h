#pragma once

#include <glm/glm.hpp>
#include <stack>

namespace eve::graphics
{

class Transform {
public:
    Transform();
    virtual ~Transform() {}

    void rotate(float r, glm::vec2 p);
	void scale(float all);
	void scale(glm::vec2 s);
	void translate(glm::vec2 v);
	void origin();

    void rotate3d(float r, glm::vec3 v);
	void scale3d(glm::vec3 s);
	void translate3d(glm::vec3 v);

    void push();
    void pop();
    glm::mat4 top();

protected:
    std::stack<glm::mat4> matrix;
};

} // namespace eve::graphics

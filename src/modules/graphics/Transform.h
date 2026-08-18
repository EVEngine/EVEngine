#pragma once

#include <glm/glm.hpp>
#include <stack>

namespace eve::graphics
{

/** @brief 矩阵变换栈（2D/3D）：旋转/缩放/平移 + push/pop。 */
class Transform {
public:
    Transform();
    virtual ~Transform() {}

    /** @brief 2D 绕点 (p) 旋转 r（弧度）。 */
    void rotate(float r, glm::vec2 p);
    /** @brief 2D 均匀/非均匀缩放。 */
	void scale(float all);
	void scale(glm::vec2 s);
	/** @brief 2D 平移。 */
	void translate(glm::vec2 v);
	/** @brief 重置为单位矩阵。 */
	void origin();

    /** @brief 3D 绕轴旋转 / 缩放 / 平移。 */
    void rotate3d(float r, glm::vec3 v);
	void scale3d(glm::vec3 s);
	void translate3d(glm::vec3 v);

    /** @brief 压栈 / 弹栈 / 取栈顶矩阵。 */
    void push();
    void pop();
    glm::mat4 top();

protected:
    std::stack<glm::mat4> matrix;
};

} // namespace eve::graphics

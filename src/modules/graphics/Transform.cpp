#include "graphics/Transform.h"


namespace eve::graphics
{

Transform::Transform() {
    origin(); // load I
}

void Transform::rotate(float r, glm::vec2 p) {

}

void Transform::scale(float all) {

}

void Transform::scale(glm::vec2 s) {

}

void Transform::translate(glm::vec2 v) {

}

void Transform::origin() {

}


void Transform::rotate3d(float r, glm::vec3 v) {

}

void Transform::scale3d(glm::vec3 s) {

}

void Transform::translate3d(glm::vec3 v) {

}


void Transform::push() {
    if (matrix.size() == 0) {
        matrix.push(glm::mat4{1,0,0,0,
                              0,1,0,0,
                              0,0,1,0,
                              0,0,0,1});
    } else {
        auto m = matrix.top();
        matrix.push(m);
    }
}

void Transform::pop() {
    if (matrix.size() > 0) matrix.pop();
}

glm::mat4 Transform::top() {
    if (matrix.size() > 0) return matrix.top();
    else return glm::mat4{0,0,0,0,
                          0,0,0,0,
                          0,0,0,0,
                          0,0,0,0};
}


} // namespace eve::graphics

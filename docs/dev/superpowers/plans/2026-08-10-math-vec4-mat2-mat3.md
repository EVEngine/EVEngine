# Math Vec4 / Mat2 / Mat3 / *V API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Math 模块补齐 `Vec4`、`Mat2`、`Mat3`，并为几何/碰撞/噪声/Bezier 等 API 增加 `*V`（Vec 参数）薄包装，同时暴露到 C++ 与 Squirrel。

**Architecture:** 薄包装层。新类型对齐现有 `Vec2`/`Vec3`/`Mat4`；`Math::*V` 拆分量调用已有 float 实现（或对 Vec4 直接算），多分量结果 `new` 返回。不改现有 float 签名。

**Tech Stack:** C++20、glm（`mat2`/`mat3`/`vec4`）、simplesquirrel 绑定、zeroerr（`test/math.cpp`）。

**Spec:** [docs/dev/superpowers/specs/2026-08-10-math-vec4-mat2-mat3-design.md](../specs/2026-08-10-math-vec4-mat2-mat3-design.md)

## Global Constraints

- C++ 与 Squirrel 都要；脚本/C++ 统一 **`V` 后缀**，不做同名重载。
- 保留全部现有 float API；不新增 `length4`/`dot4`/`distance4` float 版。
- 多分量结果返回新 `Vec*`；不做 `Into`/`out` 版。
- `Math::*V` 输入指针：调用方保证非空（薄包装不做空指针检查）。
- `Vec4`/`Mat2`/`Mat3` 成员对「另一操作数」空指针：对齐 `Vec2`/`Mat4`，`throw eve::Exception(...)`。
- `create_module(EVMath math)` 会扫描源文件；新增 `.cpp` 后若链接缺符号，对 `build/win32-debug` 重新 cmake configure 一次。
- **不要自动 git commit**，除非用户明确要求（覆盖 skill 模板里的 frequent commits）。
- Build / 测（Windows Debug）：

```powershell
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.357.0"
$env:Path = "$env:VULKAN_SDK\Bin;$env:Path"
cmake --build build/win32-debug --config Debug --target unit_test -j 8
.\build\win32-debug\test\Debug\unit_test.exe --testcase="^math\."
```

---

## File Structure

| File | Role |
|------|------|
| `src/modules/math/Vec4.h` / `Vec4.cpp` | 新建 Vec4 |
| `src/modules/math/Mat2.h` / `Mat2.cpp` | 新建 Mat2（`glm::mat2`） |
| `src/modules/math/Mat3.h` / `Mat3.cpp` | 新建 Mat3（`glm::mat3`） |
| `src/modules/math/Mat4.h` / `Mat4.cpp` | 增加 `transformVec4` |
| `src/modules/math/Math.h` / `Math.cpp` | 工厂、`*V`、expose 绑定 |
| `test/math.cpp` | 新类型 + 抽样 `*V` 测试 |
| `docs/usr/modules/math.md` | 文档同步（最后任务） |

---

### Task 1: Vec4 类型 + `newVec4` + 冒烟测试

**Files:**
- Create: `src/modules/math/Vec4.h`
- Create: `src/modules/math/Vec4.cpp`
- Modify: `src/modules/math/Math.h`（前向声明 + `newVec4`）
- Modify: `src/modules/math/Math.cpp`（实现 + expose）
- Modify: `test/math.cpp`

**Interfaces:**
- Consumes: 现有 `Vec2`/`Vec3` 模式
- Produces:
  - `class Vec4` with `get/setX/Y/Z/W`, `set(x,y,z,w)`, `length`, `lengthSquared`, `normalize`, `normalized`, `dot`, `distanceTo`, `add`, `sub`, `scale`, `lerpTo`, `clone`
  - `Vec4 *Math::newVec4(float x = 0.f, float y = 0.f, float z = 0.f, float w = 0.f)`

- [ ] **Step 1: 写失败测试**

在 `test/math.cpp` 增加：

```cpp
#include "math/Vec4.h"

TEST_CASE("math.vec4.basic") {
    auto *m = Math::create();
    std::unique_ptr<Vec4> a(m->newVec4(1.f, 2.f, 2.f, 4.f));
    CHECK(std::fabs(a->length() - 5.f) < 1e-5f);  // sqrt(1+4+4+16)=5
    a->normalize();
    CHECK(std::fabs(a->length() - 1.f) < 1e-5f);

    std::unique_ptr<Vec4> b(m->newVec4(1.f, 0.f, 0.f, 0.f));
    std::unique_ptr<Vec4> c(m->newVec4(0.f, 1.f, 0.f, 0.f));
    CHECK(std::fabs(b->dot(c.get())) < 1e-6f);
    std::unique_ptr<Vec4> sum(b->add(c.get()));
    CHECK(std::fabs(sum->getX() - 1.f) < 1e-5f);
    CHECK(std::fabs(sum->getY() - 1.f) < 1e-5f);
}
```

- [ ] **Step 2: 编译确认失败**

```powershell
cmake --build build/win32-debug --config Debug --target unit_test -j 8
```

Expected: 找不到 `Vec4` / `newVec4`。

- [ ] **Step 3: 实现 Vec4**

`Vec4.h`（对齐 `Vec3.h`，加 `w_`，**无** `cross`）：

```cpp
#pragma once

#include <cmath>

namespace eve::math {

class Vec4 {
public:
    Vec4() = default;
    Vec4(float x, float y, float z, float w) : x_(x), y_(y), z_(z), w_(w) {}

    float getX() const { return x_; }
    float getY() const { return y_; }
    float getZ() const { return z_; }
    float getW() const { return w_; }
    void  setX(float x) { x_ = x; }
    void  setY(float y) { y_ = y; }
    void  setZ(float z) { z_ = z; }
    void  setW(float w) { w_ = w; }
    void  set(float x, float y, float z, float w) {
        x_ = x; y_ = y; z_ = z; w_ = w;
    }

    float length() const { return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_ + w_ * w_); }
    float lengthSquared() const { return x_ * x_ + y_ * y_ + z_ * z_ + w_ * w_; }

    void  normalize();
    Vec4 *normalized() const;

    float  dot(const Vec4 *other) const;
    float  distanceTo(const Vec4 *other) const;

    Vec4 *add(const Vec4 *other) const;
    Vec4 *sub(const Vec4 *other) const;
    Vec4 *scale(float s) const;
    Vec4 *lerpTo(const Vec4 *other, float t) const;
    Vec4 *clone() const;

private:
    float x_ = 0.f, y_ = 0.f, z_ = 0.f, w_ = 0.f;
};

}  // namespace eve::math
```

`Vec4.cpp`：逐字仿 `Vec3.cpp` / `Vec2.cpp`（空指针 `throw eve::Exception("Vec4.dot: other is null")` 等）。

- [ ] **Step 4: 接入 Math**

`Math.h`：前向声明 `class Vec4;`，声明 `Vec4 *newVec4(...)`。

`Math.cpp`：
- `#include "math/Vec4.h"`
- `Vec4 *Math::newVec4(...) { return new Vec4(x, y, z, w); }`
- 在 `expose(Table)` 注册 `Vec4` 类（仿 `Vec3` 绑定，含 `getW`/`setW`，无 `cross`）
- 在 `expose(Class)`：`cls.addFunc("newVec4", &Math::newVec4);`

- [ ] **Step 5: 跑测试**

```powershell
cmake --build build/win32-debug --config Debug --target unit_test -j 8
.\build\win32-debug\test\Debug\unit_test.exe --testcase="^math\.vec4"
```

Expected: PASS。若链接缺 `Vec4.cpp`，在 `build/win32-debug` 重新跑一次 cmake configure 后再编。

---

### Task 2: Mat2 + 工厂 + 冒烟测试

**Files:**
- Create: `src/modules/math/Mat2.h`, `Mat2.cpp`
- Modify: `src/modules/math/Math.h`, `Math.cpp`
- Modify: `test/math.cpp`

**Interfaces:**
- Produces:
  - `Mat2`: `identity`, `rotate(float radians)`, `scale(float sx, float sy)`, `multiply`/`multiplied`, `transformVec2`, `get/set(0..3)`, `clone`, `raw()`
  - `Math::newMat2()`, `newMat2Rotation(float)`, `newMat2Scale(float,float)`

- [ ] **Step 1: 写失败测试**

```cpp
#include "math/Mat2.h"

TEST_CASE("math.mat2.transform") {
    auto *m = Math::create();
    std::unique_ptr<Mat2> rot(m->newMat2Rotation(float(M_PI) * 0.5f));
    std::unique_ptr<Vec2> p(m->newVec2(1.f, 0.f));
    std::unique_ptr<Vec2> out(rot->transformVec2(p.get()));
    CHECK(std::fabs(out->getX()) < 1e-4f);
    CHECK(std::fabs(out->getY() - 1.f) < 1e-4f);
}
```

- [ ] **Step 2: 确认编译失败**（同 Task 1 构建命令）

- [ ] **Step 3: 实现 Mat2**

`Mat2.h`：

```cpp
#pragma once
#include <glm/mat2x2.hpp>

namespace eve::math {
class Vec2;

class Mat2 {
public:
    Mat2();
    explicit Mat2(const glm::mat2 &m);
    void identity();
    void rotate(float radians);
    void scale(float sx, float sy);
    void multiply(const Mat2 *other);
    Mat2 *multiplied(const Mat2 *other) const;
    Vec2 *transformVec2(const Vec2 *v) const;
    float get(int index) const;
    void  set(int index, float value);
    Mat2 *clone() const;
    const glm::mat2 &raw() const { return m_; }
    glm::mat2       &raw() { return m_; }
private:
    glm::mat2 m_{1.f};
};
}  // namespace eve::math
```

`Mat2.cpp`：仿 `Mat4.cpp`：
- `rotate`: `m_ = glm::rotate(m_, radians);`（`#include <glm/gtc/matrix_transform.hpp>`）
- `scale`: `m_ = glm::scale(m_, glm::vec2(sx, sy));`
- `transformVec2`: `glm::vec2 r = m_ * glm::vec2(v->getX(), v->getY()); return new Vec2(r.x, r.y);`
- `get/set`: index `0..3`，用 `glm::value_ptr`；越界抛异常

工厂：

```cpp
Mat2 *Math::newMat2() { return new Mat2(); }
Mat2 *Math::newMat2Rotation(float radians) {
    auto *m = new Mat2(); m->rotate(radians); return m;
}
Mat2 *Math::newMat2Scale(float sx, float sy) {
    auto *m = new Mat2(); m->scale(sx, sy); return m;
}
```

绑定：`expose(Table)` 注册 `Mat2`；`expose(Class)` 注册三个工厂。

- [ ] **Step 4: 跑 `math.mat2.transform`** — Expected: PASS

---

### Task 3: Mat3 + 工厂 + 冒烟测试

**Files:**
- Create: `src/modules/math/Mat3.h`, `Mat3.cpp`
- Modify: `src/modules/math/Math.h`, `Math.cpp`
- Modify: `test/math.cpp`

**Interfaces:**
- Produces:
  - `Mat3`: `identity`, `translate(x,y)`, `rotate(radians)`, `scale(sx,sy)`, `multiply`/`multiplied`, `transformVec2`（点，w=1）、`transformVec3`, `get/set(0..8)`, `clone`
  - `Math::newMat3()`, `newMat3Translation`, `newMat3Rotation`, `newMat3Scale`

- [ ] **Step 1: 写失败测试**

```cpp
#include "math/Mat3.h"

TEST_CASE("math.mat3.transform") {
    auto *m = Math::create();
    std::unique_ptr<Mat3> mat(m->newMat3Translation(10.f, 20.f));
    std::unique_ptr<Vec2> p(m->newVec2(1.f, 2.f));
    std::unique_ptr<Vec2> out(mat->transformVec2(p.get()));
    CHECK(std::fabs(out->getX() - 11.f) < 1e-4f);
    CHECK(std::fabs(out->getY() - 22.f) < 1e-4f);
}
```

- [ ] **Step 2: 实现 Mat3**

`translate`：用仿射：`m_ = glm::translate(m_, glm::vec2(x, y));`（glm mat3 2D translate），或手写第三列。

`transformVec2`：

```cpp
glm::vec3 r = m_ * glm::vec3(v->getX(), v->getY(), 1.f);
return new Vec2(r.x, r.y);
```

`transformVec3`：`glm::vec3 r = m_ * glm::vec3(...); return new Vec3(r.x, r.y, r.z);`

工厂与绑定仿 Mat2/Mat4。

- [ ] **Step 3: 跑 `math.mat3.transform`** — Expected: PASS

---

### Task 4: `Mat4::transformVec4`

**Files:**
- Modify: `src/modules/math/Mat4.h`, `Mat4.cpp`
- Modify: `src/modules/math/Math.cpp`（expose `transformVec4`）
- Modify: `test/math.cpp`

**Interfaces:**
- Produces: `Vec4 *Mat4::transformVec4(const Vec4 *v) const`

- [ ] **Step 1: 测试**

```cpp
TEST_CASE("math.mat4.transformVec4") {
    auto *m = Math::create();
    std::unique_ptr<Mat4> mat(m->newMat4Translation(1.f, 2.f, 3.f));
    std::unique_ptr<Vec4> v(m->newVec4(0.f, 0.f, 0.f, 1.f));
    std::unique_ptr<Vec4> out(mat->transformVec4(v.get()));
    CHECK(std::fabs(out->getX() - 1.f) < 1e-4f);
    CHECK(std::fabs(out->getY() - 2.f) < 1e-4f);
    CHECK(std::fabs(out->getZ() - 3.f) < 1e-4f);
    CHECK(std::fabs(out->getW() - 1.f) < 1e-4f);
}
```

- [ ] **Step 2: 实现**

```cpp
Vec4 *Mat4::transformVec4(const Vec4 *v) const {
    if (!v) throw eve::Exception("Mat4.transformVec4: v is null");
    glm::vec4 r = m_ * glm::vec4(v->getX(), v->getY(), v->getZ(), v->getW());
    return new Vec4(r.x, r.y, r.z, r.w);
}
```

`m4.addFunc("transformVec4", &Mat4::transformVec4);`

- [ ] **Step 3: 跑测试** — Expected: PASS

---

### Task 5: 几何类 `*V`（向量运算）

**Files:**
- Modify: `src/modules/math/Math.h`, `Math.cpp`
- Modify: `test/math.cpp`

**Interfaces — 必须全部声明并实现：**

```cpp
float length2V(const Vec2 *v) const;
float length3V(const Vec3 *v) const;
float length4V(const Vec4 *v) const;
float distance2V(const Vec2 *a, const Vec2 *b) const;
float distance3V(const Vec3 *a, const Vec3 *b) const;
float distance4V(const Vec4 *a, const Vec4 *b) const;
float dot2V(const Vec2 *a, const Vec2 *b) const;
float dot3V(const Vec3 *a, const Vec3 *b) const;
float dot4V(const Vec4 *a, const Vec4 *b) const;
float cross2V(const Vec2 *a, const Vec2 *b) const;
float angle2V(const Vec2 *v) const;
float angleBetween2V(const Vec2 *a, const Vec2 *b) const;
Vec2 *normalize2V(const Vec2 *v) const;
Vec3 *normalize3V(const Vec3 *v) const;
Vec4 *normalize4V(const Vec4 *v) const;
Vec2 *rotate2V(const Vec2 *v, float radians) const;
Vec2 *polarV(float radius, float radians) const;
float cartesianRadiusV(const Vec2 *v) const;
float cartesianAngleV(const Vec2 *v) const;
```

- [ ] **Step 1: 写抽样失败测试**

```cpp
TEST_CASE("math.geometry.vecOpsV") {
    auto *m = Math::create();
    std::unique_ptr<Vec2> v(m->newVec2(3.f, 4.f));
    CHECK(std::fabs(m->length2V(v.get()) - 5.f) < 1e-5f);
    std::unique_ptr<Vec2> n(m->normalize2V(v.get()));
    CHECK(std::fabs(n->getX() - 0.6f) < 1e-5f);
    CHECK(std::fabs(n->getY() - 0.8f) < 1e-5f);
    CHECK(std::fabs(m->length2V(v.get()) - m->length2(3.f, 4.f)) < 1e-6f);

    std::unique_ptr<Vec4> v4(m->newVec4(1.f, 2.f, 2.f, 4.f));
    CHECK(std::fabs(m->length4V(v4.get()) - 5.f) < 1e-5f);
}
```

- [ ] **Step 2: 实现（薄包装示例）**

```cpp
float Math::length2V(const Vec2 *v) const { return length2(v->getX(), v->getY()); }
float Math::length4V(const Vec4 *v) const {
    return std::sqrt(v->getX()*v->getX() + v->getY()*v->getY()
                   + v->getZ()*v->getZ() + v->getW()*v->getW());
}
Vec2 *Math::normalize2V(const Vec2 *v) const {
    return new Vec2(normalize2X(v->getX(), v->getY()),
                    normalize2Y(v->getX(), v->getY()));
}
Vec2 *Math::rotate2V(const Vec2 *v, float radians) const {
    return new Vec2(rotate2X(v->getX(), v->getY(), radians),
                    rotate2Y(v->getX(), v->getY(), radians));
}
Vec2 *Math::polarV(float radius, float radians) const {
    return new Vec2(polarX(radius, radians), polarY(radius, radians));
}
```

其余方法同样拆分量调用对应 float API；`distance4V`/`dot4V`/`normalize4V` 直接用 Vec4 分量算。全部在 `expose(Class)` 注册。

- [ ] **Step 3: 跑 `math.geometry.vecOpsV`** — Expected: PASS

---

### Task 6: 碰撞 / 射线类 `*V`

**Files:**
- Modify: `src/modules/math/Math.h`, `Math.cpp`
- Modify: `test/math.cpp`

**Interfaces — 必须全部实现：**

```cpp
bool pointInCircleV(const Vec2 *p, const Vec2 *c, float radius) const;
bool pointInRectV(const Vec2 *p, const Vec2 *origin, const Vec2 *size) const;
bool circlesOverlapV(const Vec2 *c1, float r1, const Vec2 *c2, float r2) const;
bool rectsOverlapV(const Vec2 *o1, const Vec2 *s1, const Vec2 *o2, const Vec2 *s2) const;
bool circleRectOverlapV(const Vec2 *c, float radius, const Vec2 *origin, const Vec2 *size) const;
bool segmentsIntersectV(const Vec2 *a, const Vec2 *b, const Vec2 *c, const Vec2 *d) const;
float raycastCircle2V(const Vec2 *o, const Vec2 *d, const Vec2 *c, float radius) const;
float raycastRect2V(const Vec2 *o, const Vec2 *d, const Vec2 *origin, const Vec2 *size) const;
Vec2 *closestPointOnSegment2V(const Vec2 *p, const Vec2 *a, const Vec2 *b) const;

bool pointInSphereV(const Vec3 *p, const Vec3 *c, float radius) const;
bool pointInBoxV(const Vec3 *p, const Vec3 *minV, const Vec3 *maxV) const;
bool spheresOverlapV(const Vec3 *c1, float r1, const Vec3 *c2, float r2) const;
bool boxesOverlapV(const Vec3 *minA, const Vec3 *maxA, const Vec3 *minB, const Vec3 *maxB) const;
float raycastSphereV(const Vec3 *o, const Vec3 *d, const Vec3 *c, float radius) const;
float raycastBoxV(const Vec3 *o, const Vec3 *d, const Vec3 *minV, const Vec3 *maxV) const;
float raycastPlaneV(const Vec3 *o, const Vec3 *d, const Vec3 *p, const Vec3 *n) const;
Vec3 *closestPointOnSegment3V(const Vec3 *p, const Vec3 *a, const Vec3 *b) const;
```

约定：`pointInRectV` / `raycastRect2V` 的 `size` = `(w, h)`；AABB 用 `min`/`max` 两个 `Vec3`。

- [ ] **Step 1: 抽样测试**

```cpp
TEST_CASE("math.geometry.pickOverlapV") {
    auto *m = Math::create();
    std::unique_ptr<Vec2> p(m->newVec2(1.f, 1.f));
    std::unique_ptr<Vec2> c(m->newVec2(0.f, 0.f));
    CHECK(m->pointInCircleV(p.get(), c.get(), 2.f));
    CHECK(m->pointInCircleV(p.get(), c.get(), 2.f) ==
          m->pointInCircle(1.f, 1.f, 0.f, 0.f, 2.f));

    std::unique_ptr<Vec3> o(m->newVec3(0.f, 0.f, 0.f));
    std::unique_ptr<Vec3> d(m->newVec3(1.f, 0.f, 0.f));
    std::unique_ptr<Vec3> sc(m->newVec3(5.f, 0.f, 0.f));
    float tV = m->raycastSphereV(o.get(), d.get(), sc.get(), 1.f);
    float tF = m->raycastSphere(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f);
    CHECK(std::fabs(tV - tF) < 1e-5f);
}
```

- [ ] **Step 2: 实现示例**

```cpp
bool Math::pointInCircleV(const Vec2 *p, const Vec2 *c, float radius) const {
    return pointInCircle(p->getX(), p->getY(), c->getX(), c->getY(), radius);
}
Vec2 *Math::closestPointOnSegment2V(const Vec2 *p, const Vec2 *a, const Vec2 *b) const {
    return new Vec2(
        closestPointOnSegment2X(p->getX(), p->getY(), a->getX(), a->getY(), b->getX(), b->getY()),
        closestPointOnSegment2Y(p->getX(), p->getY(), a->getX(), a->getY(), b->getX(), b->getY()));
}
```

全部 `expose`。

- [ ] **Step 3: 跑 `math.geometry.pickOverlapV`** — Expected: PASS

---

### Task 7: Noise / Hash / Bezier `*V`

**Files:**
- Modify: `src/modules/math/Math.h`, `Math.cpp`
- Modify: `test/math.cpp`

**Interfaces — 必须全部实现：**

```cpp
float hash2V(const Vec2 *v) const;
float hash3V(const Vec3 *v) const;
float noise2V(const Vec2 *v) const;
float noise3V(const Vec3 *v) const;
float perlin2V(const Vec2 *v) const;
float perlin3V(const Vec3 *v) const;
float fbm2V(const Vec2 *v, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
float fbm3V(const Vec3 *v, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
float ridged2V(const Vec2 *v, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
float ridged3V(const Vec3 *v, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
float turbulence2V(const Vec2 *v, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
float voronoi2V(const Vec2 *v) const;
float voronoiEdge2V(const Vec2 *v) const;
float warpNoise2V(const Vec2 *v, float warpAmp = 1.f) const;
Vec2 *bezierQuadratic2V(float t, const Vec2 *p0, const Vec2 *p1, const Vec2 *p2) const;
Vec2 *bezierCubic2V(float t, const Vec2 *p0, const Vec2 *p1, const Vec2 *p2, const Vec2 *p3) const;
```

不加：`hash1`/`noise1`/`bezierQuadratic`/`bezierCubic` 的 `V` 版。

- [ ] **Step 1: 抽样测试**

```cpp
TEST_CASE("math.procgen.vecOpsV") {
    auto *m = Math::create();
    std::unique_ptr<Vec2> p(m->newVec2(1.5f, 2.5f));
    CHECK(std::fabs(m->noise2V(p.get()) - m->noise2(1.5f, 2.5f)) < 1e-6f);
    CHECK(std::fabs(m->hash2V(p.get()) - m->hash2(1.5f, 2.5f)) < 1e-6f);

    std::unique_ptr<Vec2> p0(m->newVec2(0.f, 0.f));
    std::unique_ptr<Vec2> p1(m->newVec2(0.f, 10.f));
    std::unique_ptr<Vec2> p2(m->newVec2(10.f, 10.f));
    std::unique_ptr<Vec2> out(m->bezierQuadratic2V(0.5f, p0.get(), p1.get(), p2.get()));
    CHECK(std::fabs(out->getX() - m->bezierQuadratic2X(0.5f, 0.f, 0.f, 0.f, 10.f, 10.f, 10.f)) < 1e-5f);
    CHECK(std::fabs(out->getY() - m->bezierQuadratic2Y(0.5f, 0.f, 0.f, 0.f, 10.f, 10.f, 10.f)) < 1e-5f);
}
```

- [ ] **Step 2: 实现并 expose**

```cpp
float Math::noise2V(const Vec2 *v) const { return noise2(v->getX(), v->getY()); }
Vec2 *Math::bezierQuadratic2V(float t, const Vec2 *p0, const Vec2 *p1, const Vec2 *p2) const {
    return new Vec2(
        bezierQuadratic(t, p0->getX(), p1->getX(), p2->getX()),
        bezierQuadratic(t, p0->getY(), p1->getY(), p2->getY()));
}
```

- [ ] **Step 3: 跑 `math.procgen.vecOpsV` + 全 math 回归**

```powershell
.\build\win32-debug\test\Debug\unit_test.exe --testcase="^math\."
```

Expected: 全部 PASS。

---

### Task 8: 用户文档同步

**Files:**
- Modify: `docs/usr/modules/math.md`

- [ ] **Step 1:** 在概述中把 `Vec2/Vec3/Mat4` 改为含 `Vec4`/`Mat2`/`Mat3`。
- [ ] **Step 2:** 在 API 列表中补充工厂、`*V` 代表项，并注明「脚本无重载，Vec 参数版统一 `V` 后缀；与对应 float API 数值一致」。
- [ ] **Step 3:** 可选加一小段示例：

```squirrel
local math = eve.Math();
local p = math.newVec2(1, 1);
local c = math.newVec2(0, 0);
if (math.pointInCircleV(p, c, 2.0)) {
    // ...
}
```

---

## Spec Coverage Checklist

| Spec 项 | Task |
|---------|------|
| Vec4 | 1 |
| Mat2 + 工厂 | 2 |
| Mat3 + 工厂 | 3 |
| Mat4::transformVec4 / newVec4 | 1, 4 |
| 几何 `*V` | 5 |
| 碰撞/射线 `*V` | 6 |
| noise/hash/bezier `*V` | 7 |
| 无 length4 float | 5（直接算） |
| Squirrel 绑定 | 1–7 各任务 expose |
| 测试冒烟 + 抽样 | 1–7 |
| 文档 | 8 |

## Self-Review Notes

- 无 TBD/TODO 占位。
- `*V` 清单与 spec 表对齐；标量 hash1/noise1/bezier 不加 V。
- 类型名与方法名在各任务 Interfaces 中一致。

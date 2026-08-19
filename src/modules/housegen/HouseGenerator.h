#pragma once

#include "housegen/HouseComponentLibrary.h"

#include <string>

namespace eve::housegen {

class HouseLayout;

/** @brief 布局生成器：把 HouseRequest 解析为 HouseLayout（组件库驱动）。 */
class HouseGenerator {
public:
    /** @brief 绑定可选组件库（生成时也可用 setLibrary）。 */
    explicit HouseGenerator(const HouseComponentLibrary *library = nullptr) : library_(library) {}
    /** @brief 更换组件库。 */
    void setLibrary(const HouseComponentLibrary *library) { library_ = library; }
    /** @brief 当前组件库。 */
    const HouseComponentLibrary *library() const { return library_; }
    /** @brief 生成布局；失败返回 false 并写入 error。 */
    bool generate(const HouseRequest &request, HouseLayout &out, std::string *error = nullptr) const;

private:
    const HouseComponentLibrary *library_ = nullptr;
};

}  // namespace eve::housegen

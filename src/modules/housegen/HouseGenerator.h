#pragma once

#include "common/Result.h"
#include "housegen/HouseComponentLibrary.h"

#include <string>

namespace eve::housegen {

class HouseLayout;

/** @brief 布局生成器：把 HouseRequest 解析为 HouseLayout（组件库驱动）。 */
class HouseGenerator {
public:
    /** @brief 绑定生成所需的组件库；库的生命周期必须覆盖本生成器。 */
    explicit HouseGenerator(const HouseComponentLibrary &library) : library_(library) {}
    /** @brief 当前组件库。 */
    [[nodiscard]] const HouseComponentLibrary &library() const noexcept { return library_; }
    /** @brief 生成布局；失败时不修改输出布局并返回结构化诊断。 */
    [[nodiscard]] eve::Result<void> generate(const HouseRequest &request,
                                               HouseLayout &out) const;

private:
    const HouseComponentLibrary &library_;
};

}  // namespace eve::housegen

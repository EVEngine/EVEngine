#pragma once

#include "dialogue/Dialogue.h"

#include <string>

namespace eve::dialogue {

/**
 * .dnut → pools 表的 C++ 解析器。
 *
 * 把可读的对话内容语法（pool / when 条件分组 / speaker: "文本" / 属性）编译为
 * Dialogue::loadPoolsFromData 所需的 DataValue 根表，不引入第二套运行时。
 * 失败时 error 形如 "path:行: 信息"。
 */
bool parseDnut(const std::string &source, const std::string &path, DataValue &outRoot,
               std::string &error);

}  // namespace eve::dialogue

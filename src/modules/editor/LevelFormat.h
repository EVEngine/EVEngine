#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {
class LevelDocument;

/** @brief Plug-in contract for importing and exporting a level representation. */
class LevelFormat {
public:
    virtual ~LevelFormat()                                                                         = default;
    virtual std::string                    id() const                                              = 0;
    virtual std::vector<std::string>       extensions() const                                      = 0;
    virtual bool                           canRead(const std::string& text) const                  = 0;
    virtual std::unique_ptr<LevelDocument> read(const std::string& text, std::string& error) const = 0;
    virtual bool write(const LevelDocument& level, std::string& text, std::string& error) const    = 0;
};

/**
 * @brief Registry and conversion gateway for level formats.
 *
 * Own formats may be registered from C++ with registerFormat(). Built-ins are
 * `eve.level` (lossless EVEngine JSON) and `tiled.json` (Tiled JSON maps).
 */
class LevelFormatRegistry {
public:
    LevelFormatRegistry();
    void                           registerFormat(std::unique_ptr<LevelFormat> format);
    int                            getFormatCount() const { return static_cast<int>(formats_.size()); }
    std::string                    getFormatId(int index) const;
    std::string                    detect(const std::string& path, const std::string& text) const;
    std::unique_ptr<LevelDocument> decode(const std::string& format, const std::string& text,
                                          std::string* error = nullptr) const;
    std::string encode(const std::string& format, const LevelDocument& level, std::string* error = nullptr) const;
    std::unique_ptr<LevelDocument> load(const std::string& path, const std::string& format = {},
                                        std::string* error = nullptr) const;
    bool save(const std::string& path, const LevelDocument& level, const std::string& format = {},
              std::string* error = nullptr) const;

private:
    const LevelFormat*                        find(const std::string& id) const;
    std::vector<std::unique_ptr<LevelFormat>> formats_;
};
}  // namespace eve::editor

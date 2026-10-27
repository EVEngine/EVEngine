#pragma once

#include "common/Module.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::modelconverter {

/**
 * Conversion parameters, mirroring procgen::Params. Keys are strings, stored as
 * strings (no overloads); typed setters/getters are provided for Squirrel.
 */
class ModelConverterParams {
public:
    void        setInt(const std::string &key, int value);
    void        setFloat(const std::string &key, float value);
    void        setString(const std::string &key, const std::string &value);
    bool        has(const std::string &key) const;
    int         getInt(const std::string &key, int defaultValue) const;
    float       getFloat(const std::string &key, float defaultValue) const;
    std::string getString(const std::string &key, const std::string &defaultValue) const;

    /** Serialize all values as a JSON object string (for the job file). */
    std::string toJson() const;

private:
    std::unordered_map<std::string, std::string> values_;
};

/**
 * EVEngine native plugin that drives Blender's Python package (bpy) to convert
 * a primitive model into a richer one. Loaded via eve.Plugins.load().
 *
 * The plugin spawns a Python subprocess (`python -m eve_blender_converter`) with
 * a plain-text job file, and reads back a plain-text result file. This keeps the
 * plugin dependency-free (no Poco / no JSON parser).
 */
class ModelConverter : public Module {
public:
    Module_REG(ModelConverter);
    ModelConverter();
    ~ModelConverter() override = default;

    ModelConverterParams *newParams();

    /**
     * @param converterDir     Directory containing converter folders (manifest.json).
     * @param pythonExe        Python executable that can `import bpy`.
     * @param pythonRuntimeDir Directory containing the `eve_blender_converter` package.
     * @param tempDir          Scratch dir for job/result files ("" -> system temp).
     */
    bool configure(const std::string &converterDir, const std::string &pythonExe,
                   const std::string &pythonRuntimeDir, const std::string &tempDir = "");

    /** Verify python + bpy are usable. */
    bool check();

    int         getConverterCount() const;
    std::string getConverterId(int index) const;
    bool        hasConverter(const std::string &id) const;

    /** Run a converter. inputModel/outputModel are file paths. */
    bool convert(const std::string &converterId, const std::string &inputModel,
                 const std::string &outputModel, const std::string &format,
                 const ModelConverterParams *params);

    std::string lastError() const;
    /** Last successful output path (set by convert()). */
    std::string lastOutput() const;

private:
    bool runPython(const std::vector<std::string> &args, std::string &capturedError);
    bool spawn(const std::string &command);

    std::string converterDir_;
    std::string pythonExe_;
    std::string pythonRuntimeDir_;
    std::string tempDir_;

    mutable std::string              lastError_;
    std::string                      lastOutput_;
    mutable std::vector<std::string> converterIdsCache_;
};

}  // namespace eve::modelconverter

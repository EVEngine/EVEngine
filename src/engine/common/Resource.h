#pragma once
#include "Object.h"

#include <string>
#include <map>
#include <vector>

namespace eve {

/**
 * @brief Resource is a game object that is managed by the ResourceManager.
 * It can be loaded from a file or generated at runtime.
 * If the resource is loaded from file, a monitor will be created to watch the file changes.
 * When the file is modified, the resource will be reloaded.
 * If the resource is generated at runtime, it can depend on a few other resources
 * and the ResourceManager will update the resource when the dependencies are changed.
 *
 * Resource ID format:
 * - File: file://path/to/file
 * - Generated: res://category/name
 * - Online: http://example.com/path/to/resource
 * - Save: save://path/name
 * - Config: config://name
 */
class Resource : public Object {
public:
    virtual ~Resource() = 0 {}

    std::string getUri() const { return uri; }
	std::vector<eve::ref<Resource>> getDependencies() const { return dependencies; }

	virtual void addDependency(eve::ref<Resource> resource) { dependencies.push_back(resource); }
protected:
    Resource(std::string uri) : uri(uri) {}

    std::string uri;
	std::vector<eve::ref<Resource>> dependencies;
};


/**
 * @brief ResourceManager is a singleton that manages all resources in the game.
 * It provides a way to load, reload, and unload resources.
 */
class ResourceManager {
public:
	static ResourceManager& getInstance();

	/**
	 * @brief Load a resource from a URI.
	 * If the resource is already loaded, return the existing resource.
	 * If the resource is not loaded, create a new resource and load it.
	 * If the resource is a file, a monitor will be created to watch the file changes.
	 */
	ref<Resource> get(std::string uri);

	/**
	 * @brief Unload a resource from a URI.
	 * If the resource is already loaded, unload it.
	 * If the resource is not loaded, do nothing.
	 */
	void unload(std::string uri);

protected:
	std::map< std::string, ref<Resource> > resources;
};

}  // namespace eve

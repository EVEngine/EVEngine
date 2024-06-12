#include "common/Resource.h"

namespace eve {

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    return instance;
}

ref<Resource> ResourceManager::get(std::string uri) {
    auto it = resources.find(uri);
    if (it != resources.end()) {
        return it->second;
    }

    Resource* resource = nullptr;
    // if (uri.starts_with("file://") == 0)
    //     resource = new FileResource(uri);
    // if (uri.starts_with("res://") == 0)
    //     resource = new GeneratedResource(uri);
    // if (uri.starts_with("http://") == 0)
    //     resource = new OnlineResource(uri);
    // if (uri.starts_with("save://") == 0) 
    //     resource = new SavedResource(uri);
    // if (uri.starts_with("config://") == 0) 
    //     resource = new ConfigResource(uri);

    if (resource != nullptr)
        resources[uri] = resource;
    return resource;
}

void ResourceManager::unload(std::string uri) {
    auto it = resources.find(uri);
    if (it != resources.end()) {
        resources.erase(it);
    }
}



}  // namespace eve

#pragma once

namespace eve
{

class Resource {
public:
	virtual ~Resource() {}
	virtual void* getHandle() const = 0;
};

} // namespace eve

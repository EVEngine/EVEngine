
#pragma once

#include "common/Data.h"
#include <cstddef>

namespace eve
{
namespace data
{

/**
 * @brief Contains a reference to a subsection of an existing Data object.
 **/
class DataView : public eve::Data
{
public:
	DataView(Data *data, size_t offset, size_t size);
	DataView(const DataView &d);
	virtual ~DataView();

	// Implements Data.
	DataView *clone() const override;
	void *getData() const override;
	size_t getSize() const override;

private:
	Data* data;
	size_t offset;
	size_t size;

}; // DataView

} // data
} // eve

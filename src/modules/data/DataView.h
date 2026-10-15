
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
	/** @brief Data view. */
	DataView(Data *data, size_t offset, size_t size);
	/** @brief Data view. */
	DataView(const DataView &d);
	/** @brief Data view. */
	virtual ~DataView();

	// Implements Data.
	/** @brief Deep copy. @ownership Caller deletes. */
	DataView *clone() const override;
	/** @brief Returns the data. */
	void *getData() const override;
	/** @brief Returns the size. */
	size_t getSize() const override;

private:
	Data* data;
	size_t offset;
	size_t size;

}; // DataView

} // data
} // eve

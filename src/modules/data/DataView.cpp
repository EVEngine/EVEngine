

#include "DataView.h"
#include "common/Exception.h"

namespace eve
{
namespace data
{


DataView::DataView(Data *data, size_t offset, size_t size)
	: data(data)
	, offset(offset)
	, size(size)
{
	if (offset >= data->getSize() || size > data->getSize() || offset > data->getSize() - size)
		throw love::Exception("Offset and size of Data View must fit within the original Data's size.");

	if (size == 0)
		throw love::Exception("DataView size must be greater than 0.");
}

DataView::DataView(const DataView &d)
	: data(d.data)
	, offset(d.offset)
	, size(d.size)
{
}

DataView::~DataView()
{
}

DataView *DataView::clone() const
{
	return new DataView(*this);
}

void *DataView::getData() const
{
	return (uint8 *) data->getData() + offset;
}

size_t DataView::getSize() const
{
	return size;
}

} // data
} // eve

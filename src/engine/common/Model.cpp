#include "common/Model.h"
#include <iostream>

namespace eve
{

Model::Model(ssq::Object object)
    : object(object)
{
    printf("Model::Model(ssq::Object object)\n");
}


} // namespace eve

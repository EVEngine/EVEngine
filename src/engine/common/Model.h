#pragma once

#include "common/Object.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include <map>
#include <list>
#include <vector>

namespace eve
{


class Model : public Object
{
public:
    Model(ssq::Object object);
protected:

    ssq::Object object;
};

class ModelClass : public Model
{
public:
    std::vector<ssq::Object> attrs;
};

class ModelVector : public Model
{
public:

};

class ModelMap : public Model
{
public:

};




class ModelManager
{
public:


protected:
    std::list<std::map<Model*, std::vector<Object*>>> models;
};



} // namespace eve

#pragma once

#include "common/Object.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include <map>
#include <list>
#include <vector>

namespace eve
{

/** @brief Script-backed model: wraps a Squirrel object (table/class instance). */
class Model : public Object
{
public:
    /** @brief Wraps an existing Squirrel object. */
    Model(ssq::Object object);
protected:

    ssq::Object object;
};

/** @brief Model with a list of Squirrel attribute objects. */
class ModelClass : public Model
{
public:
    std::vector<ssq::Object> attrs;
};

/** @brief Model that maps to a Squirrel array. */
class ModelVector : public Model
{
public:

};

/** @brief Model that maps to a Squirrel table (key/value). */
class ModelMap : public Model
{
public:

};

/** @brief Registry tracking Model → Object relationships. */
class ModelManager
{
public:


protected:
    std::list<std::map<Model*, std::vector<Object*>>> models;
};



} // namespace eve

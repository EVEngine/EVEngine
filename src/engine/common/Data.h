#pragma once

namespace eve {

class Data {
public:
    /**
     * Destructor.
     **/
    virtual ~Data() {}

    /**
     * Creates a duplicate of Data derived class instance.
     **/
    virtual Data *clone() const = 0;

    /**
     * Gets a pointer to the data. This pointer will obviously not
     * be valid if the Data object is destroyed.
     **/
    virtual void *getData() const = 0;

    /**
     * Gets the size of the Data in bytes.
     **/
    virtual size_t getSize() const = 0;

};

}  // namespace eve

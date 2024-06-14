#pragma once

#include <type_traits>

namespace eve
{

class Object;
template <typename T>
concept DerivedFromObject = std::is_base_of_v<eve::Object, T>;

/**
 * @brief Object is the base class for all game objects.
 * It provides reference counting and dirty flag for update.
 */
class Object {
public:
    Object() { update = this; }

    /**
     * @brief Update the current object with a replacement.
     * You don't need to care about the reference to the old object
     * since it will be automatically updated in the ref smart pointer.
     */
    void setUpdate(Object *update) { this->update = update; }

protected:
    virtual ~Object() {}

    template <typename T>
    friend class ref;

    bool isDirty() const { return this != update; }
    Object *getUpdate() const { return update; }
    
    void ref() { ++ref_count; }
    void unref() { if (--ref_count == 0) delete this; }
    
    Object* update;
    unsigned ref_count = 0;
};


/**
 * @brief ref is a smart pointer that automatically calls ref() and unref() on the object.
 * Type T must be a subclass of Object.
 */
template <typename T>
class ref {
public:
    static_assert( DerivedFromObject<T> );

    // Default constructor creates a null pointer.
    ref() : ptr(nullptr) {}

    // Constructor from a raw pointer.
    ref(T* ptr) : ptr(ptr) { ptr->ref(); }

    // Copy constructor.
    ref(const ref& other) : ptr(other.ptr) { ptr->ref(); }

    // Destructor. will call unref() on the object.
    ~ref() { ptr->unref(); }

    // Assignment operator. The old object will be unref() and the new one will be ref().
    ref& operator=(const ref& other) {
        if (ptr != other.ptr) {
            ptr->unref();
            ptr = other.ptr;
            ptr->ref();
        }
        return *this;
    }

    // Assignment operator from a raw pointer.
    ref& operator=(T* other) {
        if (ptr != other) {
            ptr->unref();
            ptr = other;
            ptr->ref();
        }
        return *this;
    }

    operator T*()    { checkDirty(); return ptr; }
    T* operator->()  { checkDirty(); return ptr; }
    T& operator*()   { checkDirty(); return *ptr; }

    T* get()  { checkDirty(); return ptr; }
protected:
    void checkDirty() { if (ptr->isDirty()) ptr = ptr->getUpdate(); }

    T* ptr;
};


} // namespace eve

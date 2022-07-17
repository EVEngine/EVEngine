#pragma once

namespace eve
{

class Object {
public:
    Object() { update = this; }

    bool isDirty() const { return this != update; }
    Object *getUpdate() const { return update; }
    void setUpdate(Object *update) { this->update = update; }

    void ref() { ++ref_count; }
    void unref() { if (--ref_count == 0) delete this; }

protected:
    virtual ~Object() {}
    Object* update;
    unsigned ref_count = 0;
};


template <typename T>
class ref {
public:
    ref(T* ptr) : ptr(ptr) { ptr->ref(); }
    ref(const ref& other) : ptr(other.ptr) { ptr->ref(); }
    ~ref() { ptr->unref(); }

    ref& operator=(const ref& other) {
        if (ptr != other.ptr) {
            ptr->unref();
            ptr = other.ptr;
            ptr->ref();
        }
        return *this;
    }

    ref& operator=(T* other) {
        if (ptr != other) {
            ptr->unref();
            ptr = other;
            ptr->ref();
        }
        return *this;
    }

    operator T*() const   { checkDirty(); return ptr; }
    T* operator->() const { checkDirty(); return ptr; }
    T& operator*() const  { checkDirty(); return *ptr; }

protected:
    void checkDirty() { if (ptr->isDirty()) ptr = ptr->getUpdate(); }

    T* ptr;
};


} // namespace eve

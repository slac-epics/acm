#ifndef ACM_UTIL_H
#define ACM_UTIL_H

#include <sstream>
#include <memory>
#include <algorithm>

#include <epicsGuard.h>
#include <epicsMutex.h>

#if __cplusplus<201103L
#  define override
#  define final
#endif

namespace util {
/** A semi-hack to help with migration from std::auto_ptr to std::unique_ptr,
 * and avoid copious deprecation warning spam
 * which may be hiding legitimate issues.
 *
 * Provides util::auto_ptr<T> and util::swap()
 *
 * util::auto_ptr<T> is std::auto_ptr<T> for c++98
 * and std::unique_ptr<T> for >= c++11.
 *
 * util::swap() is the only supported operation.
 * copy/assignment/return are not supported
 * (use auto_ptr or unique_ptr explicitly).
 */
#if __cplusplus>=201103L
template<typename T>
using auto_ptr = std::unique_ptr<T>;
template<typename T>
static inline void swap(auto_ptr<T>& lhs, auto_ptr<T>& rhs) {
    lhs.swap(rhs);
}
#else
using std::auto_ptr;
template<typename T>
static inline void swap(auto_ptr<T>& lhs, auto_ptr<T>& rhs) {
    auto_ptr<T> temp(lhs);
    lhs = rhs;
    rhs = temp;
}
#endif

// like std::vector<T> with stable capacity and resize() w/o initialization.
// only works for POD types.
template<typename T>
class Buffer {
    T* buf;
    size_t mark, limit;
public:

    Buffer() :buf(0), mark(0u), limit(0u) {}
    explicit Buffer(size_t limit):buf(new T[limit]), mark(0u), limit(limit) {}
    Buffer(const Buffer& o)
        :buf(!o.buf ? NULL : new T[o.limit])
        ,mark(o.mark)
        ,limit(o.limit)
    {
        if(buf)
            std::copy(o.buf,
                      o.buf+mark,
                      buf);
    }
    Buffer& operator=(const Buffer& o)
    {
        if(this!=&o) {
            T* temp = !o.buf ? NULL : new T[o.limit];
            if(temp)
                std::copy(temp,
                          temp+mark,
                          o.buf);

            delete[] buf;
            buf = temp;
            mark = o.mark;
            limit = o.limit;
        }
        return *this;
    }
    ~Buffer() { delete[] buf; }

    void swap(Buffer& o) {
        if(this!=&o) {
            std::swap(buf, o.buf);
            std::swap(mark, o.mark);
            std::swap(limit, o.limit);
        }
    }

    void reserve(size_t limit) {
        T* temp = new T[limit];
        std::swap(temp, buf);
        mark = 0u;
        this->limit = limit;
        delete[] temp;
    }

    size_t size() const { return mark; }
    bool empty() const { return mark==0u; }
    size_t capacity() const { return limit; }

    T* data() { return buf; }
    const T* data() const { return buf; }

    void resize(size_t s) {
        if(s<=limit)
            mark = s;
        else
            throw std::invalid_argument("Can't set size() in excess of capacity()");
    }

    T& operator[](size_t i) { return buf[i]; }
    const T&  operator[](size_t i) const { return buf[i]; }

    T& at(size_t i) {
        if(i<mark)
            return buf[i];
        else
            throw std::out_of_range("index out of range");
    }
    const T& at(size_t i) const {
        if(i<mark)
            return buf[i];
        else
            throw std::out_of_range("index out of range");
    }
};

} // namespace util

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

//! in-line string builder (eg. for exception messages)
//! eg. @code throw std::runtime_error(SB()<<"Some message"<<42); @endcode
struct SB {
    std::ostringstream strm;
    SB() {}
    operator std::string() const { return strm.str(); }
    template<typename T>
    SB& operator<<(T i) { strm<<i; return *this; }
};

#endif // ACM_UTIL_H

#ifndef OBJECT_POOL
#define OBJECT_POOL

#include <memory>
#include <deque>

#ifdef CONFIG_PARALLEL
#include <tbb/concurrent_queue.h>
#endif

template <typename T>
class Object_pool {
private:
#ifdef CONFIG_PARALLEL
    tbb::concurrent_queue<std::shared_ptr<T>> pool;
#else
    std::deque<std::shared_ptr<T>> pool;
#endif

public:
    template <typename... Args>
    std::shared_ptr<T> acquire(Args&&... args) {
#ifdef CONFIG_PARALLEL
        std::shared_ptr<T> obj;
        if (pool.try_pop(obj)) {
            obj->reset(std::forward<Args>(args)...); // Initialize the object
            return obj;
        }
        return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
#else
        if (pool.empty()) {
            return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
        }
        std::shared_ptr<T> obj = pool.back();
        pool.pop_back();
        obj->reset(std::forward<Args>(args)...); // Initialize the object
        return obj;
#endif
    }

    void release(const std::shared_ptr<T>& obj) {
        if (!obj.unique())
            return; // Do not release if the object is shared
#ifdef CONFIG_PARALLEL
        pool.push(obj);
#else
        pool.emplace_back(obj);
#endif
    }
};

#endif // !OBJECT_POOL
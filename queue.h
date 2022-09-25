#include <list>
#include <mutex>
#include <queue>

template <typename T>
class Queue {
private:
    std::mutex m;
    std::list<T> q;
public:
    Queue() : m(), q() {}

    void push(T element) {
        const std::lock_guard<std::mutex> lock(m);
        q.push_back(element);
    }

    // User should check if empty first
    T poll() {
        const std::lock_guard<std::mutex> lock(m);
        T toReturn = q.front();
        q.pop_front();
        return toReturn;
    }

    // Single producer, single consumer, so empty method is safe
    bool isEmpty() {
        return q.empty();
    }

};
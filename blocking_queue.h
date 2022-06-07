#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H
#include <variant>
#include <optional>
#include <functional>
#include <exception>
#include <queue>

#include <boost/thread.hpp>

/* Szablon klasy implementująca kolejkę blokującą. */
template<class T>
class BlockingQueue {
private:
    boost::mutex mutex;
    boost::condition_variable pop_q;
    std::queue<T> q{};
public:
    BlockingQueue() {};

    /* Metoda atomowo umieszcza element na końcu kolejki.
     * argumenty:
     * - v - element do wrzucenia na koniec kolejki
     */
    void push(T &v) {
        boost::unique_lock<boost::mutex> lock(mutex);
        q.push(v);
        pop_q.notify_one();
    }

    void push(T &&v) {
        boost::unique_lock<boost::mutex> lock(mutex);
        q.push(v);
        pop_q.notify_one();
    }

    /* Metoda atomowo usuwa element z początku kolejki i go zwraca. W przypadku,
     * gdy kolejka jest pusta wątek jest wstrzymywany.
     * return - wartość z początku kolejki.
     */
    T pop() {
        boost::unique_lock<boost::mutex> lock(mutex);
        while (q.empty()) {
            pop_q.wait(lock);
        }
        T v = q.front();
        q.pop();
        pop_q.notify_one();
        return v;
    }
};

#endif // BLOCKING_QUEUE
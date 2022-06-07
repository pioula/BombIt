#ifndef LATCH_H
#define LATCH_H
#include <variant>
#include <optional>
#include <functional>
#include <exception>
#include <queue>

#include <boost/thread.hpp>

// Klasa implementująca latch.
class Latch {
private:
    mutable boost::mutex mutex;
    mutable boost::condition_variable waiter;
    int number_of_events; // Liczba wydarzeń na które czekają wątki.
public:
    Latch(int _number_of_events) :
        number_of_events(_number_of_events) {};

    // Metoda, która atomowo zmniejsza liczbę wydarzeń.
    void decrease() {
        boost::unique_lock<boost::mutex> lock(mutex);
        number_of_events--;
        if (number_of_events == 0)
            waiter.notify_one();
    }

    // Metoda, która wstrzymuje wywołującą ją proces do czasu gdy nie wykona się
    // number_of_events wydarzeń.
    void wait() const {
        boost::unique_lock<boost::mutex> lock(mutex);
        while (number_of_events > 0)
            waiter.wait(lock);
    }
};

#endif // LATCH
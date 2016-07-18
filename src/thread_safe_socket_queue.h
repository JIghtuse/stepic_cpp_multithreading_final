#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

class ThreadSafeSocketQueue {
public:
    ThreadSafeSocketQueue()
    {
    }
    void push(int socket);
    int wait_and_pop();

private:
    std::mutex mut{};
    std::queue<int> sockets{};
    std::condition_variable socket_available{};
};

#include "request.hpp"
#include <string>
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <optional>
#include <sys/socket.h>
#include <netinet/in.h>

class TaskQueue{
public:
    struct Entry{
        sockaddr_in client_addr;
        Request req;
        std::string key;
        std::optional<std::string> value;
    
        Entry() : client_addr(), req(), key(), value(std::nullopt) {
            client_addr.sin_family = AF_INET;
        }
    
        Entry(const sockaddr_in &addr, Request req, const std::string &key, const std::optional<std::string> &value) :
            client_addr(addr), req(req), key(key), value(value) {}
    };
    
private:
    std::array<Entry, 20> queue;
    int head;
    int tail;
    std::atomic<int> size;
        
public:
    TaskQueue(): head(1), tail(0), size(0){}

    void add_entry(const sockaddr_in &client_addr, Request req, const std::string &key, const std::optional<std::string> &value){
        while((head + 1) % 20 == tail){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        queue[head] = Entry(client_addr, req, key, value);
        head = (head + 1) % 20;
    }

    void add_entry(Entry entry){
        while((head + 1) % 20 == tail){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        queue[head] = entry;
        head = (head + 1) % 20;
    }

    Entry read_entry(){
        while(head == tail){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        Entry entry = queue[tail];
        queue[tail] = Entry();
        tail = (tail + 1) % 20;
        return entry;
    }
};

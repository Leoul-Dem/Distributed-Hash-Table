#include "request.hpp"
#include <string>
#include <optional>
#include <any>
#include <array>
#include <atomic>

class TaskQueue{
private:   
    struct Entry{
        int ip;
        Request req;
        std::string key;
        std::optional<std::any> value;

        Entry() : ip(0), req(), key(), value(std::nullopt) {}

        Entry(int ip, Request req, std::string &key) :
            ip(ip), req(req), key(key), value(std::nullopt) {}

        Entry(int ip, Request req, std::string &key, std::any value) :
            ip(ip), req(req), key(key), value(value) {}       
    };

    std::array<Entry, 20> queue;
    std::atomic<int> head;
    std::atomic<int> tail;

    void increment_head(){
        int old_val = head.load(std::memory_order_relaxed);
        int new_val;
        do {
            new_val = (old_val + 1) % 20;
        } while (!head.compare_exchange_weak(old_val, new_val, 
                 std::memory_order_release, 
                 std::memory_order_relaxed));
    }


    void decrement_tail(){
        int old_val = tail.load(std::memory_order_relaxed);
        int new_val;
        do {
            new_val = (old_val - 1 + 20) % 20;
        } while (!tail.compare_exchange_weak(old_val, new_val, 
                 std::memory_order_release, 
                 std::memory_order_relaxed));
    }

public:
    TaskQueue(): head(1), tail(0) {}
    
    void push_entry(){
        
    }
    
    Entry pop_entry_front(){
        
    }
    
    Entry pop_entry_back(){
        
    }
};
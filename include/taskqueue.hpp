#include "request.hpp"
#include <string>
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <optional>

/*
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
    std::atomic<int> size;

    int increment_head(){
        int old_val;
        int new_val;
        do {
            old_val = head.load(std::memory_order_relaxed);
            new_val = (old_val + 1) % 20;
        } while (!head.compare_exchange_weak(old_val, new_val,
                 std::memory_order_release,
                 std::memory_order_relaxed));
        return old_val;
    }

    int decrement_head(){
        int old_val, new_val;
        do {
            old_val = head.load(std::memory_order_relaxed);
            new_val = (old_val - 1 + 20) % 20;
        } while (!head.compare_exchange_weak(old_val, new_val,
                 std::memory_order_release,
                 std::memory_order_relaxed));
        return old_val;
    }


    int increment_tail(){
        int old_val;
        int new_val;
        do {
            old_val = tail.load(std::memory_order_relaxed);
            new_val = (old_val + 1) % 20;
        } while (!tail.compare_exchange_weak(old_val, new_val,
                 std::memory_order_release,
                 std::memory_order_relaxed));
        return old_val;
    }

public:
    TaskQueue(): head(1), tail(0) {}

    void write(int ip, Request req, std::string key, std::any value){
        int idx = increment_head();
        queue[idx] = Entry(ip, req, key, value);
    }

    void write(int ip, Request req, std::string key){
        int idx = increment_head();
        queue[idx] = Entry(ip, req, key);
    }

    void write(Entry entry){
        int idx = increment_head();
        queue[idx] = entry;
    }

    Entry read(){
        int idx = increment_tail();
        Entry entry = queue[idx];
        if(queue[idx].ip != 0){

        }

    }

    Entry steal(){

    }
};

*/


class TaskQueue{
public:
    struct Entry{
        int client_fd;
        Request req;
        std::string key;
        std::optional<std::string> value;
    
        Entry() : client_fd(0), req(), key(), value(std::nullopt) {}
    
        Entry(int ip, Request req, std::string &key, std::optional<std::string> value) :
            client_fd(ip), req(req), key(key), value(value) {}
    };
    
private:
    std::array<Entry, 20> queue;
    int head;
    int tail;
    std::atomic<int> size;
        
public:
    TaskQueue(): head(1), tail(0), size(0){}

    void add_entry(int ip, Request req, std::string key, std::optional<std::string> value){
        while((head + 1) % 20 == tail){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        queue[head] = Entry(ip, req, key, value);
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

#include <optional>
#include <any>
#include <array>
#include <atomic>
#include <thread>
#include <chrono>

class ResponseQueue{
private:
    struct Entry{
        int ip;
        std::any resp;

        Entry() : ip(0), resp(std::nullopt) {}

        Entry(int ip, std::any resp) : ip(ip), resp(resp) {}
    };

    std::array<Entry, 20> queue;
    int head;
    int tail;
    std::atomic<int> size;

public:
    ResponseQueue(): head(1), tail(0), size(0){}

    void add_entry(int ip, std::any resp){
        while((head + 1) % 20 == tail){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        queue[head] = Entry(ip, resp);
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
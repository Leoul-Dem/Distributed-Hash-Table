#include <any>
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <bit>

class ResponseQueue{
private:
    struct Entry{
        int client_fd;
        std::string resp;

        Entry() : client_fd(0), resp() {}

        Entry(const int &ip, const std::string &resp) : client_fd(ip), resp(resp) {}

        Entry(const int &ip, const std::any &resp) : client_fd(ip), resp(std::bit_cast<std::string>(resp)) {}
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
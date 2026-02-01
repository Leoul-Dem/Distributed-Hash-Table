#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>

class ResponseQueue{
private:
    struct Entry{
        sockaddr_in client_addr;
        std::string resp;

        Entry() : client_addr(), resp() {
            client_addr.sin_family = AF_INET;
        }

        Entry(const sockaddr_in &addr, const std::string &resp) : client_addr(addr), resp(resp) {}
    };

    std::array<Entry, 20> queue;
    int head;
    int tail;
    std::atomic<int> size;

public:
    ResponseQueue(): head(1), tail(0), size(0){}


    void add_entry(const sockaddr_in &client_addr, const std::string &resp){
        while((head + 1) % 20 == tail){
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        queue[head] = Entry(client_addr, resp);
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
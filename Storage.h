#ifndef DISTIBUTED_HASH_TABLE_STORAGE_H
#define DISTIBUTED_HASH_TABLE_STORAGE_H

#include <atomic>
#include <netinet/in.h>

#include "ds/concurrentqueue.h"
#include "ds/HashMap/phmap.hpp"
#include "Request.h"

struct TaskEntry
{
    sockaddr_in client_addr{};
    Request req{};
    std::string key;
    std::optional<std::string> value;

    TaskEntry() = default;
    TaskEntry(const sockaddr_in& addr, Request r, std::string k, std::optional<std::string> v)
        : client_addr(addr), req(r), key(std::move(k)), value(std::move(v)) {}
};

struct ResponseEntry
{
    sockaddr_in client_addr{};
    std::string response;

    ResponseEntry() = default;
    ResponseEntry(const sockaddr_in& addr, std::string resp)
        : client_addr(addr), response(std::move(resp)) {}
};

class Storage
{
    using HashTable = gtl::parallel_flat_hash_map_m<std::string, std::string>;
    HashTable table;

    moodycamel::ConcurrentQueue<TaskEntry> task_queue;
    moodycamel::ConcurrentQueue<ResponseEntry> response_queue;
    
    std::array<std::thread, 5> workers;
    uint16_t port;
    int server_fd = 0;

    // Shutdown flag
    std::atomic<bool> running{false};
    
    // Performance counters
    std::atomic<uint64_t> received_count{0};
    std::atomic<uint64_t> executed_count{0};
    std::atomic<uint64_t> responded_count{0};

    int create_server(int& server_fd) const;
    void receive(int server_fd);
    void execute();
    void respond(int server_fd);
    static int parse_req(const std::string& input, Request& req,
                         std::string& key, std::optional<std::string>& value);

public:
    explicit Storage(uint16_t port = 1895);
    ~Storage();
    
    void run();
    void stop();
    bool is_running() const { return running.load(); }

    uint64_t get_received_count() const { return received_count.load(); }
    uint64_t get_executed_count() const { return executed_count.load(); }
    uint64_t get_responded_count() const { return responded_count.load(); }
};

#endif //DISTIBUTED_HASH_TABLE_STORAGE_H
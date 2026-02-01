#pragma once
#include "hashtable.hpp"
#include "taskqueue.hpp"
#include "responsequeue.hpp"
#include <array>
#include <thread>
#include <future>
#include <string>
#include <optional>
#include <any>

class Storage {
private:
    static const size_t TABLE_SIZE = 10000000;
    HashTable<TABLE_SIZE> table;
    TaskQueue tasks;
    ResponseQueue responses;
    std::array<std::thread, 3> workers;
    std::promise<void> shutdown_signal;
    static Storage* instance;
    uint16_t port;
    
    int create_server(int &server_fd, uint16_t port);
    void recieve(const int server_fd);
    void execute();
    void respond(const int server_fd);
    int parse_req(const std::string &input, Request &req,
                  std::string &key, std::optional<std::string> &value);
    std::string serialize_response(Request req, const std::any &result);
    
public:
    Storage(uint16_t port);
    static void signalHandler(int signal);
    void run();
};


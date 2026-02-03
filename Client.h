//
// Created by leoul on 2/1/26.
//

#ifndef DISTIBUTED_HASH_TABLE_CLIENT_H
#define DISTIBUTED_HASH_TABLE_CLIENT_H
#include <any>
#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <netinet/in.h>

#include "./Request.h"


class Client
{
private:
    int socket_fd;
    std::array<sockaddr_in, 3> server_addrs;
    std::atomic<bool> running{true};
    std::atomic<uint64_t> successful_ops{0};
    std::atomic<uint64_t> timeout_count{0};

    int try_send_request(const Request &request, const std::string &key, const std::optional<std::string> &value) const;
    std::any receive_response();
    static std::string serialize_request(const Request &request, const std::string &key, const std::optional<std::string> &value);
    std::any send_request(const Request &request, const std::string &key, const std::optional<std::string> &value);

public:
    Client(const std::array<sockaddr_in, 3> &server_addrs, uint16_t client_port = 0);
    ~Client();
    void run();
    void stop() { running.store(false); }
    uint64_t get_successful_ops() const { return successful_ops.load(); }
    uint64_t get_timeout_count() const { return timeout_count.load(); }
};


#endif //DISTIBUTED_HASH_TABLE_CLIENT_H
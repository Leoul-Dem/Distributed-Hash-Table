#pragma once

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <array>
#include <any>
#include <optional>

#include "request.hpp"
#include "report.hpp"

class Client{
private:
    int socket_fd;
    std::array<sockaddr_in, 5> server_addrs;
    ClientMetrics metrics;  // per-client metrics

    int try_send_request(const Request &request, const std::string &key, const std::optional<std::string> &value);
    std::any receive_response();
    std::string serialize_request(const Request &request, const std::string &key, const std::optional<std::string> &value);
    std::any send_request(const Request &request, const std::string &key, const std::optional<std::string> &value);
    
public:
    Client(const std::array<sockaddr_in, 5> &server_addrs, uint16_t port);
    ~Client();
    void run();
    
    // Expose metrics for aggregation
    ClientMetrics& get_metrics() { return metrics; }
    const ClientMetrics& get_metrics() const { return metrics; }
};
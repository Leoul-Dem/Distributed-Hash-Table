#include "Client.h"

#include <chrono>
#include <random>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>

Client::Client(const std::array<sockaddr_in, 3>& server_addrs, const size_t num_servers, const uint16_t client_port) 
    : server_addrs(server_addrs), num_servers(num_servers)
{
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd == -1){
        exit(EXIT_FAILURE);
    }

    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 15000;  // 15ms timeout
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (client_port != 0) {
        sockaddr_in client_addr{};
        client_addr.sin_family = AF_INET;
        client_addr.sin_addr.s_addr = INADDR_ANY;
        client_addr.sin_port = htons(client_port);

        if (bind(socket_fd, reinterpret_cast<sockaddr*>(&client_addr), sizeof(client_addr)) == -1) {
            close(socket_fd);
            exit(EXIT_FAILURE);
        }
    }
}

Client::~Client()
{
    close(socket_fd);
}

int Client::try_send_request(const Request& request, const std::string& key, const std::optional<std::string>& value) const
{
    // Should be changed if data type is not integer strings
    const int idx = std::stoi(key) % num_servers;
    sockaddr_in addr = server_addrs[idx];
    const std::string request_str = serialize_request(request, key, value);
    if(const ssize_t bytes_sent = sendto(socket_fd, request_str.data(), request_str.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)); bytes_sent == -1){
        return -1;
    }
    return 0;
}

std::any Client::receive_response()
{
    std::array<char, 1024> buffer;
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    const ssize_t bytes_received = recvfrom(socket_fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    if(bytes_received == -1){
        return std::nullopt;
    }
    return std::any(std::string(buffer.data(), bytes_received));
}

std::any Client::send_request(const Request& request, const std::string& key, const std::optional<std::string>& value)
{
    constexpr int num_retries = 3;
    for(int i = 0; i < num_retries && running.load(std::memory_order_relaxed); i++){
        if(try_send_request(request, key, value) == 0){
            if(std::any response = receive_response(); response.has_value()){
                successful_ops.fetch_add(1, std::memory_order_relaxed);
                return response;
            }
        }
        if (i < num_retries - 1) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));  // 0.5ms between retries
        }
    }
    // Only count as timeout when ALL retries have failed
    if (running.load(std::memory_order_relaxed)) {
        timeout_count.fetch_add(1, std::memory_order_relaxed);
    }
    return std::nullopt;
}

std::string Client::serialize_request(const Request& request, const std::string& key, const std::optional<std::string>& value)
{
    std::string request_str = request == PUT ? "PUT" : "GET";
    request_str += ":" + key;
    if(request == PUT && value.has_value()){
        request_str += ":" + value.value();
    }
    return request_str;
}

void Client::run()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution op_dist(0, 1);
    std::uniform_int_distribution key_value_dist(0, 10000);

    while(running.load(std::memory_order_relaxed)){
        if(const int operation = op_dist(gen); operation == 0){
            // PUT operation: generate random key and value
            const int key = key_value_dist(gen);
            const int value = key_value_dist(gen);
            send_request(PUT, std::to_string(key), std::to_string(value));
        } else {
            // GET operation: generate random key
            const int key = key_value_dist(gen);
            send_request(GET, std::to_string(key), std::nullopt);
        }
    }
}

#include "../include/client.hpp"
#include <thread>
#include <chrono>
#include <random>

Client::Client(const std::array<sockaddr_in, 5> &server_addrs, uint16_t port) : server_addrs(server_addrs){
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd == -1){
        std::fputs("socket\n", stderr);
        exit(EXIT_FAILURE);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1){
        std::fputs("bind\n", stderr);
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
}

Client::~Client(){
    close(socket_fd);
}

std::string Client::serialize_request(const Request &request, const std::string &key, const std::optional<std::string> &value){
    std::string request_str = request == Request::PUT ? "PUT" : "GET";
    request_str += ":" + key;
    if(request == Request::PUT && value.has_value()){
        request_str += ":" + value.value();
    }
    return request_str;
}

int Client::try_send_request(const Request &request, const std::string &key, const std::optional<std::string> &value){
    sockaddr_in addr = server_addrs[0];
    std::string request_str = serialize_request(request, key, value);
    ssize_t bytes_sent = sendto(socket_fd, request_str.data(), request_str.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
    if(bytes_sent == -1){
        std::fputs("try send request\n", stderr);
        return -1;
    }
    return 0;
}

std::any Client::receive_response(){
    std::array<char, 2048> buffer;
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    ssize_t bytes_received = recvfrom(socket_fd, buffer.data(), buffer.size(), 0, (struct sockaddr*)&addr, &addr_len);
    if(bytes_received == -1){
        std::fputs("receive response\n", stderr);
        return std::nullopt;
    }
    return std::any(std::string(buffer.data(), bytes_received));
}

std::any Client::send_request(const Request &request, const std::string &key, const std::optional<std::string> &value){
    RequestTimer timer(metrics);  // starts timing
    
    for(int i = 0; i < 5; i++){
        if(try_send_request(request, key, value) == 0){
            std::any response = receive_response();
            if(response.has_value()){
                timer.complete();  // records latency on success
                return response;
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    std::fputs("no response\n", stderr);
    timer.fail();  // records failure
    return std::nullopt;
}

void Client::run(){
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> op_dist(0, 1);
    std::uniform_int_distribution<int> key_value_dist(0, 10000);

    for(int i = 0; i < 1000000; i++){
        int operation = op_dist(gen);
        
        if(operation == 0){
            // PUT operation: generate random key and value
            int key = key_value_dist(gen);
            int value = key_value_dist(gen);
            send_request(Request::PUT, std::to_string(key), std::to_string(value));
        } else {
            // GET operation: generate random key
            int key = key_value_dist(gen);
            send_request(Request::GET, std::to_string(key), std::nullopt);
        }
    }
}
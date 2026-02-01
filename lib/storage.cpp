#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <unistd.h>
#include <future>
#include <csignal>
#include <iostream>
#include <string>
#include <string_view>
#include <ranges>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../include/storage.hpp"

Storage* Storage::instance = nullptr;

Storage::Storage(uint16_t port) : table(), tasks(), responses(), port(port) {
    instance = this;
    std::signal(SIGINT, Storage::signalHandler);
}

int Storage::create_server(int &server_fd, uint16_t port){
    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(server_fd == -1){
        std::cerr << "Failed to create socket\n";
        return -1;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1){
        std::cerr << "Failed to bind socket\n";
        close(server_fd);
        return -1;
    }

    return 0;
}

void Storage::signalHandler(int signal){
    if(signal == SIGINT && instance){
        instance->shutdown_signal.set_value();
    }
}

void Storage::run(){
        int server_fd;
        if(create_server(server_fd, port) != 0){
            std::cerr << "Failed to create server on port " << port << std::endl;
            exit(EXIT_FAILURE);
        }
        
        std::cout << "Server started on port " << port << std::endl;
        
        workers[0] = std::thread(&Storage::recieve, this, server_fd);
        workers[1] = std::thread(&Storage::execute, this);
        workers[2] = std::thread(&Storage::respond, this, server_fd);
        
        shutdown_signal.get_future().wait();
        
        std::cout << "Shutting down gracefully..." << std::endl;
        
        for(auto &i : workers){
            i.join();
        }
        
        close(server_fd);
    }

void Storage::recieve(const int server_fd){
        sockaddr_in addr {};
        std::array<char, 2048> buffer;
        socklen_t addr_len = sizeof(addr);
        
        auto run = [this, server_fd, &addr, &buffer, &addr_len](){
            while(true){
                addr_len = sizeof(addr);
                ssize_t bytes_received = recvfrom(server_fd, buffer.data(), buffer.size(), 0,
                                                  (struct sockaddr*)&addr, &addr_len);
                if(bytes_received == -1){
                    std::fputs("recvfrom\n", stderr);
                    continue;
                }
                buffer[bytes_received] = '\0';
                
                Request req;
                std::string key;
                std::optional<std::string>value{std::nullopt};
                if(parse_req(std::string(buffer.data()), req, key, value) == 0){
                    tasks.add_entry(addr, req, key, value);
                }
            }  
        };

        std::jthread th1(run);
    }

std::string Storage::serialize_response(Request req, const std::any &result){
        switch (req) {
            case Request::PUT: {
                bool success = std::any_cast<bool>(result);
                return success ? "PUT:SUCCESS:true" : "PUT:SUCCESS:false";
            }
            case Request::GET: {
                auto opt_val = std::any_cast<std::optional<std::any>>(result);
                if(opt_val.has_value()){
                    try {
                        std::string val_str = std::any_cast<std::string>(opt_val.value());
                        return "GET:VALUE:" + val_str;
                    } catch (...) {
                        return "GET:VALUE:unknown";
                    }
                } else {
                    return "GET:NULL";
                }
            }
            default:
                return "ERROR:unknown";
        }
    }

void Storage::execute(){
        auto run = [this](){
            while(true){
                TaskQueue::Entry todo = tasks.read_entry();
                std::any resp;
                switch (todo.req) {
                    case Request::GET:{
                        resp = table.get(todo.key);
                    }
                    case Request::PUT:{
                        if (todo.value.has_value()) {
                            std::any val = std::any(todo.value.value());
                            resp = table.put(todo.key, val);
                        }else{
                            continue;
                        }
                    }
                    default:
                        continue;
                }
                std::string serialized = serialize_response(todo.req, resp);
                responses.add_entry(todo.client_addr, serialized);
            }
        };
        
        std::jthread th1(run);
    }

void Storage::respond(const int server_fd){
        auto run = [this, server_fd](){
            while(true){
                auto tosend = responses.read_entry();
                socklen_t addr_len = sizeof(tosend.client_addr);
                ssize_t bytes_sent = sendto(server_fd, tosend.resp.data(),
                                            tosend.resp.size(), 0,
                                            (struct sockaddr*)&tosend.client_addr, addr_len);
                if(bytes_sent == -1){
                    std::fputs("sendto\n", stderr);
                }          
            }
        };
        
        std::jthread th1(run);
    }

int Storage::parse_req(const std::string &input, Request &req,
                    std::string &key, std::optional<std::string> &value){
        std::string_view str{input};
        std::vector<std::string_view> split_str;
        for (auto part : str | std::views::split(':')) {
            split_str.emplace_back(part.begin(), part.end());
        }

        if (split_str.size() < 2) {
            std::fputs("Invalid request format\n", stderr);
            return -1;
        }

        Request t_req;
        if (split_str[0] == "GET") {
            t_req = Request::GET;
        } else if (split_str[0] == "PUT") {
            t_req = Request::PUT;
        } else {
            std::fputs("Invalid request type\n", stderr);
            return -1;
        }

        if (t_req == Request::GET && split_str.size() == 2){
            req = t_req;
            key = std::string(split_str[1]);
            value = std::nullopt;
            return 0;
        }else if (t_req == Request::PUT && split_str.size() == 3){
            req = t_req;
            key = std::string(split_str[1]);
            value = std::string(split_str[2]);
            return 0;
        }
        std::fputs("Invalid request\n", stderr);
        return -1;
    }
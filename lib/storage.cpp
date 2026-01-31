#include <array>
#include <cstddef>
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
#include <bit>
#include <ranges>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../include/storage.hpp"

class Storage{
private:
    static const size_t TABLE_SIZE = 10000000;
    HashTable<TABLE_SIZE> table;
    TaskQueue tasks;
    ResponseQueue responses;
    std::array<std::thread, 3> workers;
    std::promise<void> shutdown_signal;
    static Storage* instance;
    
    int create_server(int &server_fd){
        server_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(server_fd == -1){
            std::cerr << "server\n";
            return -1;
        }
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(1859);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        if(bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1){
            std::fputs("bind\n", stderr);
            close(server_fd);
            return -1;
        }

        return 0;
    }
    
    /*int accept_client_conn(const int serverfd, std::array<int, 10> &client_fd){
        for(int i = 0; i < 10; ){
            int new_client_fd = accept(serverfd, nullptr, nullptr);
            if(new_client_fd != -1){
                client_fd[i] = new_client_fd;
                i++;
            }
        }
        int new_client_fd = accept(serverfd, nullptr, nullptr);
        if(new_client_fd != -1){
            client_fd[0] = new_client_fd;
            return 0;
        }
        return -1;
    }*/
   
public:
    Storage(): table(), tasks(), responses(){
        instance = this;
        std::signal(SIGINT, Storage::signalHandler);
    }
    
    static void signalHandler(int signal){
        if(signal == SIGINT && instance){
            instance->shutdown_signal.set_value();
        }
    }
    
    void run(){        
        int server_fd;
        if(!create_server(server_fd)){
            std::cerr << "Failed to create server.\n";
            exit(EXIT_FAILURE);
        }
        
        
        workers[0] = std::thread(&Storage::recieve, this, server_fd);
        workers[1] = std::thread(&Storage::execute, this);
        workers[2] = std::thread(&Storage::respond, this, server_fd);
        
        shutdown_signal.get_future().wait();
        
        std::cout << "Shutting down gracefully..." << std::endl;
        
        for(auto &i : workers){
            i.join();
        }
    }
    
    void recieve(const int server_fd){
        sockaddr_in addr {};
        std::array<char, 2048> buffer;
        socklen_t addr_len = sizeof(addr);
        
        auto run = [&](){
            while(true){
                ssize_t bytes_received = recvfrom(server_fd, buffer.data(), buffer.size(), 0,
                                                  (struct sockaddr*)&addr, &addr_len);
                if(bytes_received == -1){
                    std::fputs("recvfrom\n", stderr);
                    continue;
                }
                buffer[bytes_received] = '\0';
                
                int c_fd;
                Request req;
                std::string key;
                std::optional<std::string>value{std::nullopt};
                if(parse_req(std::string(buffer.data()), c_fd, req, key, value) == 0){
                    tasks.add_entry(c_fd, req, key, value);
                }
            }  
        };

        std::jthread th1(run);   
    }
    
    void execute(){
        auto run = [&](){
            while(true){
                TaskQueue::Entry todo = tasks.read_entry();
                std::any resp;
                switch (todo.req) {
                    case Request::GET:
                        resp = table.get(todo.key);
                        break;
                    case Request::PUT:
                        resp = table.put(todo.key, todo.value);
                        break;                
                    default:
                        break;
                }
                responses.add_entry(todo.client_fd, resp);
            }
        };
        
        std::jthread th1(run);
    }
    
    void respond(const int server_fd){
        
        auto run = [&](){
            auto tosend = responses.read_entry();
            ssize_t bytes_sent = sendto(server_fd, std::bit_cast<std::string>(tosend.resp).data(),
                                        std::bit_cast<std::string>(tosend.resp).size(), 0,
                                        (struct sockaddr*)&addr, sizeof(addr));
            if(bytes_sent == -1){
                std::fputs("sendto\n", stderr);
            }          
        };
        
        
    }  
    
    int parse_req(const std::string &input, int &client_fd, Request &req,
                    std::string &key, std::optional<std::string> &value){
        std::string_view str{input};
        auto split_str = str | std::views::split(':')
                             | std::ranges::to<std::vector<std::string_view>>();

        Request t_req = std::bit_cast<Request>(split_str[1]);

        if (t_req == Request::GET && split_str.length() == 3){
            client_fd = std::bit_cast<int>(split_str[0]);
            req = t_req;
            key = split_str[2];
            return 0;
        }else if (t_req == Request::PUT && split_str.length() == 4){
            client_fd = std::bit_cast<int>(split_str[0]);
            req = t_req;
            key = split_str[2];
            value = split_str[3];
            return 0;
        }
        std::fputs("Invalid request\n", stderr);
        return -1;
    }
    
};

Storage* Storage::instance = nullptr;
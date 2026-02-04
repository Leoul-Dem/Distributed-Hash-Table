#include "./Client.h"
#include "./Storage.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <sstream>
#include <vector>

// Configurable number of clients per machine (can be overridden by NUM_CLIENTS env var)
constexpr size_t DEFAULT_NUM_CLIENTS = 50;

// Global pointers for signal handling
static Storage* g_storage = nullptr;
static std::vector<Client*> g_clients;
static std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        g_shutdown_requested.store(true);
        for (auto* client : g_clients)
        {
            if (client != nullptr)
            {
                client->stop();
            }
        }
        if (g_storage != nullptr)
        {
            g_storage->stop();
        }
    }
}

void run_storage(Storage& storage)
{
    storage.run();
}

void run_client(Client& client)
{
    client.run();
}

std::vector<std::string> parse_server_ips(const char* env_value)
{
    std::vector<std::string> ips;
    std::string str(env_value);
    std::istringstream stream(str);
    std::string ip;
    
    while (std::getline(stream, ip, '|'))
    {
        if (!ip.empty())
        {
            ips.push_back(ip);
        }
    }
    
    return ips;
}

int run_server_mode(uint16_t port)
{    
    Storage storage(port);
    g_storage = &storage;
    
    std::thread storage_thread(run_storage, std::ref(storage));
    
    while (!g_shutdown_requested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    storage.stop();
    
    if (storage_thread.joinable())
    {
        storage_thread.join();
    }
    
    std::cout << "\nServer-side metrics:" << std::endl;
    std::cout << "Received: " << storage.get_received_count() << std::endl;
    std::cout << "Executed: " << storage.get_executed_count() << std::endl;
    std::cout << "Responded: " << storage.get_responded_count() << std::endl;
    
    g_storage = nullptr;
    return 0;
}

int run_client_mode(uint16_t port, const std::vector<std::string>& server_ips, size_t num_clients)
{    
    std::array<sockaddr_in, 3> server_addrs{};
    
    for (size_t i = 0; i < std::min(server_ips.size(), static_cast<size_t>(3)); ++i)
    {
        server_addrs[i].sin_family = AF_INET;
        server_addrs[i].sin_port = htons(port);
        
        if (inet_pton(AF_INET, server_ips[i].c_str(), &server_addrs[i].sin_addr) <= 0)
        {
            std::cerr << "Invalid server IP address: " << server_ips[i] << std::endl;
            return 1;
        }
        
    }
    
    std::vector<std::unique_ptr<Client>> clients;
    std::vector<std::thread> client_threads;
    
    clients.reserve(num_clients);
    client_threads.reserve(num_clients);
    g_clients.reserve(num_clients);
    
    for (size_t i = 0; i < num_clients; ++i)
    {
        auto client = std::make_unique<Client>(server_addrs, server_ips.size(), 0);
        g_clients.push_back(client.get());
        clients.push_back(std::move(client));
    }
    
    const auto start_time = std::chrono::steady_clock::now();
    
    for (size_t i = 0; i < num_clients; ++i)
    {
        client_threads.emplace_back(run_client, std::ref(*clients[i]));
    }
    
    while (!g_shutdown_requested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    const auto end_time = std::chrono::steady_clock::now();
    const auto run_duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    for (auto& client : clients)
    {
        client->stop();
    }
    
    for (auto& thread : client_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    
    uint64_t total_ops = 0;
    uint64_t total_timeouts = 0;
    
    for (size_t i = 0; i < num_clients; ++i)
    {
        uint64_t ops = clients[i]->get_successful_ops();
        uint64_t timeouts = clients[i]->get_timeout_count();
        total_ops += ops;
        total_timeouts += timeouts;
    }
    
    std::cout << "\nCLIENT RESULTS" << std::endl;
    std::cout << "Total clients: " << num_clients << std::endl;
    std::cout << "Run duration: " << run_duration.count() << " seconds" << std::endl;
    std::cout << "Total successful operations: " << total_ops << std::endl;
    
    if (run_duration.count() > 0)
    {
        std::cout << "Throughput: " << (total_ops / run_duration.count()) << " ops/sec" << std::endl;
    }
    
    g_clients.clear();
    return 0;
}

int main(int argc, char** argv)
{
    uint16_t port = 1895;
    if (argc > 1)
    {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    const char* server_ips_env = std::getenv("SERVER_IPS");
    
    if (server_ips_env == nullptr || std::string(server_ips_env).empty())
    {
        return run_server_mode(port);
    }
    else
    {
        std::vector<std::string> server_ips = parse_server_ips(server_ips_env);
        
        if (server_ips.empty())
        {
            std::cerr << "No valid server IPs." << std::endl;
            return 1;
        }
        
        size_t num_clients = DEFAULT_NUM_CLIENTS;
        const char* num_clients_env = std::getenv("NUM_CLIENTS");
        if (num_clients_env != nullptr)
        {
            num_clients = static_cast<size_t>(std::stoi(num_clients_env));
        }
        
        return run_client_mode(port, server_ips, num_clients);
    }
}
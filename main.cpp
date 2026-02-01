#include "./lib/storage.cpp"
#include "./lib/client.cpp"
#include <cstdint>
#include <thread>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <arpa/inet.h>

constexpr uint16_t DEFAULT_PORT = 1895;

int run_storage(uint16_t port){
    Storage storage(port);
    storage.run();
    return 0;
}

int run_client(const std::array<sockaddr_in, 5> &server_addrs, uint16_t port){
    Client client(server_addrs, port);
    client.run();
    return 0;
}

int main(int argc, char *argv[]){
    uint16_t port = DEFAULT_PORT;
    
    // Optional: override port via command line
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    // Check for SERVER_IPS environment variable
    const char* server_ips_env = std::getenv("SERVER_IPS");
    
    if (server_ips_env == nullptr) {
        // No environment variable set - run as storage server
        return run_storage(port);
    }
    
    // Environment variable set - run as client
    // Parse the IPs separated by '|' using string_view
    std::array<sockaddr_in, 5> server_addrs{};
    std::string_view ips_view(server_ips_env);
    size_t idx = 0;
    
    while (!ips_view.empty() && idx < 5) {
        size_t delim_pos = ips_view.find('|');
        std::string_view ip = ips_view.substr(0, delim_pos);
        
        if (!ip.empty()) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(DEFAULT_PORT);
            
            // string_view isn't null-terminated, so create a temp string for inet_pton
            std::string ip_str(ip);
            if (inet_pton(AF_INET, ip_str.c_str(), &addr.sin_addr) != 1) {
                std::cerr << "Invalid IP address: " << ip_str << std::endl;
                return 1;
            }
            
            server_addrs[idx++] = addr;
        }
        
        if (delim_pos == std::string_view::npos) break;
        ips_view.remove_prefix(delim_pos + 1);
    }
    
    if (idx == 0) {
        std::cerr << "No valid server IPs provided" << std::endl;
        return 1;
    }
    
    return run_client(server_addrs, port);
}


// Example: run evaluation with multiple clients
int run_evaluation(const std::array<sockaddr_in, 5> &server_addrs, int num_clients, uint16_t base_port) {
    AggregateReport report;
    std::vector<std::unique_ptr<Client>> clients;
    std::vector<std::thread> threads;
    
    // Create clients and register their metrics
    for (int i = 0; i < num_clients; i++) {
        clients.push_back(std::make_unique<Client>(server_addrs, base_port + i));
        report.register_client(&clients.back()->get_metrics());
    }
    
    // Mark start of evaluation and record in each client
    report.mark_start();
    for (auto& client : clients) {
        client->get_metrics().start_time = Clock::now();
    }
    
    // Run clients concurrently
    for (auto& client : clients) {
        threads.emplace_back([&client]() {
            client->run();
        });
    }
    
    // Wait for all clients to finish
    for (auto& t : threads) {
        t.join();
    }
    
    // Mark end times
    for (auto& client : clients) {
        client->get_metrics().end_time = Clock::now();
    }
    report.mark_end();
    
    // Print results
    report.print_report();
    
    return 0;
}
#include "./lib/storage.cpp"
#include "./lib/client.cpp"
#include <cstdint>
#include <thread>
#include <vector>

int main(int argc, char *argv[]){
    return 0;
}


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
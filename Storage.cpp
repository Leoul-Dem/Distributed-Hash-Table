#include "Storage.h"

#include <chrono>
#include <ranges>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

Storage::Storage(const uint16_t port) : port(port) {}

Storage::~Storage()
{
    stop();
}

int Storage::create_server(int& server_fd) const
{
    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd == -1)
    {
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
    {
        close(server_fd);
        return -1;
    }

    return 0;
}

void Storage::receive(const int server_fd)
{
    constexpr size_t BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 50000;  // 50ms
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (running.load(std::memory_order_relaxed))
    {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        const ssize_t bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0,
                                        reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (bytes_received == -1)
        {
            continue;
        }

        buffer[bytes_received] = '\0';
        const std::string input(buffer);

        Request req;
        std::string key;
        std::optional<std::string> value;

        if (parse_req(input, req, key, value) == -1)
        {
            continue;
        }

        task_queue.enqueue(TaskEntry{client_addr, req, std::move(key), std::move(value)});
        received_count.fetch_add(1, std::memory_order_relaxed);
    }
}

void Storage::execute()
{
    constexpr size_t BULK_SIZE = 32;
    TaskEntry tasks[BULK_SIZE];
    
    while (running.load(std::memory_order_relaxed))
    {
        const size_t count = task_queue.try_dequeue_bulk(tasks, BULK_SIZE);
        
        if (count == 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }
        
        for (size_t i = 0; i < count; ++i)
        {
            TaskEntry& task = tasks[i];
            std::string response;

            switch (task.req)
            {
                case GET:
                {
                    std::string found_value;
                    const bool found = table.if_contains(task.key, [&found_value](const auto& item) {
                        found_value = item.second;
                    });
                    response = found ? found_value : "";
                    break;
                }
                case PUT:
                {
                    if (task.value.has_value())
                    {
                        const bool inserted = table.try_emplace_l(
                            task.key,
                            [](auto&) {},
                            task.value.value()
                        );
                        response = inserted ? "TRUE" : "FALSE";
                    }
                    else
                    {
                        response = "FALSE";
                    }
                    break;
                }
            }

            response_queue.enqueue(ResponseEntry{task.client_addr, std::move(response)});
            executed_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void Storage::respond(const int server_fd)
{
    constexpr size_t BULK_SIZE = 32;
    ResponseEntry responses[BULK_SIZE];
    
    while (running.load(std::memory_order_relaxed))
    {
        const size_t count = response_queue.try_dequeue_bulk(responses, BULK_SIZE);
        
        if (count == 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }

        for (size_t i = 0; i < count; ++i)
        {
            ResponseEntry& resp = responses[i];
            sendto(server_fd, resp.response.c_str(), resp.response.size(), 0,
                   reinterpret_cast<const sockaddr*>(&resp.client_addr),
                   sizeof(resp.client_addr));
            responded_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

int Storage::parse_req(const std::string& input, Request& req, std::string& key, std::optional<std::string>& value)
{
    const size_t first_colon = input.find(':');
    if (first_colon == std::string::npos)
    {
        return -1;
    }

    const std::string_view cmd(input.data(), first_colon);
    
    if (cmd == "GET")
    {
        req = GET;
        key = input.substr(first_colon + 1);
        value = std::nullopt;
        return 0;
    }
    
    if (cmd == "PUT")
    {
        const size_t second_colon = input.find(':', first_colon + 1);
        if (second_colon == std::string::npos)
        {
            return -1;
        }
        req = PUT;
        key = input.substr(first_colon + 1, second_colon - first_colon - 1);
        value = input.substr(second_colon + 1);
        return 0;
    }

    return -1;
}

void Storage::run()
{
    if (create_server(server_fd) != 0)
    {
        return;
    }
    
    running.store(true, std::memory_order_relaxed);

    workers[0] = std::thread(&Storage::receive, this, server_fd);
    workers[1] = std::thread(&Storage::execute, this);
    workers[2] = std::thread(&Storage::execute, this);
    workers[3] = std::thread(&Storage::execute, this);
    workers[4] = std::thread(&Storage::respond, this, server_fd);

    for (auto& worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    
    close(server_fd);
    server_fd = 0;
}

void Storage::stop()
{
    if (!running.load(std::memory_order_relaxed))
    {
        return;
    }
    
    running.store(false, std::memory_order_relaxed);

    for (auto& worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    
    if (server_fd != 0)
    {
        close(server_fd);
        server_fd = 0;
    }
}
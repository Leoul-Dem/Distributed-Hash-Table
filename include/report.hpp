#pragma once

#include <chrono>
#include <vector>
#include <mutex>
#include <cstdint>
#include <cstdio>

using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;

/**
 * ClientMetrics - per-client metrics collection (O(1) memory)
 * Uses running sum for average calculation instead of storing all latencies
 */
struct ClientMetrics {
    double latency_sum_us = 0.0;    // running sum of latencies in microseconds
    uint64_t requests_completed = 0;
    uint64_t requests_failed = 0;
    TimePoint start_time;
    TimePoint end_time;
    
    void reset() {
        latency_sum_us = 0.0;
        requests_completed = 0;
        requests_failed = 0;
    }
    
    void record_latency(double latency_us) {
        latency_sum_us += latency_us;
        requests_completed++;
    }
    
    void record_failure() {
        requests_failed++;
    }
    
    double get_average_latency_us() const {
        if (requests_completed == 0) return 0.0;
        return latency_sum_us / static_cast<double>(requests_completed);
    }
    
    // Throughput: requests per second from this client's perspective
    double get_throughput() const {
        auto elapsed = std::chrono::duration<double>(end_time - start_time).count();
        if (elapsed <= 0.0) return 0.0;
        return static_cast<double>(requests_completed) / elapsed;
    }
};

/**
 * RequestTimer - RAII helper to measure individual request latency
 */
class RequestTimer {
private:
    ClientMetrics& metrics;
    TimePoint start;
    bool completed = false;
    
public:
    explicit RequestTimer(ClientMetrics& m) : metrics(m), start(Clock::now()) {}
    
    void complete() {
        if (!completed) {
            auto end = Clock::now();
            double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
            metrics.record_latency(latency_us);
            completed = true;
        }
    }
    
    void fail() {
        if (!completed) {
            metrics.record_failure();
            completed = true;
        }
    }
    
    ~RequestTimer() {
        if (!completed) {
            metrics.record_failure();
        }
    }
};

/**
 * AggregateReport - collects metrics from multiple clients
 * Thread-safe for concurrent registration
 */
class AggregateReport {
private:
    std::mutex mutex;
    std::vector<ClientMetrics*> client_metrics;
    TimePoint global_start;
    TimePoint global_end;
    
public:
    void register_client(ClientMetrics* metrics) {
        std::lock_guard<std::mutex> lock(mutex);
        client_metrics.push_back(metrics);
    }
    
    void mark_start() {
        global_start = Clock::now();
    }
    
    void mark_end() {
        global_end = Clock::now();
    }
    
    // Average latency: weighted by request count per client
    // Formula: Σ(client_sum) / Σ(client_count)
    double get_average_latency_us() const {
        double total_sum = 0.0;
        uint64_t total_count = 0;
        
        for (const auto* cm : client_metrics) {
            total_sum += cm->latency_sum_us;
            total_count += cm->requests_completed;
        }
        
        if (total_count == 0) return 0.0;
        return total_sum / static_cast<double>(total_count);
    }
    
    // Aggregate throughput: total requests / wall-clock time
    double get_aggregate_throughput() const {
        uint64_t total_completed = get_total_completed();
        auto elapsed = std::chrono::duration<double>(global_end - global_start).count();
        if (elapsed <= 0.0) return 0.0;
        return static_cast<double>(total_completed) / elapsed;
    }
    
    uint64_t get_total_completed() const {
        uint64_t total = 0;
        for (const auto* cm : client_metrics) {
            total += cm->requests_completed;
        }
        return total;
    }
    
    uint64_t get_total_failed() const {
        uint64_t total = 0;
        for (const auto* cm : client_metrics) {
            total += cm->requests_failed;
        }
        return total;
    }
    
    double get_elapsed_seconds() const {
        return std::chrono::duration<double>(global_end - global_start).count();
    }
    
    void print_report() const {
        std::printf("\n=== Aggregate Report ===\n");
        std::printf("Clients:            %zu\n", client_metrics.size());
        std::printf("Total completed:    %lu\n", get_total_completed());
        std::printf("Total failed:       %lu\n", get_total_failed());
        std::printf("Elapsed time:       %.3f s\n", get_elapsed_seconds());
        std::printf("Avg latency:        %.2f us\n", get_average_latency_us());
        std::printf("Throughput:         %.2f req/s\n", get_aggregate_throughput());
        std::printf("========================\n\n");
    }
};

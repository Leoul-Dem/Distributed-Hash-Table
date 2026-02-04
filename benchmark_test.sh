#!/bin/bash

# =============================================================================
# Distributed Hash Table - Benchmark Testing Script
# =============================================================================
# Tests the DHT with specified combinations and outputs results to CSV
# Calculates average latency using Little's Law: Latency = 1 / (throughput / num_clients)
# =============================================================================

# Don't exit on first error - we handle errors ourselves
set +e

# Configuration
PORT_BASE=1895
TEST_DURATION=20
BINARY_PATH="./build/Distibuted_Hash_Table"
CSV_OUTPUT="benchmark_results.csv"

# Test combinations as specified
SERVER_COUNTS=(1 2 3)
CLIENT_THREADS=(50 100 150 200 250 300)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Arrays to store PIDs
declare -a SERVER_PIDS

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

cleanup() {
    log_info "Cleaning up..."

    # Kill all server processes
    for pid in "${SERVER_PIDS[@]}"; do
        kill -9 $pid 2>/dev/null || true
    done

    # Kill any remaining DHT processes
    pkill -9 -f "$BINARY_PATH" 2>/dev/null || true

    SERVER_PIDS=()
}

trap cleanup EXIT

check_binary() {
    if [ ! -f "$BINARY_PATH" ]; then
        log_error "Binary not found at $BINARY_PATH"
        log_info "Building the project..."

        mkdir -p build
        cd build
        cmake ..
        make -j$(nproc)
        cd ..

        if [ ! -f "$BINARY_PATH" ]; then
            log_error "Build failed!"
            exit 1
        fi

        log_success "Build completed"
    fi
}

start_servers() {
    local num_servers=$1
    SERVER_PIDS=()

    for ((i=0; i<num_servers; i++)); do
        local port=$((PORT_BASE + i))
        $BINARY_PATH $port > /tmp/dht_server_${i}.log 2>&1 &
        SERVER_PIDS+=($!)
    done

    # Wait for servers to initialize
    sleep 2

    # Verify all servers are running
    for ((i=0; i<num_servers; i++)); do
        if ! kill -0 ${SERVER_PIDS[$i]} 2>/dev/null; then
            log_error "Server $i failed to start!"
            cat /tmp/dht_server_${i}.log
            return 1
        fi
    done

    return 0
}

stop_servers() {
    local num_servers=$1

    for ((i=0; i<num_servers; i++)); do
        if [ -n "${SERVER_PIDS[$i]}" ]; then
            kill -TERM ${SERVER_PIDS[$i]} 2>/dev/null || true
        fi
    done

    # Wait for graceful shutdown
    sleep 2

    # Force kill if still running
    for ((i=0; i<num_servers; i++)); do
        if [ -n "${SERVER_PIDS[$i]}" ]; then
            kill -9 ${SERVER_PIDS[$i]} 2>/dev/null || true
        fi
    done

    SERVER_PIDS=()
}

run_test() {
    local num_servers=$1
    local num_clients=$2

    echo ""
    echo -e "${CYAN}=============================================================================${NC}"
    echo -e "${CYAN}  TEST: $num_servers server(s), $num_clients client threads, ${TEST_DURATION}s${NC}"
    echo -e "${CYAN}=============================================================================${NC}"

    # Clean up any previous processes
    pkill -9 -f "$BINARY_PATH" 2>/dev/null || true
    sleep 1

    # Start servers
    log_info "Starting $num_servers server(s)..."
    if ! start_servers $num_servers; then
        log_error "Failed to start servers"
        # Write error row to CSV
        echo "$num_servers,$num_clients,ERROR,ERROR,ERROR,ERROR" >> "$CSV_OUTPUT"
        return 1
    fi
    log_success "All servers running"

    # Build SERVER_IPS string
    local server_ips=""
    for ((i=0; i<num_servers; i++)); do
        local port=$((PORT_BASE + i))
        if [ -n "$server_ips" ]; then
            server_ips="${server_ips}|"
        fi
        server_ips="${server_ips}127.0.0.1"
    done

    # Start client
    log_info "Starting client with $num_clients threads..."
    SERVER_IPS="$server_ips" NUM_CLIENTS=$num_clients $BINARY_PATH $PORT_BASE > /tmp/dht_client.log 2>&1 &
    local client_pid=$!

    if ! kill -0 $client_pid 2>/dev/null; then
        log_error "Client failed to start!"
        cat /tmp/dht_client.log
        stop_servers $num_servers
        # Write error row to CSV
        echo "$num_servers,$num_clients,ERROR,ERROR,ERROR,ERROR" >> "$CSV_OUTPUT"
        return 1
    fi

    log_success "Client running (PID: $client_pid)"

    # Run test with progress bar
    log_info "Test running for $TEST_DURATION seconds..."
    local elapsed=0
    while [ $elapsed -lt $TEST_DURATION ]; do
        local percent=$((elapsed * 100 / TEST_DURATION))
        local filled=$((percent / 2))
        local empty=$((50 - filled))

        printf "\r  ["
        printf "%${filled}s" | tr ' ' '#'
        printf "%${empty}s" | tr ' ' '-'
        printf "] %3d%% (%ds/%ds)" $percent $elapsed $TEST_DURATION

        sleep 1
        ((elapsed++))
    done
    echo ""

    # Stop client
    log_info "Stopping client..."
    kill -TERM $client_pid 2>/dev/null || true
    sleep 3
    kill -9 $client_pid 2>/dev/null || true

    # Stop servers
    log_info "Stopping servers..."
    stop_servers $num_servers

    # Extract results
    echo ""
    echo -e "${GREEN}--- CLIENT RESULTS ---${NC}"
    cat /tmp/dht_client.log

    # Extract metrics from log
    local throughput=$(grep "Throughput:" /tmp/dht_client.log 2>/dev/null | awk '{print $2}' || echo "0")
    local total_ops=$(grep "Total successful operations:" /tmp/dht_client.log 2>/dev/null | awk '{print $4}' || echo "0")
    local timeouts=$(grep "Total timeouts:" /tmp/dht_client.log 2>/dev/null | awk '{print $3}' || echo "0")
    local duration=$(grep "Run duration:" /tmp/dht_client.log 2>/dev/null | awk '{print $3}' || echo "$TEST_DURATION")

    # Handle empty or N/A values
    if [ -z "$throughput" ] || [ "$throughput" == "N/A" ]; then
        throughput="0"
    fi
    if [ -z "$total_ops" ] || [ "$total_ops" == "N/A" ]; then
        total_ops="0"
    fi
    if [ -z "$timeouts" ] || [ "$timeouts" == "N/A" ]; then
        timeouts="0"
    fi
    if [ -z "$duration" ] || [ "$duration" == "N/A" ]; then
        duration="$TEST_DURATION"
    fi

    # Calculate average latency using Little's Law
    # Each client has 1 request in flight at a time, so L = num_clients
    # Latency W = L / throughput = num_clients / throughput (in seconds)
    # Convert to milliseconds
    local avg_latency_ms="0"
    if [ "$throughput" != "0" ] && [ -n "$throughput" ]; then
        avg_latency_ms=$(echo "scale=4; ($num_clients / $throughput) * 1000" | bc 2>/dev/null || echo "0")
    fi

    # Write to CSV
    echo "$num_servers,$num_clients,$total_ops,$timeouts,$throughput,$avg_latency_ms" >> "$CSV_OUTPUT"

    echo ""
    echo -e "${GREEN}Calculated Average Latency: ${avg_latency_ms} ms${NC}"
    echo ""

    echo -e "${GREEN}--- SERVER RESULTS ---${NC}"
    for ((i=0; i<num_servers; i++)); do
        echo "Server $i (port $((PORT_BASE + i))):"
        cat /tmp/dht_server_${i}.log
        echo ""
    done

    return 0
}

print_summary() {
    echo ""
    echo -e "${CYAN}=============================================================================${NC}"
    echo -e "${CYAN}                           SUMMARY OF ALL TESTS${NC}"
    echo -e "${CYAN}=============================================================================${NC}"
    echo ""
    echo -e "${GREEN}Results saved to: $CSV_OUTPUT${NC}"
    echo ""

    # Print CSV contents as a table
    echo "CSV Contents:"
    echo ""
    column -t -s',' "$CSV_OUTPUT" 2>/dev/null || cat "$CSV_OUTPUT"
    echo ""
    echo -e "${GREEN}All tests completed!${NC}"
}

main() {
    echo ""
    echo -e "${CYAN}=============================================================================${NC}"
    echo -e "${CYAN}     Distributed Hash Table - Benchmark Test Suite${NC}"
    echo -e "${CYAN}=============================================================================${NC}"
    echo ""
    log_info "Test Configuration:"
    echo "  - Server counts: ${SERVER_COUNTS[*]}"
    echo "  - Client thread counts: ${CLIENT_THREADS[*]}"
    echo "  - Test duration: ${TEST_DURATION} seconds per test"
    echo "  - Total tests: $((${#SERVER_COUNTS[@]} * ${#CLIENT_THREADS[@]}))"
    echo "  - Output file: $CSV_OUTPUT"
    echo ""

    # Check if binary exists
    check_binary

    # Initial cleanup
    cleanup

    # Initialize CSV file with header
    echo "servers,clients,total_ops,timeouts,throughput_ops_sec,avg_latency_ms" > "$CSV_OUTPUT"
    log_info "Created CSV file: $CSV_OUTPUT"

    # Run all test combinations
    for num_servers in "${SERVER_COUNTS[@]}"; do
        for num_clients in "${CLIENT_THREADS[@]}"; do
            run_test $num_servers $num_clients
        done
    done

    # Print summary
    print_summary
}

# Run main
main "$@"

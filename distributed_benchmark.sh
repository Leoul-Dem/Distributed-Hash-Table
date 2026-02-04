#!/bin/bash

# =============================================================================
# Distributed Hash Table - Distributed Benchmark Script
# =============================================================================
# This script:
# 1. SSH's into remote machines and selects the least-used ones for servers
# 2. Starts servers on remote machines
# 3. Runs clients LOCALLY on this machine
# 4. Tests all combinations of servers (1,2,3) and clients (50,100,150,200,250,300)
# 5. Records results to CSV with throughput and calculated latency
# =============================================================================

set +e

# =============================================================================
# Configuration
# =============================================================================
USER="lgd226"
DOMAIN="cse.lehigh.edu"
PORT=1895
TEST_DURATION=20

# Local binary path (for running clients locally)
LOCAL_BINARY_PATH="./build/Distibuted_Hash_Table"

# Remote binary path (for running servers on remote machines)
REMOTE_BINARY_PATH="/home/lgd226/CSE376/Distributed-Hash-Table/build/Distibuted_Hash_Table"

# Output CSV file
CSV_OUTPUT="distributed_benchmark_results.csv"

# Test combinations (reduced for faster iteration)
SERVER_COUNTS=(1 2 3)
CLIENT_THREADS=(500 1000)

# Maximum servers to select (we'll pick the 3 best machines)
MAX_SERVERS=3

# Available machines
MACHINES=(
    "ariel"
    "caliban"
    "callisto"
    "ceres"
    "chiron"
    "cupid"
    "eris"
    "europa"
    "hydra"
    "iapetus"
    "io"
    "ixion"
    "mars"
    "mercury"
    "neptune"
    "nereid"
    "nix"
    "orcus"
    "phobos"
    "puck"
    "saturn"
    "triton"
    "varda"
    "vesta"
    "xena"
)

# =============================================================================
# Data structures
# =============================================================================
declare -A MACHINE_SCORES
declare -A MACHINE_CPU
declare -A MACHINE_LOAD
declare -a SELECTED_SERVERS
declare -A SERVER_IPS

# =============================================================================
# Colors for output
# =============================================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# =============================================================================
# Helper Functions
# =============================================================================

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

ssh_cmd() {
    local machine=$1
    local cmd=$2
    local timeout=${3:-10}
    ssh -o ConnectTimeout=$timeout \
        -o StrictHostKeyChecking=no \
        -o BatchMode=yes \
        -o LogLevel=ERROR \
        "${USER}@${machine}.${DOMAIN}" "$cmd" 2>/dev/null
}

# =============================================================================
# Check local binary
# =============================================================================

check_local_binary() {
    if [ ! -f "$LOCAL_BINARY_PATH" ]; then
        log_error "Local binary not found at $LOCAL_BINARY_PATH"
        log_info "Building the project..."

        mkdir -p build
        cd build
        cmake ..
        make -j$(nproc)
        cd ..

        if [ ! -f "$LOCAL_BINARY_PATH" ]; then
            log_error "Build failed!"
            exit 1
        fi

        log_success "Build completed"
    else
        log_success "Local binary found: $LOCAL_BINARY_PATH"
    fi
}

# =============================================================================
# Machine selection and server management
# =============================================================================

check_machine_usage() {
    local machine=$1
    local host="${USER}@${machine}.${DOMAIN}"

    local result
    result=$(ssh -o ConnectTimeout=8 \
                 -o StrictHostKeyChecking=no \
                 -o BatchMode=yes \
                 -o LogLevel=ERROR \
                 "$host" '
        cpu_idle=$(top -bn1 2>/dev/null | grep "Cpu(s)" | awk "{print \$8}" | cut -d"%" -f1)
        if [ -z "$cpu_idle" ]; then
            cpu_idle=$(vmstat 1 2 2>/dev/null | tail -1 | awk "{print \$15}")
        fi
        [ -z "$cpu_idle" ] && cpu_idle=50
        cpu_usage=$(awk "BEGIN {printf \"%.1f\", 100 - $cpu_idle}")

        net_bytes=$(cat /proc/net/dev 2>/dev/null | grep -v "lo:" | grep ":" | awk "{rx+=\$2; tx+=\$10} END {print rx+tx}")
        [ -z "$net_bytes" ] && net_bytes=0

        load=$(cat /proc/loadavg 2>/dev/null | awk "{print \$1}")
        [ -z "$load" ] && load=1

        echo "$cpu_usage $net_bytes $load"
    ' 2>/dev/null)

    if [ -z "$result" ]; then
        echo "-1 -1 -1"
    else
        echo "$result"
    fi
}

get_machine_ip() {
    local machine=$1
    ssh_cmd "$machine" "hostname -I | awk '{print \$1}'" 5
}

select_server_machines() {
    echo ""
    log_info "Scanning ${#MACHINES[@]} machines for availability and resource usage..."
    echo ""

    local available_count=0

    for machine in "${MACHINES[@]}"; do
        printf "  Checking %-15s ... " "$machine"

        local usage
        usage=$(check_machine_usage "$machine")

        local cpu net load
        read -r cpu net load <<< "$usage"

        if [ "$cpu" = "-1" ]; then
            echo -e "${RED}UNREACHABLE${NC}"
            continue
        fi

        local net_normalized
        net_normalized=$(awk "BEGIN {v = $net / 100000000; if (v > 100) v = 100; printf \"%.2f\", v}")

        local load_normalized
        load_normalized=$(awk "BEGIN {v = $load * 10; if (v > 100) v = 100; printf \"%.2f\", v}")

        local score
        score=$(awk "BEGIN {printf \"%.2f\", ($cpu * 0.4) + ($net_normalized * 0.3) + ($load_normalized * 0.3)}")

        MACHINE_SCORES[$machine]=$score
        MACHINE_CPU[$machine]=$cpu
        MACHINE_LOAD[$machine]=$load
        ((available_count++))

        echo -e "${GREEN}OK${NC} (CPU: ${cpu}%, Load: ${load}, Score: ${score})"
    done

    echo ""
    log_info "Found $available_count available machines"

    if [ $available_count -lt $MAX_SERVERS ]; then
        log_error "Not enough available machines. Need $MAX_SERVERS, found $available_count"
        exit 1
    fi

    # Sort machines by score and select top MAX_SERVERS
    log_info "Selecting $MAX_SERVERS least-used machines as potential servers..."

    SELECTED_SERVERS=($(
        for machine in "${!MACHINE_SCORES[@]}"; do
            echo "${MACHINE_SCORES[$machine]} $machine"
        done | sort -n | head -n $MAX_SERVERS | awk '{print $2}'
    ))

    echo ""
    log_success "Selected server machines:"
    for machine in "${SELECTED_SERVERS[@]}"; do
        local ip
        ip=$(get_machine_ip "$machine")
        SERVER_IPS[$machine]=$ip
        echo "  - $machine ($ip) - Score: ${MACHINE_SCORES[$machine]}, CPU: ${MACHINE_CPU[$machine]}%, Load: ${MACHINE_LOAD[$machine]}"
    done
}

start_remote_servers() {
    local num_servers=$1

    log_info "Starting $num_servers remote server(s)..."

    for ((i=0; i<num_servers; i++)); do
        local machine="${SELECTED_SERVERS[$i]}"

        # Kill any existing instances first
        ssh_cmd "$machine" "pkill -9 -f '$REMOTE_BINARY_PATH' 2>/dev/null; sleep 1" 5

        # Start the server in background
        ssh_cmd "$machine" "nohup $REMOTE_BINARY_PATH $PORT > /tmp/dht_server.log 2>&1 & disown" 5

        if [ $? -eq 0 ]; then
            log_success "Server start command sent to $machine (${SERVER_IPS[$machine]})"
        else
            log_error "Failed to send start command to $machine"
            return 1
        fi
    done

    # Wait for servers to initialize
    log_info "Waiting for servers to initialize (3 seconds)..."
    sleep 3

    # Verify servers are running
    for ((i=0; i<num_servers; i++)); do
        local machine="${SELECTED_SERVERS[$i]}"
        local pid
        pid=$(ssh_cmd "$machine" "pgrep -f '$REMOTE_BINARY_PATH'" 5)

        if [ -n "$pid" ]; then
            log_success "Server on $machine is running (PID: $pid)"
        else
            log_error "Server on $machine is NOT running"
            return 1
        fi
    done

    return 0
}

stop_remote_servers() {
    local num_servers=$1

    log_info "Stopping $num_servers remote server(s)..."

    for ((i=0; i<num_servers; i++)); do
        local machine="${SELECTED_SERVERS[$i]}"
        ssh_cmd "$machine" "pkill -SIGTERM -f '$REMOTE_BINARY_PATH' 2>/dev/null" 3
    done

    sleep 2

    # Force kill if needed
    for ((i=0; i<num_servers; i++)); do
        local machine="${SELECTED_SERVERS[$i]}"
        ssh_cmd "$machine" "pkill -9 -f '$REMOTE_BINARY_PATH' 2>/dev/null" 3
    done
}

collect_server_metrics() {
    local num_servers=$1

    echo ""
    echo -e "${GREEN}--- SERVER RESULTS ---${NC}"
    for ((i=0; i<num_servers; i++)); do
        local machine="${SELECTED_SERVERS[$i]}"
        echo "Server $i ($machine - ${SERVER_IPS[$machine]}):"
        ssh_cmd "$machine" "cat /tmp/dht_server.log 2>/dev/null" 10
        echo ""
    done
}

# =============================================================================
# Run a single test
# =============================================================================

run_test() {
    local num_servers=$1
    local num_clients=$2

    echo ""
    echo -e "${CYAN}=============================================================================${NC}"
    echo -e "${CYAN}  TEST: $num_servers server(s), $num_clients client threads, ${TEST_DURATION}s${NC}"
    echo -e "${CYAN}=============================================================================${NC}"

    # Stop any previous servers
    stop_remote_servers $MAX_SERVERS 2>/dev/null
    sleep 1

    # Start the required number of servers
    if ! start_remote_servers $num_servers; then
        log_error "Failed to start servers"
        echo "$num_servers,$num_clients,ERROR,ERROR,ERROR,ERROR" >> "$CSV_OUTPUT"
        return 1
    fi

    # Build SERVER_IPS string (pipe-delimited) for the client
    local server_ips_str=""
    for ((i=0; i<num_servers; i++)); do
        local machine="${SELECTED_SERVERS[$i]}"
        if [ -n "$server_ips_str" ]; then
            server_ips_str="${server_ips_str}|"
        fi
        server_ips_str="${server_ips_str}${SERVER_IPS[$machine]}"
    done

    log_info "Server IPs for clients: $server_ips_str"

    # Start client LOCALLY
    log_info "Starting LOCAL client with $num_clients threads..."
    SERVER_IPS="$server_ips_str" NUM_CLIENTS=$num_clients $LOCAL_BINARY_PATH $PORT > /tmp/dht_client.log 2>&1 &
    local client_pid=$!

    sleep 1

    if ! kill -0 $client_pid 2>/dev/null; then
        log_error "Client failed to start!"
        cat /tmp/dht_client.log
        stop_remote_servers $num_servers
        echo "$num_servers,$num_clients,ERROR,ERROR,ERROR,ERROR" >> "$CSV_OUTPUT"
        return 1
    fi

    log_success "Local client running (PID: $client_pid)"

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

    # Stop local client
    log_info "Stopping local client..."
    kill -TERM $client_pid 2>/dev/null || true
    sleep 3
    kill -9 $client_pid 2>/dev/null || true

    # Stop remote servers
    stop_remote_servers $num_servers

    # Extract results
    echo ""
    echo -e "${GREEN}--- CLIENT RESULTS ---${NC}"
    cat /tmp/dht_client.log

    # Extract metrics from log
    local throughput=$(grep "Throughput:" /tmp/dht_client.log 2>/dev/null | awk '{print $2}' || echo "0")
    local total_ops=$(grep "Total successful operations:" /tmp/dht_client.log 2>/dev/null | awk '{print $4}' || echo "0")
    local timeouts=$(grep "Total timeouts:" /tmp/dht_client.log 2>/dev/null | awk '{print $3}' || echo "0")

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

    # Collect server metrics
    collect_server_metrics $num_servers

    return 0
}

# =============================================================================
# Cleanup
# =============================================================================

cleanup() {
    log_info "Cleaning up..."

    # Kill local client if running
    pkill -9 -f "$LOCAL_BINARY_PATH" 2>/dev/null || true

    # Stop all remote servers
    for machine in "${SELECTED_SERVERS[@]}"; do
        ssh_cmd "$machine" "pkill -9 -f '$REMOTE_BINARY_PATH' 2>/dev/null" 3
    done
}

trap cleanup EXIT

# =============================================================================
# Print summary
# =============================================================================

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

# =============================================================================
# Main
# =============================================================================

main() {
    echo ""
    echo -e "${CYAN}=============================================================================${NC}"
    echo -e "${CYAN}     Distributed Hash Table - Distributed Benchmark Suite${NC}"
    echo -e "${CYAN}     (Remote Servers + Local Clients)${NC}"
    echo -e "${CYAN}=============================================================================${NC}"
    echo ""
    log_info "Test Configuration:"
    echo "  - Server counts to test: ${SERVER_COUNTS[*]}"
    echo "  - Client thread counts to test: ${CLIENT_THREADS[*]}"
    echo "  - Test duration per test: ${TEST_DURATION} seconds"
    echo "  - Total tests: $((${#SERVER_COUNTS[@]} * ${#CLIENT_THREADS[@]}))"
    echo "  - Output file: $CSV_OUTPUT"
    echo ""
    echo "  - Local binary: $LOCAL_BINARY_PATH"
    echo "  - Remote binary: $REMOTE_BINARY_PATH"
    echo "  - Port: $PORT"
    echo ""

    # Check local binary exists
    check_local_binary

    # Select server machines
    select_server_machines

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

#!/bin/bash


USER="lgd226"
DOMAIN="cse.lehigh.edu"
PORT=1895
N_MACHINES=${1:-8}  # Total number of machines to select
N_SERVERS=${2:-3}   # Number of server machines (rest will be clients)
NUM_CLIENTS=${3:-50} # Number of client threads per machine

machines=(
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

RESULTS_FILE=$(mktemp)

cleanup() {
    jobs -p | xargs -r kill 2>/dev/null
    rm -f "$RESULTS_FILE"
}
trap cleanup EXIT INT TERM

check_machine() {
    local machine_name=$1
    local host="${machine_name}.${DOMAIN}"

    result=$(ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o BatchMode=yes \
        -o ServerAliveInterval=5 -o ServerAliveCountMax=1 \
        "${USER}@${host}" \
        "port_conns=\$(ss -tn 2>/dev/null | grep -c ':${PORT}' || echo 0); \
         load=\$(cat /proc/loadavg 2>/dev/null | awk '{print \$1}' || echo 999); \
         echo \"\${port_conns} \${load}\"" 2>/dev/null)

    if [ $? -eq 0 ] && [ -n "$result" ]; then
        last_line=$(echo "$result" | tail -n 1)
        port_conns=$(echo "$last_line" | awk '{print $1}')
        load=$(echo "$last_line" | awk '{print $2}')

        if [[ "$port_conns" =~ ^[0-9]+$ ]] && [[ "$load" =~ ^[0-9.]+$ ]]; then
            score=$(echo "$port_conns $load" | awk '{printf "%.2f", $1 * 10 + $2}')
            echo "${machine_name} ${score}"
        fi
    fi
}

get_ipv4() {
    local machine_name=$1
    local host="${machine_name}.${DOMAIN}"

    ip=$(ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o BatchMode=yes \
        "${USER}@${host}" \
        "hostname -I | awk '{print \$1}'" 2>/dev/null)

    if [ $? -eq 0 ] && [[ "$ip" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "$ip"
    else
        echo "UNKNOWN"
    fi
}


# Check all machines in parallel
for machine in "${machines[@]}"; do
    check_machine "$machine" >> "$RESULTS_FILE" &
done
wait

# Select N least busy machines into array
readarray -t SELECTED_MACHINES < <(sort -k2 -n "$RESULTS_FILE" | head -n "$N_MACHINES" | awk '{print $1}')

if [ ${#SELECTED_MACHINES[@]} -eq 0 ]; then
    echo "ERROR: No reachable machines found!" >&2
    exit 1
fi

# Ensure we have enough machines for the requested split
if [ ${#SELECTED_MACHINES[@]} -lt $N_SERVERS ]; then
    echo "ERROR: Not enough machines (${#SELECTED_MACHINES[@]}) for $N_SERVERS servers!" >&2
    exit 1
fi

# Split into server and client machines
SERVER_MACHINES=("${SELECTED_MACHINES[@]:0:$N_SERVERS}")
CLIENT_MACHINES=("${SELECTED_MACHINES[@]:$N_SERVERS}")

# Get IPv4 addresses for server machines in parallel
declare -A SERVER_IPS
IP_RESULTS_FILE=$(mktemp)

for machine in "${SERVER_MACHINES[@]}"; do
    (echo "$machine $(get_ipv4 "$machine")") >> "$IP_RESULTS_FILE" &
done
wait

# Read IP results into associative array
while read -r machine ip; do
    SERVER_IPS["$machine"]="$ip"
done < "$IP_RESULTS_FILE"
rm -f "$IP_RESULTS_FILE"

echo "=== CONFIGURATION ==="
echo "Port: ${PORT}"
echo "Clients per machine: ${NUM_CLIENTS}"
echo ""

echo "=== SERVER MACHINES (${#SERVER_MACHINES[@]}) ==="
for machine in "${SERVER_MACHINES[@]}"; do
    echo "${machine}.${DOMAIN} -> ${SERVER_IPS[$machine]}"
done

echo ""
echo "=== CLIENT MACHINES (${#CLIENT_MACHINES[@]}) ==="
for machine in "${CLIENT_MACHINES[@]}"; do
    echo "${machine}.${DOMAIN}"
done

# Build the SERVER_IPS environment variable (pipe-delimited)
SERVER_IPS_ENV=""
for machine in "${SERVER_MACHINES[@]}"; do
    if [ -n "$SERVER_IPS_ENV" ]; then
        SERVER_IPS_ENV="${SERVER_IPS_ENV}|${SERVER_IPS[$machine]}"
    else
        SERVER_IPS_ENV="${SERVER_IPS[$machine]}"
    fi
done

BINARY_PATH="~/CSE376/Distributed-Hash-Table/main"

echo ""
echo "=== STARTING SERVERS ==="

# Start servers in parallel (no SERVER_IPS env = storage server mode)
for machine in "${SERVER_MACHINES[@]}"; do
    host="${machine}.${DOMAIN}"
    echo "Starting server on ${host}..."
    ssh -o StrictHostKeyChecking=no "${USER}@${host}" \
        "nohup ${BINARY_PATH} ${PORT} </dev/null > ~/server.log 2>&1 &"
done

echo "Waiting for servers to initialize..."
sleep 3

# Verify servers are running
echo ""
echo "=== VERIFYING SERVERS ==="
ALL_SERVERS_RUNNING=true
for machine in "${SERVER_MACHINES[@]}"; do
    host="${machine}.${DOMAIN}"
    # Check if the process is running and port is listening
    status=$(ssh -o StrictHostKeyChecking=no "${USER}@${host}" \
        "pgrep -f '${BINARY_PATH}' > /dev/null && ss -tln | grep -q ':${PORT}' && echo 'RUNNING' || echo 'NOT RUNNING'" 2>/dev/null)

    if [ "$status" = "RUNNING" ]; then
        echo "  [OK] ${host} - server running on port ${PORT}"
    else
        echo "  [FAIL] ${host} - server not running"
        ALL_SERVERS_RUNNING=false
    fi
done

if [ "$ALL_SERVERS_RUNNING" = false ]; then
    echo ""
    echo "ERROR: Not all servers started successfully. Check logs on server machines."
    exit 1
fi

echo ""
echo "=== STARTING CLIENTS ==="
echo "Server IPs for clients: ${SERVER_IPS_ENV}"
echo "Client threads per machine: ${NUM_CLIENTS}"

# Start clients in parallel (with SERVER_IPS env = client mode)
for machine in "${CLIENT_MACHINES[@]}"; do
    host="${machine}.${DOMAIN}"
    echo "Starting client on ${host}..."
    ssh -o StrictHostKeyChecking=no "${USER}@${host}" \
        "export SERVER_IPS='${SERVER_IPS_ENV}'; export NUM_CLIENTS='${NUM_CLIENTS}'; nohup ${BINARY_PATH} ${PORT} </dev/null > ~/client.log 2>&1 &"
done

echo ""
echo "=== ALL PROCESSES STARTED ==="
echo "Servers: ${#SERVER_MACHINES[@]} running"
echo "Clients: ${#CLIENT_MACHINES[@]} started (${NUM_CLIENTS} threads each)"
echo "Total client threads: $((${#CLIENT_MACHINES[@]} * NUM_CLIENTS))"
echo ""
echo "To check logs:"
echo "  Server logs: ssh ${USER}@<server>.${DOMAIN} 'cat ~/server.log'"
echo "  Client logs: ssh ${USER}@<client>.${DOMAIN} 'cat ~/client.log'"

echo ""
echo "=== WAITING 3 MINUTES ==="
echo "Processes will be terminated at $(date -d '+3 minutes' '+%H:%M:%S')"
sleep 180

echo ""
echo "=== SENDING SIGTERM TO ALL PROCESSES ==="

# Stop clients first
for machine in "${CLIENT_MACHINES[@]}"; do
    host="${machine}.${DOMAIN}"
    echo "Stopping client on ${host}..."
    ssh -o StrictHostKeyChecking=no "${USER}@${host}" \
        "pkill -TERM -f '${BINARY_PATH}'" 2>/dev/null &
done
wait

# Brief pause to let clients disconnect gracefully
sleep 2

# Stop servers
for machine in "${SERVER_MACHINES[@]}"; do
    host="${machine}.${DOMAIN}"
    echo "Stopping server on ${host}..."
    ssh -o StrictHostKeyChecking=no "${USER}@${host}" \
        "pkill -TERM -f '${BINARY_PATH}'" 2>/dev/null &
done
wait

echo ""
echo "=== COLLECTING RESULTS ==="

# Collect client results
echo ""
echo "--- CLIENT RESULTS ---"
for machine in "${CLIENT_MACHINES[@]}"; do
    host="${machine}.${DOMAIN}"
    echo ""
    echo "[$host]:"
    ssh -o StrictHostKeyChecking=no "${USER}@${host}" \
        "tail -20 ~/client.log 2>/dev/null | grep -E '(operations|timeouts|Throughput|CLIENT RESULTS)'" 2>/dev/null
done

# Collect server results
echo ""
echo "--- SERVER RESULTS ---"
for machine in "${SERVER_MACHINES[@]}"; do
    host="${machine}.${DOMAIN}"
    echo ""
    echo "[$host]:"
    ssh -o StrictHostKeyChecking=no "${USER}@${host}" \
        "tail -10 ~/server.log 2>/dev/null | grep -E '(Received|Executed|Responded|metrics)'" 2>/dev/null
done

echo ""
echo "=== ALL PROCESSES TERMINATED ==="
echo "Run completed at $(date '+%H:%M:%S')"

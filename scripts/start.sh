#!/bin/bash

# ssh lgd226@[machine_name].cse.lehigh.edu
# port 1895 only

USER="lgd226"
DOMAIN="cse.lehigh.edu"
PORT=1895
N_MACHINES=${1:-8}  # Total number of machines to select
N_SERVERS=${2:-3}   # Number of server machines (rest will be clients)

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

echo "=== SERVER MACHINES (${#SERVER_MACHINES[@]}) ==="
for machine in "${SERVER_MACHINES[@]}"; do
    echo "${machine}.${DOMAIN} -> ${SERVER_IPS[$machine]}"
done

echo ""
echo "=== CLIENT MACHINES (${#CLIENT_MACHINES[@]}) ==="
for machine in "${CLIENT_MACHINES[@]}"; do
    echo "${machine}.${DOMAIN}"
done

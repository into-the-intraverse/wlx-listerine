#!/bin/bash
# Bash sample script

NAME="world"
COUNT=42

function greet() {
    local who="$1"
    echo "Hello, ${who}!"
}

for i in $(seq 1 $COUNT); do
    greet "$NAME"
done

if [ -f "/etc/hosts" ]; then
    cat /etc/hosts | head -5
fi

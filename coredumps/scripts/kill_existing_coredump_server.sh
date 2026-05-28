#!/bin/bash
PORT=1234

# Find the PID holding the port and kill it, if any.
PID=$(lsof -ti tcp:"$PORT" 2>/dev/null || true)
if [ -n "$PID" ]; then
    echo "killing stale gdbserver on port $PORT (pid $PID)"
    kill "$PID" 2>/dev/null || true
    # Wait for it to actually release the port (max ~2s).
    for _ in $(seq 1 20); do
        sleep 0.1
        lsof -ti tcp:"$PORT" >/dev/null 2>&1 || break
    done
    # If still alive, force it.
    if lsof -ti tcp:"$PORT" >/dev/null 2>&1; then
        echo "forcing kill"
        kill -9 "$PID" 2>/dev/null || true
        sleep 0.2
    fi
fi

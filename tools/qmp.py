#!/usr/bin/env python3
"""Minimal QMP client: qmp.py SOCKET COMMAND [JSON-ARGS]  — prints the JSON reply."""
import json, socket, sys

def main():
    sock_path, command = sys.argv[1], sys.argv[2]
    args = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(30)
    s.connect(sock_path)
    f = s.makefile("rw")
    json.loads(f.readline())  # greeting
    for cmd in ({"execute": "qmp_capabilities"}, {"execute": command, "arguments": args}):
        f.write(json.dumps(cmd) + "\n"); f.flush()
        while True:
            reply = json.loads(f.readline())
            if "return" in reply or "error" in reply:
                break
    print(json.dumps(reply))
    sys.exit(1 if "error" in reply else 0)

main()

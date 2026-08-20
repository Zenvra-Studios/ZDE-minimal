import json, sys, subprocess

req_init = {
    "jsonrpc": "2.0", "id": 1, "method": "initialize",
    "params": {
        "processId": None, "rootUri": "file:///C:/Users/Administrator/Downloads/ZDE-minimal",
        "capabilities": {}
    }
}

req_open = {
    "jsonrpc": "2.0", "method": "textDocument/didOpen",
    "params": {
        "textDocument": {
            "uri": "file:///C:/Users/Administrator/Downloads/ZDE-minimal/test.cpp",
            "languageId": "cpp", "version": 1,
            "text": "#include <vector>\nint main() { std::vector<int> v; std::\n}\n"
        }
    }
}

req_comp = {
    "jsonrpc": "2.0", "id": 2, "method": "textDocument/completion",
    "params": {
        "textDocument": {"uri": "file:///C:/Users/Administrator/Downloads/ZDE-minimal/test.cpp"},
        "position": {"line": 2, "character": 37},
        "context": {"triggerKind": 1}
    }
}

def make_msg(d):
    s = json.dumps(d)
    return f"Content-Length: {len(s)}\r\n\r\n{s}"

with open("test_clangd.py", "w") as f:
    f.write("""import json, sys, subprocess, threading, time
p = subprocess.Popen(["clangd"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
def read_msgs():
    while True:
        line = p.stdout.readline()
        if not line: break
        if line.startswith(b"Content-Length: "):
            l = int(line.split(b": ")[1].strip())
            p.stdout.readline()
            msg = p.stdout.read(l)
            print(msg.decode('utf-8'))
threading.Thread(target=read_msgs, daemon=True).start()
p.stdin.write(b'""" + make_msg(req_init).replace("\n", "\\n").replace("\r", "\\r") + """')
p.stdin.flush()
time.sleep(1)
p.stdin.write(b'""" + make_msg(req_open).replace("\n", "\\n").replace("\r", "\\r") + """')
p.stdin.flush()
time.sleep(1)
p.stdin.write(b'""" + make_msg(req_comp).replace("\n", "\\n").replace("\r", "\\r") + """')
p.stdin.flush()
time.sleep(2)
""")

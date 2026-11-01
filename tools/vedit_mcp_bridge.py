"""VEDIT: stdio bridge from Claude Code (or any MCP client) to the editor's localhost MCP.

Registered instead of `ECEG.exe --mcp-stdio` because an MCP client keeps its server process
alive for the whole session, and a running ECEG.exe locks the exe that builds and the desktop
copy need to overwrite. This script holds no lock on anything.

Each request re-reads the session file the editor writes when Agent access is turned on, so the
editor can be opened, closed or restarted while the client stays connected. initialize and
tools/list are answered from a cache when the editor is closed, so the client still registers
the tools and a later call works once the editor is up.

    python tools/vedit_mcp_bridge.py
"""
import json
import os
import sys
import urllib.error
import urllib.request

SESSION_FILE = os.environ.get("DRIFT_MCP_SESSION_PATH") or os.path.join(
    os.path.expanduser("~"), "drift", "mcp-session.json")
CACHE_FILE = os.path.join(
    os.environ.get("LOCALAPPDATA") or os.path.expanduser("~"), "EasyCutEasyGo", "mcp-bridge-cache.json")

OFFLINE = ("'Easy Cut, Easy Go!' 편집기에 연결하지 못했습니다. 편집기를 켜고 위쪽 "
           "'에이전트' 버튼에서 '이번 세션 허용'을 켜 주세요. ('시작할 때 켜기'를 켜 두면 다음부터 자동입니다.)")


class Offline(Exception):
    pass


def load_cache():
    try:
        with open(CACHE_FILE, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_cache(cache):
    try:
        os.makedirs(os.path.dirname(CACHE_FILE), exist_ok=True)
        with open(CACHE_FILE, "w", encoding="utf-8") as f:
            json.dump(cache, f, ensure_ascii=False)
    except OSError:
        pass


def forward(message):
    try:
        with open(SESSION_FILE, encoding="utf-8") as f:
            session = json.load(f)
        url, token = session["url"], session["token"]
    except (OSError, ValueError, KeyError):
        raise Offline()
    req = urllib.request.Request(url, json.dumps(message).encode("utf-8"), {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
        "Authorization": "Bearer " + token,
    })
    try:
        with urllib.request.urlopen(req, timeout=600) as resp:
            body = resp.read()
    except urllib.error.HTTPError as e:
        body = e.read()
        if not body:
            raise Offline()
    except (urllib.error.URLError, OSError):
        raise Offline()
    return json.loads(body.decode("utf-8")) if body.strip() else None


def reply(msg_id, result=None, error=None):
    out = {"jsonrpc": "2.0", "id": msg_id}
    if error is not None:
        out["error"] = error
    else:
        out["result"] = result
    sys.stdout.buffer.write(json.dumps(out, ensure_ascii=False).encode("utf-8") + b"\n")
    sys.stdout.buffer.flush()


def handle(message, cache):
    msg_id = message.get("id")
    method = message.get("method", "")
    if msg_id is None:
        return  # notifications need no answer and the editor keeps no per-client state

    if method == "ping":
        return reply(msg_id, {})

    try:
        response = forward(message)
    except Offline:
        response = None

    if method == "initialize":
        result = (response or {}).get("result") or cache.get("initialize") or {
            "protocolVersion": "2025-03-26", "capabilities": {"tools": {}}}
        if response and "result" in response:
            cache["initialize"] = result
            save_cache(cache)
        result = dict(result, serverInfo={"name": "easycuteasygo", "version": result.get(
            "serverInfo", {}).get("version", "")})
        return reply(msg_id, result)

    if method == "tools/list":
        if response and "result" in response:
            cache["tools"] = response["result"]
            save_cache(cache)
            return reply(msg_id, response["result"])
        return reply(msg_id, cache.get("tools") or {"tools": []})

    if response is None:
        if method == "tools/call":
            return reply(msg_id, {"content": [{"type": "text", "text": OFFLINE}], "isError": True})
        return reply(msg_id, error={"code": -32000, "message": OFFLINE})
    sys.stdout.buffer.write(json.dumps(response, ensure_ascii=False).encode("utf-8") + b"\n")
    sys.stdout.buffer.flush()


def main():
    cache = load_cache()
    for line in sys.stdin.buffer:
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line.decode("utf-8-sig"))
        except ValueError:
            continue
        if isinstance(message, dict):
            handle(message, cache)


if __name__ == "__main__":
    main()

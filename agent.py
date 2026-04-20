#!/usr/bin/env python3

import json
import os
import socket
import sys
import time
import urllib.error
import urllib.request


def load_env_file(path: str) -> None:
    if not os.path.exists(path):
        return

    with open(path, "r", encoding="utf-8") as env_file:
        for raw_line in env_file:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[7:].strip()
            if "=" not in line:
                continue

            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if not key or key in os.environ:
                continue
            if len(value) >= 2 and value[0] == value[-1] and value[0] in ('"', "'"):
                value = value[1:-1]
            os.environ[key] = value


load_env_file(".env")


HOST = os.environ.get("AIDRAW_HOST", "127.0.0.1")
PORT = int(os.environ.get("AIDRAW_PORT", "4444"))
MODEL = os.environ.get("GEMINI_MODEL", "gemini-2.5-flash")

PROMPT_PREAMBLE = """Generate only drawing DSL commands for a 320x200 canvas.
Rules:
- Output only these commands: CLEAR, PIXEL, LINE, RECT, FILLRECT, CIRCLE, FILLCIRCLE, END.
- Use integer arguments only.
- Colors must be in the range 0..15.
- Coordinates should stay within 0..319 for x and 0..199 for y.
- End the output with END.
- Do not include markdown, prose, or explanations.

Color hints:
0 black
1 dark blue
2 dark green
4 dark red
6 brown
7 light gray
9 bright blue
10 bright green
12 bright red
14 yellow
15 white

User prompt:
"""


def log(message: str) -> None:
    print(message, file=sys.stderr)


def recv_line(sock_file) -> str | None:
    raw = sock_file.readline()
    if not raw:
        return None
    return raw.decode("utf-8", "ignore").rstrip("\r\n")


def read_request(sock_file) -> str | None:
    while True:
        line = recv_line(sock_file)
        if line is None:
            return None
        if line == "BEGIN_REQ":
            break

    mode = recv_line(sock_file)
    if mode is None:
        return None

    prompt_lines = []
    while True:
        line = recv_line(sock_file)
        if line is None:
            return None
        if line == "END_REQ":
            break
        prompt_lines.append(line)

    if mode != "DRAW":
        return ""
    return " ".join(part for part in prompt_lines if part).strip()


def canned_response(prompt: str) -> str:
    lowered = prompt.strip().lower()

    if "flag" in lowered and "us" in lowered:
        return "\n".join(
            [
                "CLEAR 15",
                "FILLRECT 0 0 320 200 15",
                "FILLRECT 0 0 128 112 1",
                "FILLRECT 0 18 320 18 12",
                "FILLRECT 0 54 320 18 12",
                "FILLRECT 0 90 320 18 12",
                "FILLRECT 0 126 320 18 12",
                "FILLRECT 0 162 320 18 12",
                "END",
            ]
        )
    if "smiley" in lowered or "face" in lowered:
        return "\n".join(
            [
                "CLEAR 9",
                "FILLCIRCLE 160 100 58 14",
                "CIRCLE 160 100 58 15",
                "FILLCIRCLE 138 82 7 0",
                "FILLCIRCLE 182 82 7 0",
                "LINE 130 130 146 144 0",
                "LINE 146 144 174 144 0",
                "LINE 174 144 190 130 0",
                "END",
            ]
        )
    if "house" in lowered:
        return "\n".join(
            [
                "CLEAR 9",
                "FILLRECT 0 145 320 55 2",
                "FILLRECT 96 86 112 70 6",
                "RECT 96 86 112 70 15",
                "FILLRECT 138 118 28 38 4",
                "RECT 138 118 28 38 15",
                "LINE 96 86 152 48 12",
                "LINE 152 48 208 86 12",
                "LINE 96 87 152 49 12",
                "LINE 152 49 208 87 12",
                "END",
            ]
        )

    return "\n".join(
        [
            "CLEAR 9",
            "FILLRECT 0 140 320 60 2",
            "FILLCIRCLE 250 45 24 14",
            "CIRCLE 250 45 24 15",
            "FILLRECT 90 85 95 70 6",
            "RECT 90 85 95 70 15",
            "FILLRECT 123 118 26 37 4",
            "RECT 123 118 26 37 15",
            "LINE 90 85 138 52 4",
            "LINE 138 52 185 85 4",
            "END",
        ]
    )


def maybe_generate_with_gemini(prompt: str) -> str | None:
    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        return None

    payload = {
        "contents": [
            {
                "parts": [
                    {
                        "text": PROMPT_PREAMBLE + prompt,
                    }
                ]
            }
        ]
    }
    request = urllib.request.Request(
        f"https://generativelanguage.googleapis.com/v1beta/models/{MODEL}:generateContent?key={api_key}",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            body = json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, TimeoutError, ValueError) as exc:
        log(f"Gemini request failed: {exc}")
        return None

    try:
        parts = body["candidates"][0]["content"]["parts"]
    except (KeyError, IndexError, TypeError):
        log("Gemini response did not contain candidate text")
        return None

    text = "\n".join(part.get("text", "") for part in parts).strip()
    return text or None


def validate_response(text: str) -> str:
    allowed = {
        "CLEAR": 1,
        "PIXEL": 3,
        "LINE": 5,
        "RECT": 5,
        "FILLRECT": 5,
        "CIRCLE": 4,
        "FILLCIRCLE": 4,
        "END": 0,
    }
    cleaned = []
    saw_end = False

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split()
        command = parts[0]
        if command not in allowed:
            raise ValueError(f"unsupported command: {command}")
        if len(parts) - 1 != allowed[command]:
            raise ValueError(f"wrong arity for command: {line}")
        if command == "END":
            cleaned.append("END")
            saw_end = True
            break
        values = []
        for item in parts[1:]:
            values.append(int(item))
        if command == "CLEAR":
            if not 0 <= values[0] <= 15:
                raise ValueError(f"invalid color in line: {line}")
        else:
            if not 0 <= values[-1] <= 15:
                raise ValueError(f"invalid color in line: {line}")
        cleaned.append(" ".join([command] + [str(value) for value in values]))

    if not saw_end:
        cleaned.append("END")
    return "\n".join(cleaned)


def generate_response(prompt: str) -> str:
    candidate = maybe_generate_with_gemini(prompt)
    if candidate is None:
        return canned_response(prompt)
    try:
        return validate_response(candidate)
    except ValueError as exc:
        log(f"invalid Gemini output, using canned fallback: {exc}")
        return canned_response(prompt)


def send_response(sock: socket.socket, body: str) -> None:
    payload = f"BEGIN_RESP\n{body.rstrip()}\nEND_RESP\n".encode("utf-8")
    sock.sendall(payload)


def run_agent() -> None:
    while True:
        try:
            sock = socket.create_connection((HOST, PORT), timeout=2)
            break
        except OSError:
            log(f"waiting for qemu-ai on {HOST}:{PORT}")
            time.sleep(1)

    log(f"connected to qemu-ai on {HOST}:{PORT}")
    with sock:
        sock.settimeout(None)
        sock_file = sock.makefile("rb")
        while True:
            prompt = read_request(sock_file)
            if prompt is None:
                log("serial connection closed")
                return
            log(f"prompt: {prompt!r}")
            body = generate_response(prompt)
            send_response(sock, body)


if __name__ == "__main__":
    try:
        run_agent()
    except KeyboardInterrupt:
        log("agent stopped")
#!/usr/bin/env python3

import json
import os
import socket
import sys
import time
import urllib.error
import urllib.request


COMMAND_ARITY = {
    "CLEAR": 1,
    "PIXEL": 3,
    "LINE": 5,
    "RECT": 5,
    "FILLRECT": 5,
    "CIRCLE": 4,
    "FILLCIRCLE": 4,
    "END": 0,
}
MAX_MODEL_REPAIRS = 2

PROMPT_HINTS = {
    "car": "A car should usually have a rectangular body, a smaller roof, and two wheel circles.",
    "bus": "A bus should usually have a long rectangular body, windows made from rectangles, and two or more wheel circles.",
    "truck": "A truck should usually have a cargo body, a cab, and at least two wheel circles.",
    "vehicle": "A vehicle should usually have a body rectangle and wheel circles.",
    "house": "A house should usually have a wall rectangle, a door rectangle, and roof lines.",
    "flag": "A flag should usually be built from rectangles, not circles.",
    "smiley": "A smiley should usually have one large face circle, two eye circles, and a mouth line.",
    "mountain": "Mountains should usually be built from sloped lines forming peaks.",
    "sunset": "A sunset should usually include a sun circle and a horizon or landscape.",
}

JSON_EXAMPLE = {
    "commands": [
        {"cmd": "CLEAR", "color": 9},
        {"cmd": "FILLRECT", "x": 10, "y": 120, "w": 120, "h": 50, "color": 6},
        {"cmd": "RECT", "x": 10, "y": 120, "w": 120, "h": 50, "color": 15},
        {"cmd": "FILLCIRCLE", "x": 200, "y": 60, "radius": 20, "color": 14},
        {"cmd": "LINE", "x1": 20, "y1": 40, "x2": 140, "y2": 40, "color": 12},
        {"cmd": "END"},
    ]
}
JSON_EXAMPLE_TEXT = json.dumps(JSON_EXAMPLE, indent=2)
REPAIR_JSON_EXAMPLE_TEXT = JSON_EXAMPLE_TEXT.replace("{", "{{").replace("}", "}}")


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
AI_BACKEND = os.environ.get("AI_BACKEND", "gemini").strip().lower()
OPENAI_MODEL = os.environ.get("OPENAI_MODEL", "gpt-5.4")
GEMINI_MODEL = os.environ.get("GEMINI_MODEL", "gemini-3-flash-preview")
OPENAI_API_URL = os.environ.get("OPENAI_API_URL", "https://api.openai.com/v1/responses")

PROMPT_PREAMBLE = f"""Generate a drawing plan for a 320x200 canvas.
Return JSON only. Do not return DSL directly.
Return exactly one JSON object with this shape:
{JSON_EXAMPLE_TEXT}

Rules:
- The only allowed command names are CLEAR, PIXEL, LINE, RECT, FILLRECT, CIRCLE, FILLCIRCLE, END.
- Use integers only.
- Colors must be in the range 0..15.
- Coordinates should stay within 0..319 for x and 0..199 for y.
- The drawing must visibly match the user's request.
- Do not reuse a house scene unless the user asked for a house.
- Do not add a sun, house, or ground unless the user asked for them.
- If asked for a car or bus, include wheels.
- If asked for a smiley face, include a circular face, two eyes, and a mouth.
- If asked for a flag, use rectangles for the flag body.
- If asked for mountains, use lines to form peaks.
- End the command list with {{"cmd": "END"}}.
- Do not include markdown, prose, comments, or code fences.

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

REPAIR_PREAMBLE = f"""Rewrite the broken drawing response so it becomes valid.
Return JSON only. Do not return DSL directly.
Return exactly one JSON object with this shape:
{REPAIR_JSON_EXAMPLE_TEXT}

Rules:
- Use only these commands: CLEAR, PIXEL, LINE, RECT, FILLRECT, CIRCLE, FILLCIRCLE, END.
- Use integers only.
- Colors must be in the range 0..15.
- End the command list with {{{{"cmd": "END"}}}}.
- Do not include markdown, bullets, explanations, or code fences.

Original drawing request:
{{prompt}}

Validation error:
{{error}}

Broken output:
{{bad_output}}

Drawing hints:
{{hints}}
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


def extract_openai_response_text(body: dict) -> str | None:
    output_text = body.get("output_text")
    if isinstance(output_text, str) and output_text.strip():
        return output_text.strip()

    parts = []
    for item in body.get("output", []):
        if not isinstance(item, dict):
            continue
        for content in item.get("content", []):
            if not isinstance(content, dict):
                continue
            text = content.get("text")
            if isinstance(text, str) and content.get("type") in {"output_text", "text"}:
                parts.append(text)

    text = "\n".join(part.strip() for part in parts if part and part.strip()).strip()
    return text or None


def gemini_request_text(prompt_text: str) -> str | None:
    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        return None

    payload = {
        "contents": [{"parts": [{"text": prompt_text}]}],
        "generationConfig": {
            "temperature": 0.2,
            "topP": 0.8,
            "topK": 20,
            "maxOutputTokens": 4096,
            "responseMimeType": "application/json",
            "thinkingConfig": {"thinkingBudget": 0},
        },
    }
    request = urllib.request.Request(
        f"https://generativelanguage.googleapis.com/v1beta/models/{GEMINI_MODEL}:generateContent?key={api_key}",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            body = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        details = exc.read().decode("utf-8", "ignore")
        log(f"Gemini request failed: HTTP {exc.code}: {details}")
        return None
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


def openai_request_text(prompt_text: str) -> str | None:
    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        return None

    payload = {
        "model": OPENAI_MODEL,
        "input": prompt_text,
        "max_output_tokens": 4096,
        "reasoning": {"effort": "low"},
    }
    request = urllib.request.Request(
        OPENAI_API_URL,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            body = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        details = exc.read().decode("utf-8", "ignore")
        log(f"OpenAI request failed: HTTP {exc.code}: {details}")
        return None
    except (urllib.error.URLError, TimeoutError, ValueError) as exc:
        log(f"OpenAI request failed: {exc}")
        return None

    text = extract_openai_response_text(body)
    if text is None:
        log("OpenAI response did not contain candidate text")
        return None
    return text


def backend_display_name(backend: str) -> str:
    if backend == "gemini":
        return "Gemini"
    if backend == "openai":
        return "OpenAI"
    return backend


def backend_attempt_order() -> list[str]:
    if AI_BACKEND in {"gemini", "openai"}:
        return [AI_BACKEND]
    return ["gemini", "openai"]


def request_text_for_backend(backend: str, prompt_text: str) -> str | None:
    if backend == "gemini":
        return gemini_request_text(prompt_text)
    if backend == "openai":
        return openai_request_text(prompt_text)
    return None


def normalize_candidate_text(text: str) -> list[str]:
    normalized = []

    for raw_line in text.replace(";", "\n").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("```"):
            continue
        if ":" in line:
            prefix, suffix = line.split(":", 1)
            if prefix.strip().upper() in COMMAND_ARITY:
                line = prefix.strip() + " " + suffix.strip()
        line = line.replace(",", " ")
        line = line.replace("(", " ")
        line = line.replace(")", " ")
        parts = [part for part in line.split() if part not in {"-", "*"}]
        if not parts:
            continue
        parts[0] = parts[0].upper()
        normalized.append(" ".join(parts))

    return normalized


def extract_json_object(text: str) -> str:
    start = text.find("{")
    end = text.rfind("}")

    if start < 0 or end < 0 or end <= start:
        raise ValueError("no JSON object found")
    return text[start:end + 1]


def command_to_dsl(command: dict) -> str:
    cmd = str(command.get("cmd", "")).upper()

    if cmd == "CLEAR":
        return f"CLEAR {int(command['color'])}"
    if cmd == "PIXEL":
        return f"PIXEL {int(command['x'])} {int(command['y'])} {int(command['color'])}"
    if cmd == "LINE":
        return f"LINE {int(command['x1'])} {int(command['y1'])} {int(command['x2'])} {int(command['y2'])} {int(command['color'])}"
    if cmd == "RECT":
        return f"RECT {int(command['x'])} {int(command['y'])} {int(command['w'])} {int(command['h'])} {int(command['color'])}"
    if cmd == "FILLRECT":
        return f"FILLRECT {int(command['x'])} {int(command['y'])} {int(command['w'])} {int(command['h'])} {int(command['color'])}"
    if cmd == "CIRCLE":
        return f"CIRCLE {int(command['x'])} {int(command['y'])} {int(command['radius'])} {int(command['color'])}"
    if cmd == "FILLCIRCLE":
        return f"FILLCIRCLE {int(command['x'])} {int(command['y'])} {int(command['radius'])} {int(command['color'])}"
    if cmd == "END":
        return "END"
    raise ValueError(f"unsupported command in JSON: {cmd}")


def validate_json_response(text: str) -> str:
    try:
        payload = json.loads(extract_json_object(text))
    except (json.JSONDecodeError, ValueError) as exc:
        raise ValueError(f"invalid JSON response: {exc}")

    commands = payload.get("commands")
    if not isinstance(commands, list) or not commands:
        raise ValueError("JSON response must contain a non-empty commands list")

    lines = []
    for command in commands:
        if not isinstance(command, dict):
            raise ValueError("each JSON command must be an object")
        lines.append(command_to_dsl(command))

    return validate_response("\n".join(lines))


def prompt_hints(prompt: str) -> str:
    lowered = prompt.lower()
    hints = []

    for key, value in PROMPT_HINTS.items():
        if key in lowered:
            hints.append(value)

    if not hints:
        hints.append("Use simple large shapes that clearly match the requested object.")
    return "\n".join(f"- {hint}" for hint in hints)


def vehicle_template(prompt: str) -> str:
    lowered = prompt.lower()

    if "bus" in lowered:
        return "\n".join(
            [
                "CLEAR 9",
                "FILLRECT 0 150 320 50 2",
                "FILLRECT 40 95 220 55 12",
                "RECT 40 95 220 55 15",
                "FILLRECT 55 108 30 18 15",
                "FILLRECT 92 108 30 18 15",
                "FILLRECT 129 108 30 18 15",
                "FILLRECT 166 108 30 18 15",
                "FILLRECT 203 108 30 18 15",
                "FILLCIRCLE 90 155 18 0",
                "FILLCIRCLE 210 155 18 0",
                "CIRCLE 90 155 18 7",
                "CIRCLE 210 155 18 7",
                "END",
            ]
        )

    return "\n".join(
        [
            "CLEAR 9",
            "FILLRECT 0 150 320 50 2",
            "FILLRECT 60 115 150 32 12",
            "FILLRECT 95 90 70 30 12",
            "RECT 60 115 150 32 15",
            "RECT 95 90 70 30 15",
            "FILLRECT 105 98 22 14 15",
            "FILLRECT 132 98 22 14 15",
            "FILLCIRCLE 95 150 16 0",
            "FILLCIRCLE 180 150 16 0",
            "CIRCLE 95 150 16 7",
            "CIRCLE 180 150 16 7",
            "END",
        ]
    )


def targeted_template(prompt: str) -> str | None:
    lowered = prompt.lower()

    if any(word in lowered for word in ["car", "bus", "truck", "vehicle"]):
        return vehicle_template(prompt)
    return None


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


def maybe_generate_with_backend(backend: str, prompt: str) -> str | None:
    return request_text_for_backend(backend, PROMPT_PREAMBLE + prompt)


def repair_output(backend: str, prompt: str, bad_output: str, error: str) -> str | None:
    try:
        repair_prompt = REPAIR_PREAMBLE.format(
            prompt=prompt,
            error=error,
            bad_output=bad_output,
            hints=prompt_hints(prompt),
        )
    except (KeyError, ValueError) as exc:
        log(f"failed to build {backend_display_name(backend)} repair prompt: {exc}")
        return None

    return request_text_for_backend(backend, repair_prompt)


def validate_response(text: str) -> str:
    cleaned = []
    saw_end = False
    saw_command = False

    for line in normalize_candidate_text(text):
        parts = line.split()
        command = parts[0]
        if command not in COMMAND_ARITY:
            continue
        saw_command = True
        if len(parts) - 1 != COMMAND_ARITY[command]:
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
            for value in values[:-1]:
                if value < -4096 or value > 4096:
                    raise ValueError(f"unreasonable coordinate in line: {line}")
            if not 0 <= values[-1] <= 15:
                raise ValueError(f"invalid color in line: {line}")
        cleaned.append(" ".join([command] + [str(value) for value in values]))

    if not saw_command:
        raise ValueError("no valid DSL commands found")
    if not saw_end:
        cleaned.append("END")
    return "\n".join(cleaned)


def semantic_validate_response(prompt: str, text: str) -> None:
    lowered = prompt.lower()
    lines = [line.strip() for line in text.splitlines() if line.strip() and line.strip() != "END"]
    commands = [line.split()[0] for line in lines]
    circle_count = sum(command in {"CIRCLE", "FILLCIRCLE"} for command in commands)
    rect_count = sum(command in {"RECT", "FILLRECT"} for command in commands)
    line_count = sum(command == "LINE" for command in commands)

    if any(word in lowered for word in ["car", "bus", "truck", "vehicle"]):
        if circle_count < 2 or rect_count < 1:
            raise ValueError("vehicle prompt requires at least two wheel circles and one body rectangle")
    if "house" in lowered:
        if rect_count < 2 or line_count < 2:
            raise ValueError("house prompt requires wall rectangles and roof lines")
    if "flag" in lowered:
        if rect_count < 2:
            raise ValueError("flag prompt requires rectangles for the flag body")
    if any(word in lowered for word in ["smiley", "smile", "face"]):
        if circle_count < 3 or line_count < 1:
            raise ValueError("smiley prompt requires a face circle, eyes, and a mouth line")
    if any(word in lowered for word in ["mountain", "mountains", "peak", "peaks"]):
        if line_count < 2:
            raise ValueError("mountain prompt requires line-based peaks")
    if "sunset" in lowered:
        if circle_count < 1:
            raise ValueError("sunset prompt requires a sun circle")


def generate_response(prompt: str) -> str:
    for backend in backend_attempt_order():
        backend_name = backend_display_name(backend)
        candidate = maybe_generate_with_backend(backend, prompt)
        if candidate is None:
            log(f"{backend_name} unavailable, trying next backend")
            continue

        for attempt in range(MAX_MODEL_REPAIRS + 1):
            try:
                try:
                    validated = validate_json_response(candidate)
                except ValueError:
                    validated = validate_response(candidate)
                semantic_validate_response(prompt, validated)
                log(f"accepted {backend_name} output: {validated!r}")
                return validated
            except ValueError as exc:
                log(f"invalid {backend_name} output on attempt {attempt + 1}: {exc}")
                log(f"raw {backend_name} output: {candidate!r}")
                if attempt == MAX_MODEL_REPAIRS:
                    break
                repaired = repair_output(backend, prompt, candidate, str(exc))
                if repaired is None:
                    log(f"{backend_name} repair failed, trying next backend")
                    break
                candidate = repaired

    targeted = targeted_template(prompt)
    if targeted is not None:
        log("using prompt-specific template fallback")
        return targeted
    log("using canned fallback")
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
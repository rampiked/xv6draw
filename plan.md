# AI Drawing Assistant Implementation Plan

## Goal

Build an end-to-end AI-assisted drawing system for xv6 where:

1. A user runs `aidraw` inside xv6.
2. `aidraw` sends a natural-language prompt to the host through COM1.
3. A host-side Python agent converts the prompt into a constrained drawing command language.
4. xv6 parses those commands and renders the result using the existing VGA/display stack.
5. The rendered image appears on screen in VGA mode `0x13`.

This plan intentionally uses a constrained drawing DSL as the primary path. Live generation of arbitrary xv6 C code is treated as a stretch goal, not the core implementation path.

## Why This Design

This repository already contains a working VGA and display device path:

- `display.c` exposes the display device.
- `vga.c` and `vga.h` support VGA mode switching and palette control.
- `imshow.c` demonstrates full-frame blitting through the display device.
- `ioctl` support already exists for devices.

The main unsolved problem is safe host/guest communication. The plan below solves that first and keeps rendering logic inside xv6, where it is easier to control and debug.

## Final Architecture

### Guest side

- `aidraw.c` is the user program.
- `drawlib.c` and `drawlib.h` implement drawing into a user-space canvas buffer.
- A new syscall `ai_query()` sends a prompt to the host and receives a textual response.
- The kernel serial bridge manages UART request/response transport.
- `aidraw` parses the returned DSL, renders into a 64000-byte canvas, then blits it to `display` in chunks.

### Host side

- `agent.py` listens on the QEMU serial connection.
- It reads framed requests from xv6.
- It calls Gemini or returns canned responses during early testing.
- It validates and normalizes output into the drawing DSL.
- It sends a framed response back to xv6.

### Transport

- Use COM1.
- Prefer a TCP-backed QEMU serial endpoint during development on macOS because it is easier to inspect and debug than QEMU named pipes.
- Keep the wire protocol line-based and explicitly framed.

## Non-Goals For The First Working Version

- No live compilation of newly generated xv6 user binaries.
- No automatic exec of host-generated ELF files inside a running xv6 instance.
- No free-form arbitrary code execution from model output.
- No conversational multi-turn mode until single-shot drawing works reliably.

## Core Design Decisions

### 1. Use a drawing DSL instead of generated C

The host agent returns commands such as:

```text
CLEAR 1
RECT 0 0 320 200 1
RECT 0 0 106 200 4
LINE 0 0 319 199 15
END
```

Benefits:

- Smaller attack surface.
- Easier validation.
- No live filesystem injection problem.
- Faster iteration.
- Easier debugging inside xv6.

### 2. Keep the canvas in user space

Rendering will happen into a local `unsigned char canvas[320 * 200]` buffer.

Benefits:

- Easy BMP export later.
- No need to read back from VGA memory.
- Parsing and clipping stay in user space.
- Matches the current full-frame display model used by `imshow.c`.

### 3. Write full frames in chunks

`blit_to_display()` should write the framebuffer in chunks of at most 1000 bytes, matching the existing pattern in `imshow.c`.

### 4. Reopen display before each frame

`displaywrite()` advances `f->off`, so the display fd must be reopened before each fresh blit to guarantee offset zero.

### 5. Use framed UART traffic

The AI transport must have explicit framing so the receiver can ignore unrelated bytes.

### 6. Use a small fixed palette first

The model should target color indices `0` through `15` only. These map to a documented subset of VGA palette entries and are much easier to reason about than all `256` colors.

## Proposed DSL

### Supported commands

```text
CLEAR color
PIXEL x y color
LINE x1 y1 x2 y2 color
RECT x y w h color
FILLRECT x y w h color
CIRCLE x y radius color
FILLCIRCLE x y radius color
END
```

### Optional later commands

```text
TRI x1 y1 x2 y2 x3 y3 color
TEXT x y color string...
PALETTE idx r g b
```

### Command rules

- One command per line.
- Integers only.
- Coordinates are clipped into `0 <= x < 320`, `0 <= y < 200`.
- Colors are restricted to `0..15` initially.
- Unknown commands are rejected.
- Parsing stops only at `END`.

## Wire Protocol

Keep the serial protocol minimal and text-based.

### Request format

```text
BEGIN_REQ
DRAW
<prompt text>
END_REQ
```

### Response format

```text
BEGIN_RESP
CLEAR 1
FILLRECT 0 0 320 200 1
END
END_RESP
```

### Protocol rules

- All protocol markers are ASCII.
- The kernel only needs to detect `BEGIN_RESP` and `END_RESP`.
- Everything between them is copied into the response buffer.
- The response is considered valid only if both markers appear in order.
- Timeout or malformed framing returns an error to user space.

## Development Phases

## Phase 0: Verify Existing Graphics Path

### Goal

Confirm the current display stack is stable before adding new features.

### Tasks

- Build xv6.
- Run `imshow`.
- Verify mode switch to VGA `0x13` works.
- Verify image blit works.
- Verify return to text mode works.

### Success criteria

- `imshow` consistently renders `cover.raw`.
- System returns cleanly to text mode.

## Phase 1: Add User-Space Draw Library

### Goal

Create a reusable software renderer for a 320x200 indexed-color canvas.

### New files

- `drawlib.c`
- `drawlib.h`

### Functions

- `void canvas_clear(unsigned char *canvas, unsigned char color);`
- `void canvas_pixel(unsigned char *canvas, int x, int y, unsigned char color);`
- `void canvas_line(unsigned char *canvas, int x1, int y1, int x2, int y2, unsigned char color);`
- `void canvas_rect(unsigned char *canvas, int x, int y, int w, int h, unsigned char color);`
- `void canvas_fillrect(unsigned char *canvas, int x, int y, int w, int h, unsigned char color);`
- `void canvas_circle(unsigned char *canvas, int x, int y, int radius, unsigned char color);`
- `void canvas_fillcircle(unsigned char *canvas, int x, int y, int radius, unsigned char color);`
- `int canvas_blit(unsigned char *canvas);`

### Design notes

- All clipping happens inside the library.
- `canvas_blit()` reopens `display` each time.
- `canvas_blit()` uses chunked writes of `1000` bytes.

### Success criteria

- A test program can draw shapes without any AI involvement.

## Phase 2: Add Local Demo Program

### Goal

Prove the renderer works using a fixed image generated entirely in xv6.

### New file

- `drawdemo.c`

### Tasks

- Draw at least one flag or geometric composition.
- Use `drawlib` only.
- Switch to graphics mode.
- Render.
- Sleep briefly.
- Return to text mode.

### Success criteria

- `drawdemo` works repeatedly and leaves the console usable afterward.

## Phase 3: Add QEMU AI Transport Mode

### Goal

Create a debug-friendly host/guest serial setup for AI traffic.

### Files to update

- `Makefile`

### Tasks

- Add a new target such as `qemu-ai`.
- Keep normal `qemu` unchanged.
- Configure COM1 for a TCP-backed serial endpoint for the AI bridge.
- Preserve graphical QEMU mode so VGA output remains visible.

### Recommended QEMU flags

```text
-chardev socket,id=serial1,host=localhost,port=4444,server=on,wait=off
-serial chardev:serial1
```

### Notes

- Development path should favor TCP over named pipes on macOS.
- Named pipes can be added later if required.

### Success criteria

- Agent can connect reliably.
- UART bytes sent from xv6 are visible to the host.
- Host replies can reach xv6 over COM1.

## Phase 4: Add Kernel AI Bridge

### Goal

Add a kernel-managed request/response channel on top of UART.

### New file

- `ai.c`

### Files to update

- `defs.h`
- `uart.c`
- `console.c`
- `main.c`

### Kernel state

- request buffer
- response buffer
- response length
- active flag
- response complete flag
- spinlock
- sleep channel unique to AI state

### Required functions

- `void aiinit(void);`
- `int aisend(char *buf, int len);`
- `int aiwait(char *dst, int max);`
- `void ai_uart_rx(int c);`
- `int ai_is_active(void);`

### UART integration

- When AI mode is inactive, UART input continues to flow through `consoleintr()`.
- When AI mode is active, UART input is parsed by the AI transport.
- Only suppress ordinary `consputc -> uartputc` mirroring while a framed AI response is actively being received.
- Panic paths must still be able to emit diagnostics.

### Initialization

- `aiinit()` must be called from `main.c` during kernel startup.
- It should be initialized alongside the other core kernel subsystems, after the serial hardware is available and before user processes begin issuing AI requests.

### Success criteria

- xv6 can send a test string over UART.
- xv6 can block until a framed response arrives.
- Console input is not corrupted after the transaction ends.

## Phase 5: Add `ai_query()` System Call

### Goal

Expose the AI bridge to user space cleanly.

### Files to update

- `syscall.h`
- `syscall.c`
- `sysproc.c`
- `usys.S`
- `user.h`
- `defs.h`

### Proposed API

```c
int ai_query(char *prompt, char *response, int maxlen);
```

### Behavior

- Copy prompt from user space.
- Validate length.
- Send framed request through UART.
- Sleep on AI-specific channel.
- Wait for a response for at most `500` ticks, which is approximately `5` seconds at xv6's `100 Hz` timer rate.
- Copy response back to user buffer.
- Return number of bytes written or `-1` on error.

### Error cases

- invalid pointer
- prompt too long
- response buffer too small
- timeout after `500` ticks
- malformed host response
- concurrent AI request already active

### Success criteria

- A minimal test program can send a prompt and print the returned response string.

## Phase 6: Add `aidraw.c`

### Goal

Create the first real user-facing drawing program.

### New file

- `aidraw.c`

### Behavior

- Accept prompt from command line.
- Call `ai_query()`.
- Parse DSL line by line.
- Render into canvas using `drawlib`.
- Switch display to graphics mode.
- Blit canvas.
- Optionally wait.
- Restore text mode.

### Required parser behavior

- Reject malformed lines.
- Ignore blank lines.
- Stop only at `END`.
- Enforce coordinate and color ranges.

### Success criteria

- `aidraw "draw the US flag"` produces a correct image using canned host output.

## Phase 7: Add Host Agent Stub

### Goal

Build a deterministic host agent before integrating Gemini.

### New file

- `agent.py`

### Local configuration

- `agent.py` should load optional local settings from `.env` if present.
- Keep a `.env.example` file in the repo as the template.
- Use `.env` for `GEMINI_API_KEY`, `AIDRAW_HOST`, `AIDRAW_PORT`, and `GEMINI_MODEL`.
- Shell environment variables should continue to override `.env` values.

### Behavior

- Connect to the QEMU serial endpoint.
- Wait for `BEGIN_REQ ... END_REQ`.
- Parse prompt text.
- Return canned command scripts for known prompts.
- Return a fallback image for unknown prompts.

### Good starter prompts

- `draw the US flag`
- `draw a smiley face`
- `draw a house`
- `draw a sunset`

### Success criteria

- Full end-to-end path works with no LLM dependency.

## Phase 8: Add Gemini Integration

### Goal

Replace canned responses with model-generated DSL.

### Tasks

- Add Gemini API client to `agent.py`.
- Send a tightly constrained system prompt.
- Instruct the model to emit only DSL commands.
- Validate output before forwarding it to xv6.

### Required prompt constraints

- Output only the DSL.
- Use only commands supported by xv6.
- Use only colors `0..15`.
- Use coordinates inside the 320x200 canvas.
- End output with `END`.

### Validation rules in `agent.py`

- Strip commentary.
- Reject unsupported commands.
- Clamp out-of-range integers if needed.
- Reject missing `END`.
- Cap command count to prevent oversized responses.

### Success criteria

- Live natural-language prompts produce images through the full pipeline.

## Phase 9: Add BMP Export

### Goal

Save the rendered canvas as a BMP file from user space.

### Options

- Add BMP writing to `aidraw.c` directly.
- Or create a helper file such as `bmp.c` and `bmp.h`.

### Reason to keep this in user space

- Canvas is already available there.
- No need to read VGA memory back.
- Easier debugging and testing.

### BMP format note

- The canvas is stored top-down in memory.
- Standard BMP pixel rows are written bottom-up.
- BMP export must therefore emit rows in reverse `y` order unless a top-down BMP variant is used deliberately.

### Success criteria

- After drawing, xv6 can save a valid BMP to the filesystem.

## Stretch Goals

- `aichat.c` for repeated prompt-response sessions.
- More drawing primitives.
- Text rendering.
- Custom palette changes through DSL.
- Offline host-generated xv6 C experiments.
- Measured evaluation of prompt classes and failure cases.

## File-by-File Change Plan

### Build system

- `Makefile`
  - add `drawlib.o` dependencies through new user programs
  - add `_drawdemo` and `_aidraw`
  - add `qemu-ai` target

### Kernel files

- `defs.h`
  - add AI bridge prototypes
  - add syscall prototype if needed
- `main.c`
  - call `aiinit()` during kernel startup
- `uart.c`
  - route incoming UART bytes to AI bridge when active
- `console.c`
  - narrow UART mirroring during AI response window
- `sysproc.c`
  - implement `sys_ai_query`
- `syscall.c`
  - register syscall number and handler
- `syscall.h`
  - assign syscall number
- `usys.S`
  - add syscall stub
- `user.h`
  - add user declaration

### New guest files

- `ai.c`
- `drawlib.c`
- `drawlib.h`
- `drawdemo.c`
- `aidraw.c`

### New host file

- `agent.py`

## Testing Strategy

### Unit-style guest tests

- test `canvas_pixel` clipping
- test `canvas_line` edge cases
- test `canvas_fillrect`
- test parser acceptance and rejection cases

### End-to-end checkpoints

1. `imshow` works.
2. `drawdemo` works.
3. UART can send a request string.
4. UART can receive a framed canned response.
5. `ai_query()` returns the response to user space.
6. `aidraw` renders canned host output.
7. `aidraw` renders Gemini-generated output.

### Failure cases to test

- malformed response
- missing `END_RESP`
- oversized response
- invalid color index
- out-of-range coordinates
- host disconnect
- second AI request while first is active

## Risk Register

### Risk 1: QEMU serial setup on macOS

Mitigation:

- start with TCP-backed serial
- keep normal `qemu` target untouched
- verify transport before adding Gemini

### Risk 2: Console and AI traffic colliding

Mitigation:

- explicit framing
- AI-specific UART receive path
- narrow suppression window
- keep panic output available

### Risk 3: Model output is malformed

Mitigation:

- strict DSL
- host-side validation
- fallback canned response

### Risk 4: Display state becomes inconsistent

Mitigation:

- always reopen display for each frame
- always restore text mode before exit where possible
- keep a simple demo path for regression testing

### Risk 5: Scope creep

Mitigation:

- single-shot `aidraw` first
- no `aichat` until core path is stable
- no live xv6 C generation in first milestone

## Milestone Schedule

### Milestone 1

- `drawlib`
- `drawdemo`
- repeatable graphics demo in xv6

### Milestone 2

- `qemu-ai`
- `agent.py` canned responses
- kernel AI bridge
- `ai_query()` syscall

### Milestone 3

- `aidraw`
- full end-to-end canned prompt rendering

### Milestone 4

- Gemini integration
- prompt validation and fallback behavior

### Milestone 5

- BMP export
- cleanup
- demo preparation

## Recommended First Implementation Step

Start with Phase 1 and Phase 2 only:

1. add `drawlib.c` and `drawlib.h`
2. add `drawdemo.c`
3. update `Makefile`
4. verify graphics output without any AI code

That gives a stable rendering base before serial and host-agent complexity are introduced.
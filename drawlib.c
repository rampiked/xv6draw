#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "drawlib.h"

#define DISPLAY_PATH "display"
#define DISPLAY_CHUNK 1000

static int
abs_int(int value)
{
  if(value < 0)
    return -value;
  return value;
}

static void
draw_hline(unsigned char *canvas, int y, int x0, int x1, unsigned char color)
{
  int x;
  int left = x0;
  int right = x1;

  if(y < 0 || y >= CANVAS_HEIGHT)
    return;
  if(left > right){
    left = x1;
    right = x0;
  }
  if(right < 0 || left >= CANVAS_WIDTH)
    return;
  if(left < 0)
    left = 0;
  if(right >= CANVAS_WIDTH)
    right = CANVAS_WIDTH - 1;

  for(x = left; x <= right; x++)
    canvas[y * CANVAS_WIDTH + x] = color;
}

void
canvas_clear(unsigned char *canvas, unsigned char color)
{
  memset(canvas, color, CANVAS_SIZE);
}

void
canvas_pixel(unsigned char *canvas, int x, int y, unsigned char color)
{
  if(x < 0 || x >= CANVAS_WIDTH || y < 0 || y >= CANVAS_HEIGHT)
    return;
  canvas[y * CANVAS_WIDTH + x] = color;
}

void
canvas_line(unsigned char *canvas, int x1, int y1, int x2, int y2, unsigned char color)
{
  int dx = abs_int(x2 - x1);
  int sx = x1 < x2 ? 1 : -1;
  int dy = -abs_int(y2 - y1);
  int sy = y1 < y2 ? 1 : -1;
  int err = dx + dy;
  int err2;

  for(;;){
    canvas_pixel(canvas, x1, y1, color);
    if(x1 == x2 && y1 == y2)
      break;
    err2 = 2 * err;
    if(err2 >= dy){
      err += dy;
      x1 += sx;
    }
    if(err2 <= dx){
      err += dx;
      y1 += sy;
    }
  }
}

void
canvas_rect(unsigned char *canvas, int x, int y, int w, int h, unsigned char color)
{
  if(w <= 0 || h <= 0)
    return;
  canvas_line(canvas, x, y, x + w - 1, y, color);
  canvas_line(canvas, x, y, x, y + h - 1, color);
  canvas_line(canvas, x + w - 1, y, x + w - 1, y + h - 1, color);
  canvas_line(canvas, x, y + h - 1, x + w - 1, y + h - 1, color);
}

void
canvas_fillrect(unsigned char *canvas, int x, int y, int w, int h, unsigned char color)
{
  int row;

  if(w <= 0 || h <= 0)
    return;
  for(row = 0; row < h; row++)
    draw_hline(canvas, y + row, x, x + w - 1, color);
}

void
canvas_circle(unsigned char *canvas, int x, int y, int radius, unsigned char color)
{
  int cx = radius;
  int cy = 0;
  int err = 1 - cx;

  if(radius < 0)
    return;

  while(cx >= cy){
    canvas_pixel(canvas, x + cx, y + cy, color);
    canvas_pixel(canvas, x + cy, y + cx, color);
    canvas_pixel(canvas, x - cy, y + cx, color);
    canvas_pixel(canvas, x - cx, y + cy, color);
    canvas_pixel(canvas, x - cx, y - cy, color);
    canvas_pixel(canvas, x - cy, y - cx, color);
    canvas_pixel(canvas, x + cy, y - cx, color);
    canvas_pixel(canvas, x + cx, y - cy, color);

    cy++;
    if(err < 0)
      err += 2 * cy + 1;
    else {
      cx--;
      err += 2 * (cy - cx) + 1;
    }
  }
}

void
canvas_fillcircle(unsigned char *canvas, int x, int y, int radius, unsigned char color)
{
  int cx = radius;
  int cy = 0;
  int err = 1 - cx;

  if(radius < 0)
    return;

  while(cx >= cy){
    draw_hline(canvas, y + cy, x - cx, x + cx, color);
    draw_hline(canvas, y - cy, x - cx, x + cx, color);
    draw_hline(canvas, y + cx, x - cy, x + cy, color);
    draw_hline(canvas, y - cx, x - cy, x + cy, color);

    cy++;
    if(err < 0)
      err += 2 * cy + 1;
    else {
      cx--;
      err += 2 * (cy - cx) + 1;
    }
  }
}

int
canvas_blit(unsigned char *canvas)
{
  int fd;
  int offset;

  fd = open(DISPLAY_PATH, O_WRONLY);
  if(fd < 0)
    return -1;

  for(offset = 0; offset < CANVAS_SIZE; offset += DISPLAY_CHUNK){
    int chunk = CANVAS_SIZE - offset;
    int written;

    if(chunk > DISPLAY_CHUNK)
      chunk = DISPLAY_CHUNK;
    written = write(fd, canvas + offset, chunk);
    if(written != chunk){
      close(fd);
      return -1;
    }
  }

  close(fd);
  return 0;
}
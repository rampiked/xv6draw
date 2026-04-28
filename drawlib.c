#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "drawlib.h"

#define DISPLAY_PATH "display"
#define DISPLAY_CHUNK 1000
#define POLYGON_MAX_POINTS 16

static int
abs_int(int value)
{
  if(value < 0)
    return -value;
  return value;
}

static void
swap_int(int *lhs, int *rhs)
{
  int tmp = *lhs;
  *lhs = *rhs;
  *rhs = tmp;
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

static int
min3(int a, int b, int c)
{
  int result = a;

  if(b < result)
    result = b;
  if(c < result)
    result = c;
  return result;
}

static int
max3(int a, int b, int c)
{
  int result = a;

  if(b > result)
    result = b;
  if(c > result)
    result = c;
  return result;
}

static int64
edge_function(int ax, int ay, int bx, int by, int px, int py)
{
  return (int64)(px - ax) * (by - ay) - (int64)(py - ay) * (bx - ax);
}

static void
sort_ints(int *values, int count)
{
  int i;
  int j;

  for(i = 0; i < count; i++){
    for(j = i + 1; j < count; j++){
      if(values[j] < values[i])
        swap_int(&values[i], &values[j]);
    }
  }
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

void
canvas_triangle(unsigned char *canvas, int x1, int y1, int x2, int y2, int x3, int y3, unsigned char color)
{
  canvas_line(canvas, x1, y1, x2, y2, color);
  canvas_line(canvas, x2, y2, x3, y3, color);
  canvas_line(canvas, x3, y3, x1, y1, color);
}

void
canvas_filltriangle(unsigned char *canvas, int x1, int y1, int x2, int y2, int x3, int y3, unsigned char color)
{
  int min_x = min3(x1, x2, x3);
  int max_x = max3(x1, x2, x3);
  int min_y = min3(y1, y2, y3);
  int max_y = max3(y1, y2, y3);
  int64 area = edge_function(x1, y1, x2, y2, x3, y3);
  int x;
  int y;

  if(area == 0){
    canvas_triangle(canvas, x1, y1, x2, y2, x3, y3, color);
    return;
  }

  if(min_x < 0)
    min_x = 0;
  if(max_x >= CANVAS_WIDTH)
    max_x = CANVAS_WIDTH - 1;
  if(min_y < 0)
    min_y = 0;
  if(max_y >= CANVAS_HEIGHT)
    max_y = CANVAS_HEIGHT - 1;

  for(y = min_y; y <= max_y; y++){
    for(x = min_x; x <= max_x; x++){
      int64 w0 = edge_function(x2, y2, x3, y3, x, y);
      int64 w1 = edge_function(x3, y3, x1, y1, x, y);
      int64 w2 = edge_function(x1, y1, x2, y2, x, y);

      if((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
         (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0))
        canvas_pixel(canvas, x, y, color);
    }
  }
}

void
canvas_ellipse(unsigned char *canvas, int x, int y, int rx, int ry, unsigned char color)
{
  int px;

  if(rx < 0 || ry < 0)
    return;
  if(rx == 0 && ry == 0){
    canvas_pixel(canvas, x, y, color);
    return;
  }

  for(px = -rx; px <= rx; px++){
    int64 rx2 = (int64)rx * rx;
    int64 ry2 = (int64)ry * ry;
    int64 inside = ry2 * (rx2 - (int64)px * px);
    int py = 0;

    if(rx == 0)
      break;
    while((int64)py * py * rx2 <= inside)
      py++;
    py--;
    canvas_pixel(canvas, x + px, y + py, color);
    canvas_pixel(canvas, x + px, y - py, color);
  }

  if(rx == 0){
    for(px = -ry; px <= ry; px++)
      canvas_pixel(canvas, x, y + px, color);
  }
}

void
canvas_fillellipse(unsigned char *canvas, int x, int y, int rx, int ry, unsigned char color)
{
  int py;
  int64 rx2;
  int64 ry2;

  if(rx < 0 || ry < 0)
    return;
  if(rx == 0 && ry == 0){
    canvas_pixel(canvas, x, y, color);
    return;
  }
  if(rx == 0){
    for(py = -ry; py <= ry; py++)
      canvas_pixel(canvas, x, y + py, color);
    return;
  }
  if(ry == 0){
    draw_hline(canvas, y, x - rx, x + rx, color);
    return;
  }

  rx2 = (int64)rx * rx;
  ry2 = (int64)ry * ry;
  for(py = -ry; py <= ry; py++){
    int64 inside = rx2 * (ry2 - (int64)py * py);
    int px = 0;

    while((int64)px * px * ry2 <= inside)
      px++;
    px--;
    draw_hline(canvas, y + py, x - px, x + px, color);
  }
}

void
canvas_polygon(unsigned char *canvas, int *xs, int *ys, int count, unsigned char color)
{
  int i;

  if(count < 2)
    return;
  for(i = 0; i < count; i++){
    int next = (i + 1) % count;

    canvas_line(canvas, xs[i], ys[i], xs[next], ys[next], color);
  }
}

void
canvas_fillpolygon(unsigned char *canvas, int *xs, int *ys, int count, unsigned char color)
{
  int min_y;
  int max_y;
  int y;
  int i;

  if(count < 3 || count > POLYGON_MAX_POINTS)
    return;

  min_y = ys[0];
  max_y = ys[0];
  for(i = 1; i < count; i++){
    if(ys[i] < min_y)
      min_y = ys[i];
    if(ys[i] > max_y)
      max_y = ys[i];
  }
  if(min_y < 0)
    min_y = 0;
  if(max_y >= CANVAS_HEIGHT)
    max_y = CANVAS_HEIGHT - 1;

  for(y = min_y; y <= max_y; y++){
    int intersections[POLYGON_MAX_POINTS];
    int hits = 0;

    for(i = 0; i < count; i++){
      int next = (i + 1) % count;
      int y0 = ys[i];
      int y1 = ys[next];
      int x0 = xs[i];
      int x1 = xs[next];

      if(y0 == y1)
        continue;
      if((y >= y0 && y < y1) || (y >= y1 && y < y0)){
        intersections[hits++] = x0 + (int)(((int64)(y - y0) * (x1 - x0)) / (y1 - y0));
      }
    }

    if(hits < 2)
      continue;

    sort_ints(intersections, hits);
    for(i = 0; i + 1 < hits; i += 2)
      draw_hline(canvas, y, intersections[i], intersections[i + 1], color);
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

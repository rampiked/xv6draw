#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "drawlib.h"

static int
set_display_mode(int mode)
{
  int fd;
  int rc;

  fd = open("display", O_WRONLY);
  if(fd < 0)
    return -1;
  rc = ioctl(fd, 1, mode);
  close(fd);
  return rc;
}

int
main(int argc, char **argv)
{
  unsigned char *canvas;
  int hill_xs[5] = {18, 42, 72, 96, 58};
  int hill_ys[5] = {150, 112, 120, 152, 170};

  canvas = malloc(CANVAS_SIZE);
  if(canvas == 0){
    printf(2, "drawdemo: failed to allocate canvas\n");
    exit();
  }

  canvas_clear(canvas, 9);

  canvas_fillrect(canvas, 0, 140, CANVAS_WIDTH, 60, 2);
  canvas_fillcircle(canvas, 250, 45, 24, 14);
  canvas_circle(canvas, 250, 45, 24, 15);

  canvas_fillrect(canvas, 90, 85, 95, 70, 6);
  canvas_rect(canvas, 90, 85, 95, 70, 15);
  canvas_fillrect(canvas, 123, 118, 26, 37, 4);
  canvas_rect(canvas, 123, 118, 26, 37, 15);
  canvas_fillrect(canvas, 102, 96, 18, 18, 15);
  canvas_fillrect(canvas, 154, 96, 18, 18, 15);
  canvas_line(canvas, 90, 85, 138, 52, 4);
  canvas_line(canvas, 138, 52, 185, 85, 4);
  canvas_line(canvas, 91, 84, 138, 53, 4);
  canvas_line(canvas, 137, 53, 184, 84, 4);

  canvas_fillcircle(canvas, 47, 52, 18, 10);
  canvas_fillcircle(canvas, 68, 46, 15, 15);
  canvas_fillcircle(canvas, 86, 54, 18, 7);
  canvas_fillcircle(canvas, 58, 63, 17, 15);
  canvas_fillcircle(canvas, 77, 66, 15, 7);

  canvas_filltriangle(canvas, 18, 140, 54, 86, 98, 140, 8);
  canvas_triangle(canvas, 18, 140, 54, 86, 98, 140, 15);
  canvas_fillellipse(canvas, 264, 112, 34, 18, 1);
  canvas_ellipse(canvas, 264, 112, 34, 18, 15);
  canvas_fillpolygon(canvas, hill_xs, hill_ys, 5, 10);
  canvas_polygon(canvas, hill_xs, hill_ys, 5, 15);

  canvas_line(canvas, 240, 18, 265, 48, 12);
  canvas_line(canvas, 228, 30, 272, 52, 12);
  canvas_line(canvas, 226, 45, 274, 45, 12);

  if(set_display_mode(0x13) < 0){
    printf(2, "drawdemo: failed to switch to VGA mode\n");
    exit();
  }

  if(canvas_blit(canvas) < 0){
    printf(2, "drawdemo: failed to blit canvas\n");
    set_display_mode(0x3);
    exit();
  }

  sleep(200);

  if(set_display_mode(0x3) < 0){
    printf(2, "drawdemo: failed to restore text mode\n");
    exit();
  }

  free(canvas);
  exit();
  return 0;
}

#pragma once

#define CANVAS_WIDTH 320
#define CANVAS_HEIGHT 200
#define CANVAS_SIZE (CANVAS_WIDTH * CANVAS_HEIGHT)

void canvas_clear(unsigned char *canvas, unsigned char color);
void canvas_pixel(unsigned char *canvas, int x, int y, unsigned char color);
void canvas_line(unsigned char *canvas, int x1, int y1, int x2, int y2, unsigned char color);
void canvas_rect(unsigned char *canvas, int x, int y, int w, int h, unsigned char color);
void canvas_fillrect(unsigned char *canvas, int x, int y, int w, int h, unsigned char color);
void canvas_circle(unsigned char *canvas, int x, int y, int radius, unsigned char color);
void canvas_fillcircle(unsigned char *canvas, int x, int y, int radius, unsigned char color);
int canvas_blit(unsigned char *canvas);
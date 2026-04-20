#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "drawlib.h"

#define AI_RESPONSE_MAX 8192
#define AI_PROMPT_MAX 512

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

static void
skip_spaces(char **cursor)
{
  while(**cursor == ' ' || **cursor == '\t')
    (*cursor)++;
}

static int
line_done(char *cursor)
{
  skip_spaces(&cursor);
  return *cursor == 0;
}

static int
match_command(char **cursor, char *name)
{
  int len = strlen(name);

  if(strncmp(*cursor, name, len) != 0)
    return 0;
  if((*cursor)[len] != 0 && (*cursor)[len] != ' ' && (*cursor)[len] != '\t')
    return 0;
  *cursor += len;
  return 1;
}

static int
parse_int(char **cursor, int *value)
{
  int sign = 1;
  int result = 0;
  int saw_digit = 0;

  skip_spaces(cursor);
  if(**cursor == '-'){
    sign = -1;
    (*cursor)++;
  }
  while(**cursor >= '0' && **cursor <= '9'){
    saw_digit = 1;
    result = result * 10 + (**cursor - '0');
    (*cursor)++;
  }
  if(!saw_digit)
    return -1;

  *value = sign * result;
  return 0;
}

static int
parse_line(unsigned char *canvas, char *line, int *saw_end)
{
  char *cursor = line;
  int a;
  int b;
  int c;
  int d;
  int e;

  *saw_end = 0;
  skip_spaces(&cursor);
  if(*cursor == 0)
    return 0;

  if(match_command(&cursor, "END")){
    if(!line_done(cursor))
      return -1;
    *saw_end = 1;
    return 0;
  }
  if(match_command(&cursor, "CLEAR")){
    if(parse_int(&cursor, &a) < 0 || !line_done(cursor))
      return -1;
    canvas_clear(canvas, a);
    return 0;
  }
  if(match_command(&cursor, "PIXEL")){
    if(parse_int(&cursor, &a) < 0 || parse_int(&cursor, &b) < 0 ||
       parse_int(&cursor, &c) < 0 || !line_done(cursor))
      return -1;
    canvas_pixel(canvas, a, b, c);
    return 0;
  }
  if(match_command(&cursor, "LINE")){
    if(parse_int(&cursor, &a) < 0 || parse_int(&cursor, &b) < 0 ||
       parse_int(&cursor, &c) < 0 || parse_int(&cursor, &d) < 0 ||
       parse_int(&cursor, &e) < 0 || !line_done(cursor))
      return -1;
    canvas_line(canvas, a, b, c, d, e);
    return 0;
  }
  if(match_command(&cursor, "RECT")){
    if(parse_int(&cursor, &a) < 0 || parse_int(&cursor, &b) < 0 ||
       parse_int(&cursor, &c) < 0 || parse_int(&cursor, &d) < 0 ||
       parse_int(&cursor, &e) < 0 || !line_done(cursor))
      return -1;
    canvas_rect(canvas, a, b, c, d, e);
    return 0;
  }
  if(match_command(&cursor, "FILLRECT")){
    if(parse_int(&cursor, &a) < 0 || parse_int(&cursor, &b) < 0 ||
       parse_int(&cursor, &c) < 0 || parse_int(&cursor, &d) < 0 ||
       parse_int(&cursor, &e) < 0 || !line_done(cursor))
      return -1;
    canvas_fillrect(canvas, a, b, c, d, e);
    return 0;
  }
  if(match_command(&cursor, "CIRCLE")){
    if(parse_int(&cursor, &a) < 0 || parse_int(&cursor, &b) < 0 ||
       parse_int(&cursor, &c) < 0 || parse_int(&cursor, &d) < 0 ||
       !line_done(cursor))
      return -1;
    canvas_circle(canvas, a, b, c, d);
    return 0;
  }
  if(match_command(&cursor, "FILLCIRCLE")){
    if(parse_int(&cursor, &a) < 0 || parse_int(&cursor, &b) < 0 ||
       parse_int(&cursor, &c) < 0 || parse_int(&cursor, &d) < 0 ||
       !line_done(cursor))
      return -1;
    canvas_fillcircle(canvas, a, b, c, d);
    return 0;
  }

  return -1;
}

static int
join_prompt(int argc, char **argv, char *prompt)
{
  int i;
  int len = 0;

  prompt[0] = 0;
  for(i = 1; i < argc; i++){
    int j;
    int word_len = strlen(argv[i]);

    if(i > 1){
      if(len + 1 >= AI_PROMPT_MAX)
        return -1;
      prompt[len++] = ' ';
    }
    if(len + word_len >= AI_PROMPT_MAX)
      return -1;
    for(j = 0; j < word_len; j++)
      prompt[len++] = argv[i][j];
  }
  prompt[len] = 0;
  return 0;
}

int
main(int argc, char **argv)
{
  unsigned char *canvas;
  char *response;
  char prompt[AI_PROMPT_MAX];
  char *line;
  int saw_end = 0;

  if(argc < 2){
    printf(2, "usage: aidraw prompt...\n");
    exit();
  }
  if(join_prompt(argc, argv, prompt) < 0){
    printf(2, "aidraw: prompt too long\n");
    exit();
  }

  canvas = malloc(CANVAS_SIZE);
  response = malloc(AI_RESPONSE_MAX);
  if(canvas == 0 || response == 0){
    printf(2, "aidraw: allocation failed\n");
    exit();
  }

  if(ai_query(prompt, response, AI_RESPONSE_MAX) < 0){
    printf(2, "aidraw: ai query failed\n");
    free(canvas);
    free(response);
    exit();
  }

  canvas_clear(canvas, 0);
  line = response;
  while(*line){
    char *next = strchr(line, '\n');

    if(next)
      *next = 0;
    if(parse_line(canvas, line, &saw_end) < 0){
      printf(2, "aidraw: invalid drawing command '%s'\n", line);
      free(canvas);
      free(response);
      exit();
    }
    if(saw_end)
      break;
    if(next == 0)
      break;
    line = next + 1;
  }

  if(!saw_end){
    printf(2, "aidraw: response missing END\n");
    free(canvas);
    free(response);
    exit();
  }

  if(set_display_mode(0x13) < 0){
    printf(2, "aidraw: failed to switch to VGA mode\n");
    free(canvas);
    free(response);
    exit();
  }
  if(canvas_blit(canvas) < 0){
    printf(2, "aidraw: failed to blit canvas\n");
    set_display_mode(0x3);
    free(canvas);
    free(response);
    exit();
  }

  sleep(200);

  if(set_display_mode(0x3) < 0){
    printf(2, "aidraw: failed to restore text mode\n");
    free(canvas);
    free(response);
    exit();
  }

  free(canvas);
  free(response);
  exit();
  return 0;
}
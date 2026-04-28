#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"

#define AI_PROMPT_MAX 512
#define AI_RESPONSE_MAX 8192
#define AI_LINE_MAX 512
#define AI_TIMEOUT_TICKS 500000

static struct {
  struct spinlock lock;
  int active;
  int draining;
  int suppress_console;
  int in_response;
  int complete;
  int failed;
  int response_len;
  int line_len;
  char response[AI_RESPONSE_MAX];
  char line[AI_LINE_MAX];
} ai;

static int
streq(char *lhs, char *rhs)
{
  int lhs_len = strlen(lhs);
  int rhs_len = strlen(rhs);

  if(lhs_len != rhs_len)
    return 0;
  return strncmp(lhs, rhs, lhs_len) == 0;
}

static void
ai_finish_locked(int failed)
{
  ai.failed = failed;
  ai.complete = 1;
  ai.active = 0;
  ai.draining = 0;
  ai.suppress_console = 0;
  wakeup(&ai);
}

static void
ai_append_line_locked(void)
{
  int i;

  if(ai.response_len + ai.line_len + 1 >= AI_RESPONSE_MAX){
    ai_finish_locked(1);
    return;
  }

  for(i = 0; i < ai.line_len; i++)
    ai.response[ai.response_len++] = ai.line[i];
  ai.response[ai.response_len++] = '\n';
  ai.response[ai.response_len] = 0;
}

static void
ai_send_str(char *s)
{
  while(*s)
    uartputc(*(s++));
}

static void
ai_send_prompt(char *prompt, int promptlen)
{
  int i;

  for(i = 0; i < promptlen; i++){
    char c = prompt[i];
    if(c == '\n' || c == '\r')
      c = ' ';
    uartputc(c);
  }
}

void
aiinit(void)
{
  initlock(&ai.lock, "ai");
  acquire(&ai.lock);
  ai.active = 0;
  ai.draining = 0;
  ai.suppress_console = 0;
  ai.in_response = 0;
  ai.complete = 0;
  ai.failed = 0;
  ai.response_len = 0;
  ai.line_len = 0;
  ai.response[0] = 0;
  ai.line[0] = 0;
  release(&ai.lock);
}

int
ai_should_suppress_console(void)
{
  int suppress;

  acquire(&ai.lock);
  suppress = ai.suppress_console;
  release(&ai.lock);
  return suppress;
}

int
ai_is_active(void)
{
  int active;

  acquire(&ai.lock);
  active = ai.active || ai.draining;
  release(&ai.lock);
  return active;
}

void
ai_uart_rx(int c)
{
  acquire(&ai.lock);
  if(!ai.active && !ai.draining){
    release(&ai.lock);
    return;
  }

  if(c == '\r'){
    release(&ai.lock);
    return;
  }

  if(c != '\n'){
    if(ai.line_len >= AI_LINE_MAX - 1){
      ai_finish_locked(1);
      release(&ai.lock);
      return;
    }
    ai.line[ai.line_len++] = c;
    release(&ai.lock);
    return;
  }

  ai.line[ai.line_len] = 0;
  if(!ai.in_response){
    if(streq(ai.line, "BEGIN_RESP")){
      ai.in_response = 1;
      ai.response_len = 0;
      ai.response[0] = 0;
    }
  } else if(streq(ai.line, "END_RESP"))
    ai_finish_locked(0);
  else
    ai_append_line_locked();

  ai.line_len = 0;
  ai.line[0] = 0;
  release(&ai.lock);
}

void
ai_tick(void)
{
  int active;

  acquire(&ai.lock);
  active = ai.active;
  release(&ai.lock);

  if(active)
    wakeup(&ai);
}

int
aiquery(char *prompt, int promptlen, char *response, int maxlen)
{
  uint started;
  int response_len;

  if(promptlen < 0 || promptlen > AI_PROMPT_MAX || maxlen <= 0)
    return -1;

  acquire(&ai.lock);
  if(ai.active || ai.draining){
    release(&ai.lock);
    return -1;
  }

  ai.active = 1;
  ai.draining = 0;
  ai.suppress_console = 1;
  ai.in_response = 0;
  ai.complete = 0;
  ai.failed = 0;
  ai.response_len = 0;
  ai.line_len = 0;
  ai.response[0] = 0;
  ai.line[0] = 0;
  release(&ai.lock);

  ai_send_str("BEGIN_REQ\nDRAW\n");
  ai_send_prompt(prompt, promptlen);
  ai_send_str("\nEND_REQ\n");

  acquire(&tickslock);
  started = ticks;
  release(&tickslock);

  acquire(&ai.lock);
  while(!ai.complete){
    uint now;

    if(proc->killed){
      ai.active = 0;
      ai.draining = 1;
      ai.suppress_console = 0;
      release(&ai.lock);
      return -1;
    }

    acquire(&tickslock);
    now = ticks;
    release(&tickslock);
    if(now - started >= AI_TIMEOUT_TICKS){
      ai.active = 0;
      ai.draining = 1;
      ai.suppress_console = 0;
      release(&ai.lock);
      return -1;
    }

    sleep(&ai, &ai.lock);
  }

  if(ai.failed || ai.response_len + 1 > maxlen){
    release(&ai.lock);
    return -1;
  }

  response_len = ai.response_len;
  memmove(response, ai.response, response_len);
  response[response_len] = 0;
  release(&ai.lock);
  return response_len;
}

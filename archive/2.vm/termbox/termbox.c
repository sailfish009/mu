#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
/* hack: we can't define _XOPEN_SOURCE because that causes OpenBSD to not
 * include SIGWINCH. But then this prototype is not included on Linux,
 * triggering a warning. */
extern int wcwidth (wchar_t);

#include "termbox.h"

#include "bytebuffer.inl"
#include "output.inl"
#include "input.inl"

#define LAST_COORD_INIT -1

static struct termios orig_tios;

static struct bytebuffer output_buffer;
static struct bytebuffer input_buffer;

static int termw = -1;
static int termh = -1;

static int inout;
static int winch_fds[2];

static int cursor_x = 0;
static int cursor_y = 0;

static uint16_t background = TB_BLACK;
static uint16_t foreground = TB_WHITE;

static void update_size(void);
static void update_term_size(void);
static void send_attr(uint16_t fg, uint16_t bg);
static void send_clear(void);
static void sigwinch_handler(int xxx);
static int wait_fill_event(struct tb_event *event, struct timeval *timeout);

/* may happen in a different thread */
static volatile int buffer_size_change_request;

/* -------------------------------------------------------- */

int tb_init(void)
{
  inout = open("/dev/tty", O_RDWR);
  if (inout == -1) {
    return TB_EFAILED_TO_OPEN_TTY;
  }

  if (init_term() < 0) {
    close(inout);
    return TB_EUNSUPPORTED_TERMINAL;
  }

  if (pipe(winch_fds) < 0) {
    close(inout);
    return TB_EPIPE_TRAP_ERROR;
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigwinch_handler;
  sa.sa_flags = 0;
  sigaction(SIGWINCH, &sa, 0);

  tcgetattr(inout, &orig_tios);

  struct termios tios;
  memcpy(&tios, &orig_tios, sizeof(tios));

  tios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                           | INLCR | IGNCR | ICRNL | IXON);
  tios.c_oflag &= ~OPOST;
  tios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  tios.c_cflag &= ~(CSIZE | PARENB);
  tios.c_cflag |= CS8;
  tios.c_cc[VMIN] = 0;
  tios.c_cc[VTIME] = 0;
  tcsetattr(inout, TCSAFLUSH, &tios);

  bytebuffer_init(&input_buffer, 128);
  bytebuffer_init(&output_buffer, 32 * 1024);

  bytebuffer_puts(&output_buffer, funcs[T_ENTER_KEYPAD]);
  bytebuffer_puts(&output_buffer, funcs[T_ENTER_MOUSE]);
  bytebuffer_puts(&output_buffer, funcs[T_ENTER_BRACKETED_PASTE]);
  bytebuffer_flush(&output_buffer, inout);

  update_term_size();
  return 0;
}

void tb_shutdown(void)
{
  if (termw == -1) return;

  bytebuffer_puts(&output_buffer, funcs[T_SGR0]);
  bytebuffer_puts(&output_buffer, funcs[T_EXIT_KEYPAD]);
  bytebuffer_puts(&output_buffer, funcs[T_EXIT_MOUSE]);
  bytebuffer_puts(&output_buffer, funcs[T_EXIT_BRACKETED_PASTE]);
  bytebuffer_flush(&output_buffer, inout);
  tcsetattr(inout, TCSAFLUSH, &orig_tios);

  shutdown_term();
  close(inout);
  close(winch_fds[0]);
  close(winch_fds[1]);

  bytebuffer_free(&output_buffer);
  bytebuffer_free(&input_buffer);
  termw = termh = -1;
}

int tb_is_active(void)
{
  return termw != -1;
}

void tb_print(uint32_t ch, uint16_t fg, uint16_t bg)
{
  assert(termw != -1);
  send_attr(fg, bg);
  if (ch == 0) {
    // replace 0 with whitespace
    bytebuffer_puts(&output_buffer, " ");
  }
  else {
    char buf[7];
    int bw = tb_utf8_unicode_to_char(buf, ch);
    buf[bw] = '\0';
    bytebuffer_puts(&output_buffer, buf);
  }
  bytebuffer_flush(&output_buffer, inout);
}

int tb_poll_event(struct tb_event *event)
{
  assert(termw != -1);
  return wait_fill_event(event, 0);
}

int tb_peek_event(struct tb_event *event, int timeout)
{
  struct timeval tv;
  tv.tv_sec = timeout / 1000;
  tv.tv_usec = (timeout - (tv.tv_sec * 1000)) * 1000;
  assert(termw != -1);
  return wait_fill_event(event, &tv);
}

int tb_width(void)
{
  assert(termw != -1);
  return termw;
}

int tb_height(void)
{
  assert(termw != -1);
  return termh;
}

void tb_clear(void)
{
  assert(termw != -1);
  if (buffer_size_change_request) {
    update_size();
    buffer_size_change_request = 0;
  }
  send_clear();
}

void tb_set_clear_attributes(uint16_t fg, uint16_t bg)
{
  assert(termw != -1);
  foreground = fg;
  background = bg;
}

/* -------------------------------------------------------- */

static int convertnum(uint32_t num, char* buf) {
  int i, l = 0;
  int ch;
  do {
    buf[l++] = '0' + (num % 10);
    num /= 10;
  } while (num);
  for(i = 0; i < l / 2; i++) {
    ch = buf[i];
    buf[i] = buf[l - 1 - i];
    buf[l - 1 - i] = ch;
  }
  return l;
}

#define WRITE_LITERAL(X) bytebuffer_append(&output_buffer, (X), sizeof(X)-1)
#define WRITE_INT(X) bytebuffer_append(&output_buffer, buf, convertnum((X), buf))

void tb_set_cursor(int x, int y) {
  char buf[32];
  WRITE_LITERAL("\033[");
  WRITE_INT(y+1);
  WRITE_LITERAL(";");
  WRITE_INT(x+1);
  WRITE_LITERAL("H");
  bytebuffer_flush(&output_buffer, inout);
}

static void get_term_size(int *w, int *h)
{
  struct winsize sz;
  memset(&sz, 0, sizeof(sz));

  ioctl(inout, TIOCGWINSZ, &sz);

  if (w) *w = sz.ws_col;
  if (h) *h = sz.ws_row;
}

static void update_term_size(void)
{
  struct winsize sz;
  memset(&sz, 0, sizeof(sz));

  ioctl(inout, TIOCGWINSZ, &sz);

  termw = sz.ws_col;
  termh = sz.ws_row;
}

static void send_attr(uint16_t fg, uint16_t bg)
{
#define LAST_ATTR_INIT 0xFFFF
  static uint16_t lastfg = LAST_ATTR_INIT, lastbg = LAST_ATTR_INIT;
  if (fg != lastfg || bg != lastbg) {
    bytebuffer_puts(&output_buffer, funcs[T_SGR0]);

    uint16_t fgcol = fg & 0xFF;
    uint16_t bgcol = bg & 0xFF;

    if (fg & TB_BOLD)
      bytebuffer_puts(&output_buffer, funcs[T_BOLD]);
    if (bg & TB_BOLD)
      bytebuffer_puts(&output_buffer, funcs[T_BLINK]);
    if (fg & TB_UNDERLINE)
      bytebuffer_puts(&output_buffer, funcs[T_UNDERLINE]);
    if ((fg & TB_REVERSE) || (bg & TB_REVERSE))
      bytebuffer_puts(&output_buffer, funcs[T_REVERSE]);
    char buf[32];
    WRITE_LITERAL("\033[38;5;");
    WRITE_INT(fgcol);
    WRITE_LITERAL("m");
    WRITE_LITERAL("\033[48;5;");
    WRITE_INT(bgcol);
    WRITE_LITERAL("m");
    bytebuffer_flush(&output_buffer, inout);
    lastfg = fg;
    lastbg = bg;
  }
}

const char* to_unicode(uint32_t c)
{
  static char buf[7];
  int bw = tb_utf8_unicode_to_char(buf, c);
  buf[bw] = '\0';
  return buf;
}

static void send_clear(void)
{
  send_attr(foreground, background);
  bytebuffer_puts(&output_buffer, funcs[T_CLEAR_SCREEN]);
  tb_set_cursor(cursor_x, cursor_y);
  bytebuffer_flush(&output_buffer, inout);
}

static void sigwinch_handler(int xxx)
{
  (void) xxx;
  const int zzz = 1;
  int yyy = write(winch_fds[1], &zzz, sizeof(int));
  (void) yyy;
}

static void update_size(void)
{
  update_term_size();
  send_clear();
}

static int read_up_to(int n) {
  assert(n > 0);
  const int prevlen = input_buffer.len;
  bytebuffer_resize(&input_buffer, prevlen + n);

  int read_n = 0;
  while (read_n <= n) {
    ssize_t r = 0;
    if (read_n < n) {
      r = read(inout, input_buffer.buf + prevlen + read_n, n - read_n);
    }
#ifdef __CYGWIN__
    // While linux man for tty says when VMIN == 0 && VTIME == 0, read
    // should return 0 when there is nothing to read, cygwin's read returns
    // -1. Not sure why and if it's correct to ignore it, but let's pretend
    // it's zero.
    if (r < 0) r = 0;
#endif
    if (r < 0) {
      // EAGAIN / EWOULDBLOCK shouldn't occur here
      assert(errno != EAGAIN && errno != EWOULDBLOCK);
      return -1;
    } else if (r > 0) {
      read_n += r;
    } else {
      bytebuffer_resize(&input_buffer, prevlen + read_n);
      return read_n;
    }
  }
  assert(!"unreachable");
  return 0;
}

int tb_event_ready(void)
{
  return input_buffer.len > 0;
}

static int wait_fill_event(struct tb_event *event, struct timeval *timeout)
{
  // ;-)
#define ENOUGH_DATA_FOR_PARSING 64
  fd_set events;
  memset(event, 0, sizeof(struct tb_event));

  // try to extract event from input buffer, return on success
  event->type = TB_EVENT_KEY;
  if (extract_event(event, &input_buffer))
    return event->type;

  // it looks like input buffer is incomplete, let's try the short path,
  // but first make sure there is enough space
  int n = read_up_to(ENOUGH_DATA_FOR_PARSING);
  if (n < 0)
    return -1;
  if (n > 0 && extract_event(event, &input_buffer))
    return event->type;

  // n == 0, or not enough data, let's go to select
  while (1) {
    FD_ZERO(&events);
    FD_SET(inout, &events);
    FD_SET(winch_fds[0], &events);
    int maxfd = (winch_fds[0] > inout) ? winch_fds[0] : inout;
    int result = select(maxfd+1, &events, 0, 0, timeout);
    if (!result)
      return 0;

    if (FD_ISSET(inout, &events)) {
      event->type = TB_EVENT_KEY;
      n = read_up_to(ENOUGH_DATA_FOR_PARSING);
      if (n < 0)
        return -1;

      if (n == 0)
        continue;

      if (extract_event(event, &input_buffer))
        return event->type;
    }
    if (FD_ISSET(winch_fds[0], &events)) {
      event->type = TB_EVENT_RESIZE;
      int zzz = 0;
      int yyy = read(winch_fds[0], &zzz, sizeof(int));
      (void) yyy;
      buffer_size_change_request = 1;
      get_term_size(&event->w, &event->h);
      return TB_EVENT_RESIZE;
    }
  }
}

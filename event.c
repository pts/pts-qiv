/*
  Module       : event.c
  Purpose      : Handle GDK events
  More         : see qiv README
  Policy       : GNU GPL
  Homepage     : http://qiv.spiegl.de/
  Original     : http://www.klografx.net/qiv/
*/

#include <stdio.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkkeys.h>  /* gdk_keyval_to_unicode(...) */
#include <glib/gunicode.h>  /* g_unichar_to_utf8(...) */
#include "qiv.h"

#define STEP 3 //When using KP arrow, number of step for seeing all the image.

typedef enum QivMode {
  NORMAL = 0,     /* Waiting for single-keystroke commands. */
  JUMP_EDIT = 2,  /* Editing the jump target string in jcmd. Edit buffer not displayed. */
  EXT_EDIT = 3,   /* Editing the single-line external qiv-command argument in jcmd. Edit state displayed in multiline_window, starting with the argument. */
  CONFIRM = 4,    /* Waiting for any keystoke (`Push any key...') while displaying message in multiline_window. */
} QivMode;
static QivMode qiv_mode;  /* NORMAL by default. */
static char   jcmd[1024];  /* Single-line text edit buffer used by both JUMP_EDIT and EXT_EDIT. Always '\0'-terminated. */
static int    jsize;  /* Number of bytes in jcmd, excluding the trailing '\0', i.e. jsize == strlen(jcmd). */
static int    jidx;  /* Cursor position (byte offset from beginning) in jcmd. 0 <= jidx <= jsize. */
static const char *mess_buf[2] = { jcmd, NULL};
static const char **mess = mess_buf;
static int    cursor_timeout;
typedef struct _qiv_multiline_window_state {
  gint x, y, w, h;
  gboolean is_displayed;
  gboolean is_clean;
} qiv_multiline_window_state;
static qiv_multiline_window_state mws;  /* Default: .is_displayed = FALSE. */
static int last_multiline_curpos;
static const char **last_multiline_strs;
static const char *last_multiline_continue_msg;

static void qiv_enable_mouse_events(qiv_image *q)
{
  gdk_window_set_events(q->win, gdk_window_get_events(q->win) &
                        ~GDK_POINTER_MOTION_HINT_MASK);
}

static void qiv_disable_mouse_events(qiv_image *q)
{
  gdk_window_set_events(q->win, gdk_window_get_events(q->win) |
                        GDK_POINTER_MOTION_HINT_MASK);
}

static gboolean qiv_cursor_timeout(gpointer data)
{
  qiv_image *q = data;

  cursor_timeout = 0;
  hide_cursor(q);
  qiv_enable_mouse_events(q);
  return FALSE;
}

static void qiv_set_cursor_timeout(qiv_image *q)
{
  if (!cursor_timeout)
    cursor_timeout = g_timeout_add(1000, qiv_cursor_timeout, q);
  qiv_disable_mouse_events(q);
}

static void qiv_cancel_cursor_timeout(qiv_image *q)
{
  hide_cursor(q);
  if (cursor_timeout) {
    g_source_remove(cursor_timeout);
    cursor_timeout = 0;
  }
}

static void qiv_drag_image(qiv_image *q, int move_to_x, int move_to_y)
{
  q->win_x = move_to_x;
  if (q->win_w > screen_x) {
    if (q->win_x > 0)
      q->win_x = 0;
    else if (q->win_x + q->win_w < screen_x)
      q->win_x = screen_x - q->win_w;
  } else {
    if (q->win_x < 0)
      q->win_x = 0;
    else if (q->win_x + q->win_w > screen_x)
      q->win_x = screen_x - q->win_w;
  }

  q->win_y = move_to_y;
  if (q->win_h > screen_y) {
    if (q->win_y > 0)
      q->win_y = 0;
    else if (q->win_y + q->win_h < screen_y)
      q->win_y = screen_y - q->win_h;
  } else {
    if (q->win_y < 0)
      q->win_y = 0;
    else if (q->win_y + q->win_h > screen_y)
      q->win_y = screen_y - q->win_h;
  }

  update_image(q, MOVED);
  q->infotext = ("(-)");
}

static void qiv_hide_multiline_window(qiv_image *q) {
  if (mws.is_displayed) {
    update_image_or_background_noflush(q, mws.x, mws.y, mws.w, mws.h, TRUE);  /* STATUSBAR */
    if (mws.is_clean) {
      gdk_flush();
    } else {  /* Shouldn't happen. */
      update_image(q, REDRAW);
    }
  }
  mws.is_displayed = mws.is_clean = FALSE;
}

/* infotextdisplay is globally owned. */
static void qiv_display_multiline_window(qiv_image *q, const char *infotextdisplay,
                                         const char *strs[], int curpos, const char *continue_msg) {
  int temp, text_w = 0, text_h, curpos_w, i, maxlines;
  int width, height, text_left;
  qiv_multiline_window_state mws2;
  int ascent, descent;
  gboolean has_infotext_changed;

  ascent  = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics));
  descent = PANGO_PIXELS(pango_font_metrics_get_descent(metrics));

  if (fullscreen) {
    width = screen_x;
    height = screen_y;
  } else {
    width = q->win_w;
    height = q->win_h;
  }

  /* Calculate maximum number of lines to display */
  if (ascent + descent > 0)
    maxlines = height / (ascent + descent) - 3;
  else
    maxlines = 60;

  pango_layout_set_text(layout, continue_msg, -1);
  pango_layout_get_pixel_size (layout, &text_w, NULL);
  for (i = 0; strs[i] && i < maxlines; i++) {
    pango_layout_set_text(layout, strs[i], -1);
    pango_layout_get_pixel_size (layout, &temp, NULL);
    if (text_w < temp) text_w = temp;
  }
  if (curpos >= 0 && strs[0] && curpos + 0U <= strlen(strs[0]) + 0U) {
    pango_layout_set_text(layout, strs[0], curpos);
    pango_layout_get_pixel_size(layout, &curpos_w, NULL);
  } else {
    curpos_w = -1;
  }

  text_h = (i + 2) * ( ascent + descent );
  text_left = width/2 - text_w/2 - 4;
  if (text_left < 2)  text_left = 2;            /* if window/screen is smaller than text */
  mws2.x = text_left;
  mws2.y = height/2 - text_h/2 - 4;
  mws2.w = text_w + 8;
  mws2.h = text_h + 8;
  mws2.is_displayed = mws2.is_clean = TRUE;

  if (infotextdisplay) {
    has_infotext_changed = strcmp(q->infotext ? q->infotext : "(null)", infotextdisplay) != 0;
    if (has_infotext_changed) {
      q->infotext = infotextdisplay;
    }
  } else {
    has_infotext_changed = FALSE;
  }
  last_multiline_curpos = curpos;
  last_multiline_strs = strs;
  last_multiline_continue_msg = continue_msg;
#if DEBUG
  fprintf(stderr, "display update ix=%d iy=%d iw=%d ih=%d\n", q->win_x, q->win_y, q->win_w, q->win_h);
#endif
  if (!mws.is_displayed || (mws.is_clean && mws.x >= mws2.x && mws.y >= mws2.y && mws.x + mws.w <= mws2.x + mws2.w && mws.y + mws.h <= mws2.y + mws2.h)) {
    /* update_image_noflush not needed, we skip it to prevent flickering. */
    if (has_infotext_changed) update_image_noflush(q, STATUSBAR);
  } else if (mws.is_clean && mws.x <= mws2.x && mws.y == mws2.y && mws.x + mws.w >= mws2.x + mws2.w && mws.y + mws.h == mws2.y + mws2.h) {
    /* Condition above: multiline_window became narrower, typically because of <Backspace>. */
    update_image_or_background_noflush(q, mws.x, mws.y, mws2.x - mws.x, mws.h, FALSE);
    update_image_or_background_noflush(q, mws2.x + mws2.w, mws.y, mws.x + mws.w - mws2.x - mws2.w, mws.h, has_infotext_changed);  /* STATUSBAR */
  } else if (mws.x >= mws2.x && mws.y >= mws2.y && mws.x + mws.w <= mws2.x + mws2.w && mws.y + mws.h <= mws2.y + mws2.h) {
    /* The new multiline_window covers the old one. Do the slow REDRAW if
     * anything other than EXT_EDIT typing has happened (!mws.is_clean)
     * since the last draw, otherwise do the fast STATUSBAR (or even less).
     */
    if (mws.is_clean) {  /* TODO(pts): Get rid of mws.is_clean, make it always clean. */
      update_image_or_background_noflush(q, mws.x, mws.y, mws.w, mws.h, has_infotext_changed);  /* STATUSBAR */
    } else {
      update_image_noflush(q, REDRAW);
    }
  } else {  /* The new multiline_window doesn't fully cover the old one. */
    q->win_ow = q->win_oh = -1;  /* Force full (and flickering) q->win redraw below. */
    update_image_noflush(q, REDRAW);
  }
  gdk_draw_rectangle(q->win, q->bg_gc, 0, mws2.x, mws2.y, mws2.w - 1, mws2.h - 1);
  gdk_draw_rectangle(q->win, q->status_gc, 1,
                     text_left + 1,
                     height/2 - text_h/2 - 3,
                     text_w + 6, text_h + 6);
  for (i = 0; strs[i] && i < maxlines; i++) {
       pango_layout_set_text(layout, strs[i], -1);
       gdk_draw_layout (q->win, q->text_gc, text_left + 4, height/2 - text_h/2  +
                  i * (ascent + descent), layout);
  }
  if (curpos_w >= 0) {  /* Cursor in strs[0]. */
    gdk_draw_rectangle(q->win, q->bg_gc, 1, text_left + 4 + curpos_w, height/2 - text_h/2 - 1, 1, ascent + descent + 2);
  }

  /* Display continue_msg in the last line. */
  pango_layout_set_text(layout, continue_msg, -1);
  pango_layout_get_pixel_size (layout, &temp, NULL);
  gdk_draw_layout (q->win, q->text_gc, 
                   width/2 - temp/2,
                   height/2 - text_h/2 - descent + (i+1) * (ascent + descent),
                   layout);

  gdk_flush();
  mws = mws2;  /* Update global stage. */

  /* print also on console */
  if (0) {
    int i;
    for (i = 0; strs[i] != NULL; i++) {
      printf("%s\n", strs[i]);
    }
  }
}

static void switch_to_confirm_mode(qiv_image *q, const char **lines, gboolean is_help) {
  const char *infotextdisplay = is_help ? "(Showing Help)" : "(Command output)";
  qiv_mode = CONFIRM;
  qiv_display_multiline_window(q, infotextdisplay, lines, -1, "Push any key...");
  jcmd[jidx = jsize = 0] = '\0';
}

static void switch_to_normal_mode(qiv_image *q) {
  qiv_mode = NORMAL;
  jcmd[jidx = jsize = 0] = '\0';
  qiv_hide_multiline_window(q);
}

static void run_command_str(qiv_image *q, const char *str) {
  int numlines = 0;
  const int tab_mode = 0;
  const char **lines;
  /* This may reload the image. */
  run_command(q, str, tab_mode, image_names[image_idx], &numlines, &lines);
  if (lines && numlines) {
    switch_to_confirm_mode(q, lines, FALSE);
  } else {
    switch_to_normal_mode(q);
  }
}

static void switch_to_ext_edit_mode(qiv_image *q) {
  const int tab_mode = 1;
  int numlines = 0;
  const char **lines;

  switch_to_normal_mode(q);
  /* This may reload the image, and it may call update_image to hide the multiline_window. */
  run_command(q, jcmd, tab_mode, image_names[image_idx], &numlines, &lines);
  if (!lines || !numlines) {
    mess = mess_buf;  // Display jcmd.
  } else {
    /* printf("line1=(%s)\n", lines[0]); */  // no newline at the end
    strncpy(jcmd, lines[0], sizeof(jcmd));
    jcmd[sizeof(jcmd) - 1] = '\0';
    lines[0] = jcmd;
    mess = lines;
  }
  jidx = jsize = strlen(jcmd);
  qiv_mode = EXT_EDIT;
  qiv_display_multiline_window(q, "(Tab-start command)", mess, jidx,
                               "Press <Return> to send, <Esc> to abort"); // [lc]
}

#define KEY_BACKSPACE_WORD ((guint)-2)
#define KEY_DELETE_WORD ((guint)-3)
#define KEY_LEFT_WORD ((guint)-4)
#define KEY_RIGHT_WORD ((guint)-5)

/* ASCII word characters: a-z A-Z 0-9 _ (as usual).
 * TODO(pts): Optionally, also treat ':' as a word character, for v:... tagging.
 * Assumes all non-ASCII Unicode characters are composed of word bytes (encoded as UTF-8, >= 128).
 */
static gboolean isword(char c) {
  return (unsigned char)((c | 32) - 'a') <= (unsigned char)('z' - 'a') ||
         (unsigned char)(c - '0') <= (unsigned char)9 ||
         c == '_' || (unsigned char)c & (unsigned char)~127;
}

/* Returns bool indicating whether jcmd has changed. */
static gboolean apply_to_jcmd(const GdkEventKey *evk) {
  guint keyval = evk->keyval;
  if (evk->state & GDK_CONTROL_MASK) {
    if (keyval == GDK_BackSpace) {
      keyval = KEY_BACKSPACE_WORD;
    } else if (keyval == GDK_Left) {
      keyval = KEY_LEFT_WORD;
    } else if (keyval == GDK_Delete) {
      keyval = KEY_DELETE_WORD;
    } else if (keyval == GDK_Right) {
      keyval = KEY_RIGHT_WORD;
    } else if (keyval == GDK_Prior) {  /* gedit shortcut for <PageUp>. */
      keyval = GDK_Home;
    } else if (keyval == GDK_Next) {  /* gedit shortcut for <PageDown>. */
      keyval = GDK_End;
    } else if (keyval == 'a') {  /* bash shortcut. */
      keyval = GDK_Home;
    } else if (keyval == 'e') {  /* bash shortcut. */
      keyval = GDK_End;
    } else if (keyval == '?') {  /* bash and Linux (Ubuntu 14.04 X11 and console) shortcut: Ctrl-<?> */
      keyval = GDK_BackSpace;
    } else if (keyval == 'd') {  /* bash shortcut. */
      keyval = GDK_Delete;
    } else {
      return FALSE;
    }
 }
  const int jidx0 = jidx;
  if (keyval == GDK_BackSpace || keyval == GDK_Left) {
    /* Skip trailing UTF-8 continuation bytes 0x80 ... 0xbf, and then skip 1 more byte. */
    while (jidx > 0 && (unsigned char)(jcmd[--jidx] - 0x80) < (unsigned char)(0xc0 - 0x80)) {}
    if (jidx != jidx0) {
      if (keyval == GDK_BackSpace) { delete_backward:
        memmove(jcmd + jidx, jcmd + jidx0, jsize - jidx0 + 1);  /* Also copies the trailing '\0' because of the + 1. */
        jsize -= jidx0 - jidx;
      }
      return TRUE;
    }
  } else if (keyval == GDK_Delete || keyval == GDK_Right) {
    if (jidx < jsize) {
      /* Skip trailing UTF-8 continuation bytes 0x80 ... 0xbf, and then skip 1 more byte. */
      for (++jidx; jidx != jsize && (unsigned char)(jcmd[jidx] - 0x80) < (unsigned char)(0xc0 - 0x80); ++jidx) {}
      if (keyval == GDK_Delete) { delete_forward:
        memmove(jcmd + jidx0, jcmd + jidx, jsize - jidx0 + 1);  /* Also copies the trailing '\0' because of the + 1. */
        jsize -= jidx - jidx0;
        jidx = jidx0;
      }
      return TRUE;
    }
  } else if (keyval == KEY_BACKSPACE_WORD || keyval == KEY_LEFT_WORD) {
    /* Delete backward 0 or more non-word-characters, then 0 or more word-characters. */
    for (; jidx > 0 && !isword(jcmd[jidx - 1]); --jidx) {}
    for (; jidx > 0 && isword(jcmd[jidx - 1]); --jidx) {}
    if (keyval == KEY_BACKSPACE_WORD) goto delete_backward;
  } else if (keyval == KEY_DELETE_WORD || keyval == KEY_RIGHT_WORD) {
    /* Delete forward 0 or more non-word-characters, then 0 or more word-characters. */
    for (; jidx < jsize && !isword(jcmd[jidx]); ++jidx) {}
    for (; jidx < jsize && isword(jcmd[jidx]); ++jidx) {}
    if (keyval == KEY_DELETE_WORD) goto delete_forward;
  } else if (keyval == GDK_Home) {
    jidx = 0;
  } else if (keyval == GDK_End) {
    jidx = jsize;
  } else {
    char bytes[7];
    /* keyval works for ASCII, accented Latin letters, Greek, Cryrillic
     * etc. evk->string works only for ASCII, is empty for other keyvals.
     */
    const guint32 uc = gdk_keyval_to_unicode(keyval);
    if (uc >= 32) {
      /* pango_layout_set_text (which displays jcmd) called from
       * qiv_display_multiline_window expects UTF-8, no matter the locale.
       * It is called twice there, so it will display the UTF-8 warning
       * twice on non-UTF-8.
       */
      const unsigned c = g_unichar_to_utf8(uc, bytes);
      /* TODO(pts): Add dynamic allocation for jcmd. */
      if (c > 0 && jsize + c < sizeof(jcmd) - 1 && bytes[0] != '\0') {
        memmove(jcmd + jidx + c, jcmd + jidx, jsize - jidx + 1);  /* Also copies the trailing '\0' because of the + 1. */
        memcpy(jcmd + jidx, bytes, c);
        jidx += c;
        jsize += c;
        return TRUE;
      }
    }
  }
  return jidx != jidx0;
}

static void apply_to_jcmd_and_multiline_window(qiv_image *q, const GdkEventKey *evk) {
  if (apply_to_jcmd(evk)) {
    qiv_display_multiline_window(q, "(Editing command)", mess, jidx,
                                 "Press <Return> to send, <Esc> to abort");
  }
}

/* Must return false for '\0'. */
static gboolean istagspace(char c) {
  return c == ' ' || c == ',' || (unsigned char)(c - 9) <= (unsigned char)13;
}

/* Finds and returns the cursor position (byte index) of the first
 * occurrence of tag (q...p, p exclusive) in tags, or -1 if not found.
 */
int find_first_tag_pos(const char *tags, const char *q, const char *p) {
  size_t tag_size = p - q;
  const char *tags0 = tags, *tag;
  while (*tags) {
    for (; istagspace(*tags); ++tags) {}
    for (tag = tags; !istagspace(*tags); ++tags) {}
    if (0 == memcmp(tag, q, tag_size)) return tag - tags0;
  }
  return -1;
}

/* Finds and returns cursor position (byte index) in strs[0] corresponding
 * to the tag error message in strs[1], strs[2] or strs[3] (typically the
 * last one), or -1 if not found. A tag error message starts with `unknown
 * tags (TAG1 TAG2 TAG3)', and the corresponding error position is the end
 * of the first occurrence of any of the mentioned tags (TAG1, TAG2 and
 * TAG3) in strs[0].
 */
static int find_tag_error_pos(const char *strs[]) {
  int i, best_i;
  const char *p = NULL, *q, *r;
  const char *tags;
  if (!strs[0]) return -1;
  for (i = 1; (p = strs[i]) != NULL; ++i, p = NULL) {
    if (0 == strncmp(p, "unknown tags (", 14) && (r = strchr(p += 14, ')')) != NULL) break;
    if (0 == strncmp(p, "bad tag item syntax (", 21) && (r = strchr(p += 21, ')')) != NULL) break;
  }
  if (!p) return -1;
  tags = strs[0];
  best_i = -1;
  for (; p != r;) {
    for (; p != r && istagspace(*p); ++p) {}
    for (q = p; p != r && !istagspace(*p); ++p) {}
    /* Now the tag name to search is in q...p (p exclusive). */
    i = find_first_tag_pos(tags, q, p);
    if (i >= 0 && (best_i < 0 || i < best_i)) best_i = i;
  }
  if (i > 0) {  /* Move to the end of the tag. */
    for (; !istagspace(tags[i]); ++i) {}
  }
  return i;
}

#define MMIN(a, b) ((a) < (b) ? (a) : (b))
#define MMAX(a, b) ((a) > (b) ? (a) : (b))

static char infotext_xinerama[32 + sizeof(int) * 3];
static char infotext_slideshow_delay[32 + sizeof(int) * 3];

void qiv_handle_event(GdkEvent *ev, gpointer data)
{
  gboolean do_make_multiline_window_unclean = TRUE;
  gboolean exit_slideshow = FALSE;
  qiv_image *q = data;
  Window xwindow;
  int move_step;
  gint new_delay_ms;

  // get windows position if not in fullscreen mode
  // (because the user might have moved the window our since last redraw)
  if (!fullscreen) {
    gdk_window_get_position(q->win, &q->win_x, &q->win_y);
//    g_print("position   : q->win_x = %d, q->win_y = %d, q->win_w = %d\n", q->win_x, q->win_y, q->win_w);
//    gdk_window_get_origin(q->win, &q->win_x, &q->win_y);
//    gdk_window_get_root_origin(q->win, &q->win_x, &q->win_y);
  }

#if DEBUG
  switch (ev->type) {
   case GDK_NOTHING:
   case GDK_PROPERTY_NOTIFY:
   case GDK_KEY_PRESS:
   case GDK_KEY_RELEASE:
   case GDK_MOTION_NOTIFY:
   case GDK_ENTER_NOTIFY:
   case GDK_LEAVE_NOTIFY:
   case GDK_FOCUS_CHANGE:
   case GDK_MAP:
   case GDK_UNMAP:
   case GDK_VISIBILITY_NOTIFY:
   case GDK_WINDOW_STATE:
    break;
   case GDK_CONFIGURE:
    fprintf(stderr, "ev type=GDK_CONFIGURE x=%d y=%d width=%d height=%d\n", ev->configure.x, ev->configure.y, ev->configure.width, ev->configure.height);
    break;
   case GDK_EXPOSE:
    fprintf(stderr, "ev type=GDK_EXPOSE x=%d y=%d width=%d height=%d\n", ev->expose.area.x, ev->expose.area.y, ev->expose.area.width, ev->expose.area.height);
    break;
   default:
    fprintf(stderr, "ev type=%d\n", ev->type);
    break;
  }
#endif

  switch(ev->type) {
    case GDK_DELETE:
      qiv_exit(0);
      break;

#if 0  /* TODO(pts): Even with this, Ubuntu 18.04 gnome-panel is visible. */
    case GDK_MAP:
      if (fullscreen) {
        gdk_window_fullscreen(q->win);
        gdk_window_raise(q->win);
        gdk_window_fullscreen(q->win);
      }
      break;
#endif

    case GDK_EXPOSE:
      if (fullscreen) {
        const gint ex = ev->expose.area.x, ey = ev->expose.area.y, ew = ev->expose.area.width, eh = ev->expose.area.height;
#if DEBUG
        const gboolean is_full = ev->expose.area.x == 0 && ev->expose.area.y == 0 && ev->expose.area.width == 1440 && ev->expose.area.height == 900;
        gdk_draw_rectangle(q->win, is_full ? q->bg_gc : q->status_gc,
                           is_full, ev->expose.area.x, ev->expose.area.y, ev->expose.area.width - !is_full, ev->expose.area.height - !is_full);
#endif
        /* TODO(pts): Draw the statusbar only to the exposed area. */
        update_image_or_background_noflush(q, ex, ey, ew, eh, has_intersection_with_statusbar(q, ex, ey, ew, eh));  /* STATUSBAR */
        if (mws.is_displayed) {
          const gint sx = MMAX(mws.x, ex), sy = MMAX(mws.y, ey), sw = MMIN(mws.x + mws.w, ex + ew) - sx, sh = MMIN(mws.y + mws.h, ey + eh) - sy;  /* Calculate intersection. */
          if (sx > 0 && sy > 0) {
            mws.x = sx; mws.y = sy; mws.w = sw; mws.h = sh;
            mws.is_clean = TRUE;
            /* TODO(pts): Fix ownership issues of last_multiline_strs and last_multiline_continue_msg. */
            qiv_display_multiline_window(q, NULL, last_multiline_strs, last_multiline_curpos, last_multiline_continue_msg);
          }
        }
      }
      if (!q->exposed) {
        q->exposed = 1;
        qiv_set_cursor_timeout(q);
      }
      gdk_flush();
      break;

    case GDK_LEAVE_NOTIFY:
      do_make_multiline_window_unclean = FALSE;
      if (magnify && !fullscreen) {
        gdk_window_hide(magnify_img.win);
      }
      break;

    case GDK_CONFIGURE:
      if (magnify && !fullscreen) {
        gdk_window_get_root_origin(q->win,
                                   &magnify_img.frame_x, &magnify_img.frame_y);
        // printf("GDK_CONFIGURE get_root_origin  %d %d\n",
        //        magnify_img.frame_x, magnify_img.frame_y);
      }
      // gdk_draw_rectangle(q->win, q->status_gc, 1, 10, 10, 50, 50);
      if (statusbar_window) {
#ifdef DEBUG
        g_print("*** print statusbar at (%d, %d)\n", MAX(2,q->win_w-q->text_w-10), MAX(2,q->win_h-q->text_h-10));
#endif
        // printf(">>> statusbar_w %d %d %d %d\n",
        //        MAX(2,q->win_w-text_w-10), MAX(2,q->win_h-text_h-10), text_w+5, text_h+5);

        /* TODO(pts): Unify code with update_image(q, STATUSBAR); Currently it doesn't display a statusbar. */
        gdk_draw_rectangle(q->win, q->bg_gc, 0,
                           MAX(2,q->win_w-q->text_w-10), MAX(2,q->win_h-q->text_h-10),
                           q->text_w+5, q->text_h+5);
        gdk_draw_rectangle(q->win, q->status_gc, 1,
                           MAX(3,q->win_w-q->text_w-9), MAX(3,q->win_h-q->text_h-9),
                           q->text_w+4, q->text_h+4);

        qiv_render_title(q, FALSE);
        pango_layout_get_pixel_size (layout, &(q->text_w), &(q->text_h));
        gdk_draw_layout (q->win, q->text_gc, MAX(5,q->win_w-q->text_w-7),  MAX(5,q->win_h-7-q->text_h), layout);
      }

      break;

    case GDK_BUTTON_PRESS:
      if (qiv_mode != CONFIRM && qiv_mode != EXT_EDIT) {
        switch_to_normal_mode(q);
        qiv_cancel_cursor_timeout(q);
        if (fullscreen && ev->button.button == 1) {
          q->drag = 1;
          q->drag_start_x = ev->button.x;
          q->drag_start_y = ev->button.y;
          q->drag_win_x = q->win_x;
          q->drag_win_y = q->win_y;
          qiv_enable_mouse_events(q);
        }
      }
      break;

    case GDK_MOTION_NOTIFY:
      if (q->drag) {
        int move_x, move_y;
        move_x = (int)(ev->button.x - q->drag_start_x);
        move_y = (int)(ev->button.y - q->drag_start_y);
        if (q->drag == 1 && (ABS(move_x) > 3 || ABS(move_y) > 3)) {
          /* distinguish from simple mouse click... */
          q->drag = 2;
          show_cursor(q);
        }
        if (q->drag > 1 && (q->win_x != q->drag_win_x + move_x ||
                            q->win_y != q->drag_win_y + move_y)) {
          GdkEvent *e;
          qiv_disable_mouse_events(q);
          qiv_drag_image(q, q->drag_win_x + move_x, q->drag_win_y + move_y);
          q->infotext = ("(Drag)");
          /* el cheapo mouse motion compression */
          while (gdk_events_pending()) {
            e = gdk_event_get();
            if (e->type == GDK_BUTTON_RELEASE) {
              gdk_event_put(e);
              gdk_event_free(e);
              break;
            }
            gdk_event_free(e);
          }
          qiv_enable_mouse_events(q);
        }
      }
      else {
        show_cursor(q);
        // printf(" motion_notify magnify %d  is_hint %d\n", magnify, ev->motion.is_hint);
        if (magnify && !fullscreen) {
          gint xcur, ycur;
          if (ev->motion.is_hint) {
            gdk_window_get_pointer(q->win, &xcur, &ycur, NULL);
            update_magnify(q, &magnify_img,REDRAW, xcur, ycur); // [lc]
          }
          // update_magnify(q, &magnify_img,REDRAW, ev->motion.x,  ev->motion.y);
        } else {
          qiv_set_cursor_timeout(q);
        }
      }
      break;

    /* Use release instead of press (Fixes bug with junk being sent
     * to underlying xterm window on exit) */
    case GDK_BUTTON_RELEASE:
      switch_to_normal_mode(q);
      exit_slideshow = TRUE;
      switch (ev->button.button) {
        case 1:        /* left button pressed */
          if (q->drag) {
            int move_x, move_y;
            move_x = (int)(ev->button.x - q->drag_start_x);
            move_y = (int)(ev->button.y - q->drag_start_y);
            qiv_disable_mouse_events(q);
            qiv_set_cursor_timeout(q);

            if (q->drag > 1) {
              qiv_drag_image(q, q->drag_win_x + move_x, q->drag_win_y + move_y);
              q->infotext = ("(Drag)");
              q->drag = 0;
              break;
            }
            q->drag = 0;
          }
        case 5:        /* scroll wheel down emulated by button 5 */
          goto next_image;
        default:
          g_print("unmapped button event %d, exiting\n",ev->button.button);
        case 2:        /* middle button pressed */
          qiv_exit(0);
          break;
        case 3:        /* right button pressed */
        case 4:        /* scroll wheel up emulated by button 4 */
          goto previous_image;
          break;
      }
      break;

    case GDK_KEY_PRESS:

      exit_slideshow = TRUE;    /* Abort slideshow on any key by default */
      qiv_cancel_cursor_timeout(q);
   #ifdef DEBUG
      g_print("*** key:\n");    /* display key-codes */
      g_print("\tstring: %s\n",ev->key.string);
      g_print("\tkeyval: %d\n",ev->key.keyval);
   #endif

      /* Ctrl-<Q> to quit works in any qiv_mode. */
      if (ev->key.keyval == 'q' && ev->key.state & GDK_CONTROL_MASK) {
        qiv_exit(0);
      } else if (qiv_mode == CONFIRM) {  /* After `Push any key...'. */
        if (!ev->key.is_modifier) switch_to_normal_mode(q);
      } else if (qiv_mode == JUMP_EDIT) {
        /* printf("evkey=0x%x modifiers=0x%x\n", ev->key.keyval, ev->key.state); */
        if (ev->key.keyval == GDK_Escape ||
            (ev->key.keyval == 'c' && ev->key.state & GDK_CONTROL_MASK)) {
          switch_to_normal_mode(q);
        } else if (ev->key.keyval == GDK_Tab) {  /* Abort JUMP_EDIT mode, switch to EXT_EDIT mode. */
          switch_to_ext_edit_mode(q);
          do_make_multiline_window_unclean = FALSE;
        } else if (ev->key.keyval == GDK_Return || ev->key.keyval == GDK_KP_Enter) {
          jump2image(jcmd);
          q->is_updated = FALSE;
          qiv_load_image(q);  /* Does update_image(q, REDRAW) unless in slideshow mode. TODO(pts): Is slideshow mode possible here? Get rid of q->is_updated. */
          if (q->is_updated) mws.is_displayed = FALSE;  /* Speedup to avoid another flickering REDRAW. */
          switch_to_normal_mode(q);
        } else {  /* Record keystroke. */
          apply_to_jcmd(&ev->key);
        }
      } else if (qiv_mode == EXT_EDIT) {
        /* printf("evkey=0x%x modifiers=0x%x\n", ev->key.keyval, ev->key.state); */
        if (ev->key.keyval == GDK_Escape ||
            (ev->key.keyval == 'c' && ev->key.state & GDK_CONTROL_MASK)) {
          switch_to_normal_mode(q);
        } else if (ev->key.keyval == GDK_Tab && jidx != jsize) {
          /* TODO(pts): Allow tab completion in the middle of the line. */
          do_make_multiline_window_unclean = FALSE;
        } else if (ev->key.keyval == GDK_Return ||
                   ev->key.keyval == GDK_KP_Enter ||
                   ev->key.keyval == GDK_Tab) {
          int tab_mode = ev->key.keyval == GDK_Tab;
          int numlines = 0;
          const char **lines;

          if (tab_mode) {
            qiv_display_multiline_window(q, "(Tab completion)", mess, jidx,
                                         "<Tab> completion running...");
          } else {
            qiv_hide_multiline_window(q);
          }
          /* This may reload the image, and it may call update_image to hide
           * the multiline_window. Typically it only updates the STATUSBAR.
           *
           * TODO(pts): If the multiline_window is hidden by this, set
           * do_make_multiline_window_unclean = TRUE to ask for an eventual
           * redraw. It's already happening, except for tab_mode.
           */
          run_command(q, jcmd, tab_mode, image_names[image_idx], &numlines, &lines);
          if (!tab_mode && lines && numlines > 1 && lines[1][0] == '\007') {
            /* Let an error message starting with \007 propagate */
            ++lines[1];
            tab_mode = 1;
          }
          if (tab_mode) {
            int jidx2 = -1;
            if (!lines || !numlines) {
              mess = mess_buf;  // Display jcmd.
            } else {
              /* printf("line1=(%s)\n", lines[0]); */  // no newline at the end
              strncpy(jcmd, lines[0], sizeof(jcmd));
              jcmd[sizeof(jcmd) - 1] = '\0';
              lines[0] = jcmd;
              mess = lines;
              if (do_tag_error_pos) jidx2 = find_tag_error_pos(lines);
            }
            jidx = jsize = strlen(jcmd);
            if (jidx2 >= 0 && jidx2 <= jsize) jidx = jidx2;
            qiv_display_multiline_window(q, "(Expanded command)", mess, jidx,
                                         "Press <Return> to send, <Esc> to abort"); // [lc]
            do_make_multiline_window_unclean = FALSE;
          } else if (lines && numlines) {
            switch_to_confirm_mode(q, lines, FALSE);
            jcmd[jidx = jsize = 0] = '\0';  /* Not visible anymore in multiline_window. */
          } else {  /* Empty output from command, consider it finished. */
            switch_to_normal_mode(q);
          }
        } else {  /* Record keystroke. */
          do_make_multiline_window_unclean = FALSE;
          apply_to_jcmd_and_multiline_window(q, &ev->key);
        }
      } else {  /* qiv_mode == NORMAL. */
        switch (ev->key.keyval) {

          /* Function keys to qiv-command. */

          case GDK_F2:
          case GDK_F3:
          case GDK_F4:
          case GDK_F5:
          case GDK_F6:
          case GDK_F7:
          case GDK_F8:
          case GDK_F9:
          case GDK_F10:
          do_f_command:
            if (do_f_commands) {
              char tmp[10 + 3 * sizeof(int)];
              sprintf(tmp, ":f%d", ev->key.keyval - (GDK_F1 - 1));  /* ":f1" ... ":f12" */
              /* This may reload the image. */
              run_command_str(q, tmp);
            } else {
              goto do_default;
            }
            break;

          /* Help */

          case GDK_F1:
            if (do_f_commands) goto do_f_command;
            /* fallthrough */
          case '?':
            switch_to_confirm_mode(q, helpstrs, TRUE);
            do_make_multiline_window_unclean = FALSE;
            break;

            /* Exit */

          case GDK_Escape:
          case 'q':  /* Also matches Ctrl-<Q>, Alt-<Q> etc. */
            qiv_exit(0);
            break;

            /* Fullscreen mode (on/off) */

          case 'f':
            exit_slideshow = FALSE;
            destroy_win(q);
            fullscreen ^= 1;
            qiv_load_image(q);
            break;

            /* Center mode (on/off) */

          case 'e':
            exit_slideshow = FALSE;
            center ^= 1;
            q->infotext = center ? "(Centering: on)" : "(Centering: off)";
            if (center) center_image(q);
            update_image(q, MOVED);
            break;

            /* Transparency on/off */

          case 'p':
            exit_slideshow = FALSE;
            transparency ^= 1;
            q->infotext = transparency ? "(Transparency: on)" : "(Transparency: off)";
            q->win_ow = q->win_oh = -1;  /* Force full (and flickering) q->win redraw below. */
            update_image(q, REDRAW);
            break;

            /* Maxpect on/off */

          case 'm':
            scale_down = 0;
            maxpect ^= 1;
            q->infotext = maxpect ? "(Maxpect: on)" : "(Maxpect: off)";
            zoom_factor = maxpect ? 0 : fixed_zoom_factor; /* reset zoom */
            if (q->has_thumbnail) {
              if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
              qiv_load_image(q);
            } else {
              check_size(q, TRUE);
              update_image(q, REDRAW);
            }
            break;

            /* Random on/off */

          case 'r':
            random_order ^= 1;
            q->infotext = random_order ?"(Random order: on)" : "(Random order: off)";
            update_image(q, REDRAW);
            break;

            /* iconify */

          case 'I':
            exit_slideshow = TRUE;
            if (fullscreen) {
              gdk_window_withdraw(q->win);
              show_cursor(q);
              qiv_set_cursor_timeout(q);
              fullscreen=0;
              first=1;
              qiv_load_image(q);
            }
            xwindow = GDK_WINDOW_XWINDOW(q->win);
            XIconifyWindow(GDK_DISPLAY(), xwindow, DefaultScreen(GDK_DISPLAY()));
            break;

            /* Statusbar on/off  */

          case 'i':
            exit_slideshow = FALSE;
            if (fullscreen) {
              statusbar_fullscreen ^= 1;
              q->infotext = statusbar_fullscreen ? "(Statusbar: on)" : "(Statusbar: off)";
            } else {
              statusbar_window ^= 1;
              q->infotext = statusbar_window ? "(Statusbar: on)" : "(Statusbar: off)";
            }
            update_image(q, STATUSBAR);
            break;

            /* Slide show on/off */

          case 's':
            exit_slideshow = FALSE;
            slide ^= 1;
            q->infotext = slide ? "(Slideshow: on)" : "(Slideshow: off)";
            update_image(q, STATUSBAR);
            break;

            /* move image right */

          case GDK_Left:
          case GDK_KP_4:
          case GDK_KP_Left:
            if (fullscreen) {
              if (ev->key.state & GDK_SHIFT_MASK || ev->key.keyval == GDK_KP_4) {
                move_step = (MIN(screen_x, q->win_w) / STEP);
              } else {
                move_step = (q->win_w / 100);
              }
              if (move_step < 10)
                move_step = 10;

              /* is image greater than screen? */
              if (q->win_w > screen_x) {
                /* left border visible yet? */
                if (q->win_x < 0) {
                  q->win_x += move_step;
                  /* sanity check */
                  if (q->win_x > 0)
                    q->win_x = 0;
                  q->infotext = ("(Moving right)");
                } else {
                  q->infotext = ("(Cannot move further to the right)");
                }

              } else {                      /* user is just playing around */

                /* right border reached? */
                if (q->win_x + q->win_w < screen_x) {
                  q->win_x += move_step;
                  /* sanity check */
                  if (q->win_x + q->win_w > screen_x)
                    q->win_x = screen_x - q->win_w;
                  q->infotext = ("(Moving right)");
                } else {
                  q->infotext = ("(Cannot move further to the right)");
                }
              }
            } else {
              q->infotext = ("(Moving works only in fullscreen mode)");
              fprintf(stdout, "qiv: Moving works only in fullscreen mode\n");
            }
            update_image(q, MOVED);
            break;

            /* move image left */

          case GDK_Right:
          case GDK_KP_6:
          case GDK_KP_Right:
            if (fullscreen) {
              if (ev->key.state & GDK_SHIFT_MASK || ev->key.keyval == GDK_KP_6) {
                move_step = (MIN(screen_x, q->win_w) / STEP);
              } else {
                move_step = (q->win_w / 100);
              }
              if (move_step < 10)
                move_step = 10;

              /* is image greater than screen? */
              if (q->win_w > screen_x) {
                /* right border visible yet? */
                if (q->win_x + q->win_w > screen_x) {
                  q->win_x -= move_step;
                  /* sanity check */
                  if (q->win_x + q->win_w < screen_x)
                    q->win_x = screen_x - q->win_w;
                  q->infotext = ("(Moving left)");
                } else {
                  q->infotext = ("(Cannot move further to the left)");
                }

              } else {                      /* user is just playing around */

                /* left border reached? */
                if (q->win_x > 0) {
                  q->win_x -= move_step;
                  /* sanity check */
                  if (q->win_x < 0)
                    q->win_x = 0;
                  q->infotext = ("(Moving left)");
                } else {
                  q->infotext = ("(Cannot move further to the left)");
                }
              }
            } else {
              q->infotext = ("(Moving works only in fullscreen mode)");
              fprintf(stdout, "qiv: Moving works only in fullscreen mode\n");
            }
            update_image(q, MOVED);
            break;

            /* move image up */

          case GDK_Down:
          case GDK_KP_2:
          case GDK_KP_Down:
            if (fullscreen) {
              if (ev->key.state & GDK_SHIFT_MASK || ev->key.keyval == GDK_KP_2) {
                move_step = (MIN(screen_y, q->win_h) / STEP);
              } else {
                move_step = (q->win_h / 100);
              }
              if (move_step < 10)
                move_step = 10;

              /* is image greater than screen? */
              if (q->win_h > screen_y) {
                /* bottom visible yet? */
                if (q->win_y + q->win_h > screen_y) {
                  q->win_y -= move_step;
                  /* sanity check */
                  if (q->win_y + q->win_h < screen_y)
                    q->win_y = screen_y - q->win_h;
                  q->infotext = ("(Moving up)");
                } else {
                  q->infotext = ("(Cannot move further up)");
                }

              } else {                      /* user is just playing around */

                /* top reached? */
                if (q->win_y > 0) {
                  q->win_y -= move_step;
                  /* sanity check */
                  if (q->win_y < 0)
                    q->win_y = 0;
                  q->infotext = ("(Moving up)");
                } else {
                  q->infotext = ("(Cannot move further up)");
                }
              }
            } else {
              q->infotext = ("(Moving works only in fullscreen mode)");
              fprintf(stdout, "qiv: Moving works only in fullscreen mode\n");
            }
            update_image(q, MOVED);
            break;

            /* move image down */

          case GDK_Up:
          case GDK_KP_8:
          case GDK_KP_Up:
            if (fullscreen) {
              if (ev->key.state & GDK_SHIFT_MASK || ev->key.keyval == GDK_KP_8) {
                move_step = (MIN(screen_y, q->win_h) / STEP);
              } else {
                move_step = (q->win_h / 100);
              }
              if (move_step < 10)
                move_step = 10;

              /* is image greater than screen? */
              if (q->win_h > screen_y) {
                /* top visible yet? */
                if (q->win_y < 0) {
                  q->win_y += move_step;
                  /* sanity check */
                  if (q->win_y > 0)
                    q->win_y = 0;
                  q->infotext = ("(Moving down)");
                } else {
                  q->infotext = ("(Cannot move further down)");
                }

              } else {                      /* user is just playing around */

                /* bottom reached? */
                if (q->win_y + q->win_h < screen_y) {
                  q->win_y += move_step;
                  /* sanity check */
                  if (q->win_y + q->win_h > screen_y)
                    q->win_y = screen_y - q->win_h;
                  q->infotext = ("(Moving down)");
                } else {
                  q->infotext = ("(Cannot move further down)");
                }
              }
            } else {
              q->infotext = ("(Moving works only in fullscreen mode)");
              fprintf(stdout, "qiv: Moving works only in fullscreen mode\n");
            }
            update_image(q, MOVED);
            break;

            /* Scale_down */

          case 't':
            maxpect = 0;
            scale_down ^= 1;
            q->infotext = (scale_down ?
                     "(Scale down: on)" : "(Scale down: off)");
            zoom_factor = maxpect ? 0 : fixed_zoom_factor;  /* reset zoom */
            check_size(q, TRUE);
            update_image(q, REDRAW);
            break;

            /* Resize + */

          case GDK_KP_Add:
          case '+':
          case '=':
            q->infotext = ("(Zoomed in)");
            zoom_in(q);
            update_image(q, ZOOMED);
            break;

            /* Resize - */

          case GDK_KP_Subtract:
          case '-':
            q->infotext = ("(Zoomed out)");
            zoom_out(q);
            update_image(q, ZOOMED);
            break;

            /* Reset Image / Original (best fit) size */

          case GDK_Return:
          case GDK_KP_Enter:
            if (do_enter_command) {
              /* This may reload the image. */
              run_command_str(q, ":enter");
            } else {
              q->infotext = ("(Reset size)");
              reload_image(q);
              zoom_factor = fixed_zoom_factor;  /* reset zoom */
              check_size(q, TRUE);
              update_image(q, REDRAW);
            }
            break;

            /* Next picture - or loop to the first */

          case ' ':
          next_image:
            if (slide) {
              set_image_direction(1);
             abort_slideshow:
              q->infotext = ("(Slideshow aborted)");
              update_image(q, STATUSBAR);
              break;
            } else if  (ev->key.state & GDK_CONTROL_MASK) {
              q->infotext = ("(Next picture directory)");
              next_image_dir(1);
            } else {
              q->infotext = ("(Next picture)");
              next_image(1);
            }
            if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
            qiv_load_image(q);
            break;

            /* 5 pictures forward - or loop to the beginning */

          case GDK_Page_Down:
          case GDK_KP_Page_Down:
            q->infotext = ("(5 pictures forward)");
            next_image(5);
            if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
            qiv_load_image(q);
            break;

            /* Previous picture - or loop back to the last */

          case GDK_BackSpace:
          previous_image:
            if (slide) {
              set_image_direction(-1);
              goto abort_slideshow;
            } else if  (ev->key.state & GDK_CONTROL_MASK) {
              q->infotext = ("(Previous picture directory)");
              next_image_dir(-1);
            } else {
              q->infotext = ("(Previous picture)");
              next_image(-1);
            }
            if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
            qiv_load_image(q);
            break;

            /* 5 pictures backward - or loop back to the last */

          case GDK_Page_Up:
          case GDK_KP_Page_Up:
            q->infotext = ("(5 pictures backward)");
            next_image(-5);
            if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
            qiv_load_image(q);
            break;

            /* + brightness */

          case 'B':
            q->infotext = ("(More brightness)");
            q->mod.brightness += 8;
            update_image(q, REDRAW);
            break;

            /* - brightness */

          case 'b':
            q->infotext = ("(Less brightness)");
            q->mod.brightness -= 8;
            update_image(q, REDRAW);
            break;

            /* + contrast */

          case 'C':
            q->infotext = ("(More contrast)");
            q->mod.contrast += 8;
            update_image(q, REDRAW);
            break;

            /* - contrast */

          case 'c':
            q->infotext = ("(Less contrast)");
            q->mod.contrast -= 8;
            update_image(q, REDRAW);
            break;

            /* + gamma */

          case 'G':
            q->infotext = ("(More gamma)");
            q->mod.gamma += 8;
            update_image(q, REDRAW);
            break;

            /* - gamma */

          case 'g':
            q->infotext = ("(Less gamma)");
            q->mod.gamma -= 8;
            update_image(q, REDRAW);
            break;

            /* - reset brightness, contrast and gamma */

          case 'o':
            q->infotext = ("(Reset bri/con/gam)");
            reset_mod(q);
            update_image(q, REDRAW);
            break;

            /* Delete image */

          case GDK_Delete:
          case 'd':
            if (!readonly) {
              if (move2trash() == 0) {
                q->infotext = ("(Deleted last image)");
                qiv_load_image(q);
              } else {
                gdk_beep();
                q->infotext = ("(Delete failed!)");
                update_image(q, STATUSBAR);
              }
            }
            break;

            /* Undelete image */

          case 'u':
            if (!readonly) {
              if (undelete_image() == 0) {
                q->infotext = ("(Undeleted)");
                qiv_load_image(q);
              } else {
                gdk_beep();
                q->infotext = ("(Undelete failed!)");
                update_image(q, STATUSBAR);
              }
            }
            break;

            /* Copy image to selected directory */

          case 'a':
            if (copy2select() == 0) {
              q->infotext = ("(Last image copied)");
              next_image(1);
              qiv_load_image(q);
            } else {
              gdk_beep();
              q->infotext = ("(Copy failed!)");
              update_image(q, STATUSBAR);
            }
            break;

            /* Jump to image */

          case 'j':
            /* True: assert(qiv_mode == NORMAL); */
            qiv_mode = JUMP_EDIT;
            jcmd[jidx = jsize = 0] = '\0';
            break;

            /* Flip horizontal */

          case 'h':
            imlib_image_flip_horizontal();
            q->infotext = ("(Flipped horizontally)");
            update_image(q, REDRAW);
            break;

            /* Flip vertical */

          case 'v':
            imlib_image_flip_vertical();
            q->infotext = ("(Flipped vertically)");
            update_image(q, REDRAW);
            break;

            /* Watch File (on/off) */

          case 'w':
            watch_file ^= 1;
            q->infotext = (watch_file ?
                     "(File watching: on)" : "(File watching: off)");
            update_image(q, REDRAW);
            if(watch_file){
              g_idle_add (qiv_watch_file, q);
            }
            break;

            /* Rotate right */

          case 'k':
            imlib_image_orientate(1);
            q->infotext = ("(Rotated right)");
            swap(&q->orig_w, &q->orig_h);
            swap(&q->win_w, &q->win_h);
            check_size(q, FALSE);
            correct_image_position(q);
            update_image(q, REDRAW);
            break;

            /* Rotate left */

          case 'l':
            imlib_image_orientate(3);
            q->infotext = ("(Rotated left)");
            swap(&q->orig_w, &q->orig_h);
            swap(&q->win_w, &q->win_h);
            check_size(q, FALSE);
            correct_image_position(q);
            update_image(q, REDRAW);
            break;

            /* Center image on background */

          case 'x':
            to_root=1;
            set_desktop_image(q);
            q->infotext = ("(Centered image on background)");
            update_image(q, REDRAW);
            to_root=0;
            break;

            /* Tile image on background */

          case 'y':
            to_root_t=1;
            set_desktop_image(q);
            q->infotext = ("(Tiled image on background)");
            update_image(q, REDRAW);
            to_root_t=0;
            break;

          case 'z':
            to_root_s=1;
            set_desktop_image(q);
            q->infotext = ("(Stretched image on background)");
            update_image(q, REDRAW);
            to_root_s=0;
            break;


            /* Decrease slideshow delay */
          case GDK_F11:
            if (do_f_commands) goto do_f_command;
            /* fallthrough */
          case ']':  /* Faster, like mplayer(1). */
            new_delay_ms = add_to_delay(-1);
           after_change_delay:
            exit_slideshow = FALSE;
            snprintf(infotext_slideshow_delay, sizeof infotext_slideshow_delay,
                     "(Slideshow-delay: %d ms)", new_delay_ms);
            q->infotext = infotext_slideshow_delay;
            update_image(q, STATUSBAR);
            break;

            /* Increase slideshow delay */
          case GDK_F12:
            if (do_f_commands) goto do_f_command;
            /* fallthrough */
          case '[':  /* Slower, like mplayer(1). */
            new_delay_ms = add_to_delay(1);
            goto after_change_delay;

            /* Show magnifying window */
          case '<':       // [lc]
            if (!fullscreen) {
              magnify ^= 1;
              int xcur, ycur;
              if (!magnify) {
                gdk_window_hide(magnify_img.win);
              } else {
                setup_magnify(q, &magnify_img);
                gdk_window_get_pointer(q->win, &xcur, &ycur, NULL);
                update_magnify(q, &magnify_img, REDRAW, xcur, ycur); // [lc]
                qiv_disable_mouse_events(q);
              }
            }
            break;


#ifdef GTD_XINERAMA
            /* go to next xinerama screen */
          case 'X':
            if (number_xinerama_screens) {
              user_screen++;
              user_screen %= number_xinerama_screens;
              // g_print("user_screen = %d, number_xinerama_screens = %d\n", user_screen, number_xinerama_screens);
            }
            get_preferred_xinerama_screens();     // reselect appropriate screen
            snprintf(infotext_xinerama, sizeof infotext_xinerama,
                     "(xinerama screen: %d)", user_screen);
            q->infotext = infotext_xinerama;
            if (center) center_image(q);
            update_image(q, REDRAW);
          break;
#endif

            /* Run qiv-command '' <image-filename> 1 */
          case '\t':  // not sent this way
          case GDK_Tab:
            switch_to_ext_edit_mode(q);
            do_make_multiline_window_unclean = FALSE;
            break;

            /* run qiv-command */
          case '^':    // special command with options
            /* True: assert(qiv_mode == COMMAND); */
            qiv_mode = EXT_EDIT;
            jcmd[jidx = jsize = 0] = '\0';
            mess = mess_buf;
            do_make_multiline_window_unclean = FALSE;
            apply_to_jcmd_and_multiline_window(q, &ev->key);  /* Start by typing '^'. */
            break;

	  case '`':
	  case '~':
	  case '\\':
	  case '|':
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
          case 'A':
          case 'D':
          case 'E':
          case 'F':
          case 'H':
          case 'J':
          case 'K':
          case 'L':
          case 'M':
          case 'n':
          case 'N':
          case 'O':
          case 'P':
          case 'Q':
          case 'R':
          case 'S':
          case 'T':
          case 'U':
          case 'V':
          case 'W':
          case 'Y':
          case 'Z':
            /* This may reload the image. */
            run_command_str(q, ev->key.string);
            break;

          default:
          do_default:
            exit_slideshow = FALSE;
            do_make_multiline_window_unclean = FALSE;
            break;
        }
      }
      break;
    default:
      do_make_multiline_window_unclean = FALSE;
      break;
  }
  if (exit_slideshow) slide = 0;
  if (do_make_multiline_window_unclean) mws.is_clean = FALSE;
}

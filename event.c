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
#include "qiv.h"

#define STEP 3 //When using KP arrow, number of step for seeing all the image.

typedef enum QivMode {
  NORMAL = 0,     /* Waiting for single-keystroke commands. */
  JUMP_EDIT = 2,  /* Editing the jump target string in jcmd. Edit buffer not displayed. */
  EXT_EDIT = 3,   /* Editing the single-line external qiv-command argument in jcmd. Edit state displayed in multiline_window, starting with the argument. */
  CONFIRM = 4,    /* Waiting for any keystoke (`Push any key...') while displaying message in multiline_window. */
} QivMode;
static QivMode qiv_mode;  /* NORMAL by default. */
static char   jcmd[512];  /* Single-line text edit buffer used by both JUMP_EDIT and EXT_EDIT. Always '\0'-terminated. */
static int    jidx;  /* Cursor position in jcmd. Currently it always points to the trailing '\0'. */
static const char *mess_buf[2] = { jcmd, NULL};
static const char **mess = mess_buf;
static int    cursor_timeout;
typedef struct _qiv_multiline_window_state {
  gint x, y, w, h;
  gboolean is_displayed;
  gboolean is_clean;
} qiv_multiline_window_state;
static qiv_multiline_window_state mws;  /* Default: .is_displayed = FALSE. */

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
}

static void qiv_hide_multiline_window(qiv_image *q) {
  if (!mws.is_displayed) {
  } else if (mws.y >= q->win_y && mws.y + mws.h <= q->win_y + q->win_h) {
    /* Image covers multiline_window vertically, a REDRAW + some vertical background bars are enough. */
    if (q->win_x > mws.x) gdk_draw_rectangle(q->win, q->bg_gc, 1, mws.x, mws.y, q->win_x - mws.x, mws.h);
    if (mws.x + mws.w > q->win_x + q->win_w) gdk_draw_rectangle(q->win, q->bg_gc, 1, q->win_x + q->win_w, mws.y, mws.x + mws.w - q->win_x - q->win_w, mws.h);
    update_image(q, mws.is_clean ? MOVED : REDRAW);
  } else {
    update_image(q, FULL_REDRAW);
  }
  mws.is_displayed = mws.is_clean = FALSE;
}

static void qiv_display_multiline_window(qiv_image *q, const char *infotextdisplay,
                                         const char *strs[], const char *continue_msg) {
  int temp, text_w = 0, text_h, i, maxlines;
  int width, height, text_left;
  qiv_multiline_window_state mws2;
  int ascent, descent;

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

  text_h = (i + 2) * ( ascent + descent );
  text_left = width/2 - text_w/2 - 4;
  if (text_left < 2)  text_left = 2;            /* if window/screen is smaller than text */
  mws2.x = text_left;
  mws2.y = height/2 - text_h/2 - 4;
  mws2.w = text_w + 7;
  mws2.h = text_h + 7;
  mws2.is_displayed = mws2.is_clean = TRUE;

  snprintf(infotext, sizeof infotext, "%s", infotextdisplay);
  if (!mws.is_displayed || (mws.is_clean && mws.x >= mws2.x && mws.y >= mws2.y && mws.x + mws.w <= mws2.x + mws2.w && mws.y + mws.h <= mws2.y + mws2.h)) {
    /* update_image_noflush not needed, we skip it to prevent flickering. */
  } else if (mws.is_clean && mws.x <= mws2.x && mws.y == mws2.y && mws.x + mws.w >= mws2.x + mws2.w && mws.y + mws.h == mws2.y + mws2.h) {
    /* Condition above: multiline_window became narrower, typically because of <Backspace>. */
    gdk_draw_rectangle(q->win, q->bg_gc, 1, mws.x, mws.y, mws2.x - mws.x, mws.h);
    gdk_draw_rectangle(q->win, q->bg_gc, 1, mws2.x + mws2.w, mws.y, mws.x + mws.w - mws2.x - mws2.w, mws.h);
    if (mws2.x >= q->win_x || mws2.x + mws2.w <= q->win_x + q->win_w) {
      update_image_noflush(q, mws.is_clean ? MOVED : REDRAW);  /* New multiline_window doesn't cover the entire image => REDRAW the image. Unfortunately it flickers, but there is no other correct way. */
    }
  } else {
    /* Do the slowest FULL_REDRAW only if the new multiline_window doesn't
     * fully cover the old one. Otherwise do the slow REDRAW if anything
     * other than EXT_EDIT typing has happened (!mws.is_clean) since the
     * last draw, otherwise do the fast MOVED.
     */
    const int redraw_mode = !(mws.x >= mws2.x && mws.y >= mws2.y && mws.x + mws.w <= mws2.x + mws2.w && mws.y + mws.h <= mws2.y + mws2.h) ?
        FULL_REDRAW : mws.is_clean ? MOVED : REDRAW;
    /* TODO(pts): Redraw the multiline_window if the main window q->win is exposed. */
    update_image_noflush(q, redraw_mode);
  }
  gdk_draw_rectangle(q->win, q->bg_gc, 0, mws2.x, mws2.y, mws2.w, mws2.h);
  gdk_draw_rectangle(q->win, q->status_gc, 1,
                     text_left + 1,
                     height/2 - text_h/2 - 3,
                     text_w + 6, text_h + 6);
  for (i = 0; strs[i] && i < maxlines; i++) {
       pango_layout_set_text(layout, strs[i], -1);
       gdk_draw_layout (q->win, q->text_gc, text_left + 4, height/2 - text_h/2  +
                  i * (ascent + descent), layout);
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
  qiv_display_multiline_window(q, infotextdisplay, lines, "Push any key...");
  jidx = 0;
  jcmd[jidx = 0] = '\0';
}

static void switch_to_normal_mode(qiv_image *q) {
  qiv_mode = NORMAL;
  jidx = 0;
  jcmd[jidx = 0] = '\0';
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
  /* This may reload the image. */
  run_command(q, jcmd, tab_mode, image_names[image_idx], &numlines, &lines);
  if (!lines || !numlines) {
    mess = mess_buf;  // Display jcmd.
  } else {
    /* printf("line1=(%s)\n", lines[0]); */  // no newline at the end
    strncpy(jcmd, lines[0], sizeof(jcmd));
    jcmd[sizeof(jcmd) - 1] = '\0';
    jidx = strlen(jcmd);
    lines[0] = jcmd;
    mess = lines;
  }
  qiv_mode = EXT_EDIT;
  // TODO(pts): Make typing faster on a large image.
  qiv_display_multiline_window(q, "(Tab-start command)", mess,
                               "Press <Return> to send, <Esc> to abort"); // [lc]
}

/* Returns bool indicating whether jcmd has changed. */
static gboolean apply_to_jcmd(const GdkEventKey *evk) {
  if (evk->keyval == GDK_BackSpace) {
    if (jidx > 0) {
      jidx--;  /* TODO(pts): Remove multiple bytes (if UTF-8). */
      jcmd[jidx] = '\0';
      return TRUE;
    }
  } else {
    if (evk->string && *evk->string > 31 &&
        jidx + 0U < sizeof(jcmd) - 1) {
      /* TODO(pts): Add multiple characters (UTF-8). */
      /* TODO(pts): Add dynamic allocation for jcmd. */
      jcmd[jidx++] = *(evk->string);
      jcmd[jidx] = '\0';
      return TRUE;
    }
  }
  return FALSE;
}

static void apply_to_jcmd_and_multiline_window(qiv_image *q, const GdkEventKey *evk) {
  if (apply_to_jcmd(evk)) {
    qiv_display_multiline_window(q, "(Editing command)", mess,
                                 "Press <Return> to send, <Esc> to abort");
  }
}

void qiv_handle_event(GdkEvent *ev, gpointer data)
{
  gboolean do_make_multiline_window_unclean = TRUE;
  gboolean exit_slideshow = FALSE;
  qiv_image *q = data;
  Window xwindow;
  int move_step;

  // get windows position if not in fullscreen mode
  // (because the user might have moved the window our since last redraw)
  if (!fullscreen) {
    gdk_window_get_position(q->win, &q->win_x, &q->win_y);
//    g_print("position   : q->win_x = %d, q->win_y = %d, q->win_w = %d\n", q->win_x, q->win_y, q->win_w);
//    gdk_window_get_origin(q->win, &q->win_x, &q->win_y);
//    gdk_window_get_root_origin(q->win, &q->win_x, &q->win_y);
  }

  switch(ev->type) {
    case GDK_DELETE:
      qiv_exit(0);
      break;

    case GDK_EXPOSE:
      if (!q->exposed) {
        q->exposed = 1;
        qiv_set_cursor_timeout(q);
      }
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

        gdk_draw_rectangle(q->win, q->bg_gc, 0,
                           MAX(2,q->win_w-q->text_w-10), MAX(2,q->win_h-q->text_h-10),
                           q->text_w+5, q->text_h+5);
        gdk_draw_rectangle(q->win, q->status_gc, 1,
                           MAX(3,q->win_w-q->text_w-9), MAX(3,q->win_h-q->text_h-9),
                           q->text_w+4, q->text_h+4);

        pango_layout_set_text(layout, q->win_title, -1);
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
          snprintf(infotext, sizeof infotext, "(Drag)");
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
              snprintf(infotext, sizeof infotext, "(Drag)");
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
          qiv_load_image(q);  /* Does update_image(q, FULL_REDRAW) unless in slideshow mode. */
          if (q->is_updated) mws.is_displayed = FALSE;  /* Speedup to avoid another FULL_REDRAW. */
          switch_to_normal_mode(q);
        } else {  /* Record keystroke. */
          apply_to_jcmd(&ev->key);
        }
      } else if (qiv_mode == EXT_EDIT) {
        /* printf("evkey=0x%x modifiers=0x%x\n", ev->key.keyval, ev->key.state); */
        if (ev->key.keyval == GDK_Escape ||
            (ev->key.keyval == 'c' && ev->key.state & GDK_CONTROL_MASK)) {
          switch_to_normal_mode(q);
        } else if (ev->key.keyval == GDK_Return ||
                   ev->key.keyval == GDK_KP_Enter ||
                   ev->key.keyval == GDK_Tab) {
          int tab_mode = ev->key.keyval == GDK_Tab;
          int numlines = 0;
          const char **lines;

          if (tab_mode) {
            qiv_display_multiline_window(q, "(Tab completion)", mess,
                                         "<Tab> completion running...");
          } else {
            qiv_hide_multiline_window(q);
          }
          /* This may reload the image. */
          run_command(q, jcmd, tab_mode, image_names[image_idx], &numlines, &lines);
          if (!tab_mode && lines && numlines > 1 && lines[1][0] == '\007') {
            /* Let an error message starting with \007 propagate */
            ++lines[1];
            tab_mode = 1;
          }
          if (tab_mode) {
            if (!lines || !numlines) {
              mess = mess_buf;  // Display jcmd.
            } else {
              /* printf("line1=(%s)\n", lines[0]); */  // no newline at the end
              strncpy(jcmd, lines[0], sizeof(jcmd));
              jcmd[sizeof(jcmd) - 1] = '\0';
              jidx = strlen(jcmd);
              lines[0] = jcmd;
              mess = lines;
            }
            qiv_display_multiline_window(q, "(Expanded command)", mess,
                                         "Press <Return> to send, <Esc> to abort"); // [lc]
          } else if (lines && numlines) {
            switch_to_confirm_mode(q, lines, FALSE);
            jcmd[jidx = 0] = '\0';
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
            break;

            /* Exit */

          case GDK_Escape:
          case 'q':  /* Also matches Ctrl-<Q>, Alt-<Q> etc. */
            qiv_exit(0);
            break;

            /* Fullscreen mode (on/off) */

          case 'f':
            exit_slideshow = FALSE;
            gdk_window_withdraw(q->win);
            show_cursor(q);
            fullscreen ^= 1;
            first=1;
            qiv_load_image(q);
            break;

            /* Center mode (on/off) */

          case 'e':
            exit_slideshow = FALSE;
            center ^= 1;
            snprintf(infotext, sizeof infotext, center ?
                     "(Centering: on)" : "(Centering: off)");
            if (center) center_image(q);
            update_image(q, MOVED);
            break;

            /* Transparency on/off */

          case 'p':
            exit_slideshow = FALSE;
            transparency ^= 1;
            snprintf(infotext, sizeof infotext, transparency ?
                     "(Transparency: on)" : "(Transparency: off)");
            update_image(q, FULL_REDRAW);
            break;

            /* Maxpect on/off */

          case 'm':
            scale_down = 0;
            maxpect ^= 1;
            snprintf(infotext, sizeof infotext, maxpect ?
                     "(Maxpect: on)" : "(Maxpect: off)");
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
            snprintf(infotext, sizeof infotext, random_order ?
                     "(Random order: on)" : "(Random order: off)");
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
              snprintf(infotext, sizeof infotext, statusbar_fullscreen ?
                       "(Statusbar: on)" : "(Statusbar: off)");
            } else {
              statusbar_window ^= 1;
              snprintf(infotext, sizeof infotext, statusbar_window ?
                       "(Statusbar: on)" : "(Statusbar: off)");
            }
            update_image(q, REDRAW);
            break;

            /* Slide show on/off */

          case 's':
            exit_slideshow = FALSE;
            slide ^= 1;
            snprintf(infotext, sizeof infotext, slide ?
                     "(Slideshow: on)" : "(Slideshow: off)");
            update_image(q, REDRAW);
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
                  snprintf(infotext, sizeof infotext, "(Moving right)");
                } else {
                  snprintf(infotext, sizeof infotext, "(Cannot move further to the right)");
                }

              } else {                      /* user is just playing around */

                /* right border reached? */
                if (q->win_x + q->win_w < screen_x) {
                  q->win_x += move_step;
                  /* sanity check */
                  if (q->win_x + q->win_w > screen_x)
                    q->win_x = screen_x - q->win_w;
                  snprintf(infotext, sizeof infotext, "(Moving right)");
                } else {
                  snprintf(infotext, sizeof infotext, "(Cannot move further to the right)");
                }
              }
            } else {
              snprintf(infotext, sizeof infotext, "(Moving works only in fullscreen mode)");
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
                  snprintf(infotext, sizeof infotext, "(Moving left)");
                } else {
                  snprintf(infotext, sizeof infotext, "(Cannot move further to the left)");
                }

              } else {                      /* user is just playing around */

                /* left border reached? */
                if (q->win_x > 0) {
                  q->win_x -= move_step;
                  /* sanity check */
                  if (q->win_x < 0)
                    q->win_x = 0;
                  snprintf(infotext, sizeof infotext, "(Moving left)");
                } else {
                  snprintf(infotext, sizeof infotext, "(Cannot move further to the left)");
                }
              }
            } else {
              snprintf(infotext, sizeof infotext, "(Moving works only in fullscreen mode)");
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
                  snprintf(infotext, sizeof infotext, "(Moving up)");
                } else {
                  snprintf(infotext, sizeof infotext, "(Cannot move further up)");
                }

              } else {                      /* user is just playing around */

                /* top reached? */
                if (q->win_y > 0) {
                  q->win_y -= move_step;
                  /* sanity check */
                  if (q->win_y < 0)
                    q->win_y = 0;
                  snprintf(infotext, sizeof infotext, "(Moving up)");
                } else {
                  snprintf(infotext, sizeof infotext, "(Cannot move further up)");
                }
              }
            } else {
              snprintf(infotext, sizeof infotext, "(Moving works only in fullscreen mode)");
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
                  snprintf(infotext, sizeof infotext, "(Moving down)");
                } else {
                  snprintf(infotext, sizeof infotext, "(Cannot move further down)");
                }

              } else {                      /* user is just playing around */

                /* bottom reached? */
                if (q->win_y + q->win_h < screen_y) {
                  q->win_y += move_step;
                  /* sanity check */
                  if (q->win_y + q->win_h > screen_y)
                    q->win_y = screen_y - q->win_h;
                  snprintf(infotext, sizeof infotext, "(Moving down)");
                } else {
                  snprintf(infotext, sizeof infotext, "(Cannot move further down)");
                }
              }
            } else {
              snprintf(infotext, sizeof infotext, "(Moving works only in fullscreen mode)");
              fprintf(stdout, "qiv: Moving works only in fullscreen mode\n");
            }
            update_image(q, MOVED);
            break;

            /* Scale_down */

          case 't':
            maxpect = 0;
            scale_down ^= 1;
            snprintf(infotext, sizeof infotext, scale_down ?
                     "(Scale down: on)" : "(Scale down: off)");
            zoom_factor = maxpect ? 0 : fixed_zoom_factor;  /* reset zoom */
            check_size(q, TRUE);
            update_image(q, REDRAW);
            break;

            /* Resize + */

          case GDK_KP_Add:
          case '+':
          case '=':
            snprintf(infotext, sizeof infotext, "(Zoomed in)");
            zoom_in(q);
            update_image(q, ZOOMED);
            break;

            /* Resize - */

          case GDK_KP_Subtract:
          case '-':
            snprintf(infotext, sizeof infotext, "(Zoomed out)");
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
              snprintf(infotext, sizeof infotext, "(Reset size)");
              reload_image(q);
              zoom_factor = fixed_zoom_factor;  /* reset zoom */
              check_size(q, TRUE);
              update_image(q, REDRAW);
            }
            break;

            /* Next picture - or loop to the first */

          case ' ':
          next_image:
            if  (ev->key.state & GDK_CONTROL_MASK) {
              snprintf(infotext, sizeof infotext, "(Next picture directory)");
              next_image_dir(1);
            } else {
              snprintf(infotext, sizeof infotext, "(Next picture)");
              next_image(1);
            }
            if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
            qiv_load_image(q);
            break;

            /* 5 pictures forward - or loop to the beginning */

          case GDK_Page_Down:
          case GDK_KP_Page_Down:
            snprintf(infotext, sizeof infotext, "(5 pictures forward)");
            next_image(5);
            if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
            qiv_load_image(q);
            break;

            /* Previous picture - or loop back to the last */

          case GDK_BackSpace:
          previous_image:
            if  (ev->key.state & GDK_CONTROL_MASK) {
              snprintf(infotext, sizeof infotext, "(Previous picture directory)");
              next_image_dir(-1);
            } else {
              snprintf(infotext, sizeof infotext, "(Previous picture)");
              next_image(-1);
            }
            if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
            qiv_load_image(q);
            break;

            /* 5 pictures backward - or loop back to the last */

          case GDK_Page_Up:
          case GDK_KP_Page_Up:
            snprintf(infotext, sizeof infotext, "(5 pictures backward)");
            next_image(-5);
            if(magnify && !fullscreen)    gdk_window_hide(magnify_img.win); // [lc]
            qiv_load_image(q);
            break;

            /* + brightness */

          case 'B':
            snprintf(infotext, sizeof infotext, "(More brightness)");
            q->mod.brightness += 8;
            update_image(q, REDRAW);
            break;

            /* - brightness */

          case 'b':
            snprintf(infotext, sizeof infotext, "(Less brightness)");
            q->mod.brightness -= 8;
            update_image(q, REDRAW);
            break;

            /* + contrast */

          case 'C':
            snprintf(infotext, sizeof infotext, "(More contrast)");
            q->mod.contrast += 8;
            update_image(q, REDRAW);
            break;

            /* - contrast */

          case 'c':
            snprintf(infotext, sizeof infotext, "(Less contrast)");
            q->mod.contrast -= 8;
            update_image(q, REDRAW);
            break;

            /* + gamma */

          case 'G':
            snprintf(infotext, sizeof infotext, "(More gamma)");
            q->mod.gamma += 8;
            update_image(q, REDRAW);
            break;

            /* - gamma */

          case 'g':
            snprintf(infotext, sizeof infotext, "(Less gamma)");
            q->mod.gamma -= 8;
            update_image(q, REDRAW);
            break;

            /* - reset brightness, contrast and gamma */

          case 'o':
            snprintf(infotext, sizeof infotext, "(Reset bri/con/gam)");
            reset_mod(q);
            update_image(q, REDRAW);
            break;

            /* Delete image */

          case GDK_Delete:
          case 'd':
            if (!readonly) {
              if (move2trash() == 0)
                snprintf(infotext, sizeof infotext, "(Deleted last image)");
              else
                snprintf(infotext, sizeof infotext, "(Delete FAILED)");
              qiv_load_image(q);
            }
            break;

            /* Undelete image */

          case 'u':
            if (!readonly) {
              if (undelete_image() == 0)
                snprintf(infotext, sizeof infotext, "(Undeleted)");
              else
                snprintf(infotext, sizeof infotext, "(Undelete FAILED)");
              qiv_load_image(q);
            }
            break;

            /* Copy image to selected directory */

          case 'a':
            if (copy2select() == 0)
              snprintf(infotext, sizeof infotext, "(Last image copied)");
            else
              snprintf(infotext, sizeof infotext, "(Selection FAILED)");
            next_image(1);
            qiv_load_image(q);
            break;

            /* Jump to image */

          case 'j':
            /* True: assert(qiv_mode == NORMAL); */
            qiv_mode = JUMP_EDIT;
            jcmd[jidx = 0] = '\0';
            break;

            /* Flip horizontal */

          case 'h':
            imlib_image_flip_horizontal();
            snprintf(infotext, sizeof infotext, "(Flipped horizontally)");
            update_image(q, REDRAW);
            break;

            /* Flip vertical */

          case 'v':
            imlib_image_flip_vertical();
            snprintf(infotext, sizeof infotext, "(Flipped vertically)");
            update_image(q, REDRAW);
            break;

            /* Watch File (on/off) */

          case 'w':
            watch_file ^= 1;
            snprintf(infotext, sizeof infotext, watch_file ?
                     "(File watching: on)" : "(File watching: off)");
            update_image(q, REDRAW);
            if(watch_file){
              g_idle_add (qiv_watch_file, q);
            }
            break;

            /* Rotate right */

          case 'k':
            imlib_image_orientate(1);
            snprintf(infotext, sizeof infotext, "(Rotated right)");
            swap(&q->orig_w, &q->orig_h);
            swap(&q->win_w, &q->win_h);
            check_size(q, FALSE);
            correct_image_position(q);
            update_image(q, REDRAW);
            break;

            /* Rotate left */

          case 'l':
            imlib_image_orientate(3);
            snprintf(infotext, sizeof infotext, "(Rotated left)");
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
            snprintf(infotext, sizeof infotext, "(Centered image on background)");
            update_image(q, REDRAW);
            to_root=0;
            break;

            /* Tile image on background */

          case 'y':
            to_root_t=1;
            set_desktop_image(q);
            snprintf(infotext, sizeof infotext, "(Tiled image on background)");
            update_image(q, REDRAW);
            to_root_t=0;
            break;

          case 'z':
            to_root_s=1;
            set_desktop_image(q);
            snprintf(infotext, sizeof infotext, "(Stretched image on background)");
            update_image(q, REDRAW);
            to_root_s=0;
            break;


            /* Decrease slideshow delay */
          case GDK_F11:
            if (do_f_commands) goto do_f_command;
            exit_slideshow = FALSE;
            if (delay > 1000) {
              delay-=1000;
              snprintf(infotext, sizeof infotext, "(Slideshow-Delay: %d seconds (-1)", delay/1000);
              update_image(q,MOVED);
            }else{
              snprintf(infotext, sizeof infotext, "(Slideshow-Delay: can not be less than 1 second!)");
              update_image(q,MOVED);
            }
            break;

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

            /* Increase slideshow delay */
          case GDK_F12:
            if (do_f_commands) goto do_f_command;
            exit_slideshow = FALSE;
            delay+=1000;
            snprintf(infotext, sizeof infotext, "(Slideshow-Delay: %d seconds (+1)", delay/1000);
            update_image(q,MOVED);
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
            snprintf(infotext, sizeof infotext,
                     "(xinerama screen: %i)", user_screen);
            if (center) center_image(q);
            update_image(q, FULL_REDRAW);
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
            jcmd[jidx = 0] = '\0';
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

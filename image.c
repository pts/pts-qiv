/*
  Module       : image.c
  Purpose      : Routines dealing with image display
  More         : see qiv README
  Policy       : GNU GPL
  Homepage     : http://qiv.spiegl.de/
  Original     : http://www.klografx.net/qiv/
*/

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qiv.h"
#include "xmalloc.h"

static void setup_win(qiv_image *);
//static void setup_magnify(qiv_image *, qiv_mgl *); // [lc]
static int used_masks_before=0;
static double load_elapsed;
static GdkCursor *cursor, *visible_cursor, *invisible_cursor;

#ifdef HAVE_EXIF
//////////// these taken from gwenview/src/imageutils/ //////////////////
//
/* Explanation extracted from http://sylvana.net/jpegcrop/exif_orientation.html

   For convenience, here is what the letter F would look like if it were tagged
correctly and displayed by a program that ignores the orientation tag (thus
showing the stored image):

  1        2       3      4         5            6           7          8

888888  888888      88  88      8888888888  88                  88  8888888888
88          88      88  88      88  88      88  88          88  88      88  88
8888      8888    8888  8888    88          8888888888  8888888888          88
88          88      88  88
88          88  888888  888888

NORMAL  HFLIP   ROT_180 VFLIP   TRANSPOSE   ROT_90      TRANSVERSE  ROT_270
*/

enum Orientation {
    NOT_AVAILABLE=0,
    NORMAL  =1,
    HFLIP   =2,
    ROT_180 =3,
    VFLIP   =4,
    TRANSPOSE   =5,
    ROT_90  =6,
    TRANSVERSE  =7,
    ROT_270 =8
};

// Exif
#include "libexif/exif-data.h"

#define flipH(q)    imlib_image_flip_horizontal();
#define flipV(q)    imlib_image_flip_vertical();
#define transpose(q) imlib_image_flip_diagonal();
#define rot90(q)    imlib_image_orientate(1);
#define rot180(q)   imlib_image_orientate(2);
#define rot270(q)   imlib_image_orientate(3);
#define swapWH(q)  swap(&q->orig_w, &q->orig_h); swap(&q->win_w, &q->win_h);
void transform( qiv_image *q, enum Orientation orientation) {
    switch (orientation) {
     default: return;
     case HFLIP:     flipH(q); snprintf(infotext, sizeof infotext, "(Flipped horizontally)"); break;
     case VFLIP:     flipV(q); snprintf(infotext, sizeof infotext, "(Flipped vertically)"); break;
     case ROT_180:   rot180(q); snprintf(infotext, sizeof infotext, "(Turned upside down)"); break;
      case TRANSPOSE: transpose(q); swapWH(q); snprintf(infotext, sizeof infotext, "(Transposed)"); break;
     case ROT_90:    rot90(q); swapWH(q); snprintf(infotext, sizeof infotext, "(Rotated left)"); break;
     case TRANSVERSE: transpose(q); rot180(q); swapWH(q); snprintf(infotext, sizeof infotext, "(Transversed)"); break;
     case ROT_270:   rot270(q); swapWH(q); snprintf(infotext, sizeof infotext, "(Rotated left)"); break;
    }
}

//#include "libexif/exif-tag.h"   //EXIF_TAG_ORIENTATION
enum Orientation orient( const char * path) {
    enum Orientation orientation = NOT_AVAILABLE;

    ExifData * mExifData = exif_data_new_from_file( path);
    if (mExifData) {
        ExifEntry * mOrientationEntry = exif_content_get_entry( mExifData->ifd[ EXIF_IFD_0], EXIF_TAG_ORIENTATION);
        if (mOrientationEntry) {
            ExifByteOrder mByteOrder = exif_data_get_byte_order( mExifData);
            short value=exif_get_short( mOrientationEntry->data, mByteOrder);
            if (value>=NORMAL && value<=ROT_270)
                orientation = value;    //Orientation( value);
        }
        exif_data_unref( mExifData );
    }
    return orientation;
}
#endif  //HAVE_EXIF

/* The caller takes ownership of the returned value. */
static char* get_thumbnail_filename(const char *filename, char *is_maybe_image_file_out) {
  const char *r;
  const char *p;
  char *thumbnail_filename;
  char *tmp_filename = NULL;
  size_t prefixlen;
  struct stat st;
  char link_target[512];
  ssize_t readlink_result;
  char do_try_readlink = 1;

 again:
  p = r = filename + strlen(filename);
  /* Replace image extension with .th.jpg, save result to thumbnail_filename */
  while (p != filename && p[-1] != '/' && p[-1] != '.') {
    --p;
  }
  prefixlen = (p != filename && p[-1] == '.') ?
              p - filename - 1 : r - filename;
  thumbnail_filename = xmalloc(prefixlen + 8);
  memcpy(thumbnail_filename, filename, prefixlen);
  strcpy(thumbnail_filename + prefixlen, ".th.jpg");
  free(tmp_filename);  /* From this point `filename' is useless */

  if (0 == stat(thumbnail_filename, &st) && S_ISREG(st.st_mode)) {
    return thumbnail_filename;
  }
  free(thumbnail_filename);

  if (!do_try_readlink) {
    /* Already followed the .git/annex/object symlink, don't try to follow another symlink. */
  } else if ((readlink_result = readlink(filename, link_target, sizeof(link_target))) > 0 &&
             (size_t)readlink_result < sizeof(link_target)) {
    /* It's a symlink and it's not too long. For example (198 bytes):
     * ../.git/annex/objects/G1/mX/SHA256E-s12345--aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.jpg/SHA256E-s12345--aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.jpg
     */
    static const char prefix[] = ".git/annex/objects/";
    char *q;
    do_try_readlink = 0;
    link_target[readlink_result] = '\0';
    for (q = link_target; q[0] == '.' && q[1] == '.' && q[2] == '/'; q += 3) {}
    /* Found an object in the git-annex annex? */
    if (0 == strncmp(q, prefix, sizeof(prefix) - 1)) {
      size_t i;
      /* Strip the basename from the end: filename[:i]. */
      for (i = strlen(filename); i > 0 && filename[i - 1] != '/'; --i) {}
      tmp_filename = xmalloc(i + strlen(link_target) + 1);
      memcpy(tmp_filename, filename, i);
      strcpy(tmp_filename + i, link_target);
      filename = tmp_filename;  /* Look for the thumbnail there. */
      goto again;
    }
  } else if (readlink_result < 0 && errno == ENOENT) {
    *is_maybe_image_file_out = 0;
  }

  return NULL;
}

/* Get the dimensionsions from the REALDIMEN: comment in the thumbnail
 * image.
 *
 * It's very fast, because it reads only the image headers quickly.
 * Currently it supports only JPEG thumbnails. It may have to read 30 kB of
 * data to find the COM segment.
 */
static int get_real_dimensions_fast(
    FILE *f, gint *w_out, gint *h_out) {
  static const char realdimen_prefix[] = "REALDIMEN:";
  char comment[256];
  int c, m;
  unsigned i, ss;
  if ((c = getc(f)) < 0) return -2;  /* Truncated (empty). */
  if (c != 0xff) return -3;
  if ((c = getc(f)) < 0) return -2;  /* Truncated. */
  if (c != 0xd8) return -3;  /* Not a JPEG file, SOI expected. */
  for (;;) {
    if ((c = getc(f)) < 0) return -2;  /* Truncated. */
    if (c != 0xff) return -3;  /* Not a JPEG file, marker expected. */
    if ((m = getc(f)) < 0) return -2;  /* Truncated. */
    while (m == 0xff) {  /* Padding. */
      if ((m = getc(f)) < 0) return -2;  /* Truncated. */
    }
    if (m == 0xd8) return -4;  /* SOI unexpected. */
    if (m == 0xd9) break;  /* EOI. */
    if (m == 0xda) break;  /* SOS. Would need special escaping to process. */
    if ((c = getc(f)) < 0) return -2;  /* Truncated. */
    ss = (c + 0U) << 8;
    if ((c = getc(f)) < 0) return -2;  /* Truncated. */
    ss += c;
    if (ss < 2) return -5;  /* Segment too short. */
    ss -= 2;
    if (m == 0xfe && ss < sizeof(comment)) {
      for (i = 0; i < ss; ++i) {
        if ((c = getc(f)) < 0) return -2;  /* Truncated. */
        comment[i] = c;
      }
      comment[i] = '\0';
      if (strncmp(comment, realdimen_prefix,
                  sizeof(realdimen_prefix) - 1) == 0) {
        int w, h;
        if (sscanf(comment + sizeof(realdimen_prefix) - 1,
                   "%dx%d", &w, &h) == 2) {
          *w_out = w;
          *h_out = h;
          return 0;
        } else {
          return -7;  /* Bad REALDIMEN: syntax. */
        }
      }
      *w_out = *h_out = -2;
    } else {
      for (; ss > 0; --ss) {
        if ((c = getc(f)) < 0) return -2;  /* Truncated. */
      }
    }
  }
  return -6;  /* REALDIMEN: comment not found. */
}

/* Populates *w_out and *h_out with the dimensions of the image file
 * specified in image_name. On an error, returns a negative number and keeps
 * w_out and h_out unset.
 *
 * It's very fast, because it reads only the image headers quickly. Currently
 * it supports JPEG, PNG, GIF and BMP. For other file formats, it returns -6.
 * (For JPEG it may have to read 30 kB of data to find the SOF segment.)
 *
 * As a side effect, reads sequentially from f.
 */
static int get_image_dimensions_fast(
    FILE *f, gint *w_out, gint *h_out) {
  /* We could read the beginning of the image file to memory, but we'd need
   * at least 30 kB of memory for JPEG files, because they have the SOF0 (C0)
   * marker after comments. So we don't do that, but we use getc instead.
   */
  int c, m;
  unsigned ss, i;
  char head[26];
  for (i = 0; i < 4; ++i) {
    if ((c = getc(f)) < 0) return -2;  /* Truncated in header. */
    head[i] = c;
  }
  if (0 == memcmp(head, "\xff\xd8\xff", 3)) {  /* JPEG. */
    /* A typical JPEG file has markers in these order:
     *   d8 e0_JFIF e1 e1 e2 db db fe fe c0 c4 c4 c4 c4 da d9.
     *   The first fe marker (COM, comment) was near offset 30000.
     * A typical JPEG file after filtering through jpegtran:
     *   d8 e0_JFIF fe fe db db c0 c4 c4 c4 c4 da d9.
     *   The first fe marker (COM, comment) was at offset 20.
     */
    m = head[3];
    goto start_jpeg;
    for (;;) {
      /* printf("@%ld\n", ftell(f)); */
      if ((c = getc(f)) < 0) return -2;  /* Truncated. */
      if (c != 0xff) return -3;  /* Not a JPEG file, marker expected. */
      if ((m = getc(f)) < 0) return -2;  /* Truncated. */
     start_jpeg:
      while (m == 0xff) {  /* Padding. */
        if ((m = getc(f)) < 0) return -2;  /* Truncated. */
      }
      if (m == 0xd8) return -4;  /* SOI unexpected. */
      if (m == 0xd9) return -8;  /* EOI unexpected before SOF. */
      if (m == 0xda) return -9;  /* SOS unexpected before SOF. */
      /* printf("MARKER 0x%02x\n", m); */
      if ((c = getc(f)) < 0) return -2;  /* Truncated. */
      ss = (c + 0U) << 8;
      if ((c = getc(f)) < 0) return -2;  /* Truncated. */
      ss += c;
      if (ss < 2) return -5;  /* Segment too short. */
      /* SOF0 ... SOF15. */
      if (0xc0 <= m && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
        ss -= 2;
        if (ss < 5) return -7;  /* SOF segment too short. */
        for (i = 0; i < 5; ++i) {
          if ((c = getc(f)) < 0) return -2;  /* Truncated. */
          head[i] = c;
        }
        *w_out = (unsigned char)head[3] << 8 | (unsigned char)head[4];
        *h_out = (unsigned char)head[1] << 8 | (unsigned char)head[2];
        return 0;
      } else {
        for (ss -= 2; ss > 0; --ss) {
          if ((c = getc(f)) < 0) return -2;  /* Truncated. */
        }
      }
    }
    return -10;  /* Internal error, shouldn't be reached. */
  } else if (0 == memcmp(head, "BM", 2)) {  /* BMP. */
    for (; i < 26; ++i) {
      if ((c = getc(f)) < 0) return -2;  /* Truncated. */
      head[i] = c;
    }
    switch ((unsigned char)head[14]) {
     case 12: case 64:
      *w_out = (unsigned char)head[18] | (unsigned char)head[19] << 8;
      *h_out = (unsigned char)head[20] | (unsigned char)head[21] << 8;
      break;
     case 40: case 124:
      *w_out = (unsigned char)head[18]       | (unsigned char)head[19] << 8 |
               (unsigned char)head[20] << 16 | (unsigned char)head[21] << 24;
      *h_out = (unsigned char)head[22] | (unsigned char)head[23] << 8 |
               (unsigned char)head[24] << 16 | (unsigned char)head[25] << 8;
      break;
     default:  /* case 0x7C: BMP with colorspace info. */
      return -12;  /* Unrecognized BMP. */
    }
    return 0;
  } else if (0 == memcmp(head, "GIF8", 4)) {  /* GIF. */
    for (; i < 10; ++i) {
      if ((c = getc(f)) < 0) return -2;  /* Truncated. */
      head[i] = c;
    }
    if (0 != memcmp(head, "GIF87a", 6) && 0 != memcmp(head, "GIF89a", 6)) {
      return -13;  /* Invalid GIF header. */
    }
    *w_out = (unsigned char)head[6] | (unsigned char)head[7] << 8;
    *h_out = (unsigned char)head[8] | (unsigned char)head[9] << 8;
    return 0;
  } else if (0 == memcmp(head, "\211PNG", 4)) {  /* GIF. */
    for (; i < 24; ++i) {
      if ((c = getc(f)) < 0) return -2;  /* Truncated. */
      head[i] = c;
    }
    if ((unsigned char)head[11] - 015U > 062U) {
      return -14;  /* Invalid PNG header. */
    }
    head[11] = 0;
    if (0 != memcmp(head, "\211PNG\r\n\032\n\0\0\0\0IHDR", 16)) {
      return -14;  /* Invalid PNG header. */
    }
    *w_out = (unsigned char)head[19]       | (unsigned char)head[18] << 8 |
             (unsigned char)head[17] << 16 | (unsigned char)head[16] << 24;
    *h_out = (unsigned char)head[23] | (unsigned char)head[22] << 8 |
             (unsigned char)head[21] << 16 | (unsigned char)head[20] << 8;
    return 0;
  }
  return -6;  /* Unrecognized file format. */
}

/* Populates *w_out and *h_out with the dimensions of the image file
 * specified in image_name. On an error, sets *w_out = *h_out = -1.
 *
 * If th_image_name is not NULL, then before reading image_name, it tries to
 * get the dimensions of the real image from the REALDIMEN: comment in the
 * thumbnail image th_image_name.
 *
 * It's usually quite fast, because it reads only the image headers (with
 * imlib_load_image, or with a custom, faster code for JPEG files).
 *
 * As a side effect, may destroy the image in the Imlib2 image context (by
 * calling imlib_free_image()).
 */
static void get_image_dimensions(
    const char *image_name, const char *th_image_name,
    gint *w_out, gint *h_out) {
  Imlib_Image *im_orig;
  FILE *f;
  if (th_image_name && (f = fopen(th_image_name, "rb")) != NULL) {
    if (get_real_dimensions_fast(f, w_out, h_out) == 0) { fclose(f); return; }
    fclose(f);
  }
  if ((f = fopen(image_name, "rb")) != NULL) {
    /* Faster than imlib_load_image on same (rare) JPEG files. */
    if (get_image_dimensions_fast(f, w_out, h_out) == 0) { fclose(f); return; }
    fclose(f);
  }
  if ((im_orig = imlib_load_image(image_name)) != NULL) {
    imlib_context_set_image(im_orig);
    *w_out = imlib_image_get_width();
    *h_out = imlib_image_get_height();
    imlib_free_image();
  } else {
    *w_out = *h_out = -1;
  }
}

static void update_image_on_error(qiv_image *q);

/*
 *    Load & display image
 */
void qiv_load_image(qiv_image *q) {
  /* Don't initialize most variables here, initialize them after load_next_image: */
  struct stat st;
  const char *image_name;
  Imlib_Image *im;
  struct timeval load_before, load_after;

  char is_stat_ok;
  /* Used to omit slow disk operations if image_file doesn't exist or isn't
   * a file.
   */
  char is_maybe_image_file;
  char is_first_error = 1;

 load_next_image:
  is_stat_ok = 0;
  is_maybe_image_file = 1;
  image_name = image_names[image_idx];
  gettimeofday(&load_before, 0);

  if (imlib_context_get_image())
    imlib_free_image();

  q->real_w = q->real_h = -2;
  q->has_thumbnail = FALSE;
  if (!do_omit_load_stat) {
    is_stat_ok = 0 == stat(image_name, &st);
    is_maybe_image_file = is_stat_ok && S_ISREG(st.st_mode);
  }
  current_mtime = is_stat_ok ? st.st_mtime : 0;
  im = NULL;
  if (thumbnail && fullscreen && (is_stat_ok || maxpect)) {
    char *th_image_name =
        is_maybe_image_file ?
        get_thumbnail_filename(image_name, &is_maybe_image_file) : NULL;
    if (th_image_name) {
      im = imlib_load_image(th_image_name);
      if (im && maxpect) {
        get_image_dimensions(image_name, th_image_name,
                             &q->real_w, &q->real_h);
      }
      free(th_image_name);
      th_image_name = NULL;
    }
  }
  if (im) {  /* We have a dumb thumbnail in im. */
    if (maxpect) {
      current_mtime = 0;
      q->has_thumbnail = TRUE;
      /* Now im still has the thumbnail image. Keep it. */
    } else {  /* Use the real, non-thumbnail image instead. */
      imlib_context_set_image(im);
      imlib_free_image();
      im = is_maybe_image_file ? imlib_load_image((char*)image_name) : NULL;
    }
  } else {
    im = is_maybe_image_file ? imlib_load_image((char*)image_name) : NULL;
  }

  if (!im) { /* error */
    q->error = 1;
    q->orig_w = 400;
    q->orig_h = 300;

    if (to_root || to_root_t || to_root_s) {
      fprintf(stderr, "qiv: cannot load background_image\n");
      qiv_exit(1);
    }

    /* Shortcut to speed up lots of subsequent load failures. */
    if (is_first_error) {
      check_size(q, TRUE);
      if (first) {
        setup_win(q);
        first = 0;
      }
      gdk_window_set_background(q->win, &error_bg);
      gdk_beep();
      is_first_error = 0;
    }

    /* TODO(pts): Avoid the slow loop of copying pointers around in update_image_on_error. */
    update_image_on_error(q);
    /* This is a shortcut to avoid stack overflow in the recursion of
     * qiv_load_image -> update_image -> qiv_load_image -> update_image -> ...
     * if there are errors loading many subsequent images.
     */
    goto load_next_image;
  }

  if (thumbnail && !q->has_thumbnail && q->real_w < 0 && is_maybe_image_file) {
    FILE *f = fopen(image_name, "rb");
    if (f) {
      get_real_dimensions_fast(f, &q->real_w, &q->real_h);
      fclose(f);
    }
  }

  /* Retrieve image properties */
  imlib_context_set_image(im);
  q->error = 0;
  q->orig_w = imlib_image_get_width();
  q->orig_h = imlib_image_get_height();
  if (q->orig_w >= (1 << 23) / q->orig_h) {
    /* Workaround for Imlib2 1.4.6 on Ubuntu Trusty: PNG images with an
     * alpha channel and pixel count >= (1 << 23) are displayed as black.
     * imlib_image_query_pixel returns the correct value, but
     * imlib_render_pixmaps_for_whole_image_at_size renders only black
     * pixels if unzoomed.
     */
    Imlib_Color c;
    /* Without this call, imlib_image_set_has_alpha(0) is too early, and
     * it has no effect.
     */
    imlib_image_query_pixel(0, 0, &c);
    imlib_image_set_has_alpha(0);
  }
#ifdef HAVE_EXIF
  if (autorotate) {
    transform( q, orient( image_name));
  }
#endif

  check_size(q, TRUE);

  if (first) {
    setup_win(q);
    first = 0;
  }

  /* desktop-background -> exit */
  if (to_root || to_root_t || to_root_s) {
    set_desktop_image(q);
    if(slide)
      return;
    else
      qiv_exit(0);
  }

  gdk_window_set_background(q->win, &image_bg);

  if (do_grab || (fullscreen && !disable_grab) ) {
    gdk_keyboard_grab(q->win, FALSE, CurrentTime);
    gdk_pointer_grab(q->win, FALSE,
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_ENTER_NOTIFY_MASK |
      GDK_LEAVE_NOTIFY_MASK | GDK_POINTER_MOTION_MASK, NULL, NULL, CurrentTime);
  }

  gettimeofday(&load_after, 0);
  /* load_elapsed used by update_image. */
  load_elapsed = ((load_after.tv_sec +  load_after.tv_usec / 1.0e6) -
                 (load_before.tv_sec + load_before.tv_usec / 1.0e6));

  update_image(q, FULL_REDRAW);
//    if (magnify && !fullscreen) {  // [lc]
//     setup_magnify(q, &magnify_img);
//     update_magnify(q, &magnify_img, FULL_REDRAW, 0, 0);
//    }
}

static gchar blank_cursor[1];

static void setup_imlib_for_drawable(GdkDrawable * d)
{
  imlib_context_set_dither(1); /* dither for depths < 24bpp */
  imlib_context_set_display(
    gdk_x11_drawable_get_xdisplay(d));
  imlib_context_set_visual(
    gdk_x11_visual_get_xvisual(gdk_drawable_get_visual(d)));
  imlib_context_set_colormap(
    gdk_x11_colormap_get_xcolormap(gdk_drawable_get_colormap(d)));
  imlib_context_set_drawable(
    gdk_x11_drawable_get_xid(d));
}

static void setup_imlib_color_modifier(qiv_color_modifier q)
{
  if (q.gamma == DEFAULT_GAMMA &&
      q.brightness == DEFAULT_BRIGHTNESS &&
      q.contrast == DEFAULT_CONTRAST) {
    if (imlib_context_get_color_modifier())
      imlib_free_color_modifier();
    return;
  }

  if (imlib_context_get_color_modifier())
    imlib_reset_color_modifier();
  else
    imlib_context_set_color_modifier(imlib_create_color_modifier());

  imlib_modify_color_modifier_gamma(q.gamma / 256.0);
  imlib_modify_color_modifier_brightness((q.brightness - 256) / 256.0);
  imlib_modify_color_modifier_contrast(q.contrast / 256.0);
}

static void setup_win(qiv_image *q)
{
  GdkWindowAttr attr;
  GdkPixmap *cursor_pixmap;

  if (!fullscreen) {
    attr.window_type=GDK_WINDOW_TOPLEVEL;
    attr.wclass=GDK_INPUT_OUTPUT;
    attr.event_mask=GDK_ALL_EVENTS_MASK;
    attr.x = center ? q->win_x : 0;
    attr.y = center ? q->win_y : 0;
    attr.width  = q->win_w;
    attr.height = q->win_h;
    q->win = gdk_window_new(NULL, &attr, GDK_WA_X|GDK_WA_Y);

    if (center) {
      GdkGeometry geometry = {
        .min_width = q->win_w,
        .min_height = q->win_h,
        .max_width = q->win_w,
        .max_height = q->win_h,
        .win_gravity = GDK_GRAVITY_STATIC
      };
      gdk_window_set_geometry_hints(q->win, &geometry,
        GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE | GDK_HINT_WIN_GRAVITY);
      gdk_window_move_resize(q->win, q->win_x, q->win_y, q->win_w, q->win_h);
    } else {
      GdkGeometry geometry = {
        .min_width = q->win_w,
        .min_height = q->win_h,
        .max_width = q->win_w,
        .max_height = q->win_h,
      };
      gdk_window_set_geometry_hints(q->win, &geometry,
        GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
      gdk_window_resize(q->win, q->win_w, q->win_h);
    }

    gdk_window_show(q->win);

  } else { /* fullscreen */

    attr.window_type=GDK_WINDOW_TEMP;
    attr.wclass=GDK_INPUT_OUTPUT;
    attr.event_mask=GDK_ALL_EVENTS_MASK;
    attr.x = attr.y = 0;
    attr.width=screen_x;
    attr.height=screen_y;
    q->win = gdk_window_new(NULL, &attr, GDK_WA_X|GDK_WA_Y);
    gdk_window_set_cursor(q->win, cursor);
    gdk_window_show(q->win);
  }

  q->bg_gc = gdk_gc_new(q->win);
  q->text_gc = gdk_gc_new(q->win); /* black is default */
  q->status_gc = gdk_gc_new(q->win);
  gdk_gc_set_foreground(q->bg_gc, &image_bg);
  gdk_gc_set_foreground(q->status_gc, &text_bg);

  cursor_pixmap = gdk_bitmap_create_from_data(q->win, blank_cursor, 1, 1);
  invisible_cursor = gdk_cursor_new_from_pixmap(cursor_pixmap, cursor_pixmap,
                                                &text_bg, &text_bg, 0, 0);
  cursor = visible_cursor = gdk_cursor_new(CURSOR);
  gdk_window_set_cursor(q->win, cursor);

  setup_imlib_for_drawable(GDK_DRAWABLE(q->win));
}

void hide_cursor(qiv_image *q)
{
  if (cursor != invisible_cursor)
    gdk_window_set_cursor(q->win, cursor = invisible_cursor);
}

void show_cursor(qiv_image *q)
{
  if (cursor != visible_cursor)
    gdk_window_set_cursor(q->win, cursor = visible_cursor);
}

/* set image as background */

void set_desktop_image(qiv_image *q)
{
  GdkWindow *root_win = gdk_get_default_root_window();
  GdkVisual *gvis = gdk_drawable_get_visual(root_win);
  GdkPixmap *temp;
  gchar     *buffer;

  gint root_w = screen_x, root_h = screen_y;
  gint root_x = 0, root_y = 0;

  Pixmap x_pixmap, x_mask;

  if (to_root || to_root_t) {
    root_w = q->win_w;
    root_h = q->win_h;
  }

  if (to_root) {
    root_x = (screen_x - root_w) / 2;
    root_y = (screen_y - root_h) / 2;
  }

  setup_imlib_for_drawable(GDK_DRAWABLE(root_win));

  imlib_render_pixmaps_for_whole_image_at_size(&x_pixmap, &x_mask, root_w, root_h);
#ifdef DEBUG
  if (x_mask)  g_print("*** image has transparency\n");
#endif

  if(x_pixmap) {
    GdkPixmap * p = gdk_pixmap_foreign_new(x_pixmap);
    gdk_drawable_set_colormap(GDK_DRAWABLE(p),
			      gdk_drawable_get_colormap(GDK_DRAWABLE(root_win)));
    if (to_root_t) {
      gdk_window_set_back_pixmap(root_win, p, FALSE);
    } else {
      GdkGC *rootGC;
      buffer = xcalloc(1, screen_x * screen_y * gvis->depth / 8);
      rootGC = gdk_gc_new(root_win);
      temp = gdk_pixmap_create_from_data(root_win, buffer, screen_x,
                                         screen_y, gvis->depth, &image_bg, &image_bg);
     gdk_drawable_set_colormap(GDK_DRAWABLE(temp),
			      gdk_drawable_get_colormap(GDK_DRAWABLE(root_win)));
      gdk_draw_drawable(temp, rootGC, p, 0, 0, root_x, root_y, root_w, root_h);
      gdk_window_set_back_pixmap(root_win, temp, FALSE);
      g_object_unref(temp);
      g_object_unref(rootGC);
      free(buffer);
    }
    g_object_unref(p);
    imlib_free_pixmap_and_mask(x_pixmap);
  }
  gdk_window_clear(root_win);
  gdk_flush();

  setup_imlib_for_drawable(q->win);
}

void zoom_in(qiv_image *q)
{
  int zoom_percentage;
  int w_old, h_old;

  /* first compute current zoom_factor */
  if (maxpect || scale_down || fixed_window_size) {
    zoom_percentage=myround((1.0-(q->orig_w - q->win_w)/(double)q->orig_w)*100);
    zoom_factor=(zoom_percentage - 100) / 10;
  }

  maxpect = scale_down = 0;

  zoom_factor++;
  w_old = q->win_w;
  h_old = q->win_h;
  q->win_w = (gint)(q->orig_w * (1 + zoom_factor * 0.1));
  q->win_h = (gint)(q->orig_h * (1 + zoom_factor * 0.1));

  /* adapt image position */
  q->win_x -= (q->win_w - w_old) / 2;
  q->win_y -= (q->win_h - h_old) / 2;

  if (fullscreen) {
    if (center)
      center_image(q);
    else
      correct_image_position(q);
  } else {
    correct_image_position(q);
  }
}

void zoom_out(qiv_image *q)
{
  int zoom_percentage;
  int w_old, h_old;

  /* first compute current zoom_factor */
  if (maxpect || scale_down || fixed_window_size) {
    zoom_percentage=myround((1.0-(q->orig_w - q->win_w)/(double)q->orig_w)*100);
    zoom_factor=(zoom_percentage - 100) / 10;
  }

  maxpect = scale_down = 0;

  w_old = q->win_w;
  h_old = q->win_h;

  if(zoom_factor > -9 && q->win_w > MIN(64, q->orig_w) && q->win_h > MIN(64, q->orig_h)) {
    zoom_factor--;
    q->win_w = (gint)(q->orig_w * (1 + zoom_factor * 0.1));
    q->win_h = (gint)(q->orig_h * (1 + zoom_factor * 0.1));

    /* adapt image position */
    q->win_x -= (q->win_w - w_old) / 2;
    q->win_y -= (q->win_h - h_old) / 2;

    if (fullscreen) {
      if (center)
        center_image(q);
      else
        correct_image_position(q);
    } else {
      correct_image_position(q);
    }
  } else {
    snprintf(infotext, sizeof infotext, "(Cannot zoom out anymore)");
    fprintf(stderr, "qiv: cannot zoom out anymore\n");
  }
}

void zoom_maxpect(qiv_image *q)
{
#ifdef GTD_XINERAMA
  double zx = (double)preferred_screen->width / (double)q->orig_w;
  double zy = (double)preferred_screen->height / (double)q->orig_h;
#else
  double zx = (double)screen_x / (double)q->orig_w;
  double zy = (double)screen_y / (double)q->orig_h;
#endif
  /* titlebar and frames ignored on purpose to use full height/width of screen */
  q->win_w = (gint)(q->orig_w * MIN(zx, zy));
  q->win_h = (gint)(q->orig_h * MIN(zx, zy));
  center_image(q);
}

/*
  Set display settings to startup values
  which are used whenever a new image is loaded.
*/

void reload_image(qiv_image *q)
{
  imlib_image_set_changes_on_disk();

  Imlib_Image *im = imlib_load_image(image_names[image_idx]);
  if (!im && watch_file)
    return;

  struct stat statbuf;
  stat(image_names[image_idx], &statbuf);
  current_mtime = statbuf.st_mtime;

  if (imlib_context_get_image())
    imlib_free_image();

  if (!im)
  {
    q->error = 1;
    q->orig_w = 400;
    q->orig_h = 300;
  } else { /* Retrieve image properties */
    q->error = 0;
    imlib_context_set_image(im);
    q->orig_w = imlib_image_get_width();
    q->orig_h = imlib_image_get_height();
  }

  q->win_w = (gint)(q->orig_w * (1 + zoom_factor * 0.1));
  q->win_h = (gint)(q->orig_h * (1 + zoom_factor * 0.1));
  reset_mod(q);
  center_image(q);
}

void check_size(qiv_image *q, gint reset) {
  /* printf("maxpect=%d thumbnail=%d scale_down=%d reset=%d\n", maxpect, thumbnail, scale_down, reset); */
  if ((maxpect && !thumbnail) ||
      (maxpect && thumbnail && (q->orig_w>screen_x || q->orig_h>screen_y)) ||
      (scale_down && (q->orig_w>screen_x || q->orig_h>screen_y))) {
    zoom_maxpect(q);
  } else if (reset || (scale_down && (q->win_w<q->orig_w || q->win_h<q->orig_h))) {
    reset_coords(q);
  }
  if (center) center_image(q);
}

void reset_coords(qiv_image *q)
{
  if (fixed_window_size) {
    double w_o_ratio = (double)(fixed_window_size) / q->orig_w;
    q->win_w = fixed_window_size;
    q->win_h = q->orig_h * w_o_ratio;
  } else {
    if (fixed_zoom_factor) {
      zoom_factor = fixed_zoom_factor; /* reset zoom */
    }
    q->win_w = (gint)(q->orig_w * (1 + zoom_factor * 0.1));
    q->win_h = (gint)(q->orig_h * (1 + zoom_factor * 0.1));
  }
}

static void update_win_title(qiv_image *q, const char *image_name, double elapsed) {
  char dimen_msg[sizeof(gint) * 6 + 8];
  char elapsed_msg[16];
  if (isnan(elapsed)) {
    *elapsed_msg = '\0';
  } else {
    g_snprintf(elapsed_msg, sizeof elapsed_msg, "%1.01fs ", elapsed);
  }
  if (q->real_w == -1) {
    /* Showing thumbnail, real image missing or unloadable. */
    strcpy(dimen_msg, "(?x?)--");
  } else {
    g_snprintf(dimen_msg, sizeof dimen_msg, "(%dx%d)%s",
               q->real_w >= 0 ? q->real_w : q->orig_w,
               q->real_h >= 0 ? q->real_h : q->orig_h,
               q->real_w >= 0 && q->real_h >= 0 ?
                   (q->has_thumbnail ? "-" : "%") :
                   thumbnail && !maxpect ? "+" : "");
  }
  g_snprintf(q->win_title, sizeof q->win_title,
             "qiv: %s %s %s%d%% [%d/%d] b%d/c%d/g%d %s",
             image_name, dimen_msg, elapsed_msg,
             myround((1.0-(q->orig_w - q->win_w)/(double)q->orig_w)*100), image_idx+1, images,
             q->mod.brightness/8-32, q->mod.contrast/8-32, q->mod.gamma/8-32, infotext);
}

static void update_image_on_error(qiv_image *q) {
  int i;

  if (!q->error) return;  /* Unexpected. */
  g_snprintf(q->win_title, sizeof q->win_title,
      "qiv: ERROR! cannot load image: %s", image_names[image_idx]);
  gdk_beep();

  /* take this image out of the file list */
  --images;
  for(i=image_idx;i<images;++i) {
    image_names[i] = image_names[i+1];
  }

  /* If deleting the last file out of x */
  if(images == image_idx)
    image_idx = 0;

  /* If deleting the only file left */
  if(!images) {
#ifdef DEBUG
    g_print("*** deleted last file in list. Exiting.\n");
#endif
    exit(0);
  }

  /* The caller should continue with the call to: qiv_load_image(q); */
}

/* Something changed the image. Redraw it. Don't (always) flush. */
void update_image_noflush(qiv_image *q, int mode) {
  GdkPixmap * m = NULL;
  Pixmap x_pixmap, x_mask;
  double elapsed;
  struct timeval before, after;

  if (q->error) {
    gdk_beep();
    update_image_on_error(q);
    return qiv_load_image(q);
  }

  q->is_updated = TRUE;
  {
    if (mode == REDRAW || mode == FULL_REDRAW)
      setup_imlib_color_modifier(q->mod);

    if (mode == MOVED) {
      if (transparency && used_masks_before) {
        /* there should be a faster way to update the mask, but how? */
	if (q->p)
	{
	  imlib_free_pixmap_and_mask(GDK_PIXMAP_XID(q->p));
	  g_object_unref(q->p);
	}
	imlib_render_pixmaps_for_whole_image_at_size(&x_pixmap, &x_mask, q->win_w, q->win_h);
	q->p = gdk_pixmap_foreign_new(x_pixmap);
	gdk_drawable_set_colormap(GDK_DRAWABLE(q->p),
				  gdk_drawable_get_colormap(GDK_DRAWABLE(q->win)));
	m = gdk_pixmap_foreign_new(x_mask);
      }
      update_win_title(q, image_names[image_idx], NAN);
    } // mode == MOVED
    else
    {
      if (q->p) {
	imlib_free_pixmap_and_mask(GDK_PIXMAP_XID(q->p));
	g_object_unref(q->p);
      }

      /* calculate elapsed time while we render image */
      gettimeofday(&before, 0);
      imlib_render_pixmaps_for_whole_image_at_size(&x_pixmap, &x_mask, q->win_w, q->win_h);
      gettimeofday(&after, 0);
      elapsed = ((after.tv_sec +  after.tv_usec / 1.0e6) -
                 (before.tv_sec + before.tv_usec / 1.0e6));

      q->p = gdk_pixmap_foreign_new(x_pixmap);
      gdk_drawable_set_colormap(GDK_DRAWABLE(q->p),
				gdk_drawable_get_colormap(GDK_DRAWABLE(q->win)));
      m = x_mask == None ? NULL : gdk_pixmap_foreign_new(x_mask);

#ifdef DEBUG
      if (m)  g_print("*** image has transparency\n");
#endif
      update_win_title(q, image_names[image_idx], load_elapsed + elapsed);
    }
    snprintf(infotext, sizeof infotext, "(-)");
  }

  gdk_window_set_title(q->win, q->win_title);

  q->text_len = strlen(q->win_title);
  pango_layout_set_text(layout, q->win_title, -1);
  pango_layout_get_pixel_size (layout, &(q->text_w), &(q->text_h));

  if (!fullscreen) {
    GdkGeometry geometry = {
      .min_width = q->win_w,
      .min_height = q->win_h,
      .max_width = q->win_w,
      .max_height = q->win_h,
      .win_gravity = GDK_GRAVITY_STATIC
    };
    gdk_window_set_geometry_hints(q->win, &geometry,
      GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE | GDK_HINT_WIN_GRAVITY);
    gdk_window_move_resize(q->win, q->win_x, q->win_y, q->win_w, q->win_h);

    if (!q->error) {
      gdk_window_set_back_pixmap(q->win, q->p, FALSE);
      /* remove or set transparency mask */
      if (used_masks_before) {
        if (transparency)
          gdk_window_shape_combine_mask(q->win, m, 0, 0);
        else
          gdk_window_shape_combine_mask(q->win, 0, 0, 0);
      }
      else
      {
        if (transparency && m) {
          gdk_window_shape_combine_mask(q->win, m, 0, 0);
          used_masks_before=1;
        }
      }
    }
    gdk_window_clear(q->win);
  } // if (!fullscreen)
  else
  {
#ifdef GTD_XINERAMA
# define statusbar_x (statusbar_screen->x_org + statusbar_screen->width)
# define statusbar_y (statusbar_screen->y_org + statusbar_screen->height)
#else
# define statusbar_x screen_x
# define statusbar_y screen_y
#endif
    if (mode == FULL_REDRAW) {
      gdk_window_clear(q->win);
    } else {
      if (q->win_x > q->win_ox)
        gdk_draw_rectangle(q->win, q->bg_gc, 1,
          q->win_ox, q->win_oy, q->win_x - q->win_ox, q->win_oh);
      if (q->win_y > q->win_oy)
        gdk_draw_rectangle(q->win, q->bg_gc, 1,
          q->win_ox, q->win_oy, q->win_ow, q->win_y - q->win_oy);
      if (q->win_x + q->win_w < q->win_ox + q->win_ow)
        gdk_draw_rectangle(q->win, q->bg_gc, 1,
          q->win_x + q->win_w, q->win_oy, q->win_ox + q->win_ow, q->win_oh);
      if (q->win_y + q->win_h < q->win_oy + q->win_oh)
        gdk_draw_rectangle(q->win, q->bg_gc, 1,
          q->win_ox, q->win_y + q->win_h, q->win_ow, q->win_oy + q->win_oh);

      if (q->statusbar_was_on && (!statusbar_fullscreen ||
                                  q->text_ow > q->text_w || q->text_oh > q->text_h))
        gdk_draw_rectangle(q->win, q->bg_gc, 1,
            statusbar_x-q->text_ow-9, statusbar_y-q->text_oh-9,
            q->text_ow+4, q->text_oh+4);
    }

    /* remove or set transparency mask */
    if (used_masks_before) {
      if (transparency)
        gdk_window_shape_combine_mask(q->win, m, q->win_x, q->win_y);
      else
        gdk_window_shape_combine_mask(q->win, 0, q->win_x, q->win_y);
    }
    else
    {
      if (transparency && m) {
        gdk_window_shape_combine_mask(q->win, m, q->win_x, q->win_y);
        used_masks_before=1;
      }
    }

    if (!q->error)
      gdk_draw_drawable(q->win, q->bg_gc, q->p, 0, 0,
                        q->win_x, q->win_y, q->win_w, q->win_h);


    if (statusbar_fullscreen) {
      {  /* Draw the statusbar to the bottom right corner of the fullscreen window. */

        gdk_draw_rectangle(q->win, q->bg_gc, 0,
          statusbar_x-q->text_w-10, statusbar_y-q->text_h-10, q->text_w+5, q->text_h+5);

        gdk_draw_rectangle(q->win, q->status_gc, 1,
          statusbar_x-q->text_w-9, statusbar_y-q->text_h-9, q->text_w+4, q->text_h+4);

        gdk_draw_layout (q->win, q->text_gc, statusbar_x-q->text_w-7, statusbar_y-7-q->text_h, layout);
      }
    }

    q->win_ox = q->win_x;
    q->win_oy = q->win_y;
    q->win_ow = q->win_w;
    q->win_oh = q->win_h;
    q->text_ow = q->text_w;
    q->text_oh = q->text_h;
    q->statusbar_was_on = statusbar_fullscreen;
  }
}

void update_image(qiv_image *q, int mode) {
  update_image_noflush(q, mode);
  gdk_flush();
}

void reset_mod(qiv_image *q)
{
  q->mod.brightness = default_brightness;
  q->mod.contrast = default_contrast;
  q->mod.gamma = default_gamma;
}

void destroy_image(qiv_image *q)
{
  if (q->p) {
    imlib_free_pixmap_and_mask(GDK_PIXMAP_XID(q->p));
    g_object_unref(q->p);
  }
  if (q->bg_gc) g_object_unref(q->bg_gc);
  if (q->text_gc) g_object_unref(q->text_gc);
  if (q->status_gc) g_object_unref(q->status_gc);
}

void setup_magnify(qiv_image *q, qiv_mgl *m)
{
   GdkWindowAttr mgl_attr;
   GdkGeometry mgl_hints;

   m->win_w=300; m->win_h=200;

//   gdk_flush();
   gdk_window_get_root_origin(q->win, &m->frame_x, &m->frame_y);
// printf("frame %d %d\n", m->frame_x, m->frame_y);

   mgl_attr.window_type=GDK_WINDOW_TOPLEVEL; // Set up attributes for GDK to create a Window
   mgl_attr.wclass=GDK_INPUT_OUTPUT;
   mgl_attr.event_mask=GDK_STRUCTURE_MASK;
   mgl_attr.width=m->win_w;
   mgl_attr.height=m->win_h;
   mgl_attr.override_redirect=TRUE;
//   m->win=gdk_window_new(NULL,&mgl_attr,GDK_WA_X|GDK_WA_Y|GDK_WA_WMCLASS);
   m->win=gdk_window_new(NULL,&mgl_attr,GDK_WA_X|GDK_WA_Y);
   mgl_hints.min_width=m->win_w;
   mgl_hints.max_width=m->win_w;
   mgl_hints.min_height=m->win_h;
   mgl_hints.max_height=m->win_h;
   gdk_window_set_geometry_hints(m->win, &mgl_hints,
     GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
   gdk_window_set_decorations(m->win, GDK_DECOR_BORDER);
   gdk_flush();
}

void update_magnify(qiv_image *q, qiv_mgl *m, int mode, gint xcur, gint ycur)
{
//   GdkWindowAttr mgl_attr;
//   GdkGeometry mgl_hints;
  register  gint xx, yy;  // win_pos_x, win_pos_y;

/***********
  if (mode == FULL_REDRAW) {
//    printf("> update_magnify: FULL_REDRAW \n");
    m->im=gdk_imlib_crop_and_clone_image(
      q->im, 0, 0, m->win_w, m->win_h);
    gdk_imlib_apply_image(m->im,m->win);

    mgl_hints.min_width=m->win_w;
    mgl_hints.max_width=m->win_w;
    mgl_hints.min_height=m->win_h;
    mgl_hints.max_height=m->win_h;

//    gdk_window_set_hints(m->win, mgl_attr.x, mgl_attr.y,mglw,
//                         mglw,mglh, mglh, GDK_HINT_POS | GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
    gdk_window_set_hints(m->win, xcur+50, ycur-50-m->win_h,m->win_w,
                         m->win_w,m->win_h, m->win_h, GDK_HINT_POS | GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
//    gdk_window_move_resize(m->win,mgl_attr.x, mgl_attr.y, mglw, mglh);

//    gdk_window_set_geometry_hints(magnify_img.win, &mgl_hints, GDK_HINT_POS );
  }
************/
  if (mode == REDRAW ) {
    xx=xcur * q->orig_w / q->win_w;    /* xx, yy are the coords of cursor   */
    if (xx <= m->win_w/2)               /* xcur, ycur scaled to the original */
      xx=0;                             /* image; they are changed so that   */
    else                                /* the magnified part is always      */
      if (xx >= q->orig_w - m->win_w/2) /* inside the image.                 */
        xx=q->orig_w - m->win_w;
      else
        xx=xx - m->win_w/2;

    yy=ycur * q->orig_h / q->win_h;
    if (yy <= m->win_h/2)
      yy=0;
    else
      if (yy >= q->orig_h - m->win_h/2)
        yy=q->orig_h - m->win_h;
      else
        yy=yy - m->win_h/2;

//    printf("MGL: xcur: %d, ycur: %d, xx: %d, yy: %d\n", xcur, ycur, xx, yy);

    setup_imlib_for_drawable(m->win);
    imlib_render_image_part_on_drawable_at_size(xx, yy, m->win_w, m->win_h,
						0, 0, m->win_w, m->win_h);
    setup_imlib_for_drawable(q->win);
    gdk_window_show(m->win);

    // xcur= m->frame_x + xcur +
    // (xcur < m->win_w/2? 50 : - 50 - m->win_w);
    // gdk_window_get_root_origin(q->win,              // todo  [lc]
    //        &magnify_img.frame_x, &magnify_img.frame_y);  // call not necessary
    xx= m->frame_x + xcur - 50 - m->win_w;
    yy= m->frame_y + ycur - 50 - m->win_h;
    if (xx < 0) {
      if (xcur < m->win_w - magnify_img.frame_x)
        xx=m->frame_x + xcur + 50;
      else
        xx=0;
    }
    if (yy < 0) {
      if (ycur < m->win_h - magnify_img.frame_y)
        yy=m->frame_y + ycur + 50;
      else
        yy=0;
    }
//    printf("MGL: m->frame_x: %d, m->frame_y: %d, xx: %d, yy: %d\n", m->frame_x, m->frame_y, xx, yy);

//    gdk_window_move_resize(m->win, xx, yy,    m->win_w, m->win_h);
    gdk_window_move(m->win, xx, yy);
  }

// gdk_window_show(m->win);
  gdk_flush();
}

#ifdef GTD_XINERAMA
void center_image(qiv_image *q)
{
//  g_print("before: q->win_x = %d, q->win_y = %d, q->win_w = %d\n", q->win_x, q->win_y, q->win_w);
  XineramaScreenInfo * pscr = preferred_screen;

//  g_print("screen_x = %d, pscr->x_org = %d, pscr->width = %d\n", screen_x, pscr->x_org, pscr->width);

  /* Figure out x position */
  if (q->win_w <= pscr->width) {
    /* If image fits on screen try to center image
     * within preferred (sub)screen */
    q->win_x = (pscr->width - q->win_w) / 2 + pscr->x_org;
    /* Ensure image actually lies within screen boundaries */
    if (q->win_x < 0) {
      q->win_x = 0;
    }
    else if (q->win_x + q->win_w > screen_x) {
      q->win_x = screen_x - q->win_w;
    }
  }
  else {
    /* If image wider than screen, just center it over all screens */
    q->win_x = (screen_x - q->win_w) / 2;
//    g_print("q->win_x = %d, screen_x = %d, q->win_w = %d, pscr->width = %d\n", q->win_x, screen_x, q->win_w, pscr->width);
  }

  /* Same thing for y position */
  if (q->win_h <= screen_y) {
    q->win_y = (pscr->height - q->win_h) / 2 + pscr->y_org;
    if (q->win_y < 0) {
      q->win_y = 0;
    }
    else if (q->win_y + q->win_h > screen_y) {
      q->win_y = screen_y - q->win_h;
    }
  }
  else {
    q->win_y = (screen_y - q->win_h) / 2;
  }
//  g_print("after:  q->win_x = %d, q->win_y = %d, q->win_w = %d\n", q->win_x, q->win_y, q->win_w);
}
#else
void center_image(qiv_image *q)
{
  q->win_x = (screen_x - q->win_w) / 2;
  q->win_y = (screen_y - q->win_h) / 2;
}
#endif

void correct_image_position(qiv_image *q)
{
//  g_print("before: q->win_x = %d, q->win_y = %d, q->win_w = %d\n", q->win_x, q->win_y, q->win_w);

  /* try to keep inside the screen */
  if (q->win_w < screen_x) {
    if (q->win_x < 0)
      q->win_x = 0;
    if (q->win_x + q->win_w > screen_x)
      q->win_x = screen_x - q->win_w;
  } else {
    if (q->win_x > 0)
      q->win_x = 0;
    if (q->win_x + q->win_w < screen_x)
      q->win_x = screen_x - q->win_w;
  }

  /* don't leave ugly borders */
  if (q->win_h < screen_y) {
    if (q->win_y < 0)
      q->win_y = 0;
    if (q->win_y + q->win_h > screen_y)
      q->win_y = screen_y - q->win_h;
  } else {
    if (q->win_y > 0)
      q->win_y = 0;
    if (q->win_y + q->win_h < screen_y)
      q->win_y = screen_y - q->win_h;
  }
//  g_print("after:  q->win_x = %d, q->win_y = %d, q->win_w = %d\n", q->win_x, q->win_y, q->win_w);
}

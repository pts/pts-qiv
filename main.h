#ifndef MAIN_H
#define MAIN_H

int		first = 1; /* TRUE if this is first image or after fullscreen or iconifying */
char		infotext[BUF_LEN];
GMainLoop	*qiv_main_loop;
gint		screen_x, screen_y; /* Size of the screen in pixels */
GdkColormap	*cmap; /* global colormap */
char		*image_bg_spec = IMAGE_BG;
GdkColor	image_bg; /* default background */
GdkColor	text_bg; /* statusbar and help backgrounf */
GdkColor	error_bg; /* for the error window/screen */
int		images;	/* Number of images in current collection */
char		**image_names = NULL; /* Filenames of the images */
int		image_idx = 0; /* Index of current image displayed. 0 = 1st image */
int		max_image_cnt = 0; /* # images currently allocated into arrays */
time_t          current_mtime = 0; /* modification time of file currently loaded */
qiv_deletedfile *deleted_files;
int		delete_idx;
char    select_dir[FILENAME_LEN];

/* stuff for rendering statusbar, infotext, etc ... */
PangoLayout     *layout;
PangoFontMetrics *metrics;
PangoFontDescription *fontdesc;

/* Options and such */

int	filter = FILTER;
gint	center = CENTER;
gint	default_brightness = DEFAULT_BRIGHTNESS;
gint	default_contrast = DEFAULT_CONTRAST;
gint	default_gamma = DEFAULT_GAMMA;
gint	delay = SLIDE_DELAY; /* delay in slideshow mode in seconds; if nonpositive, it's an offset to an array of subsecond delays */
int	readonly = 0; /* TRUE if (un)deletion of images should be impossible */
int	random_order; /* TRUE if random delay in slideshow */
int	random_replace = 1; /* random with replacement by default */
int	fullscreen; /* TRUE if fullscreen mode */
int	maxpect; /* TRUE if autozoom (fit-to-screen) mode */
int	statusbar_fullscreen = 1; /* TRUE if statusbar in fullscreen is turned on (default) */
int	statusbar_window = 0; /* FALSE if statusbar in window is turned off (default) */
int	slide; /* 1=slide show running */
int	scale_down; /* resize down if image x/y > screen */
int	recursive; /* descend recursively */
int	to_root; /* display on root (centered) */
int	to_root_t; /* display on root (tiled) */
int	to_root_s; /* display on root (stretched) */
int	transparency; /* transparency on/off */
gboolean do_assume_files; /* Assume that all images (command line or list) are files, don't stat them. */
gboolean do_grab; /* grab keboard/pointer (default off) */
gboolean do_omit_load_stat; /* omit the stat(2) system call at load_image time, don't track changes to the file (current_mtime); useful if the thumbnail is much faster */
gboolean do_enter_command; /* run qiv-command :enter on <Enter> */
gboolean do_f_commands; /* run qiv-command :f1 etc on <F1> etc. */
gboolean do_tag_error_pos; /* Move the cursor to tag error reported by qiv-command. */
gboolean do_copy_link;  /* Create a hard link if possible when copying to .qiv-select */
gboolean disable_grab; /* disable keyboard/mouse grabbing in fullscreen mode */
int	max_rand_num; /* the largest random number range we will ask for */
int	fixed_window_size = 0; /* window width fixed size/off */
int	fixed_zoom_factor = 0; /* window fixed zoom factor (percentage)/off */
int zoom_factor = 0; /* zoom factor/off */
int watch_file = 0; /* watch current files Timestamp, reload if changed */
int magnify = 0; /* [lc] */
int user_screen = -1; /* preferred (by user) Xinerama screen */
int browse = 0; /* scan directory of file for browsing */
int autorotate = 0; /* autorotate JPEGs according to EXIF tag */

#ifdef GTD_XINERAMA
int number_xinerama_screens = 0;
XineramaScreenInfo preferred_screen[1];
XineramaScreenInfo statusbar_screen[1];
#endif


/* Used for the ? key */

const char *helpstrs[] =
{
    VERSION_FULL,
    "",
    "space/left mouse/wheel down      next picture  (with ctrl, next dir)",
    "backspace/right mouse/wheel up   previous picture  (with ctrl, prev dir)",
    "PgDn                             5 pictures forward",
    "PgUp                             5 pictures backward",
    "q/ESC/middle mouse               exit",
    "",
    "0-9                  Run 'qiv-command <key> <current-img>'",
    "^<string><return>    Run 'qiv-command ^<string> <current-img>'",
    "?/F1                 show keys (in fullscreen mode)",
    "[/F11 | ]/F12        in|decrease slideshow delay (1 second)",
    "a/A                  copy current image to .qiv-select",
    "d/D/del              move picture to .qiv-trash",
    "u                    undelete the previously trashed image",
    "+/=                  zoom in (10%)",
    "-                    zoom out (10%)",
    "e                    center mode on/off",
    "f                    fullscreen mode on/off",
    "m                    scale to screen size on/off",
    "t                    scale down on/off",
    "X                    cycle through xinerama screens",
    "s                    slide show on/off",
    "p                    transparency on/off",
    "r                    random order on/off",
    "b                    - brightness",
    "B                    + brightness",
    "c                    - contrast",
    "C                    + contrast",
    "g                    - gamma",
    "G                    + gamma",
    "o                    reset brightness, contrast, gamma",
    "h                    flip horizontal",
    "v                    flip vertical",
    "k                    rotate right",
    "l                    rotate left",
    "jtx<return>          jump to image number x",
    "jfx<return>          jump forward x images",
    "jbx<return>          jump backward x images",
    "enter/return         reset zoom, rotation and color settings",
    "i                    statusbar on/off",
    "I                    iconify window",
    "w                    watch file on/off",
    "x                    center image on background",
    "y                    tile image on background",
    "z                    stretch image on background",
    "<                    turn on/off magnifying window",
    "arrow keys                 move image (in fullscreen mode)",
    "arrow keys+Shift           move image faster (in fullscreen mode)",
    "NumPad-arrow keys+NumLock  move image faster (in fullscreen mode)",
    NULL
};

/* For --help output, we'll skip the first two lines. */

const char **helpkeys = helpstrs+2;

/* Used for filtering */

const char *image_extensions[] = {
#ifdef EXTN_JPEG
    ".jpg",".jpeg", ".jpe",
#endif
#ifdef EXTN_GIF
    ".gif",
#endif
#ifdef EXTN_TIFF
    ".tif",".tiff",
#endif
#ifdef EXTN_XPM
    ".xpm",
#endif
#ifdef EXTN_PNG
    ".png",".pjpeg",
#endif
#ifdef EXTN_PPM
    ".ppm",
#endif
#ifdef EXTN_PNM
    ".pnm",
#endif
#ifdef EXTN_PGM
    ".pgm",  ".pbm",
#endif
#ifdef EXTN_PCX
    ".pcx",
#endif
#ifdef EXTN_BMP
    ".bmp",
#endif
#ifdef EXTN_EIM
    ".eim",
#endif
#ifdef EXTN_TGA
    ".tga",
#endif
    NULL
};

#ifdef HAVE_MAGIC
const char *image_magic[] = {
#ifdef EXTN_JPEG
  "JPEG image data",
#endif
#ifdef EXTN_GIF
  "GIF image data",
#endif
#ifdef EXTN_TIFF
  "TIFF image data",
#endif
#ifdef EXTN_XPM
  "X pixmap image",
#endif
#ifdef EXTN_PNG
  "PNG image data",
#endif
#ifdef EXTN_PGM
  "Netpbm PBM",
  "Netpbm PPM",
#endif
#ifdef EXTN_BMP
  "PC bitmap data",
#endif
#ifdef EXTN_TGA
  "Targa image data",
#endif
 NULL
};
#endif


#endif /* MAIN_H */

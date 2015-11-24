#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <errno.h>
#include <debug.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/boardctl.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>

#ifdef CONFIG_NX_LCDDRIVER
#  include <nuttx/lcd/lcd.h>
#else
#  include <nuttx/video/fb.h>
#endif

#include <nuttx/nx/nx.h>
#include <nuttx/nx/nxglib.h>
#include <nuttx/nx/nxfonts.h>

#include <apps/app_utils.h>

#include "nr_display.h"


/****************************************************************************
 * Definitions
 ****************************************************************************/

/* Select renderer -- Some additional logic would be required to support
 * pixel depths that are not directly addressable (1,2,4, and 24).
 */

#if CONFIG_NEURON_DISPLAY_BPP == 1
#  define RENDERER nxf_convert_1bpp
#elif CONFIG_NEURON_DISPLAY_BPP == 2
#  define RENDERER nxf_convert_2bpp
#elif CONFIG_NEURON_DISPLAY_BPP == 4
#  define RENDERER nxf_convert_4bpp
#elif CONFIG_NEURON_DISPLAY_BPP == 8
#  define RENDERER nxf_convert_8bpp
#elif CONFIG_NEURON_DISPLAY_BPP == 16
#  define RENDERER nxf_convert_16bpp
#elif CONFIG_NEURON_DISPLAY_BPP == 24
#  define RENDERER nxf_convert_24bpp
#elif  CONFIG_NEURON_DISPLAY_BPP == 32
#  define RENDERER nxf_convert_32bpp
#else
#  error "Unsupported CONFIG_NEURON_DISPLAY_BPP"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void nxhello_redraw(NXWINDOW hwnd, FAR const struct nxgl_rect_s *rect,
                        bool morem, FAR void *arg);
static void nxhello_position(NXWINDOW hwnd, FAR const struct nxgl_size_s *size,
                          FAR const struct nxgl_point_s *pos,
                          FAR const struct nxgl_rect_s *bounds,
                          FAR void *arg);
#ifdef CONFIG_NX_XYINPUT
static void nxhello_mousein(NXWINDOW hwnd, FAR const struct nxgl_point_s *pos,
                         uint8_t buttons, FAR void *arg);
#endif

#ifdef CONFIG_NX_KBD
static void nxhello_kbdin(NXWINDOW hwnd, uint8_t nch, FAR const uint8_t *ch,
                       FAR void *arg);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char g_hello[] = "Hello, World!";

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Background window call table */

const struct nx_callback_s g_nxhellocb =
{
  nxhello_redraw,   /* redraw */
  nxhello_position  /* position */
#ifdef CONFIG_NX_XYINPUT
  , nxhello_mousein /* mousein */
#endif
#ifdef CONFIG_NX_KBD
  , nxhello_kbdin   /* my kbdin */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxhello_redraw
 ****************************************************************************/

static void nxhello_redraw(NXWINDOW hwnd, FAR const struct nxgl_rect_s *rect,
                        bool more, FAR void *arg)
{
  gvdbg("hwnd=%p rect={(%d,%d),(%d,%d)} more=%s\n",
         hwnd, rect->pt1.x, rect->pt1.y, rect->pt2.x, rect->pt2.y,
         more ? "true" : "false");
}

/****************************************************************************
 * Name: nxhello_position
 ****************************************************************************/

static void nxhello_position(NXWINDOW hwnd, FAR const struct nxgl_size_s *size,
                          FAR const struct nxgl_point_s *pos,
                          FAR const struct nxgl_rect_s *bounds,
                          FAR void *arg)
{
  /* Report the position */

  gvdbg("hwnd=%p size=(%d,%d) pos=(%d,%d) bounds={(%d,%d),(%d,%d)}\n",
        hwnd, size->w, size->h, pos->x, pos->y,
        bounds->pt1.x, bounds->pt1.y, bounds->pt2.x, bounds->pt2.y);

  /* Have we picked off the window bounds yet? */

  if (!g_nxhello.havepos)
    {
      /* Save the background window handle */

      g_nxhello.hbkgd = hwnd;

      /* Save the window limits */

      g_nxhello.xres = bounds->pt2.x + 1;
      g_nxhello.yres = bounds->pt2.y + 1;

      g_nxhello.havepos = true;
      sem_post(&g_nxhello.sem);
      gvdbg("Have xres=%d yres=%d\n", g_nxhello.xres, g_nxhello.yres);
    }
}

/****************************************************************************
 * Name: nxhello_mousein
 ****************************************************************************/

#ifdef CONFIG_NX_XYINPUT
static void nxhello_mousein(NXWINDOW hwnd, FAR const struct nxgl_point_s *pos,
                         uint8_t buttons, FAR void *arg)
{
  printf("nxhello_mousein: hwnd=%p pos=(%d,%d) button=%02x\n",
         hwnd,  pos->x, pos->y, buttons);
}
#endif

/****************************************************************************
 * Name: nxhello_kbdin
 ****************************************************************************/

#ifdef CONFIG_NX_KBD
static void nxhello_kbdin(NXWINDOW hwnd, uint8_t nch, FAR const uint8_t *ch,
                       FAR void *arg)
{
  gvdbg("hwnd=%p nch=%d\n", hwnd, nch);

   /* In this example, there is no keyboard so a keyboard event is not
    * expected.
    */

   printf("nxhello_kbdin: Unexpected keyboard callback\n");
}
#endif

/****************************************************************************
 * Name: nxhello_center
 ****************************************************************************/

static void nxhello_center(FAR struct nxgl_point_s *pos,
                           FAR const struct nx_font_s *fontset)
{
  FAR const struct nx_fontbitmap_s *fbm;
  FAR uint8_t *ptr;
  unsigned int width;

  /* Get the width of the collection of characters so that we can center the
   * hello world message.
   */

  for (ptr = (uint8_t*)g_hello, width = 0; *ptr; ptr++)
    {
      /* Get the font bitmap for this character */

      fbm = nxf_getbitmap(g_nxhello.hfont, *ptr);
      if (fbm)
        {
          /* Add the font size */

          width += fbm->metric.width + fbm->metric.xoffset;
        }
      else
        {
           /* Use the width of a space */

          width += fontset->spwidth;
        }
    }

  /* Now we know how to center the string.  Create a the position and
   * the bounding box
   */

  pos->x = (g_nxhello.xres - width) / 2;
  pos->y = (g_nxhello.yres - fontset->mxheight) / 2;
}

static void nx_display_conv_pos(FAR struct nxgl_point_s *pos, int x, int y)
{
  FAR const struct nx_font_s *fontset;

  fontset = nxf_getfontset(g_nxhello.hfont);

  pos->x = x * fontset->mxwidth;
  pos->y = y * fontset->mxheight;
}

/****************************************************************************
 * Name: nxhello_initglyph
 ****************************************************************************/

static void nxhello_initglyph(FAR uint8_t *glyph, uint8_t height,
                              uint8_t width, uint8_t stride)
{
  FAR nxgl_mxpixel_t *ptr;
#if CONFIG_NEURON_DISPLAY_BPP < 8
  nxgl_mxpixel_t pixel;
#endif
  unsigned int row;
  unsigned int col;

  /* Initialize the glyph memory to the background color */

#if CONFIG_NEURON_DISPLAY_BPP < 8

  pixel  = CONFIG_NEURON_DISPLAY_BGCOLOR;

#if CONFIG_NX_NPLANES > 1
# warning "More logic is needed for the case where CONFIG_NX_PLANES > 1"
#endif
#  if CONFIG_NEURON_DISPLAY_BPP == 1
  /* Pack 1-bit pixels into a 2-bits */

  pixel &= 0x01;
  pixel  = (pixel) << 1 |pixel;

#  endif
#  if CONFIG_NEURON_DISPLAY_BPP < 4

  /* Pack 2-bit pixels into a nibble */

  pixel &= 0x03;
  pixel  = (pixel) << 2 |pixel;

#  endif

  /* Pack 4-bit nibbles into a byte */

  pixel &= 0x0f;
  pixel  = (pixel) << 4 | pixel;

  ptr    = (FAR nxgl_mxpixel_t *)glyph;
  for (row = 0; row < height; row++)
    {
      for (col = 0; col < stride; col++)
        {
          /* Transfer the packed bytes into the buffer */

          *ptr++ = pixel;
        }
    }

#elif CONFIG_NEURON_DISPLAY_BPP == 24
# error "Additional logic is needed here for 24bpp support"

#else /* CONFIG_NEURON_DISPLAY_BPP = {8,16,32} */

  ptr = (FAR nxgl_mxpixel_t *)glyph;
  for (row = 0; row < height; row++)
    {
      /* Just copy the color value into the glyph memory */

      for (col = 0; col < width; col++)
        {
          *ptr++ = CONFIG_NEURON_DISPLAY_BGCOLOR;
#if CONFIG_NX_NPLANES > 1
# warning "More logic is needed for the case where CONFIG_NX_PLANES > 1"
#endif
        }
    }
#endif
}

/****************************************************************************
 * Name: nxhello_hello
 *
 * Description:
 *   Print "Hello, World!" in the center of the display.
 *
 ****************************************************************************/

void nxhello_hello(NXWINDOW hwnd)
{
  FAR const struct nx_font_s *fontset;
  FAR const struct nx_fontbitmap_s *fbm;
  FAR uint8_t *glyph;
  FAR const char *ptr;
  FAR struct nxgl_point_s pos;
  FAR struct nxgl_rect_s dest;
  FAR const void *src[CONFIG_NX_NPLANES];
  unsigned int glyphsize;
  unsigned int mxstride;
  int ret;

  /* Get information about the font we are going to use */

  fontset = nxf_getfontset(g_nxhello.hfont);

  /* Allocate a bit of memory to hold the largest rendered font */

  mxstride  = (fontset->mxwidth * CONFIG_NEURON_DISPLAY_BPP + 7) >> 3;
  glyphsize = (unsigned int)fontset->mxheight * mxstride;
  glyph     = (FAR uint8_t*)malloc(glyphsize);

  /* NOTE: no check for failure to allocate the memory.  In a real application
   * you would need to handle that event.
   */

  /* Get a position so the the "Hello, World!" string will be centered on the
   * display.
   */

  nxhello_center(&pos, fontset);
  printf("nxhello_hello: Position (%d,%d)\n", pos.x, pos.y);

  /* Now we can say "hello" in the center of the display. */

  for (ptr = g_hello; *ptr; ptr++)
    {
      /* Get the bitmap font for this ASCII code */

      fbm = nxf_getbitmap(g_nxhello.hfont, *ptr);
      if (fbm)
        {
          uint8_t fheight;      /* Height of this glyph (in rows) */
          uint8_t fwidth;       /* Width of this glyph (in pixels) */
          uint8_t fstride;      /* Width of the glyph row (in bytes) */

          /* Get information about the font bitmap */

          fwidth  = fbm->metric.width + fbm->metric.xoffset;
          fheight = fbm->metric.height + fbm->metric.yoffset;
          fstride = (fwidth * CONFIG_NEURON_DISPLAY_BPP + 7) >> 3;

          /* Initialize the glyph memory to the background color */

          nxhello_initglyph(glyph, fheight, fwidth, fstride);

          /* Then render the glyph into the allocated memory */

#if CONFIG_NX_NPLANES > 1
# warning "More logic is needed for the case where CONFIG_NX_PLANES > 1"
#endif
          (void)RENDERER((FAR nxgl_mxpixel_t*)glyph, fheight, fwidth,
                         fstride, fbm, CONFIG_NEURON_DISPLAY_FONTCOLOR);

          /* Describe the destination of the font with a rectangle */

          dest.pt1.x = pos.x;
          dest.pt1.y = pos.y;
          dest.pt2.x = pos.x + fwidth - 1;
          dest.pt2.y = pos.y + fheight - 1;

          /* Then put the font on the display */

          src[0] = (FAR const void *)glyph;
#if CONFIG_NX_NPLANES > 1
# warning "More logic is needed for the case where CONFIG_NX_PLANES > 1"
#endif
          ret = nx_bitmap((NXWINDOW)hwnd, &dest, src, &pos, fstride);
          if (ret < 0)
            {
              printf("nxhello_write: nx_bitmapwindow failed: %d\n", errno);
            }

           /* Skip to the right the width of the font */

          pos.x += fwidth;
        }
      else
        {
           /* No bitmap (probably because the font is a space).  Skip to the
            * right the width of a space.
            */

          pos.x += fontset->spwidth;
        }
    }

  /* Free the allocated glyph */

  free(glyph);
}

void nx_display_str_in_pixel(NXWINDOW hwnd, char *str, int x, int y)
{
  FAR const struct nx_font_s *fontset;
  FAR const struct nx_fontbitmap_s *fbm;
  FAR uint8_t *glyph;
  FAR const char *ptr;
  FAR struct nxgl_point_s pos;
  FAR struct nxgl_rect_s dest;
  FAR const void *src[CONFIG_NX_NPLANES];
  unsigned int glyphsize;
  unsigned int mxstride;
  int ret;

  /* Get information about the font we are going to use */

  fontset = nxf_getfontset(g_nxhello.hfont);

  /* Allocate a bit of memory to hold the largest rendered font */

  mxstride  = (fontset->mxwidth * CONFIG_NEURON_DISPLAY_BPP + 7) >> 3;
  glyphsize = (unsigned int)fontset->mxheight * mxstride;
  glyph     = (FAR uint8_t*)malloc(glyphsize);

  /* NOTE: no check for failure to allocate the memory.  In a real application
   * you would need to handle that event.
   */

  /* Get a position so the the "Hello, World!" string will be centered on the
   * display.
   */

  pos.x = x;
  pos.y = y;
  //log_dbg("Position (%d,%d)\n", pos.x, pos.y);

  /* Now we can say "hello" in the center of the display. */

  for (ptr = str; *ptr; ptr++)
    {
      /* Get the bitmap font for this ASCII code */

      fbm = nxf_getbitmap(g_nxhello.hfont, *ptr);
      if (fbm)
        {
          uint8_t fheight;      /* Height of this glyph (in rows) */
          uint8_t fwidth;       /* Width of this glyph (in pixels) */
          uint8_t fstride;      /* Width of the glyph row (in bytes) */

          /* Get information about the font bitmap */

          fwidth  = fbm->metric.width + fbm->metric.xoffset;
          fheight = fbm->metric.height + fbm->metric.yoffset;
          fstride = (fwidth * CONFIG_NEURON_DISPLAY_BPP + 7) >> 3;

          /* Initialize the glyph memory to the background color */

          nxhello_initglyph(glyph, fheight, fwidth, fstride);

          /* Then render the glyph into the allocated memory */

#if CONFIG_NX_NPLANES > 1
# warning "More logic is needed for the case where CONFIG_NX_PLANES > 1"
#endif
          (void)RENDERER((FAR nxgl_mxpixel_t*)glyph, fheight, fwidth,
                         fstride, fbm, CONFIG_NEURON_DISPLAY_FONTCOLOR);

          /* Describe the destination of the font with a rectangle */

          dest.pt1.x = pos.x;
          dest.pt1.y = pos.y;
          dest.pt2.x = pos.x + fwidth - 1;
          dest.pt2.y = pos.y + fheight - 1;

          /* Then put the font on the display */

          src[0] = (FAR const void *)glyph;
#if CONFIG_NX_NPLANES > 1
# warning "More logic is needed for the case where CONFIG_NX_PLANES > 1"
#endif
          ret = nx_bitmap((NXWINDOW)hwnd, &dest, src, &pos, fstride);
          if (ret < 0)
            {
              printf("nxhello_write: nx_bitmapwindow failed: %d\n", errno);
            }

           /* Skip to the right the width of the font */

          pos.x += fwidth;
        }
      else
        {
           /* No bitmap (probably because the font is a space).  Skip to the
            * right the width of a space.
            */

          pos.x += fontset->spwidth;
        }
    }

  /* Free the allocated glyph */

  free(glyph);
}

/* FIXME cutting here */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/
/* If not specified, assume that the hardware supports one video plane */

#ifndef CONFIG_NEURON_DISPLAY_VPLANE
#  define CONFIG_NEURON_DISPLAY_VPLANE 0
#endif

/* If not specified, assume that the hardware supports one LCD device */

#ifndef CONFIG_NEURON_DISPLAY_DEVNO
#  define CONFIG_NEURON_DISPLAY_DEVNO 0
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

struct nxhello_data_s g_nxhello =
{
  NULL,          /* hnx */
  NULL,          /* hbkgd */
  NULL,          /* hfont */
  0,             /* xres */
  0,             /* yres */
  false,         /* havpos */
  { 0 },         /* sem */
  NXEXIT_SUCCESS /* exit code */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxhello_initialize
 ****************************************************************************/

static inline int nxhello_initialize(void)
{
  FAR NX_DRIVERTYPE *dev;

#if defined(CONFIG_NEURON_DISPLAY_EXTERNINIT)
  struct boardioc_graphics_s devinfo;
  int ret;

  /* Use external graphics driver initialization */

  printf("nxhello_initialize: Initializing external graphics device\n");

  devinfo.devno = CONFIG_NEURON_DISPLAY_DEVNO;
  devinfo.dev = NULL;

  ret = boardctl(BOARDIOC_GRAPHICS_SETUP, (uintptr_t)&devinfo);
  if (ret < 0)
    {
      printf("nxhello_initialize: boardctl failed, devno=%d: %d\n",
             CONFIG_NEURON_DISPLAY_DEVNO, errno);
      g_nxhello.code = NXEXIT_EXTINITIALIZE;
      return ERROR;
    }

  dev = devinfo.dev;

#elif defined(CONFIG_NX_LCDDRIVER)
  int ret;

  /* Initialize the LCD device */

  printf("nxhello_initialize: Initializing LCD\n");
  ret = board_lcd_initialize();
  if (ret < 0)
    {
      printf("nxhello_initialize: board_lcd_initialize failed: %d\n", -ret);
      g_nxhello.code = NXEXIT_LCDINITIALIZE;
      return ERROR;
    }

  /* Get the device instance */

  dev = board_lcd_getdev(CONFIG_NEURON_DISPLAY_DEVNO);
  if (!dev)
    {
      printf("nxhello_initialize: board_lcd_getdev failed, devno=%d\n",
             CONFIG_NEURON_DISPLAY_DEVNO);
      g_nxhello.code = NXEXIT_LCDGETDEV;
      return ERROR;
    }

  /* Turn the LCD on at 75% power */

  (void)dev->setpower(dev, ((3*CONFIG_LCD_MAXPOWER + 3)/4));
#else
  int ret;

  /* Initialize the frame buffer device */

  printf("nxhello_initialize: Initializing framebuffer\n");
  ret = up_fbinitialize();
  if (ret < 0)
    {
      printf("nxhello_initialize: up_fbinitialize failed: %d\n", -ret);
      g_nxhello.code = NXEXIT_FBINITIALIZE;
      return ERROR;
    }

  dev = up_fbgetvplane(CONFIG_NEURON_DISPLAY_VPLANE);
  if (!dev)
    {
      printf("nxhello_initialize: up_fbgetvplane failed, vplane=%d\n", CONFIG_NEURON_DISPLAY_VPLANE);
      g_nxhello.code = NXEXIT_FBGETVPLANE;
      return ERROR;
    }
#endif

  /* Then open NX */

  printf("nxhello_initialize: Open NX\n");
  g_nxhello.hnx = nx_open(dev);
  if (!g_nxhello.hnx)
    {
      printf("nxhello_initialize: nx_open failed: %d\n", errno);
      g_nxhello.code = NXEXIT_NXOPEN;
      return ERROR;
    }
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int nr_display_init(void)
{
  nxgl_mxpixel_t color;
  int ret;

  /* Initialize NX */

  ret = nxhello_initialize();
  printf("nxhello_main: NX handle=%p\n", g_nxhello.hnx);
  if (!g_nxhello.hnx || ret < 0)
    {
      printf("nxhello_main: Failed to get NX handle: %d\n", errno);
      g_nxhello.code = NXEXIT_NXOPEN;
      goto errout;
    }

  /* Get the default font handle */

  g_nxhello.hfont = nxf_getfonthandle(CONFIG_NEURON_DISPLAY_FONTID);
  if (!g_nxhello.hfont)
    {
      printf("nxhello_main: Failed to get font handle: %d\n", errno);
      g_nxhello.code = NXEXIT_FONTOPEN;
      goto errout;
    }

  /* Set the background to the configured background color */

  printf("nxhello_main: Set background color=%d\n",
         CONFIG_NEURON_DISPLAY_BGCOLOR);

  color = CONFIG_NEURON_DISPLAY_BGCOLOR;
  ret = nx_setbgcolor(g_nxhello.hnx, &color);
  if (ret < 0)
    {
      printf("nxhello_main: nx_setbgcolor failed: %d\n", errno);
      g_nxhello.code = NXEXIT_NXSETBGCOLOR;
      goto errout_with_nx;
    }

  /* Get the background window */

  ret = nx_requestbkgd(g_nxhello.hnx, &g_nxhellocb, NULL);
  if (ret < 0)
    {
      printf("nxhello_main: nx_setbgcolor failed: %d\n", errno);
      g_nxhello.code = NXEXIT_NXREQUESTBKGD;
      goto errout_with_nx;
    }

  /* Wait until we have the screen resolution.  We'll have this immediately
   * unless we are dealing with the NX server.
   */

  while (!g_nxhello.havepos)
    {
      (void)sem_wait(&g_nxhello.sem);
    }
  printf("nxhello_main: Screen resolution (%d,%d)\n", g_nxhello.xres, g_nxhello.yres);

  usleep(100000);
  return OK;

errout_with_nx:
  printf("nxhello_main: Close NX\n");
  nx_close(g_nxhello.hnx);
errout:
  return g_nxhello.code;
}

int nr_display_deinit(void)
{
  /* Release background */

  (void)nx_releasebkgd(g_nxhello.hbkgd);

  /* Close NX */
  printf("nxhello_main: Close NX\n");
  nx_close(g_nxhello.hnx);
  return g_nxhello.code;
}

int nr_display_str_in_pixel(char *str, int px, int py)
{
  nx_display_str_in_pixel(g_nxhello.hbkgd, str, px, py);
  return OK;
}

int nr_display_str(char *str, int x, int y)
{
  FAR struct nxgl_point_s pos;
  nx_display_conv_pos(&pos, x, y);
  nx_display_str_in_pixel(g_nxhello.hbkgd, str, pos.x, pos.y);
  return OK;
}

int nr_display_clear_row(int row)
{
  FAR struct nxgl_point_s pos;
  char clear_str[32];
  memset(clear_str, ' ', sizeof(clear_str));
  nx_display_conv_pos(&pos, 0, row);
  nx_display_str_in_pixel(g_nxhello.hbkgd, clear_str, pos.x, pos.y);
  return OK;
}

int nr_display_clear(void)
{
	int i;
	for (i = 0; i < 16; i++)
		nr_display_clear_row(i);
	return OK;
}

int nr_display_main(int argc, char *argv[])
{
  nxgl_mxpixel_t color;
  int ret;

  /* Initialize NX */

  ret = nxhello_initialize();
  printf("nxhello_main: NX handle=%p\n", g_nxhello.hnx);
  if (!g_nxhello.hnx || ret < 0)
    {
      printf("nxhello_main: Failed to get NX handle: %d\n", errno);
      g_nxhello.code = NXEXIT_NXOPEN;
      goto errout;
    }

  /* Get the default font handle */

  g_nxhello.hfont = nxf_getfonthandle(CONFIG_NEURON_DISPLAY_FONTID);
  if (!g_nxhello.hfont)
    {
      printf("nxhello_main: Failed to get font handle: %d\n", errno);
      g_nxhello.code = NXEXIT_FONTOPEN;
      goto errout;
    }

  /* Set the background to the configured background color */

  printf("nxhello_main: Set background color=%d\n",
         CONFIG_NEURON_DISPLAY_BGCOLOR);

  color = CONFIG_NEURON_DISPLAY_BGCOLOR;
  ret = nx_setbgcolor(g_nxhello.hnx, &color);
  if (ret < 0)
    {
      printf("nxhello_main: nx_setbgcolor failed: %d\n", errno);
      g_nxhello.code = NXEXIT_NXSETBGCOLOR;
      goto errout_with_nx;
    }

  /* Get the background window */

  ret = nx_requestbkgd(g_nxhello.hnx, &g_nxhellocb, NULL);
  if (ret < 0)
    {
      printf("nxhello_main: nx_setbgcolor failed: %d\n", errno);
      g_nxhello.code = NXEXIT_NXREQUESTBKGD;
      goto errout_with_nx;
    }

  /* Wait until we have the screen resolution.  We'll have this immediately
   * unless we are dealing with the NX server.
   */

  while (!g_nxhello.havepos)
    {
      (void)sem_wait(&g_nxhello.sem);
    }
  printf("nxhello_main: Screen resolution (%d,%d)\n", g_nxhello.xres, g_nxhello.yres);

  /* Now, say hello and exit, sleeping a little before each. */

  sleep(1);
  nxhello_hello(g_nxhello.hbkgd);
  sleep(1);

  /* Release background */

  (void)nx_releasebkgd(g_nxhello.hbkgd);

  /* Close NX */

errout_with_nx:
  printf("nxhello_main: Close NX\n");
  nx_close(g_nxhello.hnx);
errout:
  return g_nxhello.code;
}

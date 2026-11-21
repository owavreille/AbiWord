/* AbiSource Application Framework
 * Copyright (C) 1998 AbiSource, Inc.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
 * 02110-1301 USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include <io.h>

#include "ut_assert.h"
#include "ut_string.h"
#include "ut_path.h"
#include "ut_debugmsg.h"
#include "xap_Dialog_Id.h"
#include "xap_Win32Dlg_PrintPreview.h"
#include "xap_Frame.h"
#include "xap_App.h"
#include "xap_DialogFactory.h"
#include "gr_Win32Graphics.h"
#include "ut_rand.h"
#include "ut_Win32OS.h"

#include "fp_PageSize.h"
#include "fv_View.h"

#define INITGUID
#include <ole2.h>

#define GDIPCONST const
#define WINGDIPAPI __stdcall

typedef int GpStatus;
typedef int PixelFormat;

typedef void GpImage;
typedef void GpBitmap;
typedef void GpMetafile;
typedef void GpGraphics;

struct _GdiplusStartupInput
{
    guint32 GdiplusVersion;
    gpointer DebugEventCallback;
    gboolean SuppressBackgroundThread;
    gboolean SuppressExternalCodecs;
};
typedef struct _GdiplusStartupInput GdiplusStartupInput;

struct EncoderParameter
{
  GUID    Guid;
  ULONG   NumberOfValues;
  ULONG   Type;
  VOID*   Value;
};

struct EncoderParameters
{
  UINT Count;                      /* Number of parameters in this structure */
  EncoderParameter Parameter[1];   /* Parameter values */
};

typedef enum {
  EncoderParameterValueTypeByte = 1,
  EncoderParameterValueTypeASCII = 2,
  EncoderParameterValueTypeShort = 3,
  EncoderParameterValueTypeLong = 4,
  EncoderParameterValueTypeRational = 5,
  EncoderParameterValueTypeLongRange = 6,
  EncoderParameterValueTypeUndefined = 7,
  EncoderParameterValueTypeRationalRange = 8,
  EncoderParameterValueTypePointer = 9
} EncoderParameterValueType;

enum EncoderValue
  {
    EncoderValueColorTypeCMYK,
    EncoderValueColorTypeYCCK,
    EncoderValueCompressionLZW,
    EncoderValueCompressionCCITT3,
    EncoderValueCompressionCCITT4,
    EncoderValueCompressionRle,
    EncoderValueCompressionNone,
    EncoderValueScanMethodInterlaced,
    EncoderValueScanMethodNonInterlaced,
    EncoderValueVersionGif87,
    EncoderValueVersionGif89,
    EncoderValueRenderProgressive,
    EncoderValueRenderNonProgressive,
    EncoderValueTransformRotate90,
    EncoderValueTransformRotate180,
    EncoderValueTransformRotate270,
    EncoderValueTransformFlipHorizontal,
    EncoderValueTransformFlipVertical,
    EncoderValueMultiFrame,
    EncoderValueLastFrame,
    EncoderValueFlush,
    EncoderValueFrameDimensionTime,
    EncoderValueFrameDimensionResolution,
    EncoderValueFrameDimensionPage
  };

#define    PixelFormatIndexed      0x00010000 // Indexes into a palette
#define    PixelFormatGDI          0x00020000 // Is a GDI-supported format
#define    PixelFormatAlpha        0x00040000 // Has an alpha component
#define    PixelFormatPAlpha       0x00080000 // Pre-multiplied alpha
#define    PixelFormatExtended     0x00100000 // Extended color 16 bits/channel
#define    PixelFormatCanonical    0x00200000 

#define    PixelFormatUndefined       0
#define    PixelFormatDontCare        0

#define    PixelFormat1bppIndexed     (1 | ( 1 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define    PixelFormat4bppIndexed     (2 | ( 4 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define    PixelFormat8bppIndexed     (3 | ( 8 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define    PixelFormat16bppGrayScale  (4 | (16 << 8) | PixelFormatExtended)
#define    PixelFormat16bppRGB555     (5 | (16 << 8) | PixelFormatGDI)
#define    PixelFormat16bppRGB565     (6 | (16 << 8) | PixelFormatGDI)
#define    PixelFormat16bppARGB1555   (7 | (16 << 8) | PixelFormatAlpha | PixelFormatGDI)
#define    PixelFormat24bppRGB        (8 | (24 << 8) | PixelFormatGDI)
#define    PixelFormat32bppRGB        (9 | (32 << 8) | PixelFormatGDI)
#define    PixelFormat32bppARGB       (10 | (32 << 8) | PixelFormatAlpha | PixelFormatGDI | PixelFormatCanonical)
#define    PixelFormat32bppPARGB      (11 | (32 << 8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatGDI)
#define    PixelFormat48bppRGB        (12 | (48 << 8) | PixelFormatExtended)
#define    PixelFormat64bppARGB       (13 | (64 << 8) | PixelFormatAlpha  | PixelFormatCanonical | PixelFormatExtended)
#define    PixelFormat64bppPARGB      (14 | (64 << 8) | PixelFormatAlpha  | PixelFormatPAlpha | PixelFormatExtended)

typedef DWORD ARGB;

typedef GpStatus (WINGDIPAPI* GdiplusStartupFunc) (gpointer, const gpointer, gpointer);
typedef void (WINGDIPAPI* GdiplusShutdownFunc) (ULONG_PTR);
typedef GpStatus (WINGDIPAPI* GdipDisposeImageFunc) (GpImage*);
typedef GpStatus (WINGDIPAPI* GdipSaveImageToFileFunc) (GpImage *image, GDIPCONST WCHAR* filename, 
							GDIPCONST CLSID* clsidEncoder, 
							GDIPCONST EncoderParameters* encoderParams);
typedef GpStatus (WINGDIPAPI* GdipLoadImageFromFileFunc) (GDIPCONST WCHAR* filename, GpImage **image);
typedef GpStatus (WINGDIPAPI* GdipSaveAddFunc) (GpImage *image, GDIPCONST EncoderParameters* encoderParams);
typedef GpStatus (WINGDIPAPI* GdipSaveAddImageFunc) (GpImage *orig, GpImage *image, 
						     GDIPCONST EncoderParameters* encoderParams);
typedef GpStatus (WINGDIPAPI* GdipCreateMetafileFromEmfFunc) (HENHMETAFILE hEmf, BOOL deleteEmf, GpMetafile **image);
typedef GpStatus (WINGDIPAPI* GdipCreateMetafileFromFileFunc) (GDIPCONST WCHAR* file, GpMetafile **metafile);
typedef GpStatus (WINGDIPAPI* GdipCreateBitmapFromFileFunc) (GDIPCONST WCHAR* filename, GpBitmap **bitmap);
typedef GpStatus (WINGDIPAPI* GdipDeleteGraphicsFunc) (GpGraphics *graphics);
typedef GpStatus (WINGDIPAPI* GdipCreateBitmapFromScan0Func) (INT width, INT height, INT stride, PixelFormat format, BYTE* scan0,
                                                              GpBitmap** bitmap);
typedef GpStatus (WINGDIPAPI* GdipGetImageWidthFunc) (GpImage*, guint*);
typedef GpStatus (WINGDIPAPI* GdipGetImageHeightFunc) (GpImage*, guint*);
typedef GpStatus (WINGDIPAPI* GdipDrawImageIFunc) (GpGraphics *graphics, GpImage *image, INT x, INT y);
typedef GpStatus (WINGDIPAPI* GdipDrawImageRectIFunc) (GpGraphics *graphics, GpImage *image, INT x, INT y, INT width, INT height);
typedef GpStatus (WINGDIPAPI* GdipGetImageGraphicsContextFunc) (GpImage *image, GpGraphics **graphics);
typedef GpStatus (WINGDIPAPI* GdipFlushFunc) (GpGraphics *graphics, INT intention);
typedef GpStatus (WINGDIPAPI* GdipGraphicsClearFunc) (GpGraphics *graphics, ARGB color);
typedef GpStatus (WINGDIPAPI* GdipBitmapSetResolutionFunc) (GpBitmap* bitmap, float xdpi, float ydpi);
typedef GpStatus (WINGDIPAPI* GdipGetImageHorizontalResolutionFunc) (GpImage *image, float *resolution);
typedef GpStatus (WINGDIPAPI* GdipGetImageVerticalResolutionFunc) (GpImage *image, float *resolution);
struct BitmapData
{
  UINT Width;
  UINT Height;
  INT Stride;
  PixelFormat PixelFormat;
  void *Scan0;
  UINT_PTR Reserved;
};

typedef GpStatus (WINGDIPAPI* GdipBitmapLockBitsFunc) (GpBitmap *bitmap, const RECT *rect, UINT flags, PixelFormat format, BitmapData *locked);
typedef GpStatus (WINGDIPAPI* GdipBitmapUnlockBitsFunc) (GpBitmap *bitmap, BitmapData *locked);

static GdiplusStartupFunc GdiplusStartup = nullptr;
static GdiplusShutdownFunc GdiplusShutdown = nullptr;
static ULONG_PTR gdiplusToken = 0;
static bool gdiplusStarted = false;
static GdipDisposeImageFunc GdipDisposeImage = nullptr;
static GdipSaveImageToFileFunc GdipSaveImageToFile = nullptr;
static GdipLoadImageFromFileFunc GdipLoadImageFromFile = nullptr;
static GdipSaveAddImageFunc GdipSaveAddImage = nullptr;
static GdipSaveAddFunc GdipSaveAdd = nullptr;
static GdipCreateMetafileFromEmfFunc GdipCreateMetafileFromEmf = nullptr;
static GdipCreateMetafileFromFileFunc GdipCreateMetafileFromFile = nullptr;
static GdipCreateBitmapFromFileFunc GdipCreateBitmapFromFile = nullptr;
static GdipDeleteGraphicsFunc GdipDeleteGraphics = nullptr;
static GdipCreateBitmapFromScan0Func GdipCreateBitmapFromScan0 = nullptr;
static GdipGetImageWidthFunc GdipGetImageWidth = nullptr;
static GdipGetImageHeightFunc GdipGetImageHeight = nullptr;
static GdipDrawImageIFunc GdipDrawImageI = nullptr;
static GdipDrawImageRectIFunc GdipDrawImageRectI = nullptr;
static GdipGetImageGraphicsContextFunc GdipGetImageGraphicsContext = nullptr;
static GdipFlushFunc GdipFlush = nullptr;
static GdipGraphicsClearFunc GdipGraphicsClear = nullptr;
static GdipBitmapSetResolutionFunc GdipBitmapSetResolution = nullptr;
static GdipGetImageHorizontalResolutionFunc GdipGetImageHorizontalResolution = nullptr;
static GdipGetImageVerticalResolutionFunc GdipGetImageVerticalResolution = nullptr;
static GdipBitmapLockBitsFunc GdipBitmapLockBits = nullptr;
static GdipBitmapUnlockBitsFunc GdipBitmapUnlockBits = nullptr;

static GDIPCONST CLSID tiff_clsid = { 0x557cf405, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x0, 0x0, 0xf8, 0x1e, 0xf3, 0x2e } };
static GDIPCONST GUID EncoderSaveFlag = { 0x292266FC, 0xac40, 0x47bf, { 0x8c, 0xfc, 0xa8, 0x5b, 0x89, 0xa6,0x55, 0xde } };

#if 0
#define d(x) G_STMT_START { (x); } G_STMT_END
#else
#define d(x) G_STMT_START { } G_STMT_END
#endif

static HINSTANCE gdipluslib = nullptr;

static void
gdip_drop_library (void)
{
  if (gdipluslib)
    FreeLibrary (gdipluslib);
  gdipluslib = nullptr;
}

static void
s_remove_utf16_file (gunichar2 * & name)
{
  if (!name)
    return;
  gchar * utf8 = g_utf16_to_utf8 (name, -1, nullptr, nullptr, nullptr);
  if (utf8)
    {
      g_unlink (utf8);
      g_free (utf8);
    }
  g_free (name);
  name = nullptr;
}

// Paint is started with ShellExecute and opens the TIFF after we return.
// The last preview stays on disk until the next one replaces it. Deleting
// it in the destructor made Paint report that the file does not exist.
static gunichar2 * s_lastPreviewFile = nullptr;

static void
s_keep_preview_for_viewer (gunichar2 * & name)
{
  if (s_lastPreviewFile)
    s_remove_utf16_file (s_lastPreviewFile);
  s_lastPreviewFile = name;
  name = nullptr;
}

static void
gdip_shutdown (void)
{
  if (gdiplusStarted && GdiplusShutdown)
    GdiplusShutdown (gdiplusToken);
  gdiplusToken = 0;
  gdiplusStarted = false;
}

static GpStatus
gdip_init (void)
{
  GdiplusStartupInput input;

  if (gdiplusStarted)
    return 0;

  if (!gdipluslib)
    gdipluslib = LoadLibraryW (L"gdiplus.dll");
  if (!gdipluslib)
    return 18; // GdiplusNotInitialized

#define LOOKUP(func) \
  G_STMT_START { \
    func = reinterpret_cast<func##Func>(reinterpret_cast<void *>(GetProcAddress (gdipluslib, #func))); \
    if (!func) {\
      g_warning ("Couldn't load function: %s\n", #func); \
      gdip_drop_library (); \
      return 18; \
    } \
  } G_STMT_END

  LOOKUP (GdiplusStartup);
  LOOKUP (GdiplusShutdown);
  LOOKUP (GdipDisposeImage);
  LOOKUP (GdipSaveImageToFile);
  LOOKUP (GdipLoadImageFromFile);
  LOOKUP (GdipSaveAddImage);
  LOOKUP (GdipSaveAdd);
  LOOKUP (GdipCreateMetafileFromEmf);
  LOOKUP (GdipCreateMetafileFromFile);
  LOOKUP (GdipCreateBitmapFromFile);
  LOOKUP (GdipCreateBitmapFromScan0);
  LOOKUP (GdipDeleteGraphics);
  LOOKUP (GdipGetImageWidth);
  LOOKUP (GdipGetImageHeight);
  LOOKUP (GdipDrawImageI);
  LOOKUP (GdipDrawImageRectI);
  LOOKUP (GdipGetImageGraphicsContext);
  LOOKUP (GdipFlush);
  LOOKUP (GdipGraphicsClear);
  LOOKUP (GdipBitmapSetResolution);
  LOOKUP (GdipGetImageHorizontalResolution);
  LOOKUP (GdipGetImageVerticalResolution);
  LOOKUP (GdipBitmapLockBits);
  LOOKUP (GdipBitmapUnlockBits);

#undef LOOKUP

  input.GdiplusVersion = 1;
  input.DebugEventCallback = nullptr;
  input.SuppressBackgroundThread = input.SuppressExternalCodecs = FALSE;

  GpStatus status = GdiplusStartup (&gdiplusToken, &input, nullptr);
  if (status != 0)
    {
      gdip_drop_library ();
      gdiplusToken = 0;
      return status;
    }
  gdiplusStarted = true;
  return 0;
}

#define ASSERT_GDIP_SUCCESS(status, func) \
  G_STMT_START { \
    GpStatus _gdip_status = (status); \
    if (_gdip_status != 0) { \
      g_warning ("%s failed with reason %d\n", #func, _gdip_status); \
    } \
  } G_STMT_END

// PlayEnhMetaFile maps the picture onto the destination rectangle. A bitmap
// the size of the screen (often 96 dpi) is what made the TIFF look soft.
// 300 dpi is a print resolution. The long side stays capped so an A0 page
// cannot allocate a bitmap of several hundred megabytes.
static const double s_previewDpi = 300.0;
static const guint s_previewMaxSide = 8192;

static void
s_previewPixelSize (const RECT & page, guint & width, guint & height, float & dpiX, float & dpiY)
{
  double widthMm = (double)(page.right - page.left) / 100.0;
  double heightMm = (double)(page.bottom - page.top) / 100.0;
  if (widthMm < 1.0)
    widthMm = 210.0;
  if (heightMm < 1.0)
    heightMm = 297.0;

  width = (guint)(widthMm / 25.4 * s_previewDpi + 0.5);
  height = (guint)(heightMm / 25.4 * s_previewDpi + 0.5);
  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  const guint longSide = (width > height) ? width : height;
  if (longSide > s_previewMaxSide)
    {
      const double scale = (double)s_previewMaxSide / (double)longSide;
      width = (guint)((double)width * scale);
      height = (guint)((double)height * scale);
      if (width < 1)
        width = 1;
      if (height < 1)
        height = 1;
    }

  dpiX = (float)((double)width * 25.4 / widthMm);
  dpiY = (float)((double)height * 25.4 / heightMm);
}

// The highest-resolution printer made an A3 preview allocate a bitmap of
// several hundred megabytes and abort. Recording still uses the screen DC.
// The TIFF is painted afterwards at s_previewDpi.
static HDC s_previewReferenceDC ()
{
  HDC dc = CreateDCW (L"DISPLAY", nullptr, nullptr, nullptr);
  if (!dc)
    dc = GR_Win32Graphics::createbestmetafilehdc ();
  return dc;
}

class ABI_EXPORT GR_Win32PrintPreviewGraphics : public GR_Win32Graphics
{
public:

  GR_Win32PrintPreviewGraphics(const RECT &rect)
    : GR_Win32Graphics(s_previewReferenceDC(), getDocInfo()),
      m_pGraphics(nullptr),
      page_rect(rect),
      metafile_dc(nullptr),
      m_multiPageTiff(nullptr),
      m_tiffFilename(nullptr),
      m_previewLaunched(false),
      m_previewFailed(false)
  {
    takeOwnershipOfDCAndDocInfo();

    GpStatus status = gdip_init ();
    ASSERT_GDIP_SUCCESS (status, gdip_init);
    if (status != 0)
      throw "GDI+ initialization failed";

    d (g_print ("gdip_init() was successful. preview graphics created\n"));

    metafile_xres = GetDeviceCaps(getPrimaryDC(), LOGPIXELSX);
    metafile_yres = GetDeviceCaps(getPrimaryDC(), LOGPIXELSY);

    d (g_print ("xres: %d | yres: %d\n", metafile_xres, metafile_yres));
  }

  virtual ~GR_Win32PrintPreviewGraphics()
  {
    d (g_print ("~GR_Win32PrintPreviewGraphics()\n"));
	if (m_multiPageTiff)
      GdipDisposeImage (m_multiPageTiff);
    if (m_previewLaunched)
      s_keep_preview_for_viewer (m_tiffFilename);
    else
      s_remove_utf16_file (m_tiffFilename);
    gdip_shutdown ();

    // todo: delete DC and docInfo
  }

  virtual UT_uint32 getClassId()  override {return s_getClassId();}
  
  virtual GR_Capability getCapability()  override {return GRCAP_PRINTER_ONLY;}
  
  static const char * graphicsDescriptor(){return "Win32 Print Preview";}

  virtual void drawGlyph(UT_uint32 glyph_idx, UT_sint32 xoff, UT_sint32 yoff)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("drawGlyph()\n"));
    m_pGraphics->drawGlyph(glyph_idx, xoff, yoff);
  }

  virtual void drawChar(UT_UCS4Char Char, UT_sint32 xoff, UT_sint32 yoff)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("drawChar()\n"));
    m_pGraphics->drawChar(Char, xoff, yoff);
  }

  virtual void drawChars(const UT_UCS4Char* pChars,
			 int iCharOffset, int iLength,
			 UT_sint32 xoff, UT_sint32 yoff,
			 int * pCharWidth)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("drawChars()\n"));
    m_pGraphics->drawChars(pChars, iCharOffset, iLength, xoff, yoff, pCharWidth);
  }

  virtual void setFont(const GR_Font* pFont)
   override {
    if (m_pGraphics) {
      m_pGraphics->setFont(pFont);
      d (g_print ("setFont()\n"));
    }
    else
      GR_Win32Graphics::setFont(pFont);
  }

  virtual void clearFont(void)
   override {
    if (m_pGraphics) {
      m_pGraphics->clearFont();
      d (g_print ("clearFont()\n"));
    }
    else
      GR_Win32Graphics::clearFont();
  }

  virtual void setColor(const UT_RGBColor& clr)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("setColor()\n"));
    m_pGraphics->setColor(clr);
  }

  virtual void getColor(UT_RGBColor& clr)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("getColor()\n"));
    m_pGraphics->getColor(clr);
  }

  virtual void drawLine(UT_sint32 a, UT_sint32 b, UT_sint32 c, UT_sint32 d)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("drawLine()\n"));
    m_pGraphics->drawLine(a, b, c, d);
  }

  virtual void xorLine(UT_sint32 a, UT_sint32 b, UT_sint32 c, UT_sint32 d)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("xorLine()\n"));
    m_pGraphics->xorLine(a, b, c, d);
  }

  virtual void setLineWidth(UT_sint32 w)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("setLineWidth()\n"));
    m_pGraphics->setLineWidth(w);
  }

  virtual void setLineProperties ( double inWidthPixels,
				   JoinStyle inJoinStyle = JOIN_MITER,
				   CapStyle inCapStyle   = CAP_BUTT,
				   LineStyle inLineStyle = LINE_SOLID )
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("setLineProperties()\n"));
    m_pGraphics->setLineProperties(inWidthPixels, inJoinStyle, inCapStyle, inLineStyle);
  }

  virtual void polyLine(const UT_Point * pts, UT_uint32 nPoints)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("polyLine()\n"));
    m_pGraphics->polyLine(pts, nPoints);
  }

  virtual void fillRect(const UT_RGBColor& c,
			UT_sint32 x, UT_sint32 y,
			UT_sint32 w, UT_sint32 h)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("fillRect()\n"));
    m_pGraphics->fillRect(c, x, y, w, h);
  }

  virtual void invertRect(const UT_Rect* pRect)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("invertRect()\n"));
    m_pGraphics->invertRect(pRect);
  }

  virtual void setClipRect(const UT_Rect* pRect)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("setClipRect()\n"));
    m_pGraphics->setClipRect(pRect);
  }

  virtual void clearArea(UT_sint32 a, UT_sint32 b, UT_sint32 c, UT_sint32 d)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("clearArea()\n"));
    m_pGraphics->clearArea(a, b, c, d);
  }

  virtual GR_Image* createNewImage(const char* pszName, const UT_ConstByteBufPtr& pBB, const std::string& mimetype,
				   UT_sint32 iDisplayWidth, UT_sint32 iDisplayHeight,
				   GR_Image::GRType iType)
   override {
    d (g_print ("createNewImage()\n"));
    if (m_pGraphics)
      return m_pGraphics->createNewImage(pszName, pBB, mimetype, iDisplayWidth, iDisplayHeight, iType);
    else
      return GR_Win32Graphics::createNewImage(pszName, pBB, mimetype, iDisplayWidth, iDisplayHeight, iType);
  }

  virtual void drawImage(GR_Image* pImg, UT_sint32 xDest, UT_sint32 yDest)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("drawImage()\n"));
    m_pGraphics->drawImage(pImg, xDest, yDest);
  }

  virtual bool queryProperties(GR_Graphics::Properties gp) const
   override {
    d (g_print ("queryProperties()\n"));
    return (gp == DGP_PAPER);
  }

  virtual void polygon(const UT_RGBColor& c, const UT_Point *pts, UT_uint32 nPoints)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("polygon()\n"));
    m_pGraphics->polygon(c, pts, nPoints);
  }

  virtual GR_Image * genImageFromRectangle(const UT_Rect & r)
   override {
    UT_return_val_if_fail(m_pGraphics != nullptr, nullptr);
    d (g_print ("genImageFromRectangle()\n"));
    return m_pGraphics->genImageFromRectangle(r);
  }

  virtual void saveRectangle(UT_Rect & r, UT_uint32 iIndx)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("saveRectangle()\n"));
    m_pGraphics->saveRectangle(r, iIndx);
  }

  virtual void restoreRectangle(UT_uint32 iIndx)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("restoreRectangle()\n"));
    m_pGraphics->restoreRectangle(iIndx);
  }

  virtual void flush(void)
   override {
    UT_return_if_fail(m_pGraphics != nullptr);
    d (g_print ("flush()\n"));
    m_pGraphics->flush();
  }

  virtual bool startPrint(void)
   override {
    s_remove_utf16_file (m_tiffFilename);
    std::string sName = UT_createTmpFile("pr", ".tif");

    d (g_print ("saving to %s\n", sName.c_str()));
    m_tiffFilename = g_utf8_to_utf16 (sName.c_str(), -1, nullptr, nullptr, nullptr);

    return true;
  }

  virtual bool startPage(const char * /*szPageLabel*/, UT_uint32 /*pageNumber*/,
			 bool /*bPortrait*/, UT_uint32 /*iWidth*/, UT_uint32 /*iHeight*/)
   override {
    endPage(); // prints any previous pages and clears the font cache

    d (g_print ("startPage(%d)\n", pageNumber));

    if (!getPrimaryDC())
      return false;

    metafile_dc = CreateEnhMetaFileW (getPrimaryDC(), nullptr, &page_rect, L"AbiWord\0Print Preview\0\0");
    if (!metafile_dc)
      return false;

    GR_Win32AllocInfo ai(metafile_dc, m_pDocInfo, nullptr);
    m_pGraphics = (GR_Win32Graphics *)XAP_App::getApp()->newGraphics(ai);
    if (!m_pGraphics)
      {
	HENHMETAFILE dead = CloseEnhMetaFile (metafile_dc);
	if (dead)
	  DeleteEnhMetaFile (dead);
	metafile_dc = nullptr;
	return false;
      }

    return true;
  }

  virtual bool endPrint(void)
   override {
    endPage();

    if (m_previewFailed || !m_multiPageTiff || !m_tiffFilename)
      return false;

    EncoderParameters encoder_params;
    ULONG parameterValue = EncoderValueFlush;

    encoder_params.Count = 1;
    encoder_params.Parameter[0].Guid = EncoderSaveFlag;
    encoder_params.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoder_params.Parameter[0].NumberOfValues = 1;
    encoder_params.Parameter[0].Value = &parameterValue;

    GpStatus status = GdipSaveAdd (m_multiPageTiff, &encoder_params);
    ASSERT_GDIP_SUCCESS (status, GdipSaveAdd);
    if (status != 0)
      {
	GdipDisposeImage (m_multiPageTiff);
	m_multiPageTiff = nullptr;
	m_previewFailed = true;
	return false;
      }

    status = GdipDisposeImage (m_multiPageTiff);
    m_multiPageTiff = nullptr;
    ASSERT_GDIP_SUCCESS (status, GdipDisposeImage);

    d (g_print("endPrint()\n"));

    gchar * utf8 = g_utf16_to_utf8 (m_tiffFilename, -1, nullptr, nullptr, nullptr);
    const bool exists = utf8 && g_file_test (utf8, G_FILE_TEST_EXISTS);
    g_free (utf8);
    if (!exists)
      return false;

    const INT_PTR opened = (INT_PTR)ShellExecuteW(nullptr, L"open", (WCHAR *)m_tiffFilename, nullptr, nullptr, SW_SHOW);
    m_previewLaunched = opened > 32;
    return m_previewLaunched;
  }

  void endPage()
  {
    invalidateCache(); // clear the font cache

    d (g_print ("endPage()\n"));
    if (metafile_dc)
      {
	GpStatus status;
	HENHMETAFILE metafile;
	
	metafile = CloseEnhMetaFile (metafile_dc);
	metafile_dc = nullptr;
	DELETEP(m_pGraphics);
	d (g_print ("metafile = %p\n", metafile));
	if (!metafile)
	  {
	    m_previewFailed = true;
	    return;
	  }

	// saveaddimage() needs a bitmap. PlayEnhMetaFile replays the
	// vectors into that bitmap, so the page is not a scaled screenshot.
	// https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-playenhmetafile
	guint destW = 0, destH = 0;
	float dpiX = (float)s_previewDpi, dpiY = (float)s_previewDpi;
	s_previewPixelSize (page_rect, destW, destH, dpiX, dpiY);
	d (g_print ("w: %d | h: %d\n", destW, destH));

	BITMAPINFO bi;
	memset (&bi, 0, sizeof (bi));
	bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = (LONG)destW;
	bi.bmiHeader.biHeight = -((LONG)destH);
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void *bits = nullptr;
	HBITMAP dib = CreateDIBSection (nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	HDC mem = dib ? CreateCompatibleDC (nullptr) : nullptr;
	if (!dib || !mem)
	  {
	    if (mem)
	      DeleteDC (mem);
	    if (dib)
	      DeleteObject (dib);
	    DeleteEnhMetaFile (metafile);
	    m_previewFailed = true;
	    return;
	  }

	HGDIOBJ previous = SelectObject (mem, dib);
	RECT paint = { 0, 0, (LONG)destW, (LONG)destH };
	HBRUSH white = CreateSolidBrush (RGB (255, 255, 255));
	FillRect (mem, &paint, white);
	DeleteObject (white);
	SetStretchBltMode (mem, HALFTONE);
	SetBrushOrgEx (mem, 0, 0, nullptr);
	const BOOL played = PlayEnhMetaFile (mem, metafile, &paint);
	SelectObject (mem, previous);
	DeleteDC (mem);
	DeleteEnhMetaFile (metafile);
	if (!played)
	  {
	    DeleteObject (dib);
	    m_previewFailed = true;
	    return;
	  }

	GpImage *meta = nullptr;
	status = GdipCreateBitmapFromScan0 ((INT)destW, (INT)destH, 0, PixelFormat32bppRGB, nullptr, &meta);
	ASSERT_GDIP_SUCCESS (status, GdipCreateBitmapFromScan0);
	if (status != 0 || !meta)
	  {
	    DeleteObject (dib);
	    m_previewFailed = true;
	    return;
	  }

	// 32bppRGB keeps the page opaque. A 32bpp HBITMAP would be read as
	// transparent because GDI leaves the alpha byte at zero.
	BitmapData locked;
	memset (&locked, 0, sizeof (locked));
	status = GdipBitmapLockBits (meta, nullptr, 3 /* ImageLockModeRead | ImageLockModeWrite */, PixelFormat32bppRGB, &locked);
	if (status != 0 || !locked.Scan0)
	  {
	    DeleteObject (dib);
	    GdipDisposeImage (meta);
	    m_previewFailed = true;
	    return;
	  }
	const INT srcStride = (INT)destW * 4;
	for (guint y = 0; y < destH; y++)
	  memcpy ((BYTE *)locked.Scan0 + y * locked.Stride, (BYTE *)bits + y * srcStride, srcStride);
	GdipBitmapUnlockBits (meta, &locked);
	DeleteObject (dib);

	status = GdipBitmapSetResolution (meta, dpiX, dpiY);
	ASSERT_GDIP_SUCCESS (status, GdipBitmapSetResolution);

	if (!m_multiPageTiff)
	  {
	    EncoderParameters encoder_params;
	    ULONG parameterValue = EncoderValueMultiFrame;

	    encoder_params.Count = 1;
	    encoder_params.Parameter[0].Guid = EncoderSaveFlag;
	    encoder_params.Parameter[0].Type = EncoderParameterValueTypeLong;
	    encoder_params.Parameter[0].NumberOfValues = 1;
	    encoder_params.Parameter[0].Value = &parameterValue;

	    if (!m_tiffFilename)
	      {
		GdipDisposeImage (meta);
		m_previewFailed = true;
		return;
	      }
	    status = GdipSaveImageToFile (meta, (WCHAR *)m_tiffFilename, &tiff_clsid, &encoder_params);
	    ASSERT_GDIP_SUCCESS (status, GdipSaveImageToFile);
	    if (status != 0)
	      {
		GdipDisposeImage (meta);
		m_previewFailed = true;
		return;
	      }

	    d (g_print ("Saved EMF to file\n"));

	    m_multiPageTiff = meta;
	  }
	else
	  {
	    EncoderParameters encoder_params;
	    ULONG parameterValue = EncoderValueFrameDimensionPage;

	    encoder_params.Count = 1;
	    encoder_params.Parameter[0].Guid = EncoderSaveFlag;
	    encoder_params.Parameter[0].Type = EncoderParameterValueTypeLong;
	    encoder_params.Parameter[0].NumberOfValues = 1;
	    encoder_params.Parameter[0].Value = &parameterValue;

	    status = GdipSaveAddImage (m_multiPageTiff, meta, &encoder_params);
	    ASSERT_GDIP_SUCCESS (status, GdipSaveAddImage);
	    if (status != 0)
	      m_previewFailed = true;

	    d (g_print ("SaveAddImage\n"));

	    GdipDisposeImage (meta);
	  }
      }
  }

protected:

  virtual GR_Font* _findFont(const char* pszFontFamily,
			     const char* pszFontStyle,
			     const char* pszFontVariant,
			     const char* pszFontWeight,
			     const char* pszFontStretch,
			     const char* pszFontSize,
			     const char* pszLang)
   override {
    if (!m_pGraphics)
      return GR_Win32Graphics::_findFont(pszFontFamily, pszFontStyle, pszFontVariant, pszFontWeight, pszFontStretch, pszFontSize, pszLang);
    else
      return m_pGraphics->findFont(pszFontFamily, pszFontStyle, pszFontVariant, pszFontWeight, pszFontStretch, pszFontSize, pszLang);
  }

  GR_Win32Graphics *m_pGraphics;
  RECT page_rect;
  HDC metafile_dc;
  GpBitmap *m_multiPageTiff;
  gunichar2 * m_tiffFilename;
  bool m_previewLaunched;
  bool m_previewFailed;
  int metafile_xres;
  int metafile_yres;
};

XAP_Win32Dialog_PrintPreview::XAP_Win32Dialog_PrintPreview(XAP_DialogFactory * pDlgFactory, XAP_Dialog_Id id)
  : XAP_Dialog_PrintPreview(pDlgFactory,id), m_pPrintGraphics(nullptr), m_emfFilename(nullptr)
{
}

XAP_Win32Dialog_PrintPreview::~XAP_Win32Dialog_PrintPreview(void)
{
  DELETEP(m_pPrintGraphics);
  g_free(m_emfFilename);
}

XAP_Dialog * XAP_Win32Dialog_PrintPreview::static_constructor(XAP_DialogFactory * pFactory,
							      XAP_Dialog_Id id)
{
  return new XAP_Win32Dialog_PrintPreview (pFactory,id);
}

GR_Graphics * XAP_Win32Dialog_PrintPreview::getPrinterGraphicsContext(void)
{
  return m_pPrintGraphics;
}

void XAP_Win32Dialog_PrintPreview::releasePrinterGraphicsContext(GR_Graphics * /*pGraphics*/)
{
  DELETEP(m_pPrintGraphics);
}

void XAP_Win32Dialog_PrintPreview::runModal(XAP_Frame * pFrame)
{
  RECT rect;

  FV_View * pView = static_cast<FV_View*>(pFrame->getCurrentView());

  rect.left = 0;
  rect.right = (LONG)(pView->getPageSize().Width (DIM_MM) * 100);
  rect.top = 0;
  rect.bottom = (LONG)(pView->getPageSize().Height (DIM_MM) * 100);

  try
    {
      m_pPrintGraphics = new GR_Win32PrintPreviewGraphics(rect);
    }
  catch (...)
    {
      m_pPrintGraphics = nullptr;
    }
}

/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */
/* AbiWord
 * Copyright (C) 2008 Robert Staudinger
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

#include <string.h>
#include <setjmp.h>

#include "gr_Win32CairoPrintGraphics.h"
#include "ut_bytebuf.h"
#include "ut_jpeg.h"

#include <png.h>

static const UT_uint32 c_MaxImageSide = 16384;

class GR_Win32CairoImage : public GR_CairoRasterImage
{
public:
	explicit GR_Win32CairoImage(const char* name);
	virtual ~GR_Win32CairoImage();

	virtual bool convertToBuffer(UT_ConstByteBufPtr &) const override { return false; }
	virtual bool convertFromBuffer(const UT_ConstByteBufPtr & pBB,
								   const std::string& mimetype,
								   UT_sint32 iDisplayWidth,
								   UT_sint32 iDisplayHeight) override;
	virtual bool hasAlpha(void) const override { return m_hasAlpha; }
	virtual bool isTransparentAt(UT_sint32 x, UT_sint32 y) override;
	virtual void cairoSetSource(cairo_t *cr) override;
	virtual cairo_surface_t* getCairoSurface() const override { return m_surface; }

protected:
	virtual GR_CairoRasterImage *makeSubimage(const std::string & name,
											  UT_sint32 x, UT_sint32 y,
											  UT_sint32 w, UT_sint32 h) const override;

private:
	bool loadPng(const UT_ConstByteBufPtr & pBB);
	bool loadJpeg(const UT_ConstByteBufPtr & pBB);
	void adoptSurface(cairo_surface_t *surface, bool hasAlpha);

	cairo_surface_t *m_surface;
	bool m_hasAlpha;
};

class GR_Win32CairoVectorImage : public GR_CairoVectorImage
{
public:
	explicit GR_Win32CairoVectorImage(const char* name)
	{
		setName(name ? name : "Win32CairoVectorImage");
	}

	virtual void cairoSetSource(cairo_t *) override {}
	virtual void renderToCairo(cairo_t *) override {}
};

struct PngReader
{
	const UT_Byte* data;
	UT_uint32 size;
	UT_uint32 pos;
};

static void pngRead(png_structp png, png_bytep data, png_size_t length)
{
	PngReader* reader = static_cast<PngReader*>(png_get_io_ptr(png));
	if (!reader || !reader->data)
	{
		png_error(png, "PNG read failed");
		return;
	}
	if (reader->pos > reader->size || length > (png_size_t)(reader->size - reader->pos))
	{
		png_error(png, "PNG truncated");
		return;
	}
	memcpy(data, reader->data + reader->pos, length);
	reader->pos += (UT_uint32)length;
}

static void premultiply(unsigned char* row, png_uint_32 width)
{
	for (png_uint_32 x = 0; x < width; x++)
	{
		const unsigned int a = row[3];
		row[0] = (unsigned char)((row[0] * a + 127) / 255);
		row[1] = (unsigned char)((row[1] * a + 127) / 255);
		row[2] = (unsigned char)((row[2] * a + 127) / 255);
		row += 4;
	}
}

GR_Win32CairoImage::GR_Win32CairoImage(const char* name)
	: m_surface(nullptr),
	  m_hasAlpha(false)
{
	setName(name ? name : "Win32CairoImage");
}

GR_Win32CairoImage::~GR_Win32CairoImage()
{
	if (m_surface)
		cairo_surface_destroy(m_surface);
}

void GR_Win32CairoImage::adoptSurface(cairo_surface_t *surface, bool hasAlpha)
{
	if (m_surface)
		cairo_surface_destroy(m_surface);
	m_surface = surface;
	m_hasAlpha = hasAlpha;
}

bool GR_Win32CairoImage::loadPng(const UT_ConstByteBufPtr & pBB)
{
	png_bytep volatile pngRow = nullptr;
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png)
		return false;
	png_infop info = png_create_info_struct(png);
	if (!info)
	{
		png_destroy_read_struct(&png, nullptr, nullptr);
		return false;
	}
	if (setjmp(png_jmpbuf(png)))
	{
		g_free(pngRow);
		png_destroy_read_struct(&png, &info, nullptr);
		if (m_surface)
		{
			cairo_surface_destroy(m_surface);
			m_surface = nullptr;
		}
		return false;
	}

	PngReader reader;
	reader.data = pBB->getPointer(0);
	reader.size = pBB->getLength();
	reader.pos = 0;
	png_set_read_fn(png, &reader, pngRead);
	png_read_info(png, info);

	png_uint_32 width = 0;
	png_uint_32 height = 0;
	int bitDepth = 0;
	int colorType = 0;
	png_get_IHDR(png, info, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr);
	(void)bitDepth;
	(void)colorType;
	if (width == 0 || height == 0 || width > c_MaxImageSide || height > c_MaxImageSide)
	{
		png_destroy_read_struct(&png, &info, nullptr);
		return false;
	}

	png_set_packing(png);
	png_set_expand(png);
	png_set_strip_16(png);
	png_set_gray_to_rgb(png);
	png_set_bgr(png);
	const int passes = png_set_interlace_handling(png);
	png_read_update_info(png, info);

	const bool hasAlpha = png_get_channels(png, info) == 4;
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)width, (int)height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		cairo_surface_destroy(surface);
		png_destroy_read_struct(&png, &info, nullptr);
		return false;
	}
	adoptSurface(surface, hasAlpha);

	unsigned char* pixels = cairo_image_surface_get_data(m_surface);
	const int stride = cairo_image_surface_get_stride(m_surface);
	if (!hasAlpha)
		pngRow = static_cast<png_bytep>(g_try_malloc(width * 3));
	if (!hasAlpha && !pngRow)
	{
		png_destroy_read_struct(&png, &info, nullptr);
		return false;
	}

	// Adam7 needs one full pass per interlace stage. png_combine_row writes
	// into the same row, so earlier passes must stay until the last one.
	for (int pass = 0; pass < passes; pass++)
	{
		const bool last = pass + 1 == passes;
		for (png_uint_32 y = 0; y < height; y++)
		{
			unsigned char* dest = pixels + y * stride;
			if (hasAlpha)
				png_read_row(png, dest, nullptr);
			else
				png_read_row(png, pngRow, nullptr);
			if (!last)
				continue;
			if (hasAlpha)
			{
				premultiply(dest, width);
				continue;
			}
			for (png_uint_32 x = 0; x < width; x++)
			{
				dest[x * 4 + 0] = pngRow[x * 3 + 0];
				dest[x * 4 + 1] = pngRow[x * 3 + 1];
				dest[x * 4 + 2] = pngRow[x * 3 + 2];
				dest[x * 4 + 3] = 255;
			}
		}
	}
	g_free(pngRow);
	pngRow = nullptr;
	cairo_surface_mark_dirty(m_surface);
	png_read_end(png, info);
	png_destroy_read_struct(&png, &info, nullptr);
	return true;
}

bool GR_Win32CairoImage::loadJpeg(const UT_ConstByteBufPtr & pBB)
{
	UT_sint32 width = 0;
	UT_sint32 height = 0;
	if (!UT_JPEG_getDimensions(pBB, width, height))
		return false;
	if (width <= 0 || height <= 0 || (UT_uint32)width > c_MaxImageSide || (UT_uint32)height > c_MaxImageSide)
		return false;

	const UT_sint32 srcStride = width * 3;
	UT_Byte* rgb = static_cast<UT_Byte*>(g_try_malloc((size_t)srcStride * (size_t)height));
	if (!rgb)
		return false;
	if (!UT_JPEG_getRGBData(pBB, rgb, srcStride, true, false))
	{
		g_free(rgb);
		return false;
	}

	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		cairo_surface_destroy(surface);
		g_free(rgb);
		return false;
	}
	adoptSurface(surface, false);
	unsigned char* pixels = cairo_image_surface_get_data(m_surface);
	const int stride = cairo_image_surface_get_stride(m_surface);
	for (UT_sint32 y = 0; y < height; y++)
	{
		const UT_Byte* src = rgb + (size_t)y * (size_t)srcStride;
		unsigned char* dest = pixels + y * stride;
		for (UT_sint32 x = 0; x < width; x++)
		{
			dest[x * 4 + 0] = src[x * 3 + 0];
			dest[x * 4 + 1] = src[x * 3 + 1];
			dest[x * 4 + 2] = src[x * 3 + 2];
			dest[x * 4 + 3] = 255;
		}
	}
	g_free(rgb);
	cairo_surface_mark_dirty(m_surface);
#ifdef CAIRO_MIME_TYPE_JPEG
	if (pBB->getLength() > 0)
	{
		unsigned char* jpegCopy = static_cast<unsigned char*>(g_malloc(pBB->getLength()));
		if (jpegCopy)
		{
			memcpy(jpegCopy, pBB->getPointer(0), pBB->getLength());
			if (cairo_surface_set_mime_data(m_surface, CAIRO_MIME_TYPE_JPEG,
											jpegCopy, pBB->getLength(),
											g_free, jpegCopy) != CAIRO_STATUS_SUCCESS)
			{
				g_free(jpegCopy);
			}
		}
	}
#endif
	return true;
}

bool GR_Win32CairoImage::convertFromBuffer(const UT_ConstByteBufPtr & pBB,
										   const std::string& mimetype,
										   UT_sint32 iDisplayWidth,
										   UT_sint32 iDisplayHeight)
{
	if (!pBB || pBB->getLength() < 2)
		return false;

	const UT_Byte* data = pBB->getPointer(0);
	const bool png = (mimetype == "image/png")
		|| (pBB->getLength() >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0);
	const bool jpeg = (mimetype == "image/jpeg" || mimetype == "image/jpg")
		|| (data[0] == 0xff && data[1] == 0xd8);
	bool ok = false;
	if (png)
		ok = loadPng(pBB);
	else if (jpeg)
		ok = loadJpeg(pBB);
	if (!ok || !m_surface)
		return false;

	if (iDisplayWidth <= 0)
		iDisplayWidth = cairo_image_surface_get_width(m_surface);
	if (iDisplayHeight <= 0)
		iDisplayHeight = cairo_image_surface_get_height(m_surface);
	setDisplaySize(iDisplayWidth, iDisplayHeight);
	return true;
}

bool GR_Win32CairoImage::isTransparentAt(UT_sint32 x, UT_sint32 y)
{
	if (!m_hasAlpha || !m_surface)
		return false;
	const UT_sint32 dispW = getDisplayWidth();
	const UT_sint32 dispH = getDisplayHeight();
	const UT_sint32 srcW = cairo_image_surface_get_width(m_surface);
	const UT_sint32 srcH = cairo_image_surface_get_height(m_surface);
	if (dispW <= 0 || dispH <= 0 || srcW <= 0 || srcH <= 0)
		return false;
	if (x < 0 || y < 0 || x >= dispW || y >= dispH)
		return false;
	const UT_sint32 sx = (UT_sint32)(((double)x * srcW) / dispW);
	const UT_sint32 sy = (UT_sint32)(((double)y * srcH) / dispH);
	if (sx < 0 || sy < 0 || sx >= srcW || sy >= srcH)
		return false;
	const int stride = cairo_image_surface_get_stride(m_surface);
	const unsigned char* pixel = cairo_image_surface_get_data(m_surface) + sy * stride + sx * 4;
	return pixel[3] == 0;
}

void GR_Win32CairoImage::cairoSetSource(cairo_t *cr)
{
	if (!m_surface)
		return;
	const double srcW = cairo_image_surface_get_width(m_surface);
	const double srcH = cairo_image_surface_get_height(m_surface);
	if (srcW <= 0 || srcH <= 0)
		return;
	cairo_scale(cr, (double)getDisplayWidth() / srcW, (double)getDisplayHeight() / srcH);
	cairo_set_source_surface(cr, m_surface, 0, 0);
}

GR_CairoRasterImage *GR_Win32CairoImage::makeSubimage(const std::string & name,
													  UT_sint32 x, UT_sint32 y,
													  UT_sint32 w, UT_sint32 h) const
{
	if (!m_surface || w <= 0 || h <= 0)
		return nullptr;
	const double srcW = cairo_image_surface_get_width(m_surface);
	const double srcH = cairo_image_surface_get_height(m_surface);
	const double dispW = getDisplayWidth();
	const double dispH = getDisplayHeight();
	if (srcW <= 0.0 || srcH <= 0.0 || dispW <= 0.0 || dispH <= 0.0)
		return nullptr;
	GR_Win32CairoImage *image = new GR_Win32CairoImage(name.c_str());
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		cairo_surface_destroy(surface);
		delete image;
		return nullptr;
	}
	cairo_t *cr = cairo_create(surface);
	cairo_translate(cr, -x, -y);
	cairo_scale(cr, dispW / srcW, dispH / srcH);
	cairo_set_source_surface(cr, m_surface, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_mark_dirty(surface);
	image->adoptSurface(surface, m_hasAlpha);
	image->setDisplaySize(w, h);
	return image;
}

GR_CairoPrintGraphics::GR_CairoPrintGraphics(cairo_t *cr, UT_uint32 iDeviceResolution)
	: GR_CairoGraphics(cr, iDeviceResolution),
	  m_bDoShowPage(false),
	  m_dResRatio(1.0)
{
}

GR_CairoPrintGraphics::~GR_CairoPrintGraphics()
{
}

bool GR_CairoPrintGraphics::queryProperties(GR_Graphics::Properties gp) const
{
	switch (gp)
	{
		case DGP_SCREEN:
		case DGP_OPAQUEOVERLAY:
			return false;
		case DGP_PAPER:
			return true;
		default:
			return false;
	}
}

bool GR_CairoPrintGraphics::startPrint(void)
{
	m_bDoShowPage = false;
	return true;
}

bool GR_CairoPrintGraphics::startPage(const char *, UT_uint32,
									  bool, UT_uint32, UT_uint32)
{
	if (m_bDoShowPage && m_cr)
		cairo_show_page(m_cr);
	m_bDoShowPage = true;
	return true;
}

bool GR_CairoPrintGraphics::endPrint(void)
{
	if (m_bDoShowPage && m_cr)
		cairo_show_page(m_cr);
	return true;
}

void GR_CairoPrintGraphics::queueDraw(const UT_Rect*)
{
}

GR_Font * GR_CairoPrintGraphics::getGUIFont(void)
{
	return nullptr;
}

GR_Image* GR_CairoPrintGraphics::createNewImage(const char* pszName,
												const UT_ConstByteBufPtr & pBB,
												const std::string& mimetype,
												UT_sint32 iDisplayWidth,
												UT_sint32 iDisplayHeight,
												GR_Image::GRType iType)
{
	GR_Image::GRType type = iType;
	if (type == GR_Image::GRT_Unknown && pBB)
		type = GR_Image::getBufferType(pBB);

	// iDisplayWidth and iDisplayHeight arrive in logical units (1440 DPI).
	// Convert to device units to match GR_UnixCairoGraphicsBase and GR_Win32Graphics.
	UT_sint32 devWidth = iDisplayWidth;
	if (iDisplayWidth > 0)
	{
		devWidth = tdu(iDisplayWidth);
		if (devWidth <= 0)
			devWidth = 1;
	}
	UT_sint32 devHeight = iDisplayHeight;
	if (iDisplayHeight > 0)
	{
		devHeight = tdu(iDisplayHeight);
		if (devHeight <= 0)
			devHeight = 1;
	}

	if (type == GR_Image::GRT_Vector)
	{
		GR_Win32CairoVectorImage *image = new GR_Win32CairoVectorImage(pszName);
		image->convertFromBuffer(pBB, mimetype, devWidth, devHeight);
		return image;
	}

	GR_Win32CairoImage *image = new GR_Win32CairoImage(pszName);
	if (!image->convertFromBuffer(pBB, mimetype, devWidth, devHeight))
	{
		delete image;
		return nullptr;
	}
	return image;
}

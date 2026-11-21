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

#ifndef GR_WIN32CAIROPRINTGRAPHICS_H
#define GR_WIN32CAIROPRINTGRAPHICS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cairo.h>

#include "gr_CairoGraphics.h"

class ABI_EXPORT GR_CairoPrintGraphics : public GR_CairoGraphics
{
public:
	GR_CairoPrintGraphics(cairo_t *cr, UT_uint32 iDeviceResolution);
	virtual ~GR_CairoPrintGraphics();

	static UT_uint32 s_getClassId() {return GRID_UNIX_PANGO_PRINT;}
	virtual UT_uint32 getClassId() override {return s_getClassId();}

	virtual GR_Capability getCapability() override {return GRCAP_PRINTER_ONLY;}

	virtual bool queryProperties(GR_Graphics::Properties gp) const override;
	virtual bool startPrint(void) override;
	virtual bool startPage(const char * szPageLabel, UT_uint32 pageNumber,
						   bool bPortrait, UT_uint32 iWidth, UT_uint32 iHeight) override;
	virtual bool endPrint(void) override;

	virtual void queueDraw(const UT_Rect* pRect) override;
	virtual void setCursor(GR_Graphics::Cursor) override { }
	virtual GR_Graphics::Cursor getCursor(void) const override { return GR_CURSOR_INVALID; }
	virtual GR_Font * getGUIFont(void) override;
	virtual void scroll(UT_sint32, UT_sint32) override { }
	virtual void scroll(UT_sint32, UT_sint32, UT_sint32, UT_sint32, UT_sint32, UT_sint32) override { }
	virtual GR_Image * genImageFromRectangle(const UT_Rect &) override { return nullptr; }

	virtual GR_Image* createNewImage(const char* pszName,
									 const UT_ConstByteBufPtr & pBB,
									 const std::string& mimetype,
									 UT_sint32 iDisplayWidth,
									 UT_sint32 iDisplayHeight,
									 GR_Image::GRType iType) override;

	double getResolutionRatio(void) const override { return m_dResRatio; }
	void setResolutionRatio(double dres) { m_dResRatio = dres; }

private:
	bool m_bDoShowPage;
	double m_dResRatio;
};

#endif

/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */
/* AbiWord
 * Copyright (C) 1998-2005 AbiSource, Inc.
 * Copyright (C) 2005 Dom Lachowicz <cinamod@hotmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "ut_debugmsg.h"
#include "ut_string.h"
#include "ie_exp_PDF.h"
#include "pd_Document.h"
#if defined(TOOLKIT_WIN)
#include "gr_Win32CairoPrintGraphics.h"
#else
#include "gr_CairoPrintGraphics.h"
#endif
#include "fv_View.h"
#include "fl_DocLayout.h"
#include "ap_EditMethods.h"
#include "xap_App.h"
#include "ut_misc.h"

#include <cairo-pdf.h>
#include <cairo-ps.h>

#if 0
#include <cairo-svg.h>
#endif

#include <glib/gstdio.h>

#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <set>
#include <string>

/*****************************************************************/
/*****************************************************************/

static cairo_status_t
ie_exp_cairo_write_func (void *closure, const unsigned char *data, unsigned int length)
{
	if (!gsf_output_write((GsfOutput*)closure, length, data))
		return CAIRO_STATUS_WRITE_ERROR;
    return CAIRO_STATUS_SUCCESS;
}

#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 16, 0)
static std::string ie_exp_pdf_iso_date(const std::string & raw)
{
	std::string s = raw;
	while (!s.empty() && isspace(static_cast<unsigned char>(s.back())))
		s.pop_back();
	size_t start = 0;
	while (start < s.size() && isspace(static_cast<unsigned char>(s[start])))
		++start;
	if (start > 0)
		s.erase(0, start);
	if (s.empty())
		return std::string();

	// Already ISO-8601 (YYYY-MM-DD...)
	int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
	if (sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &min, &sec) == 6 &&
		year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
		hour >= 0 && hour <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 60)
	{
		return s;
	}
	if (sscanf(s.c_str(), "%4d-%2d-%2d %2d:%2d:%2d", &year, &month, &day, &hour, &min, &sec) == 6 &&
		year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
		hour >= 0 && hour <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 60)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour, min, sec);
		return buf;
	}
	if (s.size() == 10 && sscanf(s.c_str(), "%4d-%2d-%2d", &year, &month, &day) == 3 &&
		year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= 31)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%04d-%02d-%02dT00:00:00", year, month, day);
		return buf;
	}

	// ctime() format produced by FV_View::_updateDatesBeforeSave: "Www Mmm dd hh:mm:ss yyyy"
	char wday[8] = {}, mon[8] = {};
	if (sscanf(s.c_str(), "%3s %3s %d %d:%d:%d %d", wday, mon, &day, &hour, &min, &sec, &year) == 7)
	{
		static const char * const months[12] = {
			"Jan", "Feb", "Mar", "Apr", "May", "Jun",
			"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
		};
		month = 0;
		for (int i = 0; i < 12; ++i)
		{
			if (g_ascii_strcasecmp(mon, months[i]) == 0)
			{
				month = i + 1;
				break;
			}
		}
		if (month >= 1 && year >= 1 && day >= 1 && day <= 31 &&
			hour >= 0 && hour <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 60)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour, min, sec);
			return buf;
		}
	}
	return std::string();
}

static void ie_exp_pdf_apply_metadata(cairo_surface_t * surface, PD_Document * doc)
{
	if (!surface || !doc)
		return;
	std::string val;
	if (doc->getMetaDataProp(PD_META_KEY_TITLE, val) && !val.empty())
		cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_TITLE, val.c_str());
	if (doc->getMetaDataProp(PD_META_KEY_CREATOR, val) && !val.empty())
		cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_AUTHOR, val.c_str());
	if (doc->getMetaDataProp(PD_META_KEY_SUBJECT, val) && !val.empty())
		cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_SUBJECT, val.c_str());
	if (doc->getMetaDataProp(PD_META_KEY_KEYWORDS, val) && !val.empty())
		cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_KEYWORDS, val.c_str());
	cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_CREATOR, "AbiWord");
	if (doc->getMetaDataProp(PD_META_KEY_DATE, val) && !val.empty())
	{
		const std::string iso = ie_exp_pdf_iso_date(val);
		if (!iso.empty())
			cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_CREATE_DATE, iso.c_str());
	}
	if (doc->getMetaDataProp(PD_META_KEY_DATE_LAST_CHANGED, val) && !val.empty())
	{
		const std::string iso = ie_exp_pdf_iso_date(val);
		if (!iso.empty())
			cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_MOD_DATE, iso.c_str());
	}
}
#endif

class ABI_EXPORT IE_Exp_Cairo : public IE_Exp
{
public:
	enum Format: uint8_t
    {
      BACKEND_PS,
      BACKEND_PDF,
	  BACKEND_SVG
    };
  
  IE_Exp_Cairo::Format mFormat;
  
  IE_Exp_Cairo(PD_Document * pDocument, IE_Exp_Cairo::Format format)
    : IE_Exp(pDocument), mFormat(format)
  {
  }
  
  virtual ~IE_Exp_Cairo()
  {
  }

  virtual UT_Error _writeDocument(void) override
  {
    cairo_t *cr = nullptr;
    cairo_surface_t *surface = nullptr;
    GR_CairoPrintGraphics *print_graphics = nullptr;
    FL_DocLayout *pDocLayout = nullptr;
    FV_View *printView = nullptr;

    std::set<UT_sint32> pages;
    const std::string & pages_prop = getProperty ("pages");

    double width = getDoc()->m_docPageSize.Width (DIM_IN);
    double height = getDoc()->m_docPageSize.Height (DIM_IN);
    if (width <= 0.0 || height <= 0.0)
      {
        width = 8.5;
        height = 11.0;
      }

	// Cairo expects the width/height of the surface in points (1 point == 1/72 inch).
	// Use 144 DPI device space (matching XAP_UnixDialog_Print / gr_PRINTRES) scaled to
	// 72 DPI PDF user space for sub-point border precision and sharper raster images.
	const double pointsPerInch = 72.0;
	const UT_uint32 dpi = 144;
	if (BACKEND_PDF == mFormat)
		surface = cairo_pdf_surface_create_for_stream(ie_exp_cairo_write_func, getFp(), width * pointsPerInch, height * pointsPerInch);
	else if (BACKEND_PS == mFormat)
		surface = cairo_ps_surface_create_for_stream(ie_exp_cairo_write_func, getFp(), width * pointsPerInch, height * pointsPerInch);
	else if (BACKEND_SVG == mFormat)
		{
			// surface = cairo_svg_surface_create_for_stream(ie_exp_cairo_write_func, getFp(), width * pointsPerInch, height * pointsPerInch);
			return UT_ERROR;
		}
	else
		{
			return UT_ERROR;
		}

	if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
		{
			if (surface)
				cairo_surface_destroy(surface);
			return UT_ERROR;
		}

#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 16, 0)
	if (BACKEND_PDF == mFormat)
		ie_exp_pdf_apply_metadata(surface, getDoc());
#endif

	cr = cairo_create(surface);
	if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS)
		{
			if (cr)
				cairo_destroy(cr);
			cairo_surface_finish(surface);
			cairo_surface_destroy(surface);
			return UT_ERROR;
		}

	const double scale = pointsPerInch / static_cast<double>(dpi);
	cairo_scale(cr, scale, scale);
	cairo_translate(cr, 0.5, 0.5);

	print_graphics = new GR_CairoPrintGraphics(cr, dpi);
    pDocLayout = new FL_DocLayout(getDoc(), print_graphics);
    printView = new FV_View(XAP_App::getApp(), nullptr, pDocLayout);
	// The print layout is built once, inside fillLayouts(), while the layout
	// is still filling. That pass empties the first column and breaks the
	// section, so the header, the body and the footer share each page.
	// A later formatAll() sees every block still marked for reformat and
	// breaks the section again without emptying the columns. The body is
	// then dropped, and the header and the footer land on different pages.
	if (printView->getViewMode() != VIEW_PRINT)
		printView->setViewMode(VIEW_PRINT);
    printView->getLayout()->fillLayouts();
    printView->getLayout()->recalculateTOCFields();

    if (!pages_prop.empty())
      {
		  char **page_descriptions = g_strsplit(pages_prop.c_str(), ",", -1);
	
		  int i = 0;
		  while (page_descriptions[i] != nullptr)
			  {
				  char *description = page_descriptions[i];
				  i++;
	    
				  int start_page, end_page;
	    
				  if (2 == sscanf(description, "%d-%d", &start_page, &end_page))
					  {
					  }
				  else if (1 == sscanf(description, "%d", &start_page))
					  {
						  end_page = start_page;
					  }
				  else
					  {
						  // invalid page specification
						  continue;
					  }
	    
				  for (int pageno = start_page; pageno <= end_page; pageno++)
					  {
						  if ((pageno > 0) && (pageno <= (int)pDocLayout->countPages()))
							  pages.insert(pageno);
					  }
			  }
	
		  g_strfreev(page_descriptions);
      }
    
    const UT_sint32 pageCount = pDocLayout->countPages();
    if (pageCount < 1)
      {
		DELETEP(printView);
		DELETEP(pDocLayout);
		DELETEP(print_graphics);
		cairo_surface_finish(surface);
		cairo_surface_destroy(surface);
		return UT_ERROR;
      }

    if (pages.empty())
      {
		  for (UT_sint32 i = 1; i <= pageCount; i++)
			  {
				  pages.insert(i);
			  }
      }    
	
    const bool printed = s_actuallyPrint (getDoc(), print_graphics,
					 printView, getFileName(), 
					 1, true, 
					 pDocLayout->getWidth(), pDocLayout->getHeight() / pageCount, 
					 pages);
    const cairo_status_t drawn = cairo_status(cr);

    // Destroy view and layout before print_graphics (which owns cr), then
    // finish the PDF/PS surface so streams, xref, and %%EOF are flushed and checked.
    DELETEP(printView);
    DELETEP(pDocLayout);
    DELETEP(print_graphics);

    cairo_surface_finish(surface);
    const cairo_status_t surfaceStatus = cairo_surface_status(surface);
    cairo_surface_destroy(surface);

    if (!printed || drawn != CAIRO_STATUS_SUCCESS || surfaceStatus != CAIRO_STATUS_SUCCESS)
		return UT_ERROR;
    return UT_OK;
  }
};

/*****************************************************************/
/*****************************************************************/

IE_Exp_PS_Sniffer::IE_Exp_PS_Sniffer()
: IE_ExpSniffer("application/postscript", false)
{
}

IE_Exp_PS_Sniffer::~IE_Exp_PS_Sniffer ()
{
}
  
UT_Confidence_t IE_Exp_PS_Sniffer::supportsMIME (const char * szMIME)
{
  if(!g_ascii_strcasecmp(szMIME, "application/postscript"))
    return UT_CONFIDENCE_PERFECT;
  return UT_CONFIDENCE_ZILCH;
}
  
bool IE_Exp_PS_Sniffer::recognizeSuffix (const char * szSuffix)
{
  return !g_ascii_strcasecmp(szSuffix,".ps");
}

bool IE_Exp_PS_Sniffer::getDlgLabels (const char ** szDesc,
				      const char ** szSuffixList,
				      IEFileType * ft)
{
  *szDesc = "Postscript (.ps)";
  *szSuffixList = "*.ps";
  *ft = getFileType();
  return true;
}

UT_Error IE_Exp_PS_Sniffer::constructExporter (PD_Document * pDocument,
					       IE_Exp ** ppie)
{
  *ppie = new IE_Exp_Cairo(pDocument, IE_Exp_Cairo::BACKEND_PS);
  return UT_OK;
}

/*****************************************************************/
/*****************************************************************/

IE_Exp_SVG_Sniffer::IE_Exp_SVG_Sniffer()
: IE_ExpSniffer("image/svg+xml", false)
{
}

IE_Exp_SVG_Sniffer::~IE_Exp_SVG_Sniffer ()
{
}
  
UT_Confidence_t IE_Exp_SVG_Sniffer::supportsMIME (const char * szMIME)
{
  if(!g_ascii_strcasecmp(szMIME, "image/svg+xml") ||
	 !g_ascii_strcasecmp(szMIME, "image/svg") ||
	 !g_ascii_strcasecmp(szMIME, "image/svg-xml") ||
	 !g_ascii_strcasecmp(szMIME, "image/vnd.adobe.svg+xml") ||
	 !g_ascii_strcasecmp(szMIME, "text/xml-svg"))
    return UT_CONFIDENCE_PERFECT;
  return UT_CONFIDENCE_ZILCH;
}
  
bool IE_Exp_SVG_Sniffer::recognizeSuffix (const char * szSuffix)
{
  return !g_ascii_strcasecmp(szSuffix,".svg");
}

bool IE_Exp_SVG_Sniffer::getDlgLabels (const char ** szDesc,
				      const char ** szSuffixList,
				      IEFileType * ft)
{
  *szDesc = "Scalable Vector Graphics (.svg)";
  *szSuffixList = "*.svg";
  *ft = getFileType();
  return true;
}

UT_Error IE_Exp_SVG_Sniffer::constructExporter (PD_Document * pDocument,
					       IE_Exp ** ppie)
{
  *ppie = new IE_Exp_Cairo(pDocument, IE_Exp_Cairo::BACKEND_SVG);
  return UT_OK;
}

/*****************************************************************/
/*****************************************************************/

IE_Exp_PDF_Sniffer::IE_Exp_PDF_Sniffer()
: IE_ExpSniffer("application/pdf", false)
{
}

IE_Exp_PDF_Sniffer::~IE_Exp_PDF_Sniffer ()
{
}
  
UT_Confidence_t IE_Exp_PDF_Sniffer::supportsMIME (const char * szMIME)
{
  if(!g_ascii_strcasecmp(szMIME, "application/pdf"))
    return UT_CONFIDENCE_PERFECT;
  return UT_CONFIDENCE_ZILCH;
}
  
bool IE_Exp_PDF_Sniffer::recognizeSuffix (const char * szSuffix)
{
  return !g_ascii_strcasecmp(szSuffix,".pdf");
}

bool IE_Exp_PDF_Sniffer::getDlgLabels (const char ** szDesc,
				       const char ** szSuffixList,
				      IEFileType * ft)
{
  *szDesc = "Portable Document Format (.pdf)";
  *szSuffixList = "*.pdf";
  *ft = getFileType();
  return true;
}

UT_Error IE_Exp_PDF_Sniffer::constructExporter (PD_Document * pDocument,
					       IE_Exp ** ppie)
{
  *ppie = new IE_Exp_Cairo(pDocument, IE_Exp_Cairo::BACKEND_PDF);
  return UT_OK;
}

/*****************************************************************/
/*****************************************************************/

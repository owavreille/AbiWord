/* AbiSource
 *
 * Copyright (C) 2012 Tanya Guza <tanya.guza@gmail.com>
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


#include "ODe_ThumbnailsWriter.h"

// Internal includes
#include "ODe_Common.h"

// Abiword includes
#include "ut_types.h"
#include "ut_string_class.h"
#include "ut_std_string.h"
#include "pd_Document.h"
#include "xap_App.h"
#include "xap_Frame.h"
#include "fv_View.h"
#include "gr_Graphics.h"
#include "gr_Painter.h"

#include <string.h>

bool ODe_ThumbnailsWriter::writeThumbnails(PD_Document* /*pDoc*/, GsfOutfile* oo, bool* pHasThumbnail) {
    if (pHasThumbnail) {
        *pHasThumbnail = false;
    }

    XAP_App* pApp = XAP_App::getApp();
    XAP_Frame* pFrame = pApp ? pApp->getLastFocussedFrame() : nullptr;
    // Do not create an empty/corrupt Thumbnails/thumbnail.png when no active
    // frame/view/graphics exists (e.g. headless conversion, CLI, or tests).
    if (!pFrame) {
        return true;
    }

    FV_View* pView = static_cast<FV_View*>(pFrame->getCurrentView());
    GR_Graphics* pVG = pView ? pView->getGraphics() : nullptr;
    if (!pView || !pVG) {
        return true;
    }

    const UT_sint32 iWidth = pView->getWindowWidth();
    const UT_sint32 iHeight = pView->getWindowHeight();
    if (iWidth <= 0 || iHeight <= 0) {
        return true;
    }

    UT_Rect rect(0, 0, iWidth, iHeight);
    GR_Painter painter(pVG);
    GR_Image* pImage = painter.genImageFromRectangle(rect);
    if (!pImage) {
        return true;
    }

    UT_ConstByteBufPtr pBuf;
    const bool converted = pImage->convertToBuffer(pBuf);
    DELETEP(pImage);

    // ODF 1.3 Part 2 §3.7: Thumbnails/thumbnail.png must be a valid PNG image.
    if (!converted || !pBuf || pBuf->getLength() < 8 ||
        memcmp(pBuf->getPointer(0), "\x89PNG\r\n\x1a\n", 8) != 0) {
        return true;
    }

    GsfOutput* thumbnailsDir = gsf_outfile_new_child(oo, "Thumbnails", TRUE);
    if (!thumbnailsDir) {
        return false;
    }

    GsfOutput* thumbnail = gsf_outfile_new_child(GSF_OUTFILE(thumbnailsDir),
                                                 "thumbnail.png", FALSE);
    if (!thumbnail) {
        ODe_gsf_output_close(thumbnailsDir);
        return false;
    }

    ODe_gsf_output_write(thumbnail, pBuf->getLength(), pBuf->getPointer(0));
    ODe_gsf_output_close(thumbnail);
    ODe_gsf_output_close(thumbnailsDir);

    if (pHasThumbnail) {
        *pHasThumbnail = true;
    }
    return true;
}

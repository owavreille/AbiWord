/* AbiWord
 * Copyright (C) 1998 AbiSource, Inc.
 * Copyright (C) 2001-2002 Hubert Figuiere
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

#import <Cocoa/Cocoa.h>

#include "ut_assert.h"
#include "ut_vector.h"
#include "ut_hash.h"
#include "ut_debugmsg.h"

#include "ap_CocoaToolbar_FontCombo.h"
#include "ap_FontMenu.h"
#include "ap_Toolbar_Id.h"
#include "pd_Document.h"
#include "xap_CocoaApp.h"
#include "xap_Frame.h"
#include "ap_CocoaFrame.h"

#include "ev_CocoaToolbar.h"
#include "ev_Toolbar.h"


EV_Toolbar_Control * AP_CocoaToolbar_FontCombo::static_constructor(EV_Toolbar * pToolbar,
								   XAP_Toolbar_Id tlbrid)
{
	AP_CocoaToolbar_FontCombo * p = new AP_CocoaToolbar_FontCombo(pToolbar,tlbrid);
	return p;
}

AP_CocoaToolbar_FontCombo::AP_CocoaToolbar_FontCombo(EV_Toolbar * pToolbar,
						     XAP_Toolbar_Id tlbrid)
	: EV_Toolbar_Control(pToolbar/*,id*/)
{
	UT_DEBUG_ONLY_ARG(tlbrid);

	UT_ASSERT(tlbrid == AP_TOOLBAR_ID_FMT_FONT);
	m_nPixels = 150;
	m_nLimit = 32;
}

AP_CocoaToolbar_FontCombo::~AP_CocoaToolbar_FontCombo(void)
{
}

bool AP_CocoaToolbar_FontCombo::populate(void)
{
    UT_ASSERT(m_pToolbar);

    NSArray *list = [[NSFontManager sharedFontManager] availableFontFamilies];
    std::vector<std::string> installed;
    NSEnumerator* e = [list objectEnumerator];
    while(NSString *item = [e nextObject])
    {
        const char *fName = [item UTF8String];
        if (fName && *fName)
            installed.push_back(fName);
    }

    EV_CocoaToolbar * toolbar = static_cast<EV_CocoaToolbar *>(m_pToolbar);
    PD_Document * doc = AP_documentFromFrame(toolbar ? toolbar->getFrame() : nullptr);
    AP_buildFontMenu(doc, installed, m_menuOrder);

    m_vecContents.clear();
    for (const std::string & name : m_menuOrder)
        m_vecContents.addItem(name.c_str());

    return true;
}

/* AbiWord
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <string>
#include <vector>

class PD_Document;
class XAP_Frame;

// Not a font name. The toolbar draws this row as a separator.
#define AP_FONT_MENU_SEPARATOR "\x1e"

bool AP_fontMenuIsSeparator(const char * name);

PD_Document * AP_documentFromFrame(XAP_Frame * frame);

// Fonts actually used by the document, in collation order.
void AP_collectUsedFonts(PD_Document * doc, std::vector<std::string> & fonts);

// Used fonts, then a separator, then every installed face in collation order.
// The installed faces stay in the lower part even when they are also used.
void AP_buildFontMenu(PD_Document * doc,
		      const std::vector<std::string> & installed,
		      std::vector<std::string> & menu);

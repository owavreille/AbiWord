/* AbiSource
 * 
 * Copyright (C) 2005 INdT
 * Author: Daniel d'Andrada T. de Carvalho <daniel.carvalho@indt.org.br>
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
 
// Class definition include
#include "ODe_Frame_Listener.h"

// Internal includes
#include "ODe_AutomaticStyles.h"
#include "ODe_AuxiliaryData.h"
#include "ODe_Common.h"
#include "ODe_Styles.h"
#include "ODe_Style_Style.h"
#include "ODe_Text_Listener.h"
#include "ODe_ListenerAction.h"
#include "ODe_Style_PageLayout.h"

// AbiWord includes
#include "pp_AttrProp.h"
#include "ut_units.h"

/**
 * Constructor
 */
ODe_Frame_Listener::ODe_Frame_Listener(ODe_Styles& rStyles,
                                       ODe_AutomaticStyles& rAutomatiStyles,
                                       GsfOutput* pTextOutput,
                                       ODe_AuxiliaryData& rAuxiliaryData,
                                       UT_uint8 zIndex,
                                       UT_uint8 spacesOffset,
                                       bool inHeaderFooter)
                                       :
                                       ODe_AbiDocListenerImpl(spacesOffset),
                                       m_rStyles(rStyles),
                                       m_rAutomatiStyles(rAutomatiStyles),
                                       m_pTextOutput(pTextOutput),
                                       m_rAuxiliaryData(rAuxiliaryData),
                                       m_zIndex(zIndex),
                                       m_inHeaderFooter(inHeaderFooter)
{
}


/**
 * 
 */
void ODe_Frame_Listener::openFrame(const PP_AttrProp* pAP,
                                   ODe_ListenerAction& rAction) {
    bool ok = false;
    const gchar* pValue = nullptr;
    
    ok = pAP->getProperty("frame-type", pValue);
    
    if (ok && pValue && !strcmp(pValue, "textbox")) {
        _openODTextbox(*pAP, rAction);
    }
}


/**
 * 
 */
void ODe_Frame_Listener::closeFrame(ODe_ListenerAction& rAction) {
        UT_UTF8String output;
        
        m_spacesOffset--;
        _printSpacesOffset(output);
        output += "</draw:text-box>\n";
        
        m_spacesOffset--;
        _printSpacesOffset(output);
        output += "</draw:frame>";
    
        ODe_writeUTF8String(m_pTextOutput, output);
        
        rAction.popListenerImpl();
}


/**
 * 
 */
void ODe_Frame_Listener::openTable(const PP_AttrProp* /*pAP*/,
                                   ODe_ListenerAction& rAction) {
    ODe_Text_Listener* pTextListener;
    pTextListener = new ODe_Text_Listener(m_rStyles,
                                          m_rAutomatiStyles,
                                          m_pTextOutput,
                                          m_rAuxiliaryData,
                                          m_zIndex+1,
                                          m_spacesOffset);
    rAction.pushListenerImpl(pTextListener, true);
}


/**
 * 
 */
void ODe_Frame_Listener::openBlock(const PP_AttrProp* /*pAP*/,
                                   ODe_ListenerAction& rAction) {
    ODe_Text_Listener* pTextListener;
    pTextListener = new ODe_Text_Listener(m_rStyles,
                                          m_rAutomatiStyles,
                                          m_pTextOutput,
                                          m_rAuxiliaryData,
                                          m_zIndex+1,
                                          m_spacesOffset);
    rAction.pushListenerImpl(pTextListener, true);
}


/**
 * 
 */
void ODe_Frame_Listener::_openODTextbox(const PP_AttrProp& rAP,
                                        ODe_ListenerAction& /*rAction*/) 
{
    UT_UTF8String output;
    UT_UTF8String str;
    bool ok;
    const gchar* pValue = nullptr;
    ODe_Style_Style* pStyle;
    
    pStyle = new ODe_Style_Style();
    pStyle->setFamily("graphic");
    pStyle->fetchAttributesFromAbiFrame(rAP);
    
    // Abi frames have no padding
    // (no margin between frame borders and its content)
    pStyle->setPadding("0cm");
    
    // Abi frames are always positioned from their top-left corner.
    pStyle->setHorizontalPos("from-left");
    pStyle->setVerticalPos("from-top");
    if (m_inHeaderFooter) {
        pStyle->setHorizontalRel("paragraph");
        pStyle->setVerticalRel("paragraph");
    }
    
    // For OOo/LibreOffice to recognize a textbox as a frame, it needs
    // parent style name "Frame".
    pStyle->setParentStyleName("Frame");
    if (!m_rStyles.getGraphicsStyle("Frame")) {
        ODe_Style_Style* pFrameStyle = new ODe_Style_Style();
		pFrameStyle->setStyleName("Frame");
		pFrameStyle->setFamily("graphic");
        m_rStyles.addGraphicsStyle(pFrameStyle);
    }

	m_rAutomatiStyles.storeGraphicStyle(pStyle);

    ////
    // Write <draw:frame>
    // Always anchor to paragraph (valid inside <text:p> and headers/footers,
    // and supported by both LibreOffice Writer and Microsoft Word [MS-OODF3]).
    
    _printSpacesOffset(output);
    output += "<draw:frame";

    UT_UTF8String_sprintf(str, "Frame%u", m_rAuxiliaryData.m_frameCount+1);
    ODe_writeAttribute(output, "draw:name", str);
    m_rAuxiliaryData.m_frameCount++;
    
    ODe_writeAttribute(output, "draw:style-name",
                       ODe_Style_Style::convertStyleToNCName(pStyle->getName()));

    UT_UTF8String_sprintf(str, "%u", m_zIndex);
    ODe_writeAttribute(output, "draw:z-index", str);
    ODe_writeAttribute(output, "text:anchor-type", "paragraph");

    ok = rAP.getProperty("position-to", pValue);
    const gchar* xProp = "xpos";
    const gchar* yProp = "ypos";
    if (!m_inHeaderFooter && ok && pValue && !strcmp(pValue, "column-above-text")) {
        xProp = "frame-col-xpos";
        yProp = "frame-col-ypos";
    } else if (!m_inHeaderFooter && ok && pValue && !strcmp(pValue, "page-above-text")) {
        xProp = "frame-page-xpos";
        yProp = "frame-page-ypos";
    }

    const gchar* xVal = nullptr;
    if (rAP.getProperty(xProp, xVal) && xVal && *xVal) {
        ODe_writeAttribute(output, "svg:x", xVal);
    } else if (rAP.getProperty("xpos", xVal) && xVal && *xVal) {
        ODe_writeAttribute(output, "svg:x", xVal);
    } else {
        ODe_writeAttribute(output, "svg:x", "0in");
    }

    const gchar* yVal = nullptr;
    if (rAP.getProperty(yProp, yVal) && yVal && *yVal) {
        ODe_writeAttribute(output, "svg:y", yVal);
    } else if (rAP.getProperty("ypos", yVal) && yVal && *yVal) {
        ODe_writeAttribute(output, "svg:y", yVal);
    } else {
        ODe_writeAttribute(output, "svg:y", "0in");
    }
    
    ok = rAP.getProperty("frame-width", pValue);
    if (ok && pValue != nullptr) {
        const char* width = (UT_convertToInches(pValue) < 0.0) ? "0in" : pValue;
        ODe_writeAttribute(output, "svg:width", width);
    }
    
    output += ">\n";
    
    ODe_writeUTF8String(m_pTextOutput, output);
    m_spacesOffset++;
    
    ////
    // Write <draw:text-box>
    
    output.clear();
    _printSpacesOffset(output);
    output += "<draw:text-box";
    
    ok = rAP.getProperty("frame-height", pValue);
    if (ok && pValue != nullptr) {
        // ODF 1.3 rejects a negative fo:min-height.
        const char * height = (UT_convertToInches(pValue) < 0.0) ? "0in" : pValue;
        ODe_writeAttribute(output, "fo:min-height", height);
    }
    
    output += ">\n";
    
    ODe_writeUTF8String(m_pTextOutput, output);
    m_spacesOffset++;
}

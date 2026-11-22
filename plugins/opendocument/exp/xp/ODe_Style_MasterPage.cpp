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
#include "ODe_Style_MasterPage.h"

// Internal includes
#include "ODe_Common.h"
#include "ODe_Style_Style.h"

// AbiWord includes
#include "pp_AttrProp.h"

#include <string.h>

/**
 * Constructor
 */
ODe_Style_MasterPage::ODe_Style_MasterPage (const gchar* pName,
                                            const gchar* pPageLayoutName) {

    m_name = pName;
    m_pageLayoutName = pPageLayoutName;

    m_pFooterContentTemp = gsf_output_memory_new ();
    m_pFooterEvenContentTemp = gsf_output_memory_new ();
    m_pHeaderContentTemp = gsf_output_memory_new ();
    m_pHeaderEvenContentTemp = gsf_output_memory_new ();
}


/**
 * Destructor
 */
ODe_Style_MasterPage::~ODe_Style_MasterPage() {
    if (m_pHeaderContentTemp != nullptr) {
        ODe_gsf_output_close(m_pHeaderContentTemp);
    }
 
    if (m_pHeaderEvenContentTemp != nullptr) {
        ODe_gsf_output_close(m_pHeaderEvenContentTemp);
    }

    if (m_pFooterContentTemp != nullptr) {
        ODe_gsf_output_close(m_pFooterContentTemp);
    }

    if (m_pFooterEvenContentTemp != nullptr) {
        ODe_gsf_output_close(m_pFooterEvenContentTemp);
    }
}


/**
 * Fetches info from an AbiWord <section> tag (from its attributes as properties).
 */
void ODe_Style_MasterPage::fetchAttributesFromAbiSection(const PP_AttrProp* pAP) {
    const gchar* pValue;
    bool ok;
    
    ok = pAP->getAttribute("header", pValue);
    if (ok && pValue != nullptr) {
        m_abiHeaderId = pValue;
    }

    ok = pAP->getAttribute("header-even", pValue);
    if (ok && pValue != nullptr) {
        m_abiHeaderEvenId = pValue;
    }

    ok = pAP->getAttribute("header-first", pValue);
    if (ok && pValue != nullptr) {
        m_abiHeaderFirstId = pValue;
    }

    ok = pAP->getAttribute("header-last", pValue);
    if (ok && pValue != nullptr) {
        m_abiHeaderLastId = pValue;
    }

    ok = pAP->getAttribute("footer", pValue);
    if (ok && pValue != nullptr) {
        m_abiFooterId = pValue;
    }

    ok = pAP->getAttribute("footer-even", pValue);
    if (ok && pValue != nullptr) {
        m_abiFooterEvenId = pValue;
    }

    ok = pAP->getAttribute("footer-first", pValue);
    if (ok && pValue != nullptr) {
        m_abiFooterFirstId = pValue;
    }

    ok = pAP->getAttribute("footer-last", pValue);
    if (ok && pValue != nullptr) {
        m_abiFooterLastId = pValue;
    }

    // LibreOffice writes one style:header for the page style. A document that
    // only has a first or last header still has to publish that text there.
    if (m_abiHeaderId.empty()) {
        if (!m_abiHeaderFirstId.empty())
            m_abiHeaderId = m_abiHeaderFirstId;
        else if (!m_abiHeaderLastId.empty())
            m_abiHeaderId = m_abiHeaderLastId;
    }
    if (m_abiFooterId.empty()) {
        if (!m_abiFooterFirstId.empty())
            m_abiFooterId = m_abiFooterFirstId;
        else if (!m_abiFooterLastId.empty())
            m_abiFooterId = m_abiFooterLastId;
    }
}

static bool s_sameId(const UT_UTF8String& stored, const char* id)
{
    return id && *id && !stored.empty() && !strcmp(stored.utf8_str(), id);
}

static bool s_isHeaderType(const char* type)
{
    return type && (!strcmp(type, "header") || !strcmp(type, "header-even") ||
                    !strcmp(type, "header-first") || !strcmp(type, "header-last"));
}

static bool s_isEvenType(const char* type)
{
    return type && (!strcmp(type, "header-even") || !strcmp(type, "footer-even"));
}

GsfOutput* ODe_Style_MasterPage::lookupHeaderFooter(const char* type, const char* id) const
{
    if (s_isHeaderType(type)) {
        if (s_sameId(m_abiHeaderEvenId, id))
            return m_pHeaderEvenContentTemp;
        if (s_sameId(m_abiHeaderId, id) || s_sameId(m_abiHeaderFirstId, id) ||
            s_sameId(m_abiHeaderLastId, id))
            return m_pHeaderContentTemp;
        return nullptr;
    }

    if (s_sameId(m_abiFooterEvenId, id))
        return m_pFooterEvenContentTemp;
    if (s_sameId(m_abiFooterId, id) || s_sameId(m_abiFooterFirstId, id) ||
        s_sameId(m_abiFooterLastId, id))
        return m_pFooterContentTemp;
    return nullptr;
}

GsfOutput* ODe_Style_MasterPage::adoptHeaderFooter(const char* type, const char* id)
{
    if (s_isHeaderType(type)) {
        if (s_isEvenType(type)) {
            if (m_abiHeaderEvenId.empty() && id && *id)
                m_abiHeaderEvenId = id;
            return m_pHeaderEvenContentTemp;
        }
        if (!strcmp(type, "header-first") && m_abiHeaderFirstId.empty() && id && *id)
            m_abiHeaderFirstId = id;
        else if (!strcmp(type, "header-last") && m_abiHeaderLastId.empty() && id && *id)
            m_abiHeaderLastId = id;
        else if (m_abiHeaderId.empty() && id && *id)
            m_abiHeaderId = id;
        if (m_abiHeaderId.empty()) {
            if (!m_abiHeaderFirstId.empty())
                m_abiHeaderId = m_abiHeaderFirstId;
            else if (!m_abiHeaderLastId.empty())
                m_abiHeaderId = m_abiHeaderLastId;
        }
        return m_pHeaderContentTemp;
    }

    if (!type || (strcmp(type, "footer") && strcmp(type, "footer-even") &&
                  strcmp(type, "footer-first") && strcmp(type, "footer-last")))
        return nullptr;

    if (s_isEvenType(type)) {
        if (m_abiFooterEvenId.empty() && id && *id)
            m_abiFooterEvenId = id;
        return m_pFooterEvenContentTemp;
    }
    if (!strcmp(type, "footer-first") && m_abiFooterFirstId.empty() && id && *id)
        m_abiFooterFirstId = id;
    else if (!strcmp(type, "footer-last") && m_abiFooterLastId.empty() && id && *id)
        m_abiFooterLastId = id;
    else if (m_abiFooterId.empty() && id && *id)
        m_abiFooterId = id;
    if (m_abiFooterId.empty()) {
        if (!m_abiFooterFirstId.empty())
            m_abiFooterId = m_abiFooterFirstId;
        else if (!m_abiFooterLastId.empty())
            m_abiFooterId = m_abiFooterLastId;
    }
    return m_pFooterContentTemp;
}

static void s_writeHeaderFooterBody(GsfOutput* pODT, GsfOutput* preferred, GsfOutput* fallback)
{
    GsfOutput* chosen = preferred;
    if (!chosen || gsf_output_size(chosen) == 0)
        chosen = fallback;
    if (!chosen || gsf_output_size(chosen) == 0)
        return;
    ODe_gsf_output_write(pODT, gsf_output_size(chosen),
                         gsf_output_memory_get_bytes(GSF_OUTPUT_MEMORY(chosen)));
}


/**
 * 
 */
bool ODe_Style_MasterPage::write(GsfOutput* pODT) const {
    
    UT_UTF8String output;
    
    UT_UTF8String_sprintf(output,
        "  <style:master-page style:name=\"%s\" style:page-layout-name=\"%s\">\n",
        ODe_Style_Style::convertStyleToNCName(m_name).utf8_str(),
        ODe_Style_Style::convertStyleToNCName(m_pageLayoutName).utf8_str());
        
    ODe_writeUTF8String(pODT, output);
    
    /*
    We have to deal with two confusion things when writing out header/footers:
    
    1. Oddly enough AbiWord uses "header-even" and "footer-even" for page 1, 3, 5, etc :)
    
    2. In OpenDocument you can specify an alternative header/footer for "left" pages.
       Oddly enough OpenOffice.org seems to interpret "left" pages as page 2, 4, 6, etc.
    */

    const bool headerContent =
        (m_pHeaderContentTemp && gsf_output_size(m_pHeaderContentTemp) > 0) ||
        (m_pHeaderEvenContentTemp && gsf_output_size(m_pHeaderEvenContentTemp) > 0);
    const bool footerContent =
        (m_pFooterContentTemp && gsf_output_size(m_pFooterContentTemp) > 0) ||
        (m_pFooterEvenContentTemp && gsf_output_size(m_pFooterEvenContentTemp) > 0);

    if (!m_abiHeaderId.empty() || !m_abiHeaderFirstId.empty() ||
        !m_abiHeaderLastId.empty() || headerContent) {
        // LibreOffice writes style:header whenever the page style has a header
        // text, including when that text came from a first/last header.
        ODe_writeUTF8String(pODT, "   <style:header>\n");

        // Swap even/uneven when there is an alternative header for uneven pages to
        // match what OpenOffice expects. If that buffer is empty, keep the
        // buffer that actually received the header text.
        if (m_abiHeaderEvenId.empty())
            s_writeHeaderFooterBody(pODT, m_pHeaderContentTemp, m_pHeaderEvenContentTemp);
        else
            s_writeHeaderFooterBody(pODT, m_pHeaderEvenContentTemp, m_pHeaderContentTemp);

        ODe_writeUTF8String(pODT, "   </style:header>\n");
    }

    if (!m_abiHeaderEvenId.empty() &&
        m_pHeaderContentTemp && gsf_output_size(m_pHeaderContentTemp) > 0 &&
        m_pHeaderEvenContentTemp && gsf_output_size(m_pHeaderEvenContentTemp) > 0) {
        // It has a different header for uneven pages
        ODe_writeUTF8String(pODT, "   <style:header-left>\n");

        ODe_gsf_output_write(pODT, gsf_output_size (m_pHeaderContentTemp), 
                gsf_output_memory_get_bytes (GSF_OUTPUT_MEMORY (m_pHeaderContentTemp)));
        
        ODe_writeUTF8String(pODT, "   </style:header-left>\n");
    }

    if (!m_abiFooterId.empty() || !m_abiFooterFirstId.empty() ||
        !m_abiFooterLastId.empty() || footerContent) {
        // It has a footer
        ODe_writeUTF8String(pODT, "   <style:footer>\n");

        // Swap even/uneven when there is an alternative footer for uneven pages to
        // match what OpenOffice expects. Fall back to the buffer that has text.
        if (m_abiFooterEvenId.empty())
            s_writeHeaderFooterBody(pODT, m_pFooterContentTemp, m_pFooterEvenContentTemp);
        else
            s_writeHeaderFooterBody(pODT, m_pFooterEvenContentTemp, m_pFooterContentTemp);

        ODe_writeUTF8String(pODT, "   </style:footer>\n");
    }

    if (!m_abiFooterEvenId.empty() &&
        m_pFooterContentTemp && gsf_output_size(m_pFooterContentTemp) > 0 &&
        m_pFooterEvenContentTemp && gsf_output_size(m_pFooterEvenContentTemp) > 0) {
        // It has a footer for uneven pages
        ODe_writeUTF8String(pODT, "   <style:footer-left>\n");

        ODe_gsf_output_write(pODT, gsf_output_size (m_pFooterContentTemp), 
                gsf_output_memory_get_bytes (GSF_OUTPUT_MEMORY (m_pFooterContentTemp)));

        ODe_writeUTF8String(pODT, "   </style:footer-left>\n");
    }

    ODe_writeUTF8String(pODT, "  </style:master-page>\n");

    return true;
}

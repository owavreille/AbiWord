/* AbiSource
 * 
 * Copyright (C) 2002 Dom Lachowicz <cinamod@hotmail.com>
 * Copyright (C) 2004 Robert Staudinger <robsta@stereolyzer.net>
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
#include "ODe_MetaDataWriter.h"

// Internal includes
#include "ODe_Common.h"
 
// Abiword includes
#include "ut_types.h"
#include "ut_string_class.h"
#include "ut_std_string.h"
#include "pd_Document.h"

#include <ctype.h>
#include <stdio.h>

/**
 * Normalize an AbiWord date string (ISO-8601 or ctime() output from
 * FV_View::_updateDatesBeforeSave) to strict ODF 1.3 xsd:dateTime
 * (YYYY-MM-DDThh:mm:ss). Returns empty string if the input is not a valid date.
 */
static std::string ode_formatIsoDateTime(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    if (start > 0) {
        s.erase(0, start);
    }
    if (s.empty()) {
        return std::string();
    }

    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
               &year, &month, &day, &hour, &min, &sec) == 6 &&
        year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
        hour >= 0 && hour <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 60) {
        return s;
    }

    if (sscanf(s.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
               &year, &month, &day, &hour, &min, &sec) == 6 &&
        year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
        hour >= 0 && hour <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 60) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                 year, month, day, hour, min, sec);
        return buf;
    }

    if (s.size() == 10 &&
        sscanf(s.c_str(), "%4d-%2d-%2d", &year, &month, &day) == 3 &&
        year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT00:00:00", year, month, day);
        return buf;
    }

    // ctime() format: "Www Mmm dd hh:mm:ss yyyy"
    char wday[8] = {};
    char mon[8] = {};
    if (sscanf(s.c_str(), "%3s %3s %d %d:%d:%d %d",
               wday, mon, &day, &hour, &min, &sec, &year) == 7) {
        static const char* const months[12] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        month = 0;
        for (int i = 0; i < 12; ++i) {
            if (g_ascii_strcasecmp(mon, months[i]) == 0) {
                month = i + 1;
                break;
            }
        }
        if (month >= 1 && year >= 1 && year <= 9999 && day >= 1 && day <= 31 &&
            hour >= 0 && hour <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 60) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                     year, month, day, hour, min, sec);
            return buf;
        }
    }

    return std::string();
}

/**
 * 
 */
bool ODe_MetaDataWriter::writeMetaData(PD_Document* pDoc, GsfOutfile* oo) {
    
    GsfOutput* meta = gsf_outfile_new_child (oo, "meta.xml", FALSE);

    static const UT_UTF8String preamble =
        UT_UTF8String("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-meta"
            " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
            " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
            " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
            " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
            " xmlns:ooo=\"http://openoffice.org/2004/office\""
            " office:version=\"1.3\">\n"
        "<office:meta>\n"
        "<meta:generator>AbiWord/") + PACKAGE_VERSION + " (" + PLATFORM + ", " + TOOLKIT + ")</meta:generator>\n";

    static const char * const postamble [] = {
        "</office:meta>\n",
        "</office:document-meta>\n"
    };

    ODe_writeUTF8String(meta, preamble);

    std::string meta_val, val;
    
#define WRITE_METADATA_ELEMENT(abiwordKey, odElementName) if (pDoc->getMetaDataProp(abiwordKey, meta_val) && meta_val.size()) { \
                                                               meta_val = UT_escapeXML(meta_val); \
                                                               val = UT_std_string_sprintf("<%s>%s</%s>\n", odElementName, meta_val.c_str(), odElementName); \
                                                               ODe_writeUTF8StdString (meta, val); \
                                                          }

#define WRITE_METADATA_DATE_ELEMENT(abiwordKey, odElementName) if (pDoc->getMetaDataProp(abiwordKey, meta_val) && meta_val.size()) { \
                                                                    std::string isoDate = ode_formatIsoDateTime(meta_val); \
                                                                    if (!isoDate.empty()) { \
                                                                        isoDate = UT_escapeXML(isoDate); \
                                                                        val = UT_std_string_sprintf("<%s>%s</%s>\n", odElementName, isoDate.c_str(), odElementName); \
                                                                        ODe_writeUTF8StdString (meta, val); \
                                                                    } \
                                                               }
    
    WRITE_METADATA_ELEMENT(PD_META_KEY_TITLE, "dc:title");
    WRITE_METADATA_ELEMENT(PD_META_KEY_DESCRIPTION, "dc:description");
    WRITE_METADATA_ELEMENT(PD_META_KEY_SUBJECT, "dc:subject");

    //Each keyword needs to be exported individually:

    std::string keywords;
    if (pDoc->getMetaDataProp (PD_META_KEY_KEYWORDS, keywords) && keywords.size())
    {
        UT_UTF8String buf = "";
        UT_UCS4String keyword(keywords);

        for(UT_uint32 i = 0;i < keyword.length(); i++)
        {
            if(keyword[i] != ' ')
            {
                buf += keyword[i];
            }
            else
            {
                if(buf.empty())  //only blank space encountered
                    continue;

                buf.escapeXML();
                val = UT_std_string_sprintf("<meta:keyword>%s</meta:keyword>\n", buf.utf8_str());
                ODe_writeUTF8StdString(meta, val);
                buf.clear();
            }
        }

        if(buf.length())  //there may only be one keyword (i.e. no spaces encountered)
        {
            buf.escapeXML();
            val = UT_std_string_sprintf("<meta:keyword>%s</meta:keyword>\n", buf.utf8_str());
            ODe_writeUTF8StdString(meta, val);
        }
    }

    // Should have a PD_META_KEY_INITIAL_CREATOR macro for this one, but only
    // if it gets implemented on the document properties dialog.
    WRITE_METADATA_ELEMENT("meta:initial-creator", "meta:initial-creator");
    
    WRITE_METADATA_ELEMENT(PD_META_KEY_CREATOR, "dc:creator");
    
    WRITE_METADATA_ELEMENT("meta:printed-by", "meta:printed-by");
    
    // ODF 1.3 requires xsd:dateTime (ISO-8601) for creation, modification, and print dates.
    WRITE_METADATA_DATE_ELEMENT(PD_META_KEY_DATE, "meta:creation-date");
    
    // Note that, for the OpenDocument standard, dc:date
    // is the last modification date & time.
    WRITE_METADATA_DATE_ELEMENT(PD_META_KEY_DATE_LAST_CHANGED, "dc:date");
    
    WRITE_METADATA_DATE_ELEMENT("meta:print-date", "meta:print-date");
    
    WRITE_METADATA_ELEMENT(PD_META_KEY_LANGUAGE, "dc:language");
    
#undef WRITE_METADATA_DATE_ELEMENT
#undef WRITE_METADATA_ELEMENT

    ODe_writeToStream(meta, postamble, G_N_ELEMENTS(postamble));

    ODe_gsf_output_close(meta);

    return true;
}

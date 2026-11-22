/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */

/* AbiSource
 * 
 * Copyright (C) 2007 Philippe Milot <PhilMilot@gmail.com>
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
#include "OXML_Section.h"

#include <stdio.h>
#include <stdlib.h>

// Internal includes
#include "OXML_Types.h"
#include "OXML_Element.h"
#include "OXML_Document.h"

// AbiWord includes
#include "ut_types.h"
#include "pd_Document.h"
#include "pt_Types.h"

// External includes
#include <string>

OXML_Section::OXML_Section() : 
	OXML_ObjectWithAttrProp(), 
	m_id(""), 
	m_breakType(NEXTPAGE_BREAK),
	m_lastParagraph(nullptr),
	m_target(0),
	m_handledHdrFtr(false)
{
	m_headerIds[0] = nullptr;
	m_headerIds[1] = nullptr;
	m_headerIds[2] = nullptr;

	m_footerIds[0] = nullptr;
	m_footerIds[1] = nullptr;
	m_footerIds[2] = nullptr;

	m_children.clear();
}

OXML_Section::OXML_Section(const std::string & id) : 
	OXML_ObjectWithAttrProp(), 
	m_id(id), 
	m_breakType(NEXTPAGE_BREAK),
	m_lastParagraph(nullptr),
	m_target(0),
	m_handledHdrFtr(false)
{
	m_headerIds[0] = nullptr;
	m_headerIds[1] = nullptr;
	m_headerIds[2] = nullptr;

	m_footerIds[0] = nullptr;
	m_footerIds[1] = nullptr;
	m_footerIds[2] = nullptr;

	m_children.clear();
}

OXML_Section::~OXML_Section()
{
	g_free(m_headerIds[0]); g_free(m_headerIds[1]); g_free(m_headerIds[2]);
	g_free(m_footerIds[0]); g_free(m_footerIds[1]); g_free(m_footerIds[2]);
	clearChildren();
}

bool OXML_Section::operator ==(const std::string & id)
{
	return this->m_id.compare(id) == 0;
}

OXML_SharedElement OXML_Section::getElement(const std::string & id)
{
	OXML_ElementVector::iterator it;
	it = std::find(m_children.begin(), m_children.end(), id);
	return ( it != m_children.end() ) ? (*it) : OXML_SharedElement() ;
}

UT_Error OXML_Section::appendElement(OXML_SharedElement obj)
{
	UT_return_val_if_fail(obj.get() != nullptr, UT_ERROR);

	try {
		m_children.push_back(obj);
	} catch(...) {
		UT_DEBUGMSG(("Bad alloc!\n"));
		return UT_OUTOFMEM;
	}

	obj->setTarget(m_target);

	return UT_OK;
}

UT_Error OXML_Section::clearChildren()
{
	m_children.clear();
	return m_children.size() == 0 ? UT_OK : UT_ERROR;
}

UT_Error OXML_Section::serialize(IE_Exp_OpenXML* exporter)
{
	UT_Error ret = UT_OK;
	OXML_Document* doc = OXML_Document::getInstance();

	applyDocumentProperties();

	OXML_ElementVector::size_type i;

	if(this != doc->getLastSection().get())
	{
		for (i = 0; i < m_children.size(); i++)
		{
			if(m_children[i].get() && ((m_children[i].get())->getTag() == P_TAG))
			{
				static_cast<OXML_Element_Paragraph*>(m_children[i].get())->setSection(this);
				m_lastParagraph = static_cast<OXML_Element_Paragraph*>(m_children[i].get());
			}
		}
	}

	for (i = 0; i < m_children.size(); i++)
	{
		ret = m_children[i]->serialize(exporter);
		if(ret != UT_OK)
			return ret;
	}	
	return UT_OK;
}

void OXML_Section::applyDocumentProperties()
{
	OXML_Document* pDoc = OXML_Document::getInstance();

	const gchar* num = nullptr;
	const gchar* sep = "off";
	const gchar* marginTop = nullptr;
	const gchar* marginLeft = nullptr;
	const gchar* marginRight = nullptr;
	const gchar* marginBottom = nullptr;

	if(getProperty("columns", num) != UT_OK)
		num = nullptr;

	if((getProperty("column-line", sep) != UT_OK) || (strcmp(sep, "on") != 0))
		sep = "off";

	if(getProperty("page-margin-top", marginTop) != UT_OK)
		marginTop = nullptr;

	if(getProperty("page-margin-left", marginLeft) != UT_OK)
		marginLeft = nullptr;

	if(getProperty("page-margin-right", marginRight) != UT_OK)
		marginRight = nullptr;

	if(getProperty("page-margin-bottom", marginBottom) != UT_OK)
		marginBottom = nullptr;

	if(num && sep)
		pDoc->setColumns(num, sep);

	if(marginTop || marginLeft || marginRight || marginBottom)
		pDoc->setPageMargins(marginTop ? marginTop : "1in",
							 marginLeft ? marginLeft : "1in",
							 marginRight ? marginRight : "1in",
							 marginBottom ? marginBottom : "1in");
}

UT_Error OXML_Section::serializeProperties(IE_Exp_OpenXML* exporter, OXML_Element_Paragraph* pParagraph)
{
	//TODO: Add all the property serializations here
	UT_Error err = UT_OK;

	if(pParagraph != m_lastParagraph)
	{
		return UT_OK;
	}

	OXML_Document* doc = OXML_Document::getInstance();
	bool defaultHdr = doc->isAllDefault(true);
	bool defaultFtr = doc->isAllDefault(false);

	const gchar* num = nullptr;
	const gchar* sep = "off";
	const gchar* marginTop = nullptr;
	const gchar* marginLeft = nullptr;
	const gchar* marginRight = nullptr;
	const gchar* marginBottom = nullptr;
	const gchar* footerId = nullptr;
	const gchar* headerId = nullptr;

	if(getProperty("columns", num) != UT_OK)
		num = nullptr;

	if((getProperty("column-line", sep) != UT_OK) || (strcmp(sep, "on") != 0))
		sep = "off";

	if(getProperty("page-margin-top", marginTop) != UT_OK)
		marginTop = nullptr;

	if(getProperty("page-margin-left", marginLeft) != UT_OK)
		marginLeft = nullptr;

	if(getProperty("page-margin-right", marginRight) != UT_OK)
		marginRight = nullptr;

	if(getProperty("page-margin-bottom", marginBottom) != UT_OK)
		marginBottom = nullptr;

	if(getAttribute("header", headerId) != UT_OK)
		headerId = nullptr;

	if(getAttribute("footer", footerId) != UT_OK)
		footerId = nullptr;

	err = exporter->startSectionProperties();
	if(err != UT_OK)
		return err;

	if(num && sep)
	{
		err = exporter->setColumns(m_target, num, sep);
		if(err != UT_OK)
			return err;
	}

	err = exporter->setNextPageSection(m_target);
	if(err != UT_OK)
		return err;

	if(defaultHdr && headerId && doc)
	{
		OXML_SharedSection header_section = doc->getHdrFtrById(true, headerId);
		if(header_section != nullptr)
		{
			header_section->setHandledHdrFtr(true);
			err = header_section->serializeHeader(exporter);
			if (err != UT_OK)
				return err;
		}
	}

	if(defaultFtr && footerId && doc)
	{
		OXML_SharedSection footer_section = doc->getHdrFtrById(false, footerId);
		if(footer_section != nullptr)
		{
			footer_section->setHandledHdrFtr(true);
			err = footer_section->serializeFooter(exporter);
			if (err != UT_OK)
				return err;
		}
	}

	if(marginTop || marginLeft || marginRight || marginBottom)
	{	
		err = exporter->setPageMargins(m_target,
									   marginTop ? marginTop : "1in",
									   marginLeft ? marginLeft : "1in",
									   marginRight ? marginRight : "1in",
									   marginBottom ? marginBottom : "1in");
		if(err != UT_OK)
			return err;
	}

	return exporter->finishSectionProperties();
}

bool OXML_Section::hasFirstPageHdrFtr() const
{
	UT_Error ret = UT_OK;

	const gchar* headerType;

	ret = getAttribute("type", headerType);
	if(ret != UT_OK)
		return false;

	return strstr(headerType, "first");
}

bool OXML_Section::hasEvenPageHdrFtr() const
{
	UT_Error ret = UT_OK;

	const gchar* headerType;

	ret = getAttribute("type", headerType);
	if(ret != UT_OK)
		return false;

	return strstr(headerType, "even");
}

/**
 * Serialize the section as a header
 */
UT_Error OXML_Section::serializeHeader(IE_Exp_OpenXML* exporter)
{
	UT_Error ret = UT_OK;

	const gchar* headerId;
	const gchar* headerType;

	ret = getAttribute("id", headerId);
	if(ret != UT_OK)
		return UT_OK;

	ret = getAttribute("type", headerType);
	if(ret != UT_OK)
		return UT_OK;

	const gchar* type = "default";
	//OOXML includes default, first and even.  
	if(strstr(headerType, "first"))
	{
		type = "first";
	}
	else if(strstr(headerType, "even"))
	{
		type = "even";
	}
	else if(strstr(headerType, "last"))
	{
		//last not implemented in OOXML
		return UT_OK;
	}
			
	std::string header("hId");
	header += headerId;

	ret = exporter->setHeaderReference(header.c_str(), type);
	if(ret != UT_OK)
		return ret;

	ret = exporter->setHeaderRelation(header.c_str(), headerId);
	if(ret != UT_OK)
		return ret;	

	ret = exporter->startHeaderStream(headerId);
	if(ret != UT_OK)
		return ret;	

	OXML_ElementVector::size_type i;
	for (i = 0; i < m_children.size(); i++)
	{
		m_children[i]->setTarget(TARGET_HEADER);
		ret = m_children[i]->serialize(exporter);
		if(ret != UT_OK)
			return ret;
	}

	return exporter->finishHeaderStream();
}

/**
 * Serialize the section as a footer
 */
UT_Error OXML_Section::serializeFooter(IE_Exp_OpenXML* exporter)
{
	UT_Error ret = UT_OK;

	const gchar* footerId;
	const gchar* footerType;

	ret = getAttribute("id", footerId);
	if(ret != UT_OK)
		return UT_OK;
		
	std::string footer("fId");
	footer += footerId;

	ret = getAttribute("type", footerType);
	if(ret != UT_OK)
		return UT_OK;

	const gchar* type = "default";
	//OOXML includes default, first and even.  
	if(strstr(footerType, "first"))
	{
		type = "first";
	}
	else if(strstr(footerType, "even"))
	{
		type = "even";
	}
	else if(strstr(footerType, "last"))
	{
		//last not implemented in OOXML
		return UT_OK;
	}


	ret = exporter->setFooterReference(footer.c_str(), type);
	if(ret != UT_OK)
		return ret;

	ret = exporter->setFooterRelation(footer.c_str(), footerId);
	if(ret != UT_OK)
		return ret;	

	ret = exporter->startFooterStream(footerId);
	if(ret != UT_OK)
		return ret;	

	OXML_ElementVector::size_type i;
	for (i = 0; i < m_children.size(); i++)
	{
		m_children[i]->setTarget(TARGET_FOOTER);
		ret = m_children[i]->serialize(exporter);
		if(ret != UT_OK)
			return ret;
	}

	return exporter->finishFooterStream();
}

/**
 * Serialize the section as a footnote
 */
UT_Error OXML_Section::serializeFootnote(IE_Exp_OpenXML* exporter)
{
	UT_Error ret = UT_OK;

	const gchar* footnoteId;

	ret = getAttribute("footnote-id", footnoteId);
	if(ret != UT_OK)
		return UT_OK;

	ret = exporter->startFootnote(footnoteId);
	if(ret != UT_OK)
		return ret;

	setTarget(TARGET_FOOTNOTE);
	OXML_ElementVector::size_type i;
	for (i = 0; i < m_children.size(); i++)
	{
		ret = m_children[i]->serialize(exporter);
		if(ret != UT_OK)
			return ret;
	}

	return exporter->finishFootnote();
}

/**
 * Serialize the section as a endnote
 */
UT_Error OXML_Section::serializeEndnote(IE_Exp_OpenXML* exporter)
{
	UT_Error ret = UT_OK;

	const gchar* endnoteId;

	ret = getAttribute("endnote-id", endnoteId);
	if(ret != UT_OK)
		return UT_OK;

	ret = exporter->startEndnote(endnoteId);
	if(ret != UT_OK)
		return ret;

	setTarget(TARGET_ENDNOTE);
	OXML_ElementVector::size_type i;
	for (i = 0; i < m_children.size(); i++)
	{
		ret = m_children[i]->serialize(exporter);
		if(ret != UT_OK)
			return ret;
	}

	return exporter->finishEndnote();
}

static std::string oxmlCommentParaId(const char* id)
{
	unsigned long value = 1;
	if (id && *id)
	{
		char* end = nullptr;
		value = strtoul(id, &end, 10);
		if (end == id)
		{
			value = 0;
			for (const unsigned char* p = reinterpret_cast<const unsigned char*>(id); *p; ++p)
				value = value * 131u + *p;
		}
	}
	char buf[16];
	snprintf(buf, sizeof(buf), "%08lX", value & 0xfffffffful);
	return buf;
}

static void oxmlAppendProp(std::string& props, const char* name, const char* value)
{
	if (!value || !*value)
		return;
	if (!props.empty())
		props += "; ";
	props += name;
	props += ": ";
	props += value;
}

UT_Error OXML_Section::serializeComment(IE_Exp_OpenXML* exporter)
{
	UT_Error ret = UT_OK;
	const gchar* commentId = nullptr;
	if (getAttribute("annotation-id", commentId) != UT_OK || !commentId)
		commentId = m_id.c_str();

	std::string paraId = oxmlCommentParaId(commentId);
	const gchar* stored = nullptr;
	if (getAttribute("para-id", stored) == UT_OK && stored && *stored)
		paraId = stored;
	else
		setAttribute("para-id", paraId.c_str());

	ret = exporter->startComment(commentId, this);
	if (ret != UT_OK)
		return ret;

	int lastParagraph = -1;
	for (OXML_ElementVector::size_type i = 0; i < m_children.size(); ++i)
	{
		if (m_children[i]->getTag() == P_TAG)
			lastParagraph = static_cast<int>(i);
	}
	if (lastParagraph < 0)
	{
		exporter->setNextParagraphParaId(paraId.c_str());
		ret = exporter->startParagraph(TARGET_COMMENT);
		if (ret != UT_OK)
			return ret;
		ret = exporter->finishParagraph(TARGET_COMMENT);
		if (ret != UT_OK)
			return ret;
	}
	for (OXML_ElementVector::size_type i = 0; i < m_children.size(); ++i)
	{
		if (static_cast<int>(i) == lastParagraph)
			exporter->setNextParagraphParaId(paraId.c_str());
		m_children[i]->setTarget(TARGET_COMMENT);
		ret = m_children[i]->serialize(exporter);
		if (ret != UT_OK)
			return ret;
	}
	return exporter->finishComment(this);
}

UT_Error OXML_Section::addToPTAsAnnotation(PD_Document * pDocument)
{
	std::string props;
	const gchar* value = nullptr;
	if (getAttribute("annotation-author", value) == UT_OK)
		oxmlAppendProp(props, "annotation-author", value);
	if (getAttribute("annotation-date", value) == UT_OK)
		oxmlAppendProp(props, "annotation-date", value);
	if (getAttribute("annotation-initials", value) == UT_OK)
		oxmlAppendProp(props, "annotation-initials", value);
	if (getAttribute("annotation-parent", value) == UT_OK)
		oxmlAppendProp(props, "annotation-parent", value);
	if (getAttribute("annotation-resolved", value) == UT_OK)
		oxmlAppendProp(props, "annotation-resolved", value);
	if (getAttribute("annotation-title", value) == UT_OK)
		oxmlAppendProp(props, "annotation-title", value);

	PP_PropertyVector attr = { "annotation-id", m_id };
	if (!props.empty())
	{
		attr.push_back(PT_PROPS_ATTRIBUTE_NAME);
		attr.push_back(props);
	}
	if (!pDocument->appendStrux(PTX_SectionAnnotation, attr))
		return UT_ERROR;

	if (m_children.empty())
	{
		if (!pDocument->appendStrux(PTX_Block, PP_NOPROPS))
			return UT_ERROR;
	}
	for (OXML_ElementVector::size_type i = 0; i < m_children.size(); ++i)
	{
		UT_Error ret = m_children[i]->addToPT(pDocument);
		if (ret != UT_OK)
			return ret;
	}
	return pDocument->appendStrux(PTX_EndAnnotation, PP_NOPROPS) ? UT_OK : UT_ERROR;
}

UT_Error OXML_Section::addToPT(PD_Document * pDocument)
{
	UT_Error ret = UT_OK;

	if (pDocument == nullptr)
		return UT_ERROR;

	ret = _setReferenceIds();
	UT_return_val_if_fail(ret == UT_OK, ret);

	//Appending section
	PP_PropertyVector attr = this->getAttributesWithProps();
	ret = pDocument->appendStrux(PTX_Section, attr) ? UT_OK : UT_ERROR;
	UT_return_val_if_fail(ret == UT_OK, ret);

	//Appending page break to the section if necessary
	if (m_breakType == NEXTPAGE_BREAK || m_breakType == ODDPAGE_BREAK || m_breakType == EVENPAGE_BREAK) {
		UT_UCS4Char ucs = UCS_FF;
		ret = pDocument->appendSpan(&ucs, 1) ? UT_OK : UT_ERROR;
		UT_return_val_if_fail(ret == UT_OK, ret);
	}

	OXML_ElementVector::size_type i;
	for (i = 0; i < m_children.size(); i++)
	{
		ret = m_children[i]->addToPT(pDocument);
		UT_return_val_if_fail(ret == UT_OK, ret);
	}

	return UT_OK;
}

UT_Error OXML_Section::addToPTAsFootnote(PD_Document * pDocument)
{
	UT_Error ret = UT_OK;
	const PP_PropertyVector attr = { "footnote-id", m_id };

	ret = pDocument->appendStrux(PTX_SectionFootnote, attr) ? UT_OK : UT_ERROR;
	UT_return_val_if_fail(ret == UT_OK, ret);

	const PP_PropertyVector field_fmt = {
		"type", "footnote_anchor",
		"footnote-id", m_id
	};

	if(!pDocument->appendObject(PTO_Field, field_fmt))
		return UT_ERROR;

	OXML_ElementVector::size_type i;
	i = 0;

	if(m_children[0].get() && ((m_children[0].get())->getTag() == P_TAG))
	{
		//skip the first paragraph and directly add its children
		ret = m_children[0]->addChildrenToPT(pDocument);
		UT_return_val_if_fail(ret == UT_OK, ret);
		i = 1;
	}

	for (; i < m_children.size(); i++)
	{
		ret = m_children[i]->addToPT(pDocument);
		UT_return_val_if_fail(ret == UT_OK, ret);
	}

	return pDocument->appendStrux(PTX_EndFootnote, PP_NOPROPS) ? UT_OK : UT_ERROR;
}

UT_Error OXML_Section::addToPTAsEndnote(PD_Document * pDocument)
{
	UT_Error ret = UT_OK;
	const PP_PropertyVector attr = { "endnote-id", m_id };

	ret = pDocument->appendStrux(PTX_SectionEndnote, attr) ? UT_OK : UT_ERROR;
	UT_return_val_if_fail(ret == UT_OK, ret);

	const PP_PropertyVector field_fmt = {
		"type", "endnote_anchor",
		"endnote-id", m_id
	};

	if(!pDocument->appendObject(PTO_Field, field_fmt))
		return UT_ERROR;

	OXML_ElementVector::size_type i;
	i = 0;

	if(m_children[0].get() && ((m_children[0].get())->getTag() == P_TAG))
	{
		//skip the first paragraph and directly add its children
		ret = m_children[0]->addChildrenToPT(pDocument);
		UT_return_val_if_fail(ret == UT_OK, ret);
		i = 1;
	}

	for (; i < m_children.size(); i++)
	{
		ret = m_children[i]->addToPT(pDocument);
		UT_return_val_if_fail(ret == UT_OK, ret);
	}

	return pDocument->appendStrux(PTX_EndEndnote, PP_NOPROPS) ? UT_OK : UT_ERROR;
}

UT_Error OXML_Section::addToPTAsHdrFtr(PD_Document * pDocument)
{
	UT_Error ret = UT_OK;
	const PP_PropertyVector attr = this->getAttributes();
	ret = pDocument->appendStrux(PTX_SectionHdrFtr, attr) ? UT_OK : UT_ERROR;
	UT_return_val_if_fail(ret == UT_OK, ret);

	OXML_ElementVector::size_type i;
	for (i = 0; i < m_children.size(); i++)
	{
		ret = m_children[i]->addToPT(pDocument);
		UT_return_val_if_fail(ret == UT_OK, ret);
	}
	return ret;
}

UT_Error OXML_Section::_setReferenceIds()
{
	OXML_Document * doc = OXML_Document::getInstance(); UT_ASSERT(nullptr != doc);
	OXML_SharedSection corresp_sect;
	const char * ooxml_id(nullptr), * abw_id(nullptr);

	//Headers...
	for (UT_uint32 i = 0; i <= 2; i++) {
		ooxml_id = m_headerIds[i]; abw_id = nullptr;
		if (nullptr != ooxml_id) {
			corresp_sect = doc->getHeader(ooxml_id);
			UT_return_val_if_fail( nullptr != corresp_sect.get(), UT_ERROR );
			corresp_sect->getAttribute("id", abw_id);
			UT_return_val_if_fail( nullptr != abw_id, UT_ERROR );
			if (i == DEFAULT_HDRFTR) {
				this->setAttribute("header", abw_id );
			} else if (i == FIRSTPAGE_HDRFTR) {
				this->setAttribute("header-first", abw_id );
			} else if (i == EVENPAGE_HDRFTR) {
				this->setAttribute("header-even", abw_id );
			}	
		}
	}

	//Footers...
	for (UT_uint32 i = 0; i <= 2; i++) {
		ooxml_id = m_footerIds[i]; abw_id = nullptr;
		if (nullptr != ooxml_id) {
			corresp_sect = doc->getFooter(ooxml_id);
			UT_return_val_if_fail( nullptr != corresp_sect.get(), UT_ERROR );
			corresp_sect->getAttribute("id", abw_id);
			UT_return_val_if_fail( nullptr != abw_id, UT_ERROR );
			if (i == DEFAULT_HDRFTR) {
				this->setAttribute("footer", abw_id );
			} else if (i == FIRSTPAGE_HDRFTR) {
				this->setAttribute("footer-first", abw_id );
			} else if (i == EVENPAGE_HDRFTR) {
				this->setAttribute("footer-even", abw_id );
			}	
		}
	}
	return UT_OK;
}

void OXML_Section::setTarget(int target)
{
	m_target = target;
	for (OXML_ElementVector::size_type i = 0; i < m_children.size(); ++i)
	{
		if (m_children[i])
			m_children[i]->setTarget(target);
	}
}

UT_Error OXML_Section::setPageMargins(const std::string & top, const std::string & left, const std::string & right, const std::string & bottom)
{
	UT_Error ret = UT_OK;

	if(top.compare(""))
	{
		ret = setProperty("page-margin-top", top);
		if(ret != UT_OK)
			return ret;
	}

	if(left.compare(""))
	{
		ret = setProperty("page-margin-left", left);
		if(ret != UT_OK)
			return ret;
	}

	if(right.compare(""))
	{
		ret = setProperty("page-margin-right", right);
		if(ret != UT_OK)
			return ret;
	}

	if(bottom.compare(""))
	{
		ret = setProperty("page-margin-bottom", bottom);	
		if(ret != UT_OK)
			return ret;
	}

	return ret;
}

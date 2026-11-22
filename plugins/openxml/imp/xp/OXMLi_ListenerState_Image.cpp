/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: t -*- */

/* AbiSource
 * 
 * Copyright (C) 2009 Firat Kiyak <firatkiyak@gmail.com>
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
#include "OXMLi_ListenerState_Image.h"

// Internal includes
#include "OXML_Document.h"
#include "OXML_FontManager.h"
#include "OXML_Element_Text.h"
#include "OXMLi_PackageManager.h"

// AbiWord includes
#include "ut_assert.h"
#include "ut_misc.h"
#include "ie_impGraphic.h"
#include "fg_GraphicRaster.h"

// External includes
#include <cstring>
#include <map>
#include <string>

static std::string abi_xml_local_name(const std::string& name)
{
	std::string::size_type colon = name.rfind(':');
	if (colon == std::string::npos)
		return name;
	return name.substr(colon + 1);
}

static bool image_from_is_page(const std::string& from)
{
	return from == "page";
}

static bool image_from_is_column(const std::string& from)
{
	return from == "column" || from == "margin" || from == "leftMargin" ||
		from == "rightMargin" || from == "insideMargin" || from == "outsideMargin" ||
		from == "topMargin" || from == "bottomMargin";
}

static const char* image_offset_property(const std::string& from, bool horizontal)
{
	if (image_from_is_page(from))
		return horizontal ? "frame-page-xpos" : "frame-page-ypos";
	if (image_from_is_column(from))
		return horizontal ? "frame-col-xpos" : "frame-col-ypos";
	return horizontal ? "xpos" : "ypos";
}

static void image_copy_offset(OXML_SharedElement img, const char* dest, const char* first, const char* second)
{
	const gchar* value = nullptr;
	if (!img || (img->getProperty(dest, value) == UT_OK && value && *value))
		return;
	value = nullptr;
	if (img->getProperty(first, value) == UT_OK && value && *value)
		img->setProperty(dest, value);
	else if (img->getProperty(second, value) == UT_OK && value && *value)
		img->setProperty(dest, value);
}

static void abi_append_drawing_note(OXMLi_ElementStack* stck, const std::string& note)
{
	if (!stck || stck->empty() || note.empty())
		return;
	OXML_SharedElement top = stck->top();
	OXML_SharedElement parent = top;
	if (top && top->getTag() == IMG_TAG && stck->size() >= 2)
	{
		stck->pop();
		parent = stck->top();
		stck->push(top);
	}
	if (!parent)
		return;
	parent->appendElement(OXML_SharedElement(new OXML_Element_Text(note.c_str(), static_cast<int>(note.size()))));
}

OXMLi_ListenerState_Image::OXMLi_ListenerState_Image()
  : OXMLi_ListenerState(),
	m_style(""),
	m_isInlineImage(false)
{

}

void OXMLi_ListenerState_Image::startElement (OXMLi_StartElementRequest * rqst)
{
	if(nameMatches(rqst->pName, NS_W_KEY, "object"))
	{
		rqst->handled = true;
	}

	if(nameMatches(rqst->pName, NS_W_KEY, "drawing"))
	{
		OXML_SharedElement imgElem(new OXML_Element_Image(""));
		rqst->stck->push(imgElem);
		rqst->handled = true;
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "inline"))
	{
		if(rqst->stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}
		std::string contextTag = "";
		if(!rqst->context->empty())
		{
			contextTag = rqst->context->back();
		}
		int drawing = contextMatches(contextTag, NS_W_KEY, "drawing");
		if(drawing)
		{
			m_isInlineImage = true;
			rqst->handled = true;
		}
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "anchor"))
	{
		if(rqst->stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}
		std::string contextTag = "";
		if(!rqst->context->empty())
		{
			contextTag = rqst->context->back();
		}
		int drawing = contextMatches(contextTag, NS_W_KEY, "drawing");
		if(drawing)
		{
			m_isInlineImage = false;
			rqst->handled = true;
		}
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "positionH"))
	{
		if(rqst->stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}
		std::string contextTag = "";
		if(!rqst->context->empty())
		{
			contextTag = rqst->context->back();
		}
		int anchor = contextMatches(contextTag, NS_WP_KEY, "anchor");
		if(anchor)
		{
			const gchar* from = rqst->ppAtts ? attrMatches(NS_WP_KEY, "relativeFrom", rqst->ppAtts) : nullptr;
			m_hFrom = from ? from : "";
			rqst->handled = true;
		}
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "positionV"))
	{
		if(rqst->stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}
		std::string contextTag = "";
		if(!rqst->context->empty())
		{
			contextTag = rqst->context->back();
		}
		int anchor = contextMatches(contextTag, NS_WP_KEY, "anchor");
		if(anchor)
		{
			const gchar* from = rqst->ppAtts ? attrMatches(NS_WP_KEY, "relativeFrom", rqst->ppAtts) : nullptr;
			m_vFrom = from ? from : "";
			rqst->handled = true;
		}
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "posOffset"))
	{
		if(rqst->stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}
		std::string contextTag = "";
		if(!rqst->context->empty())
		{
			contextTag = rqst->context->back();
		}
		int positionH = contextMatches(contextTag, NS_WP_KEY, "positionH");
		int positionV = contextMatches(contextTag, NS_WP_KEY, "positionV");
		if(positionH || positionV)
		{
			rqst->handled = true;
		}
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "extent"))
	{
		if(rqst->stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}

		OXML_SharedElement imgElem = rqst->stck->top();
		if(!imgElem)
			return;

		const gchar * cx = attrMatches(NS_WP_KEY, "cx", rqst->ppAtts); //width
		if(cx)
		{
			std::string width(_EmusToInches(cx));
			width += "in";
			if(m_isInlineImage)
			{
				if(imgElem->setProperty("width", width) != UT_OK)
				{
					UT_DEBUGMSG(("SERHAT:OpenXML importer inline image width property can't be set\n"));
				}
			}
			else
			{
				if(imgElem->setProperty("frame-width", width) != UT_OK)
				{
					UT_DEBUGMSG(("SERHAT:OpenXML importer positioned image width property can't be set\n"));
				}
			}
		}

		const gchar * cy = attrMatches(NS_WP_KEY, "cy", rqst->ppAtts); //height
		if(cy)
		{
			std::string height(_EmusToInches(cy));
			height += "in";
			if(m_isInlineImage)
			{
				if(imgElem->setProperty("height", height) != UT_OK)
				{
					UT_DEBUGMSG(("SERHAT:OpenXML importer inline image height property can't be set\n"));
				}
			}
			else
			{
				if(imgElem->setProperty("frame-height", height) != UT_OK)
				{
					UT_DEBUGMSG(("SERHAT:OpenXML importer positioned image height property can't be set\n"));
				}
			}
		}

		rqst->handled = true;
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "wrapSquare"))
	{
		if(rqst->stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}

		OXML_SharedElement imgElem = rqst->stck->top();
		if(!imgElem)
			return;

		const gchar * wrapText = attrMatches(NS_WP_KEY, "wrapText", rqst->ppAtts);
		if(wrapText)
		{
			if(!strcmp(wrapText, "bothSides"))
			{
				if(imgElem->setProperty("wrap-mode", "wrapped-both") != UT_OK)
				{
					UT_DEBUGMSG(("SERHAT:OpenXML importer image wrap-mode property can't be set\n"));
				}
			}
			else if(!strcmp(wrapText, "right"))
			{
				if(imgElem->setProperty("wrap-mode", "wrapped-to-right") != UT_OK)
				{
					UT_DEBUGMSG(("SERHAT:OpenXML importer image wrap-mode property can't be set\n"));
				}
			}
			else if(!strcmp(wrapText, "left"))
			{
				if(imgElem->setProperty("wrap-mode", "wrapped-to-left") != UT_OK)
				{
					UT_DEBUGMSG(("SERHAT:OpenXML importer image wrap-mode property can't be set\n"));
				}
			}
		}
		rqst->handled = true;
	}
	else if (nameMatches(rqst->pName, NS_A_KEY, "blip"))
	{
		if(rqst->stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}

		OXML_SharedElement imgElem = rqst->stck->top();
		if(!imgElem)
			return;

		const gchar * id = attrMatches(NS_R_KEY, "embed", rqst->ppAtts);
		if(id)
		{
			std::string imageId(id);
			imgElem->setId(id);
			rqst->handled = addImage(imageId);
		}
	}	
	else if(nameMatches(rqst->pName, NS_V_KEY, "shape"))
	{
		const gchar* style = attrMatches(NS_V_KEY, "style", rqst->ppAtts);
		if(style)
		{
			m_style = style;
		}
		//don't handle the request here in case shape contains some other structure, ex: textbox
	}
	else if(nameMatches(rqst->pName, NS_V_KEY, "imagedata"))
	{
		const gchar* id = attrMatches(NS_R_KEY, "id", rqst->ppAtts);
		if(id)
		{
			std::string imageId(id);
			OXML_SharedElement imgElem(new OXML_Element_Image(imageId));
			rqst->stck->push(imgElem);

			if(!addImage(imageId))
				return;

			if(m_style.compare(""))
			{
				//parse and apply style here
				std::string attrName("");
				std::string attrValue("");
				size_t attrStart = 0;
				size_t attrEnd = 0;
				while(attrStart < m_style.length())
				{
					attrEnd = m_style.find(';', attrStart);
					if(attrEnd == std::string::npos)
					{
						//this should be the last attribute
						attrEnd = m_style.length(); 
					}
					std::string attrNameValPair = m_style.substr(attrStart, attrEnd-attrStart);
					size_t seperator = attrNameValPair.find(':');
					if(seperator != std::string::npos)
					{
						attrName = attrNameValPair.substr(0, seperator);
						attrValue = attrNameValPair.substr(seperator+1);

						//convert and apply attributes here
						if(!attrName.compare("width"))
						{
							imgElem->setProperty("width", attrValue);
						}
						else if(!attrName.compare("height"))
						{
							imgElem->setProperty("height", attrValue);
						}
						//TODO: more attributes coming
					}	
					//finally update the start point for the next attribute
					attrStart = attrEnd+1;
				}
			}
			rqst->handled = true;
		}
	}
	else if (abi_xml_local_name(rqst->pName) == "chart" ||
			 abi_xml_local_name(rqst->pName) == "relIds")
	{
		const gchar* id = attrMatches(NS_R_KEY, "id", rqst->ppAtts);
		if (!id)
			id = attrMatches(NS_R_KEY, "dm", rqst->ppAtts);
		if (id)
		{
			OXMLi_PackageManager* mgr = OXMLi_PackageManager::getInstance();
			std::string text = mgr ? mgr->readPartText(id) : std::string();
			if (!text.empty())
				abi_append_drawing_note(rqst->stck, text);
		}
		rqst->handled = true;
	}
	else if (nameMatches(rqst->pName, NS_O_KEY, "OLEObject") ||
			 abi_xml_local_name(rqst->pName) == "OLEObject")
	{
		const gchar* prog = attrMatches(NS_O_KEY, "ProgID", rqst->ppAtts);
		if (!prog && rqst->ppAtts)
		{
			std::map<std::string, std::string>::iterator it = rqst->ppAtts->find("ProgID");
			if (it != rqst->ppAtts->end())
				prog = it->second.c_str();
		}
		std::string note = "OLE";
		if (prog && *prog)
		{
			note += ": ";
			note += prog;
		}
		abi_append_drawing_note(rqst->stck, note);
		rqst->handled = true;
	}

	//TODO: more coming here
}

void OXMLi_ListenerState_Image::endElement (OXMLi_EndElementRequest * rqst)
{
	if(nameMatches(rqst->pName, NS_W_KEY, "object"))
	{
		rqst->handled = true;
		return;
	}

	if(nameMatches(rqst->pName, NS_W_KEY, "drawing") || 
		nameMatches(rqst->pName, NS_V_KEY, "imagedata"))
	{
		//image is done
		rqst->handled = (_flushTopLevel(rqst->stck, rqst->sect_stck) == UT_OK);
	}
	else if(nameMatches(rqst->pName, NS_A_KEY, "blip") ||
			nameMatches(rqst->pName, NS_WP_KEY, "extent") ||
			nameMatches(rqst->pName, NS_WP_KEY, "wrapSquare") ||
			nameMatches(rqst->pName, NS_WP_KEY, "posOffset") ||
			nameMatches(rqst->pName, NS_WP_KEY, "positionH") ||
			nameMatches(rqst->pName, NS_WP_KEY, "positionV"))
	{
		rqst->handled = true;
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "anchor"))
	{
		if(rqst->stck && !rqst->stck->empty())
			placeAnchoredImage(rqst->stck->top());
		m_hFrom.clear();
		m_vFrom.clear();
		m_isInlineImage = false;
		rqst->handled = true;
	}
	else if(nameMatches(rqst->pName, NS_WP_KEY, "inline"))
	{
		m_hFrom.clear();
		m_vFrom.clear();
		m_isInlineImage = false;
		rqst->handled = true;
	}
	else if(nameMatches(rqst->pName, NS_V_KEY, "shape"))
	{
		m_style = "";
	}
	//TODO: more coming here
}

void OXMLi_ListenerState_Image::charData (OXMLi_CharDataRequest * rqst)
{
	if(rqst->stck->empty())
	{
		rqst->handled = false;
		rqst->valid = false;
		return;
	}
	if(!rqst)
	{
		UT_DEBUGMSG(("SERHAT: OpenXML importer invalid NULL request in OXMLi_ListenerState_Image.charData\n"));
		return;
	}

	std::string contextTag = "";
	if(!rqst->context->empty())
	{
		contextTag = rqst->context->back();
	}
	int posOffset = contextMatches(contextTag, NS_WP_KEY, "posOffset");
	if(posOffset && !m_isInlineImage)
	{
		if (rqst->stck->empty())
			return;
		OXML_SharedElement imgElem = rqst->stck->top();
		rqst->stck->pop();

		if(rqst->context->size() >= 2)
			contextTag = rqst->context->at(rqst->context->size() - 2);
		int positionH = contextMatches(contextTag, NS_WP_KEY, "positionH");
		int positionV = contextMatches(contextTag, NS_WP_KEY, "positionV");
		if(rqst->buffer == nullptr)
		{
			UT_DEBUGMSG(("SERHAT: Unexpected situation, request with a null buffer\n"));
			rqst->stck->push(imgElem);
			return;
		}
		const char* prop = nullptr;
		if(positionH)
			prop = image_offset_property(m_hFrom, true);
		else if(positionV)
			prop = image_offset_property(m_vFrom, false);
		if(prop)
		{
			std::string position(_EmusToInches(rqst->buffer));
			position += "in";
			imgElem->setProperty(prop, position);
		}
		rqst->stck->push(imgElem);
	}
}


void OXMLi_ListenerState_Image::placeAnchoredImage(OXML_SharedElement img)
{
	if(!img)
		return;
	const char* mode = "block-above-text";
	if(image_from_is_page(m_hFrom) && image_from_is_page(m_vFrom))
		mode = "page-above-text";
	else if(image_from_is_column(m_hFrom) && image_from_is_column(m_vFrom))
		mode = "column-above-text";
	if(!strcmp(mode, "page-above-text"))
	{
		image_copy_offset(img, "frame-page-xpos", "frame-col-xpos", "xpos");
		image_copy_offset(img, "frame-page-ypos", "frame-col-ypos", "ypos");
	}
	else if(!strcmp(mode, "column-above-text"))
	{
		image_copy_offset(img, "frame-col-xpos", "frame-page-xpos", "xpos");
		image_copy_offset(img, "frame-col-ypos", "frame-page-ypos", "ypos");
	}
	else
	{
		image_copy_offset(img, "xpos", "frame-col-xpos", "frame-page-xpos");
		image_copy_offset(img, "ypos", "frame-col-ypos", "frame-page-ypos");
	}
	img->setProperty("position-to", mode);
}

//helper functions
bool OXMLi_ListenerState_Image::addImage(const std::string & id)
{
	UT_Error err = UT_OK;
	FG_ConstGraphicPtr pFG;
		
	OXMLi_PackageManager * mgr = OXMLi_PackageManager::getInstance();
	UT_ConstByteBufPtr imageData = mgr->parseImageStream(id.c_str());

	if (!imageData)
		return false;

	err = IE_ImpGraphic::loadGraphic(imageData, IEGFT_Unknown, pFG);
	if ((err != UT_OK) || !pFG)
	{
		UT_DEBUGMSG(("FRT:OpenXML importer can't import the picture with id:%s\n", id.c_str()));
		return false;
	}

	OXML_Document * doc = OXML_Document::getInstance();
	if(!doc)
		return false;

	OXML_Image* img = new OXML_Image();
	img->setId(id.c_str());
	img->setGraphic(std::move(pFG));

	OXML_SharedImage shrImg(img);

	return doc->addImage(shrImg) == UT_OK;
}

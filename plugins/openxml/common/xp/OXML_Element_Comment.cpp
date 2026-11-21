#include "OXML_Element_Comment.h"

#include "OXML_Document.h"
#include "OXML_Section.h"

#include "ut_types.h"
#include "pd_Document.h"

OXML_Element_Comment::OXML_Element_Comment(const std::string & id) :
	OXML_Element(id, CMT_TAG, SPAN),
	m_type("start")
{
}

OXML_Element_Comment::~OXML_Element_Comment()
{
}

void OXML_Element_Comment::setType(const std::string & type)
{
	m_type = type;
}

UT_Error OXML_Element_Comment::serialize(IE_Exp_OpenXML* exporter)
{
	if (m_type == "reference")
		return UT_OK;
	if (m_type == "end")
		return exporter->finishCommentRange(TARGET, getId().c_str());
	return exporter->startCommentRange(TARGET, getId().c_str());
}

UT_Error OXML_Element_Comment::addToPT(PD_Document * pDocument)
{
	OXML_Document* oxml = OXML_Document::getInstance();
	if (!oxml || !pDocument)
		return UT_ERROR;

	if (m_type == "end")
		return pDocument->appendObject(PTO_Annotation, PP_NOPROPS) ? UT_OK : UT_ERROR;

	if (m_type == "reference" && oxml->wasCommentOpened(getId()))
		return UT_OK;

	const PP_PropertyVector attrs = {
		"annotation", getId()
	};
	if (!pDocument->appendObject(PTO_Annotation, attrs))
		return UT_ERROR;

	OXML_SharedSection body = oxml->getComment(getId());
	if (body && body->addToPTAsAnnotation(pDocument) != UT_OK)
		return UT_ERROR;

	oxml->markCommentOpened(getId());
	if (m_type == "reference")
		return pDocument->appendObject(PTO_Annotation, PP_NOPROPS) ? UT_OK : UT_ERROR;
	return UT_OK;
}

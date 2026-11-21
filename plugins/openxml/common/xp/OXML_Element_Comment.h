#ifndef _OXML_ELEMENT_COMMENT_H_
#define _OXML_ELEMENT_COMMENT_H_

#include "OXML_Element.h"
#include "ie_exp_OpenXML.h"

#include "ut_types.h"
#include "pd_Document.h"

class OXML_Element_Comment : public OXML_Element
{
public:
	OXML_Element_Comment(const std::string & id);
	virtual ~OXML_Element_Comment();

	virtual UT_Error serialize(IE_Exp_OpenXML* exporter) override;
	virtual UT_Error addToPT(PD_Document * pDocument) override;

	void setType(const std::string & type);

private:
	std::string m_type;
};

#endif

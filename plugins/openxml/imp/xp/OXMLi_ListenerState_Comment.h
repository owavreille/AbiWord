#ifndef _OXMLI_LISTENERSTATE_COMMENT_H_
#define _OXMLI_LISTENERSTATE_COMMENT_H_

#include <OXMLi_ListenerState.h>
#include <OXMLi_Types.h>
#include <OXML_Types.h>

class OXMLi_ListenerState_Comment : public OXMLi_ListenerState
{
public:
	virtual void startElement (OXMLi_StartElementRequest * rqst) override;
	virtual void endElement (OXMLi_EndElementRequest * rqst) override;
	virtual void charData (OXMLi_CharDataRequest * rqst) override;
};

#endif

#include "OXMLi_ListenerState_Comment.h"

#include "OXML_Document.h"
#include "OXML_Section.h"

#include "ut_assert.h"

void OXMLi_ListenerState_Comment::startElement (OXMLi_StartElementRequest * rqst)
{
	if (nameMatches(rqst->pName, NS_W_KEY, "comments")
		|| nameMatches(rqst->pName, NS_W15_KEY, "commentsEx"))
	{
		rqst->handled = true;
		return;
	}

	if (nameMatches(rqst->pName, NS_W15_KEY, "commentEx"))
	{
		const gchar* paraId = attrMatches(NS_W15_KEY, "paraId", rqst->ppAtts);
		const gchar* parent = attrMatches(NS_W15_KEY, "paraIdParent", rqst->ppAtts);
		const gchar* done = attrMatches(NS_W15_KEY, "done", rqst->ppAtts);
		OXML_Document* doc = OXML_Document::getInstance();
		if (doc && paraId)
			doc->setCommentExtra(paraId, parent ? parent : "", done ? done : "0");
		rqst->handled = true;
		return;
	}

	if (nameMatches(rqst->pName, NS_W_KEY, "p"))
	{
		const gchar* paraId = attrMatches(NS_W14_KEY, "paraId", rqst->ppAtts);
		if (paraId && rqst->sect_stck && !rqst->sect_stck->empty())
			rqst->sect_stck->top()->setAttribute("para-id", paraId);
	}

	if (nameMatches(rqst->pName, NS_W_KEY, "comment"))
	{
		const gchar* id = attrMatches(NS_W_KEY, "id", rqst->ppAtts);
		if (id)
		{
			OXML_SharedSection sect(new OXML_Section(id));
			const gchar* author = attrMatches(NS_W_KEY, "author", rqst->ppAtts);
			const gchar* date = attrMatches(NS_W_KEY, "date", rqst->ppAtts);
			const gchar* initials = attrMatches(NS_W_KEY, "initials", rqst->ppAtts);
			if (author)
				sect->setAttribute("annotation-author", author);
			if (date)
				sect->setAttribute("annotation-date", date);
			if (initials)
				sect->setAttribute("annotation-initials", initials);
			rqst->sect_stck->push(sect);
		}
		rqst->handled = true;
	}
}

void OXMLi_ListenerState_Comment::endElement (OXMLi_EndElementRequest * rqst)
{
	if (nameMatches(rqst->pName, NS_W_KEY, "comments")
		|| nameMatches(rqst->pName, NS_W15_KEY, "commentsEx")
		|| nameMatches(rqst->pName, NS_W15_KEY, "commentEx"))
	{
		rqst->handled = true;
		return;
	}

	if (nameMatches(rqst->pName, NS_W_KEY, "comment"))
	{
		if (rqst->sect_stck->empty())
		{
			rqst->handled = false;
			rqst->valid = false;
			return;
		}
		OXML_SharedSection sect = rqst->sect_stck->top();
		rqst->sect_stck->pop();
		OXML_Document* pDoc = OXML_Document::getInstance();
		if (pDoc && pDoc->addComment(sect) != UT_OK)
			return;
		rqst->handled = true;
	}
}

void OXMLi_ListenerState_Comment::charData (OXMLi_CharDataRequest * /*rqst*/)
{
}

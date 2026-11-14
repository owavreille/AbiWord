#ifndef XAP_WIN32ENCMGR_H
#define XAP_WIN32ENCMGR_H

#include "xap_EncodingManager.h"

/* it's assumed that only one instance of this class will exist */
class ABI_EXPORT XAP_Win32EncodingManager: public XAP_EncodingManager
{
protected:
    XAP_Win32EncodingManager();
    virtual ~XAP_Win32EncodingManager();

    void 	initialize() override;

public:

    const char* getNativeEncodingName() 	const override;
    const char* getNativeSystemEncodingName() 	const override;
    const char* getNative8BitEncodingName() const override;
    const char* getNativeNonUnicodeEncodingName() const override;
    const char* getNativeUnicodeEncodingName() const override;
	inline virtual bool isUnicodeLocale()	const  override {return m_bIsUnicodeLocale;}
    const char* getLanguageISOName() 		const override;
    const char* getLanguageISOTerritory() 	const override;

	friend class XAP_EncodingManager;

private:
	bool m_bIsUnicodeLocale;
};

#endif /* XAP_WIN32ENCMGR_H */

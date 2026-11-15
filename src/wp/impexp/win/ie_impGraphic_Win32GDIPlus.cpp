/* AbiWord
 *
 * Copyright (C) 2004 Jordi Mas i Hernàndez
 * Win32 native plugin based on win32 GDIPlus
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

#include <windows.h>
#include <new>
#include <stdlib.h>
#include "ut_string.h"
#include "ut_bytebuf.h"
#include "ie_impGraphic.h"
#include "fg_GraphicRaster.h"
#include "ut_assert.h"
#include "xap_Module.h"

/*

		GDI Plus definitions

*/

typedef ULONG_PTR Status;
// typedef DWORD ULONG_PTR;
typedef ULONG_PTR EncoderParameters;
class GpImage {};


// x64 layout is 24 bytes: version, 4 bytes of padding, then the callback
// pointer. A DWORD callback makes GdiplusStartup read the two flags past
// the object.
struct GdiplusStartupInput
{
    UINT32 GdiplusVersion;
    void * DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;

};
 
struct GdiplusStartupOutput
{
    DWORD NotificationHook;
    DWORD NotificationUnhook;
};

// GDI+ uses GUIDs to identify a particular image encoder. See
// http://msdn.microsoft.com/en-us/library/ms533843(VS.85).aspx for an
// example on retrieving the GUIDs.

/* PNG GUID's encoder */
CLSID gdip_png_encoder_guid = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

/* JPEG GUID's encoder */
CLSID gdip_jpeg_encoder_guid = { 0x557cf401, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

// GDI+ also uses GUIDs to identify image types. See GdiPlusImaging.h for the defined types, or
// http://msdn.microsoft.com/en-us/library/ms535393(VS.85).aspx for an
// example on retrieving the GUIDs.
GUID gdip_jpeg_guid = { 0xb96b3cae, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };


/* Globals */
static ULONG_PTR gdiplusToken = 0;
static HINSTANCE gdipluslib = nullptr;


/* Callbacks */
typedef Status (CALLBACK* GDIPLUSSTARTUP) (ULONG_PTR *token,  const GdiplusStartupInput *input,
    OUT GdiplusStartupOutput *output);
typedef Status (CALLBACK* GDIPLOADIMAGEFROMSTREAM) (IStream* stream, GpImage **image);
typedef Status (CALLBACK* GDIPSAVEIMAGETOSTREAM) (GpImage *image, IStream* stream,
                      CLSID* clsidEncoder, EncoderParameters* encoderParams);
typedef Status (CALLBACK* GDIPGETIMAGERRAWFFORMAT) (GpImage *image, GUID *format);

typedef Status (CALLBACK* GDIPDISPOSEIMAGE) (GpImage *image);

typedef void (CALLBACK* GDIPLUSSHUTDOWN) (ULONG_PTR token);


/*

		GDI Plus dynamic loaded functions

*/

Status 
GdiplusStartup (ULONG_PTR *token,  const GdiplusStartupInput *input,
    GdiplusStartupOutput *output)
{

	GDIPLUSSTARTUP	proc = nullptr;

	if (gdipluslib)			  
		proc = reinterpret_cast<GDIPLUSSTARTUP>(reinterpret_cast<void *>(GetProcAddress(gdipluslib, "GdiplusStartup")));	

	if (!proc)
		return -1;

	return (*proc) (token, input, output);
}

void 
GdiplusShutdown (ULONG_PTR token)
{
	GDIPLUSSHUTDOWN	proc = nullptr;

	if (gdipluslib)			  
		proc = reinterpret_cast<GDIPLUSSHUTDOWN>(reinterpret_cast<void *>(GetProcAddress(gdipluslib, "GdiplusShutdown")));	

	if (!proc)
		return;

	(*proc) (token);
}

Status 
GdipLoadImageFromStream (IStream* stream, GpImage **image)
{

	GDIPLOADIMAGEFROMSTREAM	proc = nullptr;

	if (gdipluslib)			  
		proc = reinterpret_cast<GDIPLOADIMAGEFROMSTREAM>(reinterpret_cast<void *>(GetProcAddress(gdipluslib, "GdipLoadImageFromStream")));	

	if (!proc)
		return -1;

	return (*proc) (stream, image);
}

Status 
GdipSaveImageToStream (GpImage *image, IStream* stream,
                      CLSID* clsidEncoder, EncoderParameters* encoderParams)
{
	GDIPSAVEIMAGETOSTREAM	proc = nullptr;

	if (gdipluslib)			  
		proc = reinterpret_cast<GDIPSAVEIMAGETOSTREAM>(reinterpret_cast<void *>(GetProcAddress(gdipluslib, "GdipSaveImageToStream")));	

	if (!proc)
		return -1;

	return (*proc) (image, stream, clsidEncoder, encoderParams);
}

Status
GdipGetImageRawFormat (GpImage *image, GUID *format)
{
	GDIPGETIMAGERRAWFFORMAT	proc = nullptr;

	if (gdipluslib)			  
		proc = reinterpret_cast<GDIPGETIMAGERRAWFFORMAT>(reinterpret_cast<void *>(GetProcAddress(gdipluslib, "GdipGetImageRawFormat")));	

	if (!proc)
		return -1;

	return (*proc) (image, format);
}


Status 
GdipDisposeImage (GpImage *image)
{
	GDIPDISPOSEIMAGE	proc = nullptr;

	if (gdipluslib)			  
		proc = reinterpret_cast<GDIPDISPOSEIMAGE>(reinterpret_cast<void *>(GetProcAddress(gdipluslib, "GdipDisposeImage")));	

	if (!proc)
		return -1;

	return (*proc) (image);
}


/*
	
	  Private functions

*/

// Initialize GDI+.
void 
initGDIPlus ()
{
	GdiplusStartupInput input;	

	if (!gdipluslib)
		gdipluslib = ::LoadLibrary (TEXT("gdiplus.dll"));

	if (!gdipluslib)
		return;

	input.GdiplusVersion = 1;
	input.DebugEventCallback = nullptr;
	input.SuppressBackgroundThread = input.SuppressExternalCodecs = FALSE;

	// Ok is 0. A failed startup must not leave a token that later calls treat
	// as a live GDI+.
	if (GdiplusStartup (&gdiplusToken, &input, nullptr) != 0)
		gdiplusToken = 0;
}


/*
	
	  Public functions

*/

bool 
isGDIPlusAvailable()
{
	if (gdiplusToken == 0)
		initGDIPlus();
			
	return (gdiplusToken == 0 ? false : true);
}

void 
shutDownGDIPlus()
{
	if (gdiplusToken == 0)
		return;
	GdiplusShutdown(gdiplusToken);
	gdiplusToken = 0;
}


/* Conversion function */
UT_Error 
GDIconvertGraphic(const UT_ConstByteBufPtr& pBB, const UT_ByteBufPtr& pBBOut, std::string& mimetype)
{
	IStream *stream = nullptr;
	IStream *streamOut = nullptr;
	HGLOBAL hG = nullptr;
	GpImage *image = nullptr;
	Status status;
	HRESULT hr;
	LARGE_INTEGER ulnSize;
	LARGE_INTEGER lnOffset;
	ULONG ulBytesRead = 0;
	char *pBuff = nullptr;
	GUID format;
	UT_Error result = UT_ERROR;
	lnOffset.QuadPart = 0;
	ulnSize.QuadPart = 0;

	// CreateStreamOnHGlobal(hG, FALSE) does not free hG. Every return
	// below has to release both streams, dispose a loaded image, and
	// GlobalFree the source block.
	size_t nBlockLen = pBB->getLength();
	hG = GlobalAlloc (GPTR, nBlockLen);

	if (!hG)
		return UT_IE_NOMEMORY;

	CopyMemory (hG, pBB->getPointer(0), nBlockLen);

	hr = CreateStreamOnHGlobal (hG, FALSE ,&stream);
	if (!SUCCEEDED (hr) || !stream)
	{
		GlobalFree (hG);
		return UT_IE_NOMEMORY;
	}

	hr = CreateStreamOnHGlobal (nullptr, TRUE, &streamOut);
	if (!SUCCEEDED (hr) || !streamOut)
		goto cleanup;

	status =  GdipLoadImageFromStream (stream, &image);
	if (status != 0)
		goto cleanup;

	status = GdipGetImageRawFormat (image, &format);
	if (status != 0)
		goto cleanup;

	if (format != gdip_jpeg_guid)
	{
		status = GdipSaveImageToStream (image, streamOut, &gdip_png_encoder_guid, nullptr);
		if (status != 0)
			goto cleanup;
		mimetype = "image/png";
	}
	else
	{
		status = GdipSaveImageToStream (image, streamOut, &gdip_jpeg_encoder_guid, nullptr);
		if (status != 0)
			goto cleanup;
		mimetype = "image/jpeg";
	}

	GdipDisposeImage (image);
	image = nullptr;

	if(streamOut->Seek (lnOffset, STREAM_SEEK_END, (union _ULARGE_INTEGER *) &ulnSize) != S_OK)
		goto cleanup;

	if(streamOut->Seek (lnOffset, STREAM_SEEK_SET, nullptr) != S_OK)
		goto cleanup;

	if (ulnSize.QuadPart <= 0 || ulnSize.QuadPart > 0xffffffffu)
		goto cleanup;

	{
		const DWORD sz = static_cast<DWORD>(ulnSize.QuadPart);
		pBuff = new(std::nothrow) char [sz];
		if (!pBuff)
			goto cleanup;
		if (streamOut->Read (pBuff, sz, &ulBytesRead) != S_OK)
			goto cleanup;
		if (ulBytesRead > sz)
			ulBytesRead = sz;
		pBBOut->append ((const unsigned char*)pBuff, ulBytesRead);
	}

	result = UT_OK;

cleanup:
	delete [] pBuff;
	if (image)
		GdipDisposeImage (image);
	if (streamOut)
		streamOut->Release ();
	if (stream)
		stream->Release ();
	if (hG)
		GlobalFree (hG);
	return result;
}


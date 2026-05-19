/*!
 * @file 		DeckLink3DVideoFrame.h
 * @author 		Zdenek Travnicek <travnicek@iim.cz>
 * @date		25.1.2012
 * @date		21.11.2013
 * @copyright	CESNET, z.s.p.o, 2012 - 2013
 * 				Distributed under BSD Licence, details in file doc/LICENSE
 *
 */

#ifndef DECKLINK3DVIDEOFRAME_H_
#define DECKLINK3DVIDEOFRAME_H_
#include "DeckLinkAPI_wrapper.h"
#include "yuri/core/utils/new_types.h"
//#include "yuri/core/BasicFrame.h"
namespace yuri {

namespace decklink {
bool operator==(const REFIID & first, const REFIID & second);

#ifdef DECKLINK_API_NO_FRAME_GETBYTES
class DeckLink3DVideoFrame: public IDeckLinkVideoFrame, public IDeckLinkVideoFrame3DExtensions, public IDeckLinkVideoBuffer {
#else
class DeckLink3DVideoFrame: public IDeckLinkVideoFrame, public IDeckLinkVideoFrame3DExtensions {
#endif
public:
	DeckLink3DVideoFrame(size_t width, size_t height, BMDPixelFormat format, BMDFrameFlags flags);
	virtual ~DeckLink3DVideoFrame();
	virtual ULONG STDMETHODCALLTYPE		AddRef ()									{return 1;}
	virtual ULONG STDMETHODCALLTYPE		Release ()									{return 1;}
	virtual long GetWidth (void);
	virtual long GetHeight (void);
	virtual long GetRowBytes (void);
	virtual BMDPixelFormat GetPixelFormat (void);
	virtual BMDFrameFlags GetFlags (void);
	virtual HRESULT GetBytes (/* out */ void **buffer);
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **ppv);
	virtual HRESULT GetTimecode (/* in */ BMDTimecodeFormat format, /* out */ IDeckLinkTimecode **timecode);
	virtual HRESULT GetAncillaryData (/* out */ IDeckLinkVideoFrameAncillary **ancillary);
    virtual BMDVideo3DPackingFormat Get3DPackingFormat (void);
    virtual HRESULT GetFrameForRightEye (/* out */ IDeckLinkVideoFrame* *rightEyeFrame);
#ifdef DECKLINK_API_NO_FRAME_GETBYTES
    // IDeckLinkVideoBuffer — SDK 14.3+ reads output frame pixel data via this interface
    virtual HRESULT StartAccess(BMDBufferAccessFlags /*flags*/) { return S_OK; }
    virtual HRESULT EndAccess(BMDBufferAccessFlags /*flags*/) { return S_OK; }
#ifdef DECKLINK_API_16
    virtual HRESULT GetSize(uint64_t* size) { *size = static_cast<uint64_t>(height * linesize_); return S_OK; }
#endif
#endif

    void add_right(std::shared_ptr<DeckLink3DVideoFrame> r);
    void set_packing_format(BMDVideo3DPackingFormat fmt);
    std::shared_ptr<DeckLink3DVideoFrame> get_right();
protected:
    size_t width, height;
    size_t linesize_;
    BMDPixelFormat format;
    uint8_t *buffer;

    BMDFrameFlags flags;
    std::shared_ptr<DeckLink3DVideoFrame> right;
    BMDVideo3DPackingFormat packing;
};

}

}

#endif /* DECKLINK3DVIDEOFRAME_H_ */

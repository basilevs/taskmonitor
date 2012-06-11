#pragma once
#include <Wbemidl.h>
# pragma comment(lib, "wbemuuid.lib")

#include <boost/signals2/signal.hpp>
#include <boost/thread/mutex.hpp>

class EventSink: public IWbemObjectSink
{
	boost::mutex _mutex;
	ULONG _refCount;
public:
    EventSink() { _refCount = 0;}
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE Release();        
    virtual HRESULT 
        STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv);

    virtual HRESULT STDMETHODCALLTYPE Indicate( 
            LONG lObjectCount,
            IWbemClassObject __RPC_FAR *__RPC_FAR *apObjArray
            );
        
    virtual HRESULT STDMETHODCALLTYPE SetStatus( 
            /* [in] */ LONG lFlags,
            /* [in] */ HRESULT hResult,
            /* [in] */ BSTR strParam,
            /* [in] */ IWbemClassObject __RPC_FAR *pObjParam
            );
	boost::signals2::signal<void(IWbemClassObject *)> listeners;
};




#pragma once
#include <memory>
#include <vector>
#include <functional>
#include <Wbemidl.h>
# pragma comment(lib, "wbemuuid.lib")

class EventSink: public IWbemObjectSink
{
public:
	ULONG _refCount;
    bool bDone;

public:
	typedef std::function<void(IWbemClassObject *)> Listener;
    EventSink() { _refCount = 0; }
   ~EventSink() { bDone = true; }

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
	void addListener(const Listener & listener) {_listeners.push_back(listener);}
private:
	std::vector<Listener> _listeners; //boost::signal2 is better
};




#include "StdAfx.h"
#include "EventSink.h"

ULONG EventSink::AddRef()
{
    return InterlockedIncrement(&_refCount);
}

ULONG EventSink::Release()
{
    ULONG lRef = InterlockedDecrement(&_refCount);
    if(lRef == 0)
        delete this;
    return lRef;
}

HRESULT EventSink::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == IID_IUnknown || riid == __uuidof(IWbemObjectSink))
    {
        *ppv = (IWbemObjectSink *) this;
        AddRef();
        return WBEM_S_NO_ERROR;
    }
    else return E_NOINTERFACE;
}


HRESULT EventSink::Indicate(long lObjectCount,
    IWbemClassObject **apObjArray)
{
	HRESULT hres = S_OK;
	if (bDone)
		return WBEM_S_NO_ERROR;
    for (int i = 0; i < lObjectCount; i++)
    {
		//Unfortunately COM object structure get distorted on exit and exceptions are raisen.
		//We can't really prevent this from happening without proper synchronization with EventSink::SetStatus
		if (bDone) 
			return WBEM_S_NO_ERROR; 
		for each (const Listener & listener in _listeners) {
			if (bDone)
				return WBEM_S_NO_ERROR;
			listener(apObjArray[i]);
		}
    }

    return WBEM_S_NO_ERROR;
}

HRESULT EventSink::SetStatus(
            /* [in] */ LONG lFlags,
            /* [in] */ HRESULT hResult,
            /* [in] */ BSTR strParam,
            /* [in] */ IWbemClassObject __RPC_FAR *pObjParam
        )
{
    if(lFlags == WBEM_STATUS_COMPLETE)
    {
        printf("Call complete. hResult = 0x%X\n", hResult);
		bDone = true;
    }
    else if(lFlags == WBEM_STATUS_PROGRESS)
    {
		bDone = false;
        printf("Call in progress.\n");
    }

    return WBEM_S_NO_ERROR;
}    // end of EventSink.cpp

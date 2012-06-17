#include "stdafx.h"
#include "WmiTools.h"
#include "ComError.h"

#include "EventSink.h"

using namespace std;

IWbemServicesPtr connectToWmiServices() {
	// Step 3: ---------------------------------------------------
	// Obtain the initial locator to WMI -------------------------

	IWbemLocatorPtr locator(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER);

	if (!locator) {
		throw runtime_error("Failed to obtain WMI locator");
	}

	// Step 4: ---------------------------------------------------
	// Connect to WMI through the IWbemLocator::ConnectServer method

	IWbemServicesPtr services;
	{
		// Connect to the local root\cimv2 namespace
		// and obtain pointer pSvc to make IWbemServices calls.
		HRESULT hres = locator->ConnectServer(
			_T("ROOT\\CIMV2"), 
			NULL,
			NULL, 
			0, 
			NULL, 
			0, 
			0, 
			&services
			);
		ComError::handle(hres, "Failed to connect to local root\\cimv2 namespace");
	}

	// Step 5: --------------------------------------------------
	// Set security levels on the proxy -------------------------
	{
		HRESULT hres = CoSetProxyBlanket(
			services,  // Indicates the proxy to set
			RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx 
			RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx 
			NULL,                        // Server principal name 
			RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx 
			RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
			NULL,                        // client identity
			EOAC_NONE                    // proxy capabilities 
			);
		ComError::handle(hres, "Failed to configure proxy security");
	}
	return services;
}

AsyncQueryHandle::AsyncQueryHandle(const IWbemServicesPtr & services, const IWbemObjectSinkPtr & sink, const _bstr_t & query):
	_services(services, true),
	_sink(sink, true)
{
	HRESULT hres = _services->ExecNotificationQueryAsync(
		_bstr_t("WQL"),
		query,
		WBEM_FLAG_SEND_STATUS,
		NULL,
		_sink);
	ComError::handleWithErrorInfo(hres, toConsoleEncoding(wstring(L"Failed to perform async query: ")+static_cast<wchar_t*>(query)), static_cast<IWbemServices*>(_services));
}
void AsyncQueryHandle::cancel() {
	_services->CancelAsyncCall(_sink);
	_sink->SetStatus(0, WBEM_STATUS_COMPLETE, 0, 0); //Synchronizing.
}
AsyncQueryHandle::~AsyncQueryHandle() {
	cancel();
}

class UnsecuredAppartment {
	IUnsecuredApartmentPtr _appartment;
public:
	UnsecuredAppartment() {
		_appartment.CreateInstance(CLSID_UnsecuredApartment, 0, CLSCTX_LOCAL_SERVER);
		if (!_appartment) {
			throw runtime_error("Failed to create UnsecuredApartment");
		}
	}
	//Allows asynchronous callbacks bypass security settings
	template<class T>
	T wrap(T & input) {
		IUnknown* pStubUnk = NULL;
		//Many additional references to wrapped object are made here from another thread.
		//This causes virtually infinite lifetime for wrapped object.
		HRESULT hres = _appartment->CreateObjectStub(input, &pStubUnk); 
		ComError::handle(hres, "Failed to enable unsecure callbacks");
		IUnknownPtr unk(pStubUnk); // To free the reference returned by CreateObjectStub.
		//Query adds another reference
		return query<T>(unk);
	}
};


AsyncWmiQuery::AsyncWmiQuery(const IWbemServicesPtr & services, const _bstr_t & query, std::function<void(IWbemClassObject * x)> callback)
{
	UnsecuredAppartment appartment;
	// Step 6: -------------------------------------------------
	// Receive event notifications -----------------------------
	EventSink * pSink = new EventSink;
	IWbemObjectSinkPtr sink(pSink, true);
	_connection = pSink->listeners.connect(callback);
	sink = appartment.wrap(sink);
	_handle.reset(new AsyncQueryHandle (services,  sink, query));
}

SemisyncWmiQuery::SemisyncWmiQuery(const IWbemServicesPtr & services, const _bstr_t & query) {
	HRESULT hres = services->ExecNotificationQuery(_bstr_t("WQL"), _bstr_t(query), WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY, 0, &_enumerator);
	ComError::handleWithErrorInfo(hres, "Synchronous WMI notification query failed.", services.GetInterfacePtr());
}

IWbemClassObjectPtr SemisyncWmiQuery::next() {
	ULONG count = 0;
	IWbemClassObjectPtr rv;
	HRESULT hres = _enumerator->Next(100, 1, &rv, &count);
	if (hres != WBEM_S_TIMEDOUT)
		ComError::handleWithErrorInfo(hres, "Failed to get next notification", _enumerator.GetInterfacePtr());
	return rv;
}
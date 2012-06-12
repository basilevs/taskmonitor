#include "stdafx.h"
#include "WmiTools.h"
#include "ComError.h"

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

Query::Query(IWbemServices & services, IWbemObjectSink & sink, const _bstr_t & query):
	_services(&services, true),
		_sink(&sink, true)
{
	HRESULT hres = services.ExecNotificationQueryAsync(
		_bstr_t("WQL"),
		query,
		WBEM_FLAG_SEND_STATUS,
		NULL,
		&sink);
	ComError::handleWithErrorInfo(hres, toConsoleEncoding(wstring(L"Failed to perform async query: ")+static_cast<wchar_t*>(query)), &services);
}
void Query::cancel() {
	_services->CancelAsyncCall(_sink);
	_sink->SetStatus(0, WBEM_STATUS_COMPLETE, 0, 0); //Synchronizing.
}
Query::~Query() {
	cancel();
}

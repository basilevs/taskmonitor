// TaskMonitor.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <comip.h>
#include <comdef.h>

#include "EventSink.h"
#include "ComError.h"


using namespace std;

class ComInitializer {
public:
	ComInitializer() {
		HRESULT hres =  CoInitializeEx(0, COINIT_MULTITHREADED); 
		ComError::handle(hres, "Failed to initialize COM");

		hres =  CoInitializeSecurity(
			NULL, 
			-1,                          // COM negotiates service
			NULL,                        // Authentication services
			NULL,                        // Reserved
			RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication 
			RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation  
			NULL,                        // Authentication info
			EOAC_NONE,                   // Additional capabilities 
			NULL                         // Reserved
			);
		if (FAILED(hres)) {
			CoUninitialize();
			ComError::handle(hres, "Failed to initialize security");
		}
	}
	~ComInitializer() {
		CoUninitialize();
	}
};

_COM_SMARTPTR_TYPEDEF(IWbemLocator,        __uuidof(IWbemLocator));
_COM_SMARTPTR_TYPEDEF(IWbemServices,       __uuidof(IWbemServices));
_COM_SMARTPTR_TYPEDEF(IUnsecuredApartment, __uuidof(IUnsecuredApartment));
_COM_SMARTPTR_TYPEDEF(IWbemObjectSink,     __uuidof(IWbemObjectSink));

int _tmain(int argc, _TCHAR* argv[])
{

	// Step 1: --------------------------------------------------
    // Initialize COM. ------------------------------------------
	ComInitializer comInitializer;

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

		IWbemServices *pSvc = NULL;
	
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
			&pSvc
		);
		ComError::handle(hres, "Failed to connect to local root\\cimv2 namespace");
		services.Attach(pSvc);
	}
	    

    cout << "Connected to ROOT\\CIMV2 WMI namespace" << endl;


    // Step 5: --------------------------------------------------
    // Set security levels on the proxy -------------------------
	{
		HRESULT hres = CoSetProxyBlanket(
			services.GetInterfacePtr(),  // Indicates the proxy to set
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
    // Step 6: -------------------------------------------------
    // Receive event notifications -----------------------------

    
	IUnsecuredApartmentPtr appartment;
	{
		// Use an unsecured apartment for security
		IUnsecuredApartment* pUnsecApp = NULL;

		HRESULT hres = CoCreateInstance(CLSID_UnsecuredApartment, NULL, 
			CLSCTX_LOCAL_SERVER, IID_IUnsecuredApartment, 
			(void**)&pUnsecApp);
		ComError::handle(hres, "Failed to initialize unsecured appartment");
		appartment.Attach(pUnsecApp);
	}

	EventSink* pSink = new EventSink;
	IWbemObjectSinkPtr sink(pSink, true);
	

	//Allow asynchronous callbacks bypass security settings
	{
		IUnknown* pStubUnk = NULL;
		HRESULT hres = appartment->CreateObjectStub(sink, &pStubUnk);
		ComError::handle(hres, "Failed to enable callbacks");
		IUnknownPtr unk(pStubUnk);
		sink.Attach(query<IWbemObjectSinkPtr>(unk));
	}

	{
		HRESULT hres = services->ExecNotificationQueryAsync(
			_bstr_t("WQL"),
			_bstr_t("SELECT * FROM __InstanceCreationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'"),
			WBEM_FLAG_SEND_STATUS,
			NULL,
			sink);
		ComError::handleWithErrorInfo(hres, "Failed to perform async query", services.GetInterfacePtr());
	}
	Sleep(10000);
	services->CancelAsyncCall(sink);

	return 0;
}


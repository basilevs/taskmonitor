#include "stdafx.h"


#include <memory>
#include <map>

#include <comip.h>
#include <comdef.h>

	

#include <conio.h>

#include "EventSink.h"
#include "ComError.h"
#include "Task.h"

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
_COM_SMARTPTR_TYPEDEF(EventSink,           __uuidof(IWbemObjectSink)); //Incorrect. Do not instantiate internally.

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

//Controls query interruption for exception safety
//We absolutely can't continue sending events to event handler that might reference local context, that is already out of scope.
class Query {
	IWbemServicesPtr _services;
	IWbemObjectSinkPtr _sink;
public:
	Query(IWbemServices & services, IWbemObjectSink & sink, const _bstr_t & query):
	  _services(&services, true),
		  _sink(&sink, true)
	  {
		  HRESULT hres = services.ExecNotificationQueryAsync(
			  _bstr_t("WQL"),
			  query,
			  WBEM_FLAG_SEND_STATUS,
			  NULL,
			  &sink);
		  ComError::handleWithErrorInfo(hres, string("Failed to perform async query: ")+static_cast<char*>(query), &services);
	  }
	  void cancel() {
		  _services->CancelAsyncCall(_sink);
	  }
	  ~Query() {
		  cancel();
	  }
};

unique_ptr<Query> notificationQuery(IWbemServices & services, const _bstr_t & query, IWbemObjectSink & sink)
{
	return unique_ptr<Query>(new Query(services, sink, query));
}

unique_ptr<Query> notificationQueryForType(IWbemServices & services, const _bstr_t & type, IWbemObjectSink & sink) {
	return notificationQuery(services, _bstr_t("SELECT * FROM ") + type + _bstr_t(" WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'"), sink);
}

wostream & operator <<(wostream & ostr, IWbemClassObject & object) {
	BSTR text;
	ComError::handleWithErrorInfo(object.GetObjectText(0, &text), string("Failed to get object text"), &object);
	ostr << text;
	SysFreeString(text);
	return ostr;
}

int _tmain(int argc, _TCHAR* argv[])
{

	// Step 1: --------------------------------------------------
	// Initialize COM. ------------------------------------------
	ComInitializer comInitializer;
	IWbemServicesPtr services = connectToWmiServices();
	UnsecuredAppartment appartment;

	Tasks tasks;

	// Step 6: -------------------------------------------------
	// Receive event notifications -----------------------------

	wcout.imbue(locale("Russian"));
	EventSink * pSink = new EventSink;
	IWbemObjectSinkPtr sink(pSink, true);
	pSink->addListener([&](IWbemClassObject * x) {
		if (!x)
			return;
		try {
			if (tasks.notify(*x))
				wcout << *x << endl;
		}catch (exception & e) {
			cerr << e.what() << endl;
		}
	});
	sink = appartment.wrap(sink);

	unique_ptr<Query> query1 = notificationQueryForType(services, "__InstanceCreationEvent", sink);
	unique_ptr<Query> query2 = notificationQueryForType(services, "__InstanceDeletionEvent", sink);
	unique_ptr<Query> query3 = notificationQueryForType(services, "__InstanceModificationEvent", sink);
	
	_getwch();

	return 0;
}


#include "stdafx.h"

#define _CRTDBG_MAP_ALLOC 1
#define _CRTDBG_MAP_ALLOC_NEW 1
#include <stdlib.h>
#include <crtdbg.h>

#include <memory>
#include <fstream>
#include <boost/algorithm/string/join.hpp>


#include <comdef.h>
#include <conio.h>

#include "EventSink.h"
#include "ComError.h"
#include "Task.h"

using namespace std;
using namespace boost;

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
	Query(const Query &);
	Query & operator=(const Query &);
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
		  ComError::handleWithErrorInfo(hres, toConsoleEncoding(wstring(L"Failed to perform async query: ")+static_cast<wchar_t*>(query)), &services);
	  }
	  void cancel() {
		  _services->CancelAsyncCall(_sink);
		  _sink->SetStatus(0, WBEM_STATUS_COMPLETE, 0, 0); //Synchronizing.
	  }
	  ~Query() {
		  cancel();
	  }
};

unique_ptr<Query> notificationQuery(IWbemServices & services, const _bstr_t & query, IWbemObjectSink & sink)
{
	return unique_ptr<Query>(new Query(services, sink, query));
}

unique_ptr<Query> notificationQueryForProcesses(IWbemServices & services, IWbemObjectSink & sink, const vector<wstring> & names) {
	wostringstream temp;
	temp << L"SELECT * FROM __InstanceOperationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'";
	if (names.size() >0) {
		temp << L" AND (TargetInstance.Name='";
		temp << algorithm::join(names, L"' OR TargetInstance.Name='");
		temp <<  L"')";
	}
	return notificationQuery(services, _bstr_t(temp.str().c_str()), sink);
}

wostream & operator <<(wostream & ostr, IWbemClassObject & object) {
	BSTR text;
	ComError::handleWithErrorInfo(object.GetObjectText(0, &text), string("Failed to get object text"), &object);
	ostr << text;
	SysFreeString(text);
	return ostr;
}

class VirtualSizeChanges: public Tasks {
	virtual bool shouldReport(Event e, const Task & oldState, const Task & newState) {
		if (e == CREATED || e == DELETED)
			return true;
		if (e == CHANGED) {
			if (std::abs(oldState.virtualSize() - newState.virtualSize()) > 1024*1024)
				return true;
		}
		return false;
	}
};



wostream & operator <<(wostream & ostr, const Task & task) {
	return ostr << dec << (int)task.processId() << L" " << task.virtualSize();
}

wostream & operator <<(wostream & ostr, Tasks::Event e) {
	switch(e) {
		case Tasks::CREATED: return ostr << L"Created";
		case Tasks::CHANGED: return ostr << L"Changed";
		case Tasks::DELETED: return ostr << L"Deleted";
		default: return ostr << L"Unknown";
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	try { 
	wstring logFileName = L"taskmonitor.log";
	vector<wstring> processNames;
	unique_ptr<wostream> fout;
	if (argc > 1) {
		logFileName = argv[argc-1];
		processNames.insert(processNames.begin(), argv+1, argv+argc-1);
	}
	try {
		fout.reset(new wofstream(logFileName));
		ostringstream encodingString;
		encodingString<< "." << GetACP();
		locale logLocale(encodingString.str()); 
		logLocale = locale(logLocale, &use_facet<numpunct<wchar_t> >(locale("C"))); //Remove decimal separator.
		fout->imbue(logLocale);
	} catch (std::exception & e) {
		cerr << " Failed to open file" << toConsoleEncoding(logFileName) << ": " << e.what() << endl;
	}

	{
		ostringstream encodingString;
		encodingString<< "." << GetConsoleOutputCP();
		locale consoleLocale(encodingString.str());
		consoleLocale = locale(consoleLocale, &use_facet<numpunct<wchar_t> >(locale("C"))); //Remove decimal separator.
		wcout.imbue(consoleLocale);
	}

	// Step 1: --------------------------------------------------
	// Initialize COM. ------------------------------------------
	ComInitializer comInitializer;
	IWbemServicesPtr services = connectToWmiServices();
	UnsecuredAppartment appartment;

	VirtualSizeChanges tasks;
	tasks.listeners.connect([&](Tasks::Event e, const Task& task){
		wcout << task.name() << L" " << e << L" " << task << endl;
		if (fout.get()) {
			*fout << task.name() << L" " << e << L" " << task << endl;
		}
	});

	// Step 6: -------------------------------------------------
	// Receive event notifications -----------------------------
	EventSink * pSink = new EventSink;
	IWbemObjectSinkPtr sink(pSink, true);
	signals2::scoped_connection sinkToTasksConnection = pSink->listeners.connect([&](IWbemClassObject * x) {
		if (!x)
			return;
		tasks.notify(*x);
//		wcout << *x << endl;
	});
	sink = appartment.wrap(sink);

	unique_ptr<Query> query1 = notificationQueryForProcesses(services,  sink, processNames);

	_getwch();
	} catch (std::exception & e) {
		cerr << e.what() << endl;
	}
	_CrtDumpMemoryLeaks();
	return 0;
}


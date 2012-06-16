#include "stdafx.h"

#define _CRTDBG_MAP_ALLOC 1
#define _CRTDBG_MAP_ALLOC_NEW 1
#include <stdlib.h>
#include <crtdbg.h>

#include <memory>
#include <fstream>
#include <boost/algorithm/string/join.hpp>

#include <conio.h>

#include "EventSink.h"
#include "ComError.h"
#include "Task.h"
#include "WmiTools.h"

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

static wstring buildQueryForProcesses(const vector<wstring> & names) {
	wostringstream temp;
	temp << L"SELECT * FROM __InstanceOperationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'";
	if (names.size() >0) {
		temp << L" AND (TargetInstance.Name='";
		temp << algorithm::join(names, L"' OR TargetInstance.Name='");
		temp <<  L"')";
	}
	return temp.str();
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

void asynchronousEventHandling(const IWbemServicesPtr & services, const wstring & query, std::function<void(IWbemClassObject * x)> callback) {
	UnsecuredAppartment appartment;
	// Step 6: -------------------------------------------------
	// Receive event notifications -----------------------------
	EventSink * pSink = new EventSink;
	IWbemObjectSinkPtr sink(pSink, true);
	signals2::scoped_connection sinkToTasksConnection = pSink->listeners.connect(callback);
	sink = appartment.wrap(sink);
	AsyncQueryHandle handle(services,  sink, query.c_str());
	_getwch();
}

void semisynchronousEventHandling(const IWbemServicesPtr & services, const wstring & query, std::function<void(IWbemClassObject * x)> callback) {
	IEnumWbemClassObjectPtr objects;
	HRESULT hres = services->ExecNotificationQuery(_bstr_t("WQL"), _bstr_t(query.c_str()), WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY, 0, &objects);
	ComError::handleWithErrorInfo(hres, "Synchronous WMI notification query failed.", services.GetInterfacePtr());
	while(true) {
		if (_kbhit())
			break;
		IWbemClassObjectPtr notification;
		ULONG count = 0;
		hres = objects->Next(100, 1, &notification, &count);
		if (hres == WBEM_S_TIMEDOUT)
			continue;
		ComError::handleWithErrorInfo(hres, "Failed to get", static_cast<IWbemServices*>(services));
		callback(notification.GetInterfacePtr());
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
			fout.reset();
		}

		{
			ostringstream encodingString;
			encodingString<< "." << GetConsoleOutputCP();
			locale consoleLocale(encodingString.str());
			consoleLocale = locale(consoleLocale, &use_facet<numpunct<wchar_t> >(locale("C"))); //Remove decimal separator.
			wcout.imbue(consoleLocale);
		}

		wstring query = buildQueryForProcesses(processNames);

		// Step 1: --------------------------------------------------
		// Initialize COM. ------------------------------------------
		ComInitializer comInitializer;
		IWbemServicesPtr services = connectToWmiServices();

		VirtualSizeChanges tasks;
		tasks.listeners.connect([&](Tasks::Event e, const Task& task){
			wcout.clear(); //Exotic encoding (of ProcessName) may corrupt stream state.
			wcout << task.name() << L" " << e << L" " << task << endl;
			if (fout.get()) {
				fout->clear();
				*fout << task.name() << L" " << e << L" " << task << endl;
			}
		});
		auto notify = [&](IWbemClassObject * x) {
			if (!x)
				return;
			tasks.notify(*x);
			//wcout << *x << endl;
		};
		
		if (false) {
			//Asynchrnonous unsecure event handling
			asynchronousEventHandling(services, query, notify);
		} else {
			//Synchronous event streaming
			semisynchronousEventHandling(services, query, notify);
		}
	} catch (std::exception & e) {
		cerr << e.what() << endl;
	}
	_CrtDumpMemoryLeaks();
	return 0;
}


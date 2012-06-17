#include "stdafx.h"

#define _CRTDBG_MAP_ALLOC 1
#define _CRTDBG_MAP_ALLOC_NEW 1
#include <stdlib.h>
#include <crtdbg.h>

#include <memory>
#include <iomanip>
#include <fstream>
#include <boost/algorithm/string/join.hpp>


#include <conio.h>

#include "ComError.h"
#include "Task.h"
#include "WmiTools.h"
#include "InterruptingThread.h"

using namespace std;
using namespace boost;

//Initializes and deinitializes COM for the current scope
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

//Generates notification query strings to watch for given processes
static vector<wstring> buildQueryForProcesses(const vector<wstring> & names) {
	const wstring commonPrefix = L"SELECT * FROM __InstanceOperationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'";
	vector<wstring> rv;
	if (names.size() == 0) {
		rv.push_back(commonPrefix);
		return rv;
	}
	rv.reserve(names.size());
	// Many simultaneous queries are not efficient, but we produce heaps of them to maximize number of processing threads for educational purpose.
	// To minimize number of queries we should just query for as much processes in one query as posssible (limited by maximum WMI query complexity).
	for each (const wstring & name in names) {
		wostringstream temp;
		temp << commonPrefix;
		temp << L" AND TargetInstance.Name='" << name << L"'";
		rv.push_back(temp.str());
	}
	return rv;
}

wostream & operator <<(wostream & ostr, IWbemClassObject & object) {
	BSTR text;
	ComError::handleWithErrorInfo(object.GetObjectText(0, &text), string("Failed to get object text"), &object);
	ostr << text;
	SysFreeString(text);
	return ostr;
}

class WorkingSetChanges: public Tasks {
	virtual bool shouldReport(Event e, const Task & oldState, const Task & newState) {
		if (e == CREATED || e == DELETED)
			return true;
		if (e == CHANGED) {
			if(oldState.workingSet() == 0)
				return false;
			if (std::abs(oldState.workingSet() - newState.workingSet()) > 1024*1024)
				return true;
		}
		return false;
	}
};

wostream & operator <<(wostream & ostr, const Task & task) {
	return ostr << setw(6) << task.processId() << L" " << int(double(task.workingSet())/1024/1024)<< L"M";
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

		//Process arguments
		if (argc > 1) {
			logFileName = argv[argc-1];
			processNames.insert(processNames.begin(), argv+1, argv+argc-1);
		}
		vector<wstring> queries = buildQueryForProcesses(processNames);

		//Configure output encodings
		try {
			fout.reset(new wofstream(logFileName));
			ostringstream encodingString;
			//File encoding is different from console one.
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
			//Windows console encoding is a very strange artifact and handled separately.
			encodingString<< "." << GetConsoleOutputCP();
			locale consoleLocale(encodingString.str());
			consoleLocale = locale(consoleLocale, &use_facet<numpunct<wchar_t> >(locale("C"))); //Remove decimal separator.
			wcout.imbue(consoleLocale);
		}


		// Step 1: --------------------------------------------------
		// Initialize COM. ------------------------------------------
		ComInitializer comInitializer;
		IWbemServicesPtr services = connectToWmiServices();

		//Create event filter and configure filtered events processing (dumping notifications to console and log file)
		WorkingSetChanges tasks;
		tasks.listeners.connect([&](Tasks::Event e, const Task& task){
			wcout.clear(); //Exotic encoding (of ProcessName) may corrupt stream state.
			wcout << left << setw(30) << task.name() << L" " << e << L" " << task << endl;
			if (fout.get()) {
				fout->clear();
				*fout << left << setw(30) << task.name() << L" " << e << L" " << task << endl;
			}
		});

		auto callback = [&](IWbemClassObject * x) {
			tasks.notify(x);
			if (x && false)
				wcout << *x << endl;
		};

		const bool async = false; //Change this to try different approaches for event handling

		if (async) {
			//Asynchrnonous unsecured event handling
			vector<unique_ptr<AsyncWmiQuery>> handles;
			for each (wstring query in queries) {
				handles.push_back(unique_ptr<AsyncWmiQuery>(new AsyncWmiQuery(services, query.c_str(), callback)));
			}
			_getwch();
		} else {
			// Semisynchronous event streaming
			// Multiple threads are created to illustrate how synchronization works
			vector<InterruptingThread> threads;
			for each (const wstring & query in queries) {
				threads.push_back(InterruptingThread([=](){
					SemisyncWmiQuery swq(services, query.c_str());
					while(true) {
						callback(swq.next().GetInterfacePtr());
						this_thread::interruption_point();
					}
				}));
			}
			_getwch();
		}
	} catch (std::exception & e) {
		cerr << e.what() << endl;
	}
	_CrtDumpMemoryLeaks();
	return 0;
}


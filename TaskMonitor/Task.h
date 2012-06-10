#pragma once
#include <functional>
#include <map>
#include <comdef.h>
#include <comip.h>
#include <Wbemidl.h>

_COM_SMARTPTR_TYPEDEF(IWbemClassObject,     __uuidof(IWbemClassObject));

//Stores process state to determine is notifcation is interesting
class Task
{
	IWbemClassObjectPtr _reportedNotification;
	bool shouldReport(IWbemClassObject & notification);
public:
	typedef long Pid;
	
	bool notify(IWbemClassObject & object);

	static Pid extractPid(IWbemClassObject & notification);
};

///Determines if notifcation is interesting
class Tasks {
	std::map<Task::Pid, Task> _tasks;
public:
	bool notify(IWbemClassObject & object);
};

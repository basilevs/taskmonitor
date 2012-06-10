#include "StdAfx.h"

#include "Task.h"
#include "ComError.h"

typedef Task::Pid  Pid;

using namespace std;

bool Task::notify(IWbemClassObject & notification) {
	bool rv = shouldReport(notification);
	if (rv)
		_reportedNotification.Attach(&notification, true);
	return rv;
}

bool Tasks::notify(IWbemClassObject & notification) {
	Pid pid = Task::extractPid(notification);
	auto i = _tasks.find(pid);
	if (i == _tasks.end())
		i = _tasks.insert(make_pair(pid, Task())).first;
	bool rv = i->second.notify(notification);
	if (notification.InheritsFrom(_bstr_t("__InstanceDeletionEvent")) == WBEM_S_NO_ERROR)
		_tasks.erase(i);
	return rv;
}

template<class T>
T parseVariant(VARIANT & v);

template<>
unsigned long parseVariant<unsigned long>(VARIANT & v) {
	VARIANT n;
	VariantInit(&n);
	HRESULT hres = VariantChangeType(&n, &v, 0, VT_UI4);
	ComError::handle(hres, "Failed to convert data to unsigned long");
	unsigned long rv = V_UI4(&n);
	VariantClear(&n);
	return rv;
}

template<>
long parseVariant<long>(VARIANT & v) {
	VARIANT n;
	VariantInit(&n);
	HRESULT hres = VariantChangeType(&n, &v, 0, VT_I4);
	ComError::handle(hres, "Failed to convert data to long");
	long rv = V_I4(&n);
	VariantClear(&n);
	return rv;
}



template<>
IWbemClassObjectPtr parseVariant<IWbemClassObjectPtr>(VARIANT & v) {
	VARIANT n;
	VariantInit(&n);
	HRESULT hres = VariantChangeType(&n, &v, 0, VT_UNKNOWN);
	ComError::handle(hres, "Failed to convert data to object");
//	if (V_VT(&v) != VT_UNKNOWN)
//		throw runtime_error("Not an object");
	IWbemClassObjectPtr rv = V_UNKNOWN(&v, true); //Note: reference is created here, as VariantClear() will clear variant reference.
	VariantClear(&n);
	return rv;
}



template<class T>
T getProperty(IWbemClassObject & object, const _bstr_t & name) {
	VARIANT v;
	HRESULT hres = object.Get(name, 0,  &v, 0, 0);
	ComError::handleWithErrorInfo(hres, string("Failed to extract property") + static_cast<char*>(name), &object);
	try {
		T rv = parseVariant<T>(v);
		VariantClear(&v);
		return rv;
	} catch(...) {
		VariantClear(&v);
		throw;
	}
}

Pid Task::extractPid(IWbemClassObject & notification) {
	IWbemClassObjectPtr target = getProperty<IWbemClassObjectPtr>(notification, "TargetInstance");
	return getProperty<long>(target, "ProcessId");
}

bool Task::shouldReport(IWbemClassObject & notification) {
	if (_reportedNotification == 0)
		return true;
	if (notification.InheritsFrom(_bstr_t("__InstanceCreationEvent")) == WBEM_S_NO_ERROR)
		return true;
	if (notification.InheritsFrom(_bstr_t("__InstanceDeletionEvent")) == WBEM_S_NO_ERROR)
		return true;
	if (notification.InheritsFrom(_bstr_t("__InstanceModificationEvent")) == WBEM_S_NO_ERROR) {
		IWbemClassObjectPtr target = getProperty<IWbemClassObjectPtr>(notification, "TargetInstance");
		long newWS = getProperty<long>(target, "WorkingSetSize");
		target = getProperty<IWbemClassObjectPtr>(_reportedNotification, "TargetInstance");
		long oldWS = getProperty<long>(target, "WorkingSetSize");
		if (std::abs(newWS - oldWS) > 1024*1024) {
			return true;
		}
		return false;
	}
	return true;
}


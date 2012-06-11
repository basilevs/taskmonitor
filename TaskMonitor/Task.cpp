#include "StdAfx.h"

#include "Task.h"
#include "ComError.h"

typedef Task::Pid  Pid;

using namespace std;
using namespace boost;

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
wstring parseVariant<wstring>(VARIANT & v) {
	VARIANT n;
	VariantInit(&n);
	HRESULT hres = VariantChangeType(&n, &v, 0, VT_BSTR);
	ComError::handle(hres, "Failed to convert data to string");
	_bstr_t ole(V_BSTR(&n));
	VariantClear(&n);
	return static_cast<wchar_t*>(ole);
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
	IWbemClassObjectPtr rv = V_UNKNOWN(&v);
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

Task::Task(){}

Task::Task(IWbemClassObject & notification):
	_reportedNotification(&notification, true)
{}

Pid Task:: processId() const {
	if (_reportedNotification == 0)
		return 0;
	return extractPid(_reportedNotification);
}
IWbemClassObjectPtr Task::targetInstance() const {
	return getProperty<IWbemClassObjectPtr>(_reportedNotification, "TargetInstance");
}

wstring Task::name() const {
	if (_reportedNotification == 0)
		return 0;
	return getProperty<wstring>(targetInstance(), "Name");
}
long Task::workingSet() const {
	if (_reportedNotification == 0)
		return 0;
	return getProperty<long>(targetInstance(), "WorkingSetSize");
}

long Task::virtualSize() const {
	if (_reportedNotification == 0)
		return 0;
	IWbemClassObjectPtr target = getProperty<IWbemClassObjectPtr>(_reportedNotification, "TargetInstance");
	return getProperty<long>(target, "VirtualSize");
}

Tasks::Tasks()
{
	_thread.reset(new thread(std::bind(std::mem_fn(&Tasks::run), this)));
}

Tasks::~Tasks() {
	_thread->interrupt();
	_thread->join();
}

void Tasks::notify(IWbemClassObject & notification) {
	boost::mutex::scoped_lock lock(_mutex);
	_queue.push(IWbemClassObjectPtr(&notification, true));
	_condition.notify_all();
}

void Tasks::run() {
	try {
		while(true) {
			IWbemClassObjectPtr ptr;
			{
				boost::mutex::scoped_lock lock(_mutex);
				//interruption point
				_condition.wait(_mutex, std::bind(mem_fun(&queue<IWbemClassObjectPtr>::size), &_queue));
				ptr = _queue.front();
				_queue.pop();
			}
			if (ptr == 0)
				continue;
			try {
				process(ptr);
			} catch(std::exception & e) {
				cerr << e.what() << endl;
			}
		}
	} catch (thread_interrupted&) {}
}

class BadEvent: public runtime_error {
public:
	BadEvent(): runtime_error("Bad event") {}
};

static Tasks::Event notificationType(IWbemClassObject & notification) {
	if (notification.InheritsFrom(_bstr_t("__InstanceCreationEvent")) == WBEM_S_NO_ERROR)
		return Tasks::CREATED;
	if (notification.InheritsFrom(_bstr_t("__InstanceDeletionEvent")) == WBEM_S_NO_ERROR)
		return Tasks::DELETED;
	if (notification.InheritsFrom(_bstr_t("__InstanceModificationEvent")) == WBEM_S_NO_ERROR) 
		return Tasks::CHANGED;
	throw BadEvent();
}

void Tasks::process(IWbemClassObject & notification) {
	try {
		Task newTask(notification);
		Pid pid = newTask.processId();
		auto i = _tasks.find(pid);
		if (i == _tasks.end())
			i = _tasks.insert(make_pair(pid, Task())).first;
		Event type = notificationType(notification);
		if (shouldReport(type, i->second, newTask)) {
			listeners(type, newTask);
			i->second = newTask;
		}
		if (type == DELETED)
			_tasks.erase(i);
	} catch (BadEvent) {}
}


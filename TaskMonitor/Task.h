#pragma once
#include <functional>
#include <map>
#include <queue>
#include <comdef.h>
#include <comip.h>
#include <Wbemidl.h>
#include <boost/signals2/signal.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

_COM_SMARTPTR_TYPEDEF(IWbemClassObject,     __uuidof(IWbemClassObject));

//Stores process state to determine if notifcation is interesting
class Task
{
	IWbemClassObjectPtr _reportedNotification;
	IWbemClassObjectPtr targetInstance() const;
public:
	typedef long Pid;
	Task();
	Task(IWbemClassObject & object);
	std::wstring name() const;
	long virtualSize() const;
	long workingSet() const;
	Pid processId() const;
	static Pid extractPid(IWbemClassObject & notification);
};

// Determines if notification is interesting
class Tasks: public boost::noncopyable {
	std::map<Task::Pid, Task> _tasks;
	std::queue<IWbemClassObjectPtr> _queue;
	std::unique_ptr<boost::thread> _thread;
	boost::mutex _mutex;
	boost::condition _condition;
	void process(IWbemClassObject &);
	void run();
public:
	Tasks();
	~Tasks();
	typedef std::function<void(const Task &)> Listener;
	void notify(IWbemClassObject & object);
	enum Event {CREATED, DELETED, CHANGED};
	virtual bool shouldReport(Event, const Task & oldState, const Task & newState) {
		return true;
	}
	boost::signals2::signal<void(Event, const Task &)> listeners;
};

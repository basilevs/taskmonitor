#pragma once
#include <functional>
#include <map>
#include <queue>
#include <comdef.h>
#include <comip.h>
#include <boost/signals2/signal.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include "InterruptingThread.h"


#include "WmiTools.h"

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
// Wraps notifications with convenience wrapper
// Sends interesting notification to listeners
// Notifications are filtered by virtual method shouldReport, which is given access to previous state of changing task.
class Tasks: public boost::noncopyable {
	std::map<Task::Pid, Task> _tasks;
	std::queue<IWbemClassObjectPtr> _queue;
	std::unique_ptr<InterruptingThread> _thread;
	boost::mutex _mutex;
	boost::condition _condition;
	void process(IWbemClassObject &);
	void run();
public:
	Tasks();
	virtual ~Tasks();
	typedef std::function<void(const Task &)> Listener;
	//Can be called from any thread.
	void notify(IWbemClassObject * object);
	enum Event {CREATED, DELETED, CHANGED};
	//Is always called from a single (per instance) private thread.
	virtual bool shouldReport(Event, const Task & oldState, const Task & newState) {
		return true;
	}
	boost::signals2::signal<void(Event, const Task &)> listeners;
};

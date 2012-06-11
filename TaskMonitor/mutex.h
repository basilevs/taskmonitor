#pragma once
#include <Winnt.h>

template<class T>
class scoped_lock {
	T & _mutex;
public:
	scoped_lock(T & m): _mutex(m) {_mutex.lock();}
	~scoped_lock() {_mutex.unlock();}
};

	//To be replaced with boost
class mutex
{
	HANDLE _handle;
public:
	mutex(void);
	void lock();
	void unlock();
	~mutex(void);
	typedef scoped_lock<mutex> scoped_lock;
};


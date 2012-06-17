#pragma once
#include <boost/thread/thread.hpp>

//Thread that terminates when goes out of scope
//Note, that there should be boost interruption points in callback.
//Movable semantics allows storage in containers.
//boost::thread can't be publicly inherited due to absence of virtual destructor.
class InterruptingThread : private boost::thread
{
public:
	InterruptingThread(std::function<void()> callback): thread(callback) {}
	InterruptingThread(InterruptingThread &&  that): thread(static_cast<thread&&>(that)) {
		that.detach();
	}
	virtual ~InterruptingThread() {
		interrupt();
		join();
	}
};


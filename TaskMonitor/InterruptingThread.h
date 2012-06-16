#pragma once
#include <boost/thread/thread.hpp>
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


#include "StdAfx.h"
#include "mutex.h"
#include "WinError.h"


mutex::mutex(void)
{
	_handle = CreateMutex(0, FALSE, 0);
	if (!_handle) {
		WinError::throwLastError();
	}
}


mutex::~mutex(void)
{
	CloseHandle(_handle);
}

void mutex::lock() {
	if (WaitForSingleObject(_handle, INFINITE) != WAIT_OBJECT_0) {
		WinError::throwLastError(); //Not sure that error is set for non-error return values.
	}
}
void mutex::unlock() {
	if (!ReleaseMutex(_handle))
		WinError::throwLastError();
}

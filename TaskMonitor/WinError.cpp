#include "StdAfx.h"
#include "WinError.h"
#include <windows.h>
#include <strsafe.h>
#include <stdexcept>

using namespace std;

WinError::WinError(void)
{
}


WinError::~WinError(void)
{
}

void WinError::throwLastError() {
		LPCSTR lpMsgBuf = 0;
	    if (FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR) &lpMsgBuf,
        0, NULL ) == 0) {
			throw runtime_error("FormatMessageA failed.");
		}
		runtime_error err(lpMsgBuf);
		LocalFree((LPVOID)lpMsgBuf);
		throw err;
}
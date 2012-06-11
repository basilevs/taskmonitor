#pragma once
class WinError
{
public:
	WinError(void);
	~WinError(void);
	//Always throws
	static void throwLastError();
};


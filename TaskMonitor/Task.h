#pragma once
class Task
{
	unsigned _pid;
public:
	Task(unsigned pid);
	virtual ~Task(void);
	unsigned pid() const {return _pid;}
};


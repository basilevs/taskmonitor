#include "StdAfx.h"
#include "ComError.h"

using namespace std;

ComError::ComError(const std::string & message, const _com_error & data):
	runtime_error(message),
	_com_error(data)
{
}


ComError::~ComError(void)
{
}


static string toConsoleEncoding(const wstring & input) {
	enum {BUFFER_SIZE = 1000};
	char buffer[BUFFER_SIZE];
	int count = WideCharToMultiByte(GetConsoleOutputCP(), 0, input.c_str(), input.size(), buffer, BUFFER_SIZE-1, 0, 0);
	assert(count >= 0);
	assert(count < BUFFER_SIZE);
	buffer[count] = 0;
	return buffer;
}

void ComError::handle(const _com_error & data, const std::string & message)
{
	if (!FAILED(data.Error()))
		return;
	ostringstream temp;
	temp << message <<": ";
	_bstr_t description = data.Description();
	if (description.length()>0) {
		temp << description;
	} else {
		temp << toConsoleEncoding(data.ErrorMessage());
	}
	throw ComError(temp.str(), data);
}

void ComError::handle(HRESULT result, const std::string & message)
{
	if (!FAILED(result))
		return;
	_com_error data(result);
	handle(data, message);
}

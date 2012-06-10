#pragma once
#include <stdexcept>
#include <string>

#include <comdef.h>

//Converts COM error codes to exceptions
class ComError:
	public std::runtime_error, public _com_error
{
public:
	ComError(const std::string & message, const _com_error & data);
	virtual ~ComError(void);
	static void handle(const _com_error & data, const std::string & message);
	static void handle(HRESULT result, const std::string & message);
	template<class T>
	static void handleWithErrorInfo(HRESULT result, const std::string & message, T * object = 0) {
		if (!object)
			throw invalid_argument("Bad object argument");
		if (!FAILED(result))
			return;
		ISupportErrorInfoPtr supportErrorInfo = object;
		if (!supportErrorInfo || supportErrorInfo->InterfaceSupportsErrorInfo(__uuidof(T)) == S_FALSE) {
			handle(result, message);
		} else {
			IErrorInfoPtr error;
			HRESULT hres = GetErrorInfo(0, &error); //Why this compiles?
			handle(hres, "Error handling failed");
			_com_error data(result, error);
			handle(data, message);
		}
	}

};

//Returns a smart pointer to requested interface
template<class T>
T query(IUnknown & source) {
	void * out = 0;
	HRESULT hres = source.QueryInterface(T::GetIID(), &out);
	ComError::handle(hres, "Can't query for interface");
	return T(static_cast<typename T::Interface*>(out));
}

std::string toConsoleEncoding(const std::wstring & input);

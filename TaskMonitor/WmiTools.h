#pragma once

#include <comip.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <boost/signals2/signal.hpp>
#include <boost/noncopyable.hpp>

_COM_SMARTPTR_TYPEDEF(IWbemLocator,        __uuidof(IWbemLocator));
_COM_SMARTPTR_TYPEDEF(IWbemServices,       __uuidof(IWbemServices));
_COM_SMARTPTR_TYPEDEF(IUnsecuredApartment, __uuidof(IUnsecuredApartment));
_COM_SMARTPTR_TYPEDEF(IWbemObjectSink,     __uuidof(IWbemObjectSink));
_COM_SMARTPTR_TYPEDEF(IWbemClassObject,     __uuidof(IWbemClassObject));
_COM_SMARTPTR_TYPEDEF(IEnumWbemClassObject,     __uuidof(IWbemClassObject));


IWbemServicesPtr connectToWmiServices();

//Controls query interruption for exception safety
//We absolutely can't continue sending events to event handler that might reference local context, that is already out of scope.
class AsyncQueryHandle: public boost::noncopyable {
	IWbemServicesPtr _services;
	IWbemObjectSinkPtr _sink;
public:
	AsyncQueryHandle(const IWbemServicesPtr & services, const IWbemObjectSinkPtr & sink, const _bstr_t & query);
	void cancel();
	~AsyncQueryHandle();
};


//Creates and manages EventSink and a connection to it
class AsyncWmiQuery {
	std::unique_ptr<AsyncQueryHandle> _handle;
	boost::signals2::scoped_connection _connection;
public:
	AsyncWmiQuery(const IWbemServicesPtr & services, const _bstr_t & query, std::function<void(IWbemClassObject * x)> callback);
};

class SemisyncWmiQuery {
	IEnumWbemClassObjectPtr _enumerator;
public:
	SemisyncWmiQuery(const IWbemServicesPtr & services, const _bstr_t & query);
	IWbemClassObjectPtr next();
};
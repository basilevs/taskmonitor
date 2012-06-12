#pragma once

#include <comip.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <boost/noncopyable.hpp>

_COM_SMARTPTR_TYPEDEF(IWbemLocator,        __uuidof(IWbemLocator));
_COM_SMARTPTR_TYPEDEF(IWbemServices,       __uuidof(IWbemServices));
_COM_SMARTPTR_TYPEDEF(IUnsecuredApartment, __uuidof(IUnsecuredApartment));
_COM_SMARTPTR_TYPEDEF(IWbemObjectSink,     __uuidof(IWbemObjectSink));

IWbemServicesPtr connectToWmiServices();

//Controls query interruption for exception safety
//We absolutely can't continue sending events to event handler that might reference local context, that is already out of scope.
class Query: public boost::noncopyable {
	IWbemServicesPtr _services;
	IWbemObjectSinkPtr _sink;
public:
	Query(IWbemServices & services, IWbemObjectSink & sink, const _bstr_t & query);
	void cancel();
	~Query();
};
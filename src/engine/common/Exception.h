#pragma once


#include "common/Export.h"

#include <exception>
#include <string>

namespace eve
{
    

class EVENGINE_API Exception : public std::exception
{
public:
    Exception(const char *fmt, ...);
	virtual ~Exception() throw();

    /**
	 * @brief Returns a string containing reason for the exception.
	 * @return A description of the exception.
	 **/
	inline virtual const char *what() const throw()
	{
		return message.c_str();
	}

private:
        std::string message;
};


}

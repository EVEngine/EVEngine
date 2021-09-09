#pragma once


#include <exception>
#include <string>

namespace eve
{
    

class Exception : public std::exception
{
public:
    Exception(const char *fmt, ...);
	virtual ~Exception() throw();

    /**
	 * Returns a string containing reason for the exception.
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
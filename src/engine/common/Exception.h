#pragma once


#include "common/Export.h"

#include <exception>
#include <string>

namespace eve {


/** @brief EVENGINE_API_FOUNDATION public API. */
class EVENGINE_API_FOUNDATION Exception : public std::exception {
public:
    /** @brief Exception. */
    Exception(const char *fmt, ...);
	/** @brief Exception. */
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
}  // namespace eve

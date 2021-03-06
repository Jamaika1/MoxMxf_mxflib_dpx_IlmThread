/*! \file	debug.h
 *	\brief	Debug and error handling declarations
 *
 *			The implementation of these functions is an application
 *			issue so they are not contained in the MXFLib library
 *
 *	\version $Id$
 *
 */
/* 
 *  This software is provided 'as-is', without any express or implied warranty.
 *  In no event will the authors be held liable for any damages arising from
 *  the use of this software.
 *  
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *  
 *   1. The origin of this software must not be misrepresented; you must
 *      not claim that you wrote the original software. If you use this
 *      software in a product, you must include an acknowledgment of the
 *      authorship in the product documentation.
 *  
 *   2. Altered source versions must be plainly marked as such, and must
 *      not be misrepresented as being the original software.
 *  
 *   3. This notice may not be removed or altered from any source
 *      distribution.
 */

#ifndef MXFLIB__DEBUG_H
#define MXFLIB__DEBUG_H

// Define this value here, or on the compiler command line to enable debug() function
#define MXFLIB_DEBUG

namespace mxflib
{
#ifdef MXFLIB_DEBUG
	void debug(const char *Fmt, ...);						//!< Display a general debug message
#else
	inline void debug(const char *Fmt, ...) { return; };	//!< Make debug messages optimise out
#endif

	void warning(const char *Fmt, ...);						//!< Display a warning message
	void error(const char *Fmt, ...);						//!< Display an error message
}

#endif // MXFLIB__DEBUG_H

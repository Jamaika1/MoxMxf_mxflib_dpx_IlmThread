/*! \file	mxflib.h
 *	\brief	The main MXFLib header file
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

#ifndef MXFLIB__MXFLIB_H
#define MXFLIB__MXFLIB_H

#include "mxflib_assert.h"
//! Namespace for all MXFLib items
namespace mxflib {}

#ifdef _WIN32
#pragma warning( disable:4996)   //for VC2005 to remove annoyig security warnings
#endif

//! Include system specifics first to allow any required compiler set-up
#include "system.h"

// Required std::headers

#include <list>
#include <map>
#include <cstring>


namespace mxflib
{
#define MXFLIB_VERSION_MAJOR "1"
#define MXFLIB_VERSION_MINOR "2"
#define MXFLIB_VERSION_TWEAK "1"

// date of last automated build (montonic days since 2000, see below)
// (tm.tm_year-100)*1000 + (tm.tm_mon+1)*50 + tm.tm_mday
// e.g. 2011-12-25 == 11625
#define MXFLIB_VERSION_BUILD "12224"

// automated build normally 2 for Development. override with 4 for Beta and 1 for Release
#ifndef MXFLIB_VERSION_REL
#ifdef _DEBUG
#define MXFLIB_VERSION_REL   2
#else
#define MXFLIB_VERSION_REL   1
#endif
#endif

#define MXFLIB_VERSION_RELTEXT(REL) (REL==1?"-Release":(REL==2?"-Development":(REL==3?"-Patched":(REL==4?"-Beta":(REL==5?"-Private":"")))))
#define MXFLIB_VERSION_RELNUMBER(REL) (REL==1?"1":(REL==2?"2":(REL==3?"3":(REL==4?"4":(REL==5?"5":"0")))))

	//! Get a human readable version of the library name
	inline std::string LibraryName(void) { return std::string("MXFLib"); }

	//! Get a human readable version of the Platform name
	inline std::string PlatformName(void) { return LibraryName() + "(" + OSName() + ")"; }

	//! Get a human readable version of the library version
	inline std::string LibraryVersion(void)
	{
		return LibraryName()
			   + std::string( " " MXFLIB_VERSION_MAJOR "." MXFLIB_VERSION_MINOR "." MXFLIB_VERSION_TWEAK "(" MXFLIB_VERSION_BUILD ")")
			   + std::string( MXFLIB_VERSION_RELTEXT(MXFLIB_VERSION_REL) );
	}

	//! Get a version of the library version suitable for setting ToolkitVersion
	inline std::string LibraryProductVersion(void)
	{
		return std::string( "\"" MXFLIB_VERSION_MAJOR "\",\"" MXFLIB_VERSION_MINOR "\",\"" MXFLIB_VERSION_TWEAK "\",\"" MXFLIB_VERSION_BUILD "\",\"")
			   + std::string( MXFLIB_VERSION_RELNUMBER(MXFLIB_VERSION_REL)) + std::string("\"");
	}
}

//! Touch an unused parameter in such a way that no compiler warnings are produced, but optimizer will remove anything we do
#define UNUSED_PARAMETER(x) ( x=x )

#include "debug.h"

#include "features.h"

#include "smartptr.h"

#include "endian.h"

#include "forward.h"
#include "types.h"

#include "datachunk.h"


#include "helper.h"

#include "ulmap.h"

#include "mdtraits.h"
#include "deftypes.h"
#include "rxiparser.h"
#include "legacytypes.h"

#include "primer.h"

#include "typeif.h"
#include "mdtype.h"
#include "mdobject.h"

#include "rip.h"

#include "mxffile.h"

#include "index.h"


#include "essence.h"

#include "klvobject.h"

#include "crypto.h"

#include "metadata.h"

#include "vbi.h"

#include "audiomux.h"

#include "sopsax.h"
#include "xmlparser.h"

#include "metadict.h"

#endif // MXFLIB__MXFLIB_H


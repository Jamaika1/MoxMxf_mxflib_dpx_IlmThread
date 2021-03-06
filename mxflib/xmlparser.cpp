/*! \file	xmlparser.cpp
 *	\brief	Interface to available SAX style XML parser
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

#ifdef HAVE_EXPAT

#include "mxflib.h"

#include <expat.h>



//! Use Expat parser to parse an XML file
bool mxflib::XMLParserParseFile(XML_Parser *pParser, mxflib::XMLParserHandlerPtr Hand, void *UserData, const char *filename, bool ParseNamespaces /*=false*/)
{
	if(!Hand)
	{
		error("No handler defined in call to XMLParserParseFile()\n");
		return false;
	}

	// Open the input file
	FileHandle InFile = FileOpenRead(filename);
	if(!FileValid(InFile))
	{
		Hand->fatalError(UserData, "Couldn't open file %s\n", filename);
		return false;
	}

	// Build a new parser
	XML_Parser Parser = ParseNamespaces ? XML_ParserCreateNS(NULL, '|') : XML_ParserCreate(NULL);
	if(!Parser)
	{
		Hand->fatalError(UserData, "Could't create an expat XML parser\n");
		FileClose(InFile);
		return false;
	}

	// Set the caller's parser pointer if requested
	if(pParser) *pParser = Parser;

	// Set the element handlers
	XML_SetElementHandler(Parser, Hand->startElement, Hand->endElement);

	// Set the user data
	XML_SetUserData(Parser, UserData);

	int Done = 0;
	do
	{
		const int BufferSize = 1024 * 64;
		UInt8 *Buffer = (UInt8*)XML_GetBuffer(Parser, BufferSize);

		int Bytes = (int)FileRead(InFile, Buffer, BufferSize);

		if(FileEof(InFile)) Done = -1;

		if (XML_ParseBuffer(Parser, Bytes, Done) == XML_STATUS_ERROR)
		{
			Hand->fatalError(UserData, "Parse error at line %d:\n%s\n",  XML_GetCurrentLineNumber(Parser),
																		 XML_ErrorString(XML_GetErrorCode(Parser)));

			XML_ParserFree(Parser);
			FileClose(InFile);
			return false;
		}
	} while(!Done);

	// Free the parser
	XML_ParserFree(Parser);

	FileClose(InFile);

	return true;
}



//! Use Expat parser to parse an XML file
bool mxflib::XMLParserParseString(XML_Parser *pParser, mxflib::XMLParserHandlerPtr Hand, void *UserData, std::string & strXML, bool ParseNamespaces /*=false*/)
{
	if(!Hand)
	{
		error("No handler defined in call to XMLParserParseFile()\n");
		return false;
	}


	// Build a new parser
	XML_Parser Parser = XML_ParserCreate(NULL);
	if(!Parser)
	{
		Hand->fatalError(UserData, "Could't create an expat XML parser\n");
		return false;
	}

	// Set the caller's parser pointer if requested
	if(pParser) *pParser = Parser;

	// Set the element handlers
	XML_SetElementHandler(Parser, Hand->startElement, Hand->endElement);

	// Set the user data
	XML_SetUserData(Parser, UserData);

	int Done = 0;

		const size_t BufferSize = strXML.size()+1;
		char *Buffer = (char *)XML_GetBuffer(Parser, static_cast<int>(BufferSize));
		strcpy(Buffer, strXML.c_str());

		if (XML_ParseBuffer(Parser, static_cast<int>(BufferSize-1), Done) == XML_STATUS_ERROR)
		{
			Hand->fatalError(UserData, "Parse error at line %d:\n%s\n",  XML_GetCurrentLineNumber(Parser),
				XML_ErrorString(XML_GetErrorCode(Parser)));

			XML_ParserFree(Parser);
			return false;
		}

	// Free the parser
	XML_ParserFree(Parser);

	return true;
}
#endif // HAVE_EXPAT



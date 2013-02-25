/*
 *  FakeLogging.h
 *  CalaosHome
 *
 *  Created by Raoul on 21/10/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef __FAKELOGGING
#define __FAKELOGGING

#include <iostream>

using namespace std;

namespace log4cpp
{
	typedef std::ostream Category;
	
	namespace Priority
	{
		const string EMERG = "EMERG: ";
		const string FATAL = "FATAL: ";
		const string ALERT = "ALERT: ";
		const string CRIT = "CRIT: ";
		const string ERROR = "ERROR: ";
		const string WARN = "WARN: ";
		const string NOTICE = "NOTICE: ";
		const string INFO = "INFO: ";
		const string DEBUG = "DEBUG: ";
		const string NOTSET = "NOTSET: ";
	};
	
	const string eol = "\n";
}

#endif

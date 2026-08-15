/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#ifndef CALAOS_LOGSETUP_H
#define CALAOS_LOGSETUP_H

#include <Logger.h>

//Log macros
#define cDebug() LoggerDebug(Utils::calaosLogger())
#define cInfo() LoggerInfo(Utils::calaosLogger())
#define cWarning() LoggerWarning(Utils::calaosLogger())
#define cError() LoggerError(Utils::calaosLogger())
#define cCritical() LoggerCritical(Utils::calaosLogger())

#define cDebugDom(domain) LoggerDebug(Utils::calaosLogger(domain))
#define cInfoDom(domain) LoggerInfo(Utils::calaosLogger(domain))
#define cWarningDom(domain) LoggerWarning(Utils::calaosLogger(domain))
#define cErrorDom(domain) LoggerError(Utils::calaosLogger(domain))
#define cCriticalDom(domain) LoggerCritical(Utils::calaosLogger(domain))

//-----------------------------------------------------------------------------
namespace Utils
{
void initLogger(const char *default_domain);
void freeLoggers();
Logger *calaosLogger(const char *domain = nullptr);
}

#endif

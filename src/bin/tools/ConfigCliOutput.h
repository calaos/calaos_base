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
#ifndef S_CONFIGCLIOUTPUT_H
#define S_CONFIGCLIOUTPUT_H

#include <iosfwd>
#include <string>

#include "ConfigOptions.h"

/* Presentation helpers of calaos_config.
 * ======================================
 *
 * Everything that renders the option registry for a human lives here, so that
 * calaos_config.cpp keeps to argument parsing and actions. Nothing in this file
 * is used by the machine readable outputs (get, list, options --json and
 * options --markdown): those must stay byte stable and are printed as is.
 */

namespace Calaos
{

namespace ConfigCli
{

enum class ColorMode { Auto, Always, Never };

/* Decides once and for all whether ANSI sequences may be emitted. Auto means
 * "only when stdout is a terminal and NO_COLOR is not set".
 */
void initColor(ColorMode mode);
bool colorEnabled();

//Usable width of the output, in columns: the terminal one when known, 80
//otherwise, clamped to something a human can read.
int outputWidth();

//Full documentation of one documented option. rawValue is the value currently
//stored in local_config.xml, isSet tells the empty value from the absent key.
void printDescribe(std::ostream &out, const ConfigOption &opt,
                   bool isSet, const std::string &rawValue);

//A key present in local_config.xml but absent from the registry
void printDescribeUnknown(std::ostream &out, const std::string &key,
                          const std::string &rawValue);

//One line naming the format an option accepts: type, range or value list, and
//example. Used by "set" when it refuses a value.
std::string typeHint(const ConfigOption &opt);

//The whole registry, grouped by category. consumerMask filters on
//ConfigOption::Consumer, 0 keeps everything.
void printOptions(std::ostream &out, int consumerMask);

}

}

#endif /* S_CONFIGCLIOUTPUT_H */

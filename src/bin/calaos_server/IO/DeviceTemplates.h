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
#ifndef S_DEVICE_TEMPLATES_H
#define S_DEVICE_TEMPLATES_H

#include "Calaos.h"

/* The templates the installer wizard builds devices from (spec §10). The
 * format is described in docs/17_device_templates.md; the installer applies
 * it without knowing any device family. */
namespace DeviceTemplates
{

constexpr const char *FILE_NAME = "device_templates.json";
constexpr int FORMAT = 1;

//Texts are translated in the current locale.
Json all();

//Writes FILE_NAME into `path`, created when missing. False when it could not.
bool generate(const string &path);

} //namespace DeviceTemplates

#endif

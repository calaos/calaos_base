/*** LICENCE ***************************************************************************************/
/*
  xPPLib - Simple class to manage xPL & xAP protocol

  This file is part of xPPLib.

    xPPLib is free software : you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    xPPLib is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with xPPLib.  If not, see <http://www.gnu.org/licenses/>.
*/
/***************************************************************************************************/

#ifndef SCHEMAADVCONFIG_H
#define SCHEMAADVCONFIG_H

#include "SchemaObject.h"

namespace xPL
{

/// \brief    Class to manage schema configdevice.list xPL message
class SchemaAdvanceConfigList : public SchemaObject
{
    public:
        /// \brief    Constructor of SchemaAdvanceConfigList
        SchemaAdvanceConfigList();
        /// \brief    Destructor of SchemaAdvanceConfigList
        ~SchemaAdvanceConfigList();

        SchemaAdvanceConfigList(SchemaAdvanceConfigList const& other);
        SchemaAdvanceConfigList& operator=(SchemaAdvanceConfigList const& other);
        SchemaAdvanceConfigList(SchemaAdvanceConfigList &&other);
        SchemaAdvanceConfigList& operator=(SchemaAdvanceConfigList&& other) noexcept;
};

/// \brief    Class to manage schema config.list xPL message
class SchemaAdvanceConfigListRequest : public SchemaObject
{
    public:
        /// \brief    Constructor of SchemaAdvanceConfigListRequest
        SchemaAdvanceConfigListRequest();
        /// \brief    Destructor of SchemaAdvanceConfigListRequest
        ~SchemaAdvanceConfigListRequest();

        void Check();
};

/// \brief    Class to manage schema configdevice.current xPL message
class SchemaAdvanceConfigCurrent : public SchemaObject
{
    public:
        /// \brief    Constructor of SchemaAdvanceConfigCurrent
        SchemaAdvanceConfigCurrent();
        /// \brief    Destructor of SchemaAdvanceConfigCurrent
        ~SchemaAdvanceConfigCurrent();

        SchemaAdvanceConfigCurrent(SchemaAdvanceConfigCurrent const& other);
        SchemaAdvanceConfigCurrent& operator=(SchemaAdvanceConfigCurrent const& other);
        SchemaAdvanceConfigCurrent(SchemaAdvanceConfigCurrent &&other);
        SchemaAdvanceConfigCurrent& operator=(SchemaAdvanceConfigCurrent&& other) noexcept;
};

/// \brief    Class to manage schema config.current xPL message
class SchemaAdvanceConfigCurrentCmnd : public SchemaObject
{
    public:
        /// \brief    Constructor of SchemaAdvanceConfigCurrentCmnd
        SchemaAdvanceConfigCurrentCmnd();
        /// \brief    Destructor of SchemaAdvanceConfigCurrentCmnd
        ~SchemaAdvanceConfigCurrentCmnd();

        void Check();
};

}
#endif // SCHEMAADVCONFIG_H

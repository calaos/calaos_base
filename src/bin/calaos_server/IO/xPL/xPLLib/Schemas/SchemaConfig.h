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

#ifndef SCHEMACONFIG_H
#define SCHEMACONFIG_H

#include <string>
#include <vector>
#include "SchemaHbeat.h"

namespace xPL
{

/// \brief    Class to manage schema config.end xPL message
class SchemaConfigEnd : public SchemaHbeatEnd
{
    public:
        /// \brief    Constructor of SchemaConfigEnd
        SchemaConfigEnd();
        /// \brief    Destructor of SchemaConfigEnd
        ~SchemaConfigEnd();
};

/// \brief    Class to manage schema config.basic xPL message
class SchemaConfigBasic : public SchemaHbeatBasic
{
    public:
        /// \brief    Constructor of SchemaConfigBasic
        SchemaConfigBasic();
        SchemaConfigBasic(int interval);
        /// \brief    Destructor of SchemaConfigBasic
        ~SchemaConfigBasic();
};

/// \brief    Class to manage schema config.app xPL message
class SchemaConfigApp : public SchemaHbeatApp
{
    public:
        /// \brief    Constructor of SchemaConfigApp
        SchemaConfigApp();
        SchemaConfigApp(int interval, int port, const std::string& remoteIP);
        /// \brief    Destructor of SchemaConfigApp
        ~SchemaConfigApp();
};

/// \brief    Class to manage schema config.list xPL message
class SchemaConfigList : public SchemaObject
{
    public:
        /// \brief    Constructor of SchemaConfigList
        SchemaConfigList();
        /// \brief    Destructor of SchemaConfigList
        ~SchemaConfigList();

        SchemaConfigList(SchemaConfigList const& other);
        SchemaConfigList& operator=(SchemaConfigList const& other);
        SchemaConfigList(SchemaConfigList &&other);
        SchemaConfigList& operator=(SchemaConfigList&& other) noexcept;
};

/// \brief    Class to manage schema config.current xPL message
class SchemaConfigCurrent : public SchemaObject
{
    public:
        /// \brief    Constructor of SchemaConfigCurrent
        SchemaConfigCurrent();
        /// \brief    Destructor of SchemaConfigCurrent
        ~SchemaConfigCurrent();

        SchemaConfigCurrent(SchemaConfigCurrent const& other);
        SchemaConfigCurrent& operator=(SchemaConfigCurrent const& other);
        SchemaConfigCurrent(SchemaConfigCurrent &&other);
        SchemaConfigCurrent& operator=(SchemaConfigCurrent&& other) noexcept;
};

/// \brief    Class to manage schema config.list xPL message
class SchemaConfigListRequest : public SchemaObject
{
    public:
    /// \brief    Constructor of SchemaConfigListRequest
    SchemaConfigListRequest();
    /// \brief    Destructor of SchemaConfigListRequest
    ~SchemaConfigListRequest();
    void Check();
};

/// \brief    Class to manage schema config.current xPL message
class SchemaConfigCurrentRequest : public SchemaObject
{
    public:
    /// \brief    Constructor of SchemaConfigCurrentRequest
    SchemaConfigCurrentRequest();
    /// \brief    Destructor of SchemaConfigCurrentRequest
    ~SchemaConfigCurrentRequest();
    void Check();
};

}
#endif // SCHEMACONFIG_H

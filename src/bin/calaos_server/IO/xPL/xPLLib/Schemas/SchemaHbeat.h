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

#ifndef SCHEMAHBEAT_H
#define SCHEMAHBEAT_H

#include <string>
#include "SchemaObject.h"

namespace xPL
{

/// \brief    Class to manage schema hbeat.end xPL message
class SchemaHbeatEnd : public SchemaObject
{
    public:
        /// \brief    Constructor of SchemaHbeatEnd
        SchemaHbeatEnd();
        /// \brief    Destructor of SchemaHbeatEnd
        ~SchemaHbeatEnd();

        void Check();

    protected:
    private:
};

/// \brief    Class to manage schema hbeat.basic xPL message
class SchemaHbeatBasic : public SchemaObject
{
    public:
        /// \brief    Constructor of SchemaHbeatBasic
        SchemaHbeatBasic();
        SchemaHbeatBasic(int interval);
        /// \brief    Destructor of SchemaHbeatBasic
        ~SchemaHbeatBasic();

        int GetInterval();
        void SetInterval(int interval);
        void Check();

    protected:
    private:
};

/// \brief    Class to manage schema hbeat.app xPL message
class SchemaHbeatApp : public SchemaHbeatBasic
{
    public:
        /// \brief    Constructor of SchemaHbeatApp
        SchemaHbeatApp();
        SchemaHbeatApp(int interval, int port, const std::string& remoteIP);
        /// \brief    Destructor of SchemaHbeatApp
        ~SchemaHbeatApp();

        int GetPort();
        void SetPort(int port);
        std::string GetRemoteIP();
        void SetRemoteIP(const std::string& remoteIP);
        void Check();

    protected:
    private:
};

/// \brief    Class to manage schema hbeat.request xPL message
class SchemaHbeatRequest : public SchemaObject
{
    public:
    /// \brief    Constructor of SchemaHbeatRequest
    SchemaHbeatRequest();
    /// \brief    Destructor of SchemaHbeatRequest
    ~SchemaHbeatRequest();
    void Check();
};

}
#endif // SCHEMAHBEAT_H

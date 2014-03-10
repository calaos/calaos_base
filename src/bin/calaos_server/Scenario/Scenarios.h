/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef  SCENARIOS_INC
#define  SCENARIOS_INC

#include <Calaos.h>
#include "Scenario.h"

namespace CalaosSimpleScenario
{
        class SimpleScenarios
        {
                private:
                        vector<SimpleScenario> scenarios;

                        //init. to false
                        //true if the list of scenario is loaded, we load the list only the first time
                        bool fileLoaded;

                public:
                        /** Returns the instance of SimpleScenario */
                        static SimpleScenarios& instance()
                        {
                                static SimpleScenarios s;
                                return s;
                        }

                        SimpleScenarios();

                        /** Returns a list with all scenario */
                        vector<SimpleScenario>& scenariosGet() { return scenarios; }

                        /*
                         * Return the scenario with the name "name" <br>
                         * if no scenario has this name, create a new scenario with this name
                         */
                        SimpleScenario& searchScenario(string name);

                        void deleteScenario(string name);

                        void saveIntoXML();
                        void loadFromXML();

                        string toString();

                        void checkIOs();
        };
}

#endif

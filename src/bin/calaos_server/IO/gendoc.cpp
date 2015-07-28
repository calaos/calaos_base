/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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

//
// Generate Documentation files (markdown file) and json files from IO
//

#include <Utils.h>
#include <Params.h>
#include <IOFactory.h>

using namespace Calaos;

void genDoc(string path)
{
    Params p;
    unordered_map<string, function<Input *(Params &)>> &inputs = IOFactory::Instance().inputFunctionRegistry;

    for ( auto it = inputs.begin(); it != inputs.end(); ++it )
    {
        std::cout << " " << it->first << " : ";//<< ":" << it->second;
        Input *in = IOFactory::Instance().CreateInput(it->first, p);
        cout << in->genDocMd();
        cout << endl;
    }

//    cout << io->genDocMd();
//    cout << io->genDocJson();
}


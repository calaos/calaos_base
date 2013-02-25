/** DEPRECATED ! **/

/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#include <Etage.h>
//-----------------------------------------------------------------------------
using namespace std;
using namespace Soekris;
//-----------------------------------------------------------------------------
Etage::~Etage()
{
        for (int i = 0;i < pieces.size();i++)
                delete pieces[i];

        Soekris::log(DEBUG_INFO, "Etage::~Etage(): Ok");
}
//-----------------------------------------------------------------------------
void Etage::Add(Piece *p)
{
        pieces.push_back(p);

        Soekris::log(DEBUG_INFO, "Etage::Add(): Ok");
}
//-----------------------------------------------------------------------------
void Etage::Remove(int pos)
{
        vector<Piece *>::iterator iter = pieces.begin();
        for (int i = 0;i < pos;iter++, i++) ;
        delete pieces[pos];
        pieces.erase(iter);

        Soekris::log(DEBUG_INFO, "Etage::Remove(): Ok");
}
//-----------------------------------------------------------------------------
Piece *Etage::operator[] (int i) const
{
        return pieces[i];
}
//-----------------------------------------------------------------------------

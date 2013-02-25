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
#ifndef S_ETAGE_H
#define S_ETAGE_H
//-----------------------------------------------------------------------------
#include <Soekris.h>
#include <iostream>
#include <Piece.h>
//-----------------------------------------------------------------------------
using namespace std;
//-----------------------------------------------------------------------------
namespace Soekris
{
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class Etage
{
        protected:
                Glib::ustring name;
                Glib::ustring img;

                std::vector<Piece *> pieces;

        public:
                Etage(Glib::ustring n, Glib::ustring i): name(n), img(i)
                        { Soekris::log(DEBUG_INFO, "Etage::Etage(%s, %s): Ok", name.c_str(), img.c_str()); }
                ~Etage();

                Glib::ustring &get_name() { return name; }
                Glib::ustring &get_img() { return img; }

                void set_name(Glib::ustring &s) { name = s; }
                void set_img(Glib::ustring &s) { img = s; }

                void Add(Piece *p);
                void Remove(int i);
                Piece *operator[] (int i) const;

                int get_size() { return pieces.size(); }
};
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
}
#endif

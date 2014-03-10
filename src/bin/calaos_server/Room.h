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
#ifndef S_ROOM_H
#define S_ROOM_H

#include <Calaos.h>
#include <Input.h>
#include <Output.h>
#include <type_traits>

using namespace std;

namespace Calaos
{

class Room
{
protected:
    string name;
    string type;
    int hits;

    vector<Input *> inputs;
    vector<Output *> outputs;

public:
    Room(string _name, string _type, int _hits = 0);
    ~Room();

    string &get_name() { return name; }
    string &get_type() { return type; }

    void set_name(string &s);
    void set_type(string &s);

    int get_hits() { return hits; }

    void set_hits(int h);

    void AddInput(Input *p);
    void RemoveInput(int i, bool del = true);
    void AddOutput(Output *p);
    void RemoveOutput(int i, bool del = true);

    void RemoveInputFromRoom(Input *in);
    void RemoveOutputFromRoom(Output *out);

    Input *get_input(int i) { return inputs[i]; }
    Output *get_output(int i) { return outputs[i]; }

    int get_size_in() { return inputs.size(); }
    int get_size_out() { return outputs.size(); }

    //Some templated functions to avoid lots of copy/paste
    //when looping over inputs/outputs the same way
    //Ex of use:
    //   room->get_size<Input>()
    template<typename T,
             typename std::enable_if<std::is_same<T, Input*>::value>::type* = nullptr>
    int get_size() { return inputs.size(); }

    template<typename T,
             typename std::enable_if<std::is_same<T, Output*>::value>::type* = nullptr>
    int get_size() { return outputs.size(); }

    template<typename T,
             typename std::enable_if<std::is_same<T, Input*>::value>::type* = nullptr>
    T get_io(int i) { return inputs[i]; }

    template<typename T,
             typename std::enable_if<std::is_same<T, Output*>::value>::type* = nullptr>
    T get_io(int i) { return outputs[i]; }

    bool LoadFromXml(TiXmlElement *node);
    bool SaveToXml(TiXmlElement *node);
};

}
#endif

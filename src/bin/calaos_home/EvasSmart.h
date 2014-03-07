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
#ifndef CEVASSMART_H
#define CEVASSMART_H

#include <Utils.h>
#include <Evas.h>

class EvasSmart
{
protected:
    //The main evas object
    Evas_Object *evas_object;

    Evas_Smart *smart_object;

    static Evas_Smart *EvasSmartClassCreate(string evas_smart_type);

    //Add a memmber Evas object to the smart object
    void AddMemberObject(Evas_Object *obj)
    { evas_object_smart_member_add(obj, evas_object); member_count++; }
    void DelMemberObject(Evas_Object *obj)
    { evas_object_smart_member_del(obj); member_count--; }

    int member_count;

public:
    EvasSmart(Evas *evas, string evas_smart_type);
    virtual ~EvasSmart();

    virtual void SmartMove(int x, int y) { }
    virtual void SmartResize(int w, int h) { }
    virtual void SmartShow() { }
    virtual void SmartHide() { }
    virtual void SmartColorSet(int r, int g, int b, int a) { }
    virtual void SmartClipSet(Evas_Object *clip) { }
    virtual void SmartClipUnset() { }
    virtual void SmartCalculate() { }
    virtual void SmartMemberAdd(Evas_Object *child) { }
    virtual void SmartMemberDel(Evas_Object *child) { }

    Evas_Object *getSmartObject() { return evas_object; }
};

#endif

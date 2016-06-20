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
#ifndef IOVIEW_H
#define IOVIEW_H

#include <Utils.h>
#include "BaseView.h"

#include <CalaosModel.h>


class IOBaseElement
{
protected:
    IOBase *io;

    sigc::connection con_changed;
    sigc::connection con_deleted;

    virtual void ioDeleted();

public:
    IOBaseElement(IOBase *io);
    virtual ~IOBaseElement();

    virtual IOBase *getIO() { return io; }
    virtual void setIO(IOBase *_io);

    virtual void initView();
    virtual void updateView() = 0;
};

class GenlistItemBase;
class IOView: public BaseView, public IOBaseElement
{
public:
    enum { IO_NONE = 0, IO_SCENARIO_HOME };

public:
    IOView(Evas *evas, Evas_Object *parent, IOBase *io, std::string collection);
    IOView(Evas *evas, Evas_Object *parent, IOBase *io);
    virtual ~IOView();
};

class IOViewFactory
{
public:
    static IOView *CreateIOView(Evas *evas, Evas_Object *parent, IOBase *io, int type);
    static IOView *CreateIOView(Evas *evas, Evas_Object *parent, int type);

    static IOBaseElement *CreateIOBaseElement(Evas *evas, Evas_Object *parent, IOBase *io, Evas_Object *genlist, std::string style_addition, GenlistItemBase *gparent = NULL);
};

#endif // IOVIEW_H

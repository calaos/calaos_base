/******************************************************************************
**  Copyright (c) 2006-2011, Calaos. All Rights Reserved.
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
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include "IOView.h"

#include "IO/IOScenarioHomeView.h"

#include "IO/IOWODigitalHomeView.h"
#include "IO/IOGenlistScenarioHomeView.h"
#include "IO/IOWODaliHomeView.h"
#include "IO/IOWODaliRVBHomeView.h"
#include "IO/IOInternalBoolHomeView.h"
#include "IO/IOInternalIntHomeView.h"
#include "IO/IOInternalStringHomeView.h"
#include "IO/IOWIAnalogHomeView.h"
#include "IO/IOWOAnalogHomeView.h"
#include "IO/IOWITempHomeView.h"
#include "IO/IOWOVoletHomeView.h"
#include "IO/IOWOVoletSmartHomeView.h"

IOView::IOView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string _collection):
        BaseView(_evas, _parent),
        IOBaseElement(_io)
{
        try
        {
                LoadEdje(_collection);
        }
        catch (exception const &e)
        {
                Utils::logger("root") << Priority::CRIT << "IOView: Can't load edje" << log4cpp::eol;
                throw;
        }
}

IOView::~IOView()
{
}

IOBaseElement::IOBaseElement(IOBase *_io):
        io(_io)
{
        if (io)
        {
                con_deleted = io->io_deleted.connect(sigc::mem_fun(*this, &IOBaseElement::ioDeleted));
                con_changed = io->io_changed.connect(sigc::mem_fun(*this, &IOBaseElement::updateView));
        }
}

IOBaseElement::~IOBaseElement()
{
        if (io)
        {
                con_deleted.disconnect();
                con_changed.disconnect();
        }
}

void IOBaseElement::setIO(IOBase *_io)
{
        io = _io;

        if (io)
        {
                con_deleted = io->io_deleted.connect(sigc::mem_fun(*this, &IOBaseElement::ioDeleted));
                con_changed = io->io_changed.connect(sigc::mem_fun(*this, &IOBaseElement::updateView));
        }

        updateView();
}

void IOBaseElement::ioDeleted()
{
        io = NULL;
        updateView();
}

void IOBaseElement::initView()
{
}

IOView *IOViewFactory::CreateIOView(Evas *evas, Evas_Object *parent, IOBase *io, int type)
{
        switch (type)
        {
        default:
        case IOView::IO_NONE: return NULL; break;
        case IOView::IO_SCENARIO_HOME: return new IOScenarioHomeView(evas, parent, io); break;
        }
}

IOView *IOViewFactory::CreateIOView(Evas *evas, Evas_Object *parent, int type)
{
        return CreateIOView(evas, parent, NULL, type);
}

IOBaseElement *IOViewFactory::CreateIOBaseElement(Evas *evas, Evas_Object *parent, IOBase *io, Evas_Object *genlist, string style_addition, GenlistItemBase *gparent)
{
        IOBaseElement *element = NULL;

        if (io->params["gui_type"] == "light")
                element = new IOWODigitalHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "scenario")
                element = new IOGenlistScenarioHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "light_dimmer")
                element = new IOWODaliHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "light_rgb")
                element = new IOWODaliRVBHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "var_bool")
                element = new IOInternalBoolHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "var_int")
                element = new IOInternalIntHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "var_string")
                element = new IOInternalStringHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "analog_in")
                element = new IOWIAnalogHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "analog_out")
                element = new IOWOAnalogHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "temp")
                element = new IOWITempHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "shutter")
                element = new IOWOVoletHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);
        else if (io->params["gui_type"] == "shutter_smart")
                element = new IOWOVoletSmartHomeView(evas, parent, io, style_addition, ELM_GENLIST_ITEM_NONE);

        GenlistItemBase *item = dynamic_cast<GenlistItemBase *>(element);
        if (item)
                item->Append(genlist, gparent);

        return element;
}

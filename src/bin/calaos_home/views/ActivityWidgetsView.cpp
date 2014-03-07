/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
#include "ActivityWidgetsView.h"
#include <time.h>

ActivityWidgetsView::ActivityWidgetsView(Evas *_e, Evas_Object *_parent):
        ActivityView(_e, _parent, "calaos/page/widgets")
{
        clipper = evas_object_rectangle_add(evas);
        evas_object_color_set(clipper, 0, 0, 0, 0);
        evas_object_show(clipper);

        Swallow(clipper, "widgets.swallow", true);

        //Create a temporary dir for modules
        if (ecore_file_is_dir("/tmp/calaos_widgets"))
                ecore_file_recursive_rm("/tmp/calaos_widgets");

        ecore_file_mkpath("/tmp/calaos_widgets");

        //add search paths for modules
        ModuleManager::Instance().addPath(PACKAGE_LIB_DIR "/calaos/widgets");
        ModuleManager::Instance().addPath("/usr/lib/calaos/widgets");
        ModuleManager::Instance().SearchModules();

        LoadWidgets();
}

ActivityWidgetsView::~ActivityWidgetsView()
{
        for_each(widgets.begin(), widgets.end(), Delete());
        widgets.clear();

        if (timer) delete timer;
        timer = NULL;
}

void ActivityWidgetsView::resetView()
{
        EmitSignal("normal", "calaos");
}

void ActivityWidgetsView::dimView()
{
        EmitSignal("fade", "calaos");
}

void ActivityWidgetsView::TimerTick()
{
/*
        time_t tt = time(NULL);
        struct tm *t = localtime(&tt);

        if (t->tm_mon == 11) //december
        {
                if (t->tm_mday >= 22 && !xmas_widget)
                {
                        //Create the Xmas widget
                        xmas_widget = new XmasWidget(theme, evas, xmas_def, "xmas");
                        evas_object_clip_set(xmas_widget->get_edje(), clipper);
                        evas_object_color_set(clipper, 255, 255, 255, 255);
                        xmas_widget->Show();
                }
        }

        if (t->tm_mon == 0) //january
        {
                if (t->tm_mday < 4 && !xmas_widget)
                {
                        //Create the Xmas widget
                        xmas_widget = new XmasWidget(theme, evas, xmas_def, "xmas");
                        evas_object_clip_set(xmas_widget->get_edje(), clipper);
                        evas_object_color_set(clipper, 255, 255, 255, 255);
                        xmas_widget->Show();
                }

                if (t->tm_mday >= 4 && xmas_widget)
                {
                        delete xmas_widget;
                        xmas_widget = NULL;
                        if (widgets.size() <= 0)
                                evas_object_color_set(clipper, 255, 255, 255, 0);
                }
        }
*/
}

void ActivityWidgetsView::DeleteWidget(Widget *w)
{
        //delete the specified widget instance
        for (uint i = 0;i < widgets.size();i++)
        {
                if (w == widgets[i])
                {
                        delete w;
                        widgets.erase(std::remove(widgets.begin(), widgets.end(), w), widgets.end());

                        SaveWidgets();
                }
        }

        if (widgets.size() <= 0/* && !xmas_widget*/)
                evas_object_color_set(clipper, 255, 255, 255, 0);
}

void ActivityWidgetsView::DeleteAllWidgets()
{
        for_each(widgets.begin(), widgets.end(), Delete());
        widgets.clear();
        SaveWidgets();
        //if (!xmas_widget) evas_object_color_set(clipper, 255, 255, 255, 0);
        evas_object_color_set(clipper, 0, 0, 0, 0);
}

void ActivityWidgetsView::EditMode()
{
        for (uint i = 0;i < widgets.size();i++)
        {
                widgets[i]->EditMode();
        }
}

void ActivityWidgetsView::NormalMode()
{
        for (uint i = 0;i < widgets.size();i++)
        {
                widgets[i]->NormalMode();
        }
}

void ActivityWidgetsView::_AddWidget(Widget *o)
{
        widgets.push_back(o);
        evas_object_clip_set(o->getEvasObject(), clipper);
        evas_object_color_set(clipper, 255, 255, 255, 255);
}

void ActivityWidgetsView::LoadWidgets()
{
        std::string file = Utils::getConfigFile(WIDGET_CONFIG);
        TiXmlDocument document(file);

        if (!document.LoadFile())
        {
                cErrorDom("root") <<  "There was a parse error in " << file << log4cpp::eol;
                cErrorDom("root") <<  document.ErrorDesc() << log4cpp::eol;
                cErrorDom("root") <<  "In file " << file << " At line " << document.ErrorRow() << log4cpp::eol;

                //force save of file
                SaveWidgets();
        }
        else
        {
                TiXmlHandle docHandle(&document);

                TiXmlElement *node = docHandle.FirstChildElement("calaos:widgets").FirstChildElement("calaos:widget").ToElement();
                for(; node; node = node->NextSiblingElement())
                {
                        if (node->ValueStr() == "calaos:widget" &&
                            node->Attribute("id") &&
                            node->Attribute("type") &&
                            node->Attribute("posx") &&
                            node->Attribute("posy") &&
                            node->Attribute("width") &&
                            node->Attribute("height"))
                        {
                                string type, id;

                                type = node->Attribute("type");
                                id = node->Attribute("id");

                                int x, y, w, h;
                                from_string(node->Attribute("posx"), x);
                                from_string(node->Attribute("posy"), y);
                                from_string(node->Attribute("width"), w);
                                from_string(node->Attribute("height"), h);

                                ModuleDef t;
                                vector<ModuleDef> mods = ModuleManager::Instance().getAvailableModules();
                                for (uint i = 0;i < mods.size();i++)
                                {
                                        if (mods[i].mod_fname == type)
                                        {
                                                t = mods[i];
                                                AddWidget(t, x, y, w, h, id);
                                                break;
                                        }
                                }

                                Show();
                        }
                }

        }

        //Check each 6 hours
        if (!timer) timer = new EcoreTimer(60.0 * 6.0, (sigc::slot<void>)sigc::mem_fun(*this, &ActivityWidgetsView::TimerTick) );
        TimerTick();
}

void ActivityWidgetsView::SaveWidgets()
{
        TiXmlDocument document;
        TiXmlDeclaration *decl = new TiXmlDeclaration("1.0", "UTF-8", "");
        TiXmlElement *rootnode = new TiXmlElement("calaos:widgets");
        rootnode->SetAttribute("xmlns:calaos", "http://www.calaos.fr");
        document.LinkEndChild(decl);
        document.LinkEndChild(rootnode);

        for (int i = 0;i < (int)widgets.size();i++)
        {
                widgets[i]->Save(rootnode);
        }

        string file = Utils::getConfigFile(WIDGET_CONFIG);
        document.SaveFile(file);
}

bool ActivityWidgetsView::AddWidget(ModuleDef &type, int x, int y, int w, int h, string id)
{
        for (uint i = 0;i < widgets.size();i++)
        {
                if (widgets[i]->getId() == id) id = "";
        }

        for (int nid = 0;nid < 9999;nid++)
        {
                bool failed = false;
                for (uint i = 0;i < widgets.size();i++)
                {
                        if (widgets[i]->getId() == Utils::to_string(nid))
                                failed = true;
                }

                if (!failed)
                {
                        id = Utils::to_string(nid);
                        break;
                }
        }

        //create new widget
        try
        {
                Widget *widget = new Widget(theme, evas, type, id, parent, this);
                _AddWidget(widget);
                widget->Move(x, y);
                widget->Resize(w, h);
                widget->Show();
        }
        catch(const std::exception &e)
        {
                cErrorDom("module") << "ActivityWidgetsView: Error creating widget, " <<
                        e.what() << log4cpp::eol;
                return false;
        }

        SaveWidgets();

        return true;
}

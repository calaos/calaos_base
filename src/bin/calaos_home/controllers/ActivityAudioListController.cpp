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
#include "ActivityAudioListController.h"
#include "ActivityMediaController.h"

ActivityAudioListController::ActivityAudioListController(Evas *e, Evas_Object *p, ActivityMediaController *pc):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_AUDIO_LIST),
    parentController(pc)
{
    CalaosModel::Instance();
}

ActivityAudioListController::~ActivityAudioListController()
{
}

void ActivityAudioListController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityAudioListView *audioView = dynamic_cast<ActivityAudioListView *>(view);

    audioView->button_left_click.connect(sigc::mem_fun(*this, &ActivityAudioListController::clickLeft));
    audioView->button_right_click.connect(sigc::mem_fun(*this, &ActivityAudioListController::clickRight));
    audioView->addCallback("audio", "animation,done", sigc::mem_fun(*this, &ActivityAudioListController::doneCallback));
    audioView->player_selected.connect(sigc::mem_fun(*this, &ActivityAudioListController::playerSelectCallback));

    if (!CalaosModel::Instance().isLoaded())
    {
        audioView->ShowLoading();

        CalaosModel::Instance().home_loaded.connect(sigc::mem_fun(*this, &ActivityAudioListController::load_done));

        return;
    }

    page = 0;
    updatePageView();
}

void ActivityAudioListController::load_done()
{
    ActivityAudioListView *audioView = dynamic_cast<ActivityAudioListView *>(view);
    audioView->HideLoading();

    page = 0;
    updatePageView();
}

void ActivityAudioListController::updatePageView()
{
    ActivityAudioListView *audioView = dynamic_cast<ActivityAudioListView *>(view);

    list<AudioPlayer *>::iterator it = CalaosModel::Instance().getAudio()->players.begin();
    int i = 0;

    for (int j = 0;j < page * 3;j++)
        it++;

    for (int j = 0;j < 3;j++)
    {
        //disable unused players
        audioView->disablePlayer(j);
    }

    for (;it != CalaosModel::Instance().getAudio()->players.end() && i < 3;
         it++, i++)
    {
        AudioPlayer *player = (*it);

        audioView->setPlayer(player, i);
    }

    if (page == 0)
        audioView->DisableLeftButton();
    else
        audioView->EnableLeftButton();

    int page_count = (CalaosModel::Instance().getAudio()->players.size() / 3) - 1;
    if (CalaosModel::Instance().getAudio()->players.size() % 3 > 0)
        page_count++;

    if (page < page_count &&
        (CalaosModel::Instance().getAudio()->players.size() > 3))
        audioView->EnableRightButton();
    else
        audioView->DisableRightButton();

    audioView->EmitSignal("show", "calaos");
}

void ActivityAudioListController::clickLeft()
{
    page--;
    if (page < 0)
    {
        page = 0;
        return;
    }

    ActivityAudioListView *audioView = dynamic_cast<ActivityAudioListView *>(view);
    audioView->EmitSignal("hide,right", "calaos");
    parentController->setButtonMode("mode,back");
}

void ActivityAudioListController::clickRight()
{
    page++;
    if (page > (int)(CalaosModel::Instance().getAudio()->players.size() / 3))
    {
        page = (CalaosModel::Instance().getAudio()->players.size() / 3);
        return;
    }

    ActivityAudioListView *audioView = dynamic_cast<ActivityAudioListView *>(view);
    audioView->EmitSignal("hide,left", "calaos");
    parentController->setButtonMode("mode,back");
}

void ActivityAudioListController::doneCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
    updatePageView();
}

void ActivityAudioListController::playerSelectCallback(AudioPlayer *player)
{
    //ActivityCameraSelectController *controller = new ActivityCameraSelectController(*it, evas, parent);
    //parentController->addSubController(controller);
    parentController->setButtonMode("mode,audio_detail");
}

bool ActivityAudioListController::handleButtonClick(string button)
{
    if (button == "button.audio.back")
    {
        ActivityAudioListView *audioView = dynamic_cast<ActivityAudioListView *>(view);

        if (!audioView->isEditMode())
        {
            audioView->unselectPlayers();
            parentController->setButtonMode("mode,back");
        }
        else
        {
            audioView->unsetEditMode();
        }

        return true;
    }
    else if (button == "button.audio.more")
    {
        ActivityAudioListView *audioView = dynamic_cast<ActivityAudioListView *>(view);
        audioView->setEditMode();

        return true;
    }

    return false;
}

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
#ifndef ACTIVITYAUDIOLISTVIEW_H
#define ACTIVITYAUDIOLISTVIEW_H

#include <Utils.h>

#include "CalaosModel.h"
#include "ActivityView.h"

#include "ActivityAudioPlayerObject.h"

using namespace Utils;

#define CREATE_GENLIST_HELPER(glist) \
    Evas_Object *glist = elm_genlist_add(parent); \
    elm_object_style_set(glist, "calaos"); \
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS); \
    elm_genlist_homogeneous_set(glist, true); \
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL); \
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND); \
    evas_object_show(glist);

class ActivityAudioListView: public ActivityView
{
private:
    vector<ActivityPlayerObject> players;
    ActivityPlayerObject *player_current;

    Evas_Object *gplaylist;

    Evas_Object *pager_browser;
    EdjeObject *browser_root;
    Elm_Object_Item *it_browser_root;

    vector<EdjeObject *> browser_root_buttons;

    bool in_edit_mode;

    string radio_id;

    void EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void playerSelected(ActivityPlayerObject *obj);

    void createRootBrowserPage();
    EdjeObject *createRootButton(string title, string subtitle, string total, int row, int col);

    void browserShowAlbums(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void browserShowArtists(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void browserShowGenres(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void browserShowYears(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void browserShowFolders(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void browserShowPlaylists(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void browserShowRadios(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void browserShowSearch(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void browserButtonBack(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void browserButtonRoot(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void albumSelected(void *data);
    void artistSelected(void *data);
    void yearSelected(void *data);
    void genreSelected(void *data);
    void playlistSelected(void *data);
    void folderSelected(void *data, string folder_id);
    void radioSelected(void *data, string radio_id, string subitem_id);
    void searchRadioSelected(void *data, string radio_id, string subitem_id);
    void searchRadioKeyboard_cb(string text, string radio_id, string subitem_id);
    void searchDBKeyboard_cb(string text);

    void browserShowAlbumTracks(Params &infos, int album_id, Params album_infos);
    void browserShowArtistAlbum(Params &infos, Params artist_infos);
    void browserShowYearAlbum(Params &infos, Params artist_infos);
    void browserShowGenreArtist(Params &infos, Params genre_infos);
    void browserShowPlaylistTracks(Params &infos, Params pl_infos);

    void loadFolderList(string folder_id);
    void itemListLoaded(list<Params> &infos);
    void itemRadioLoaded(list<Params> &infos);

public:
    ActivityAudioListView(Evas *evas, Evas_Object *parent);
    ~ActivityAudioListView();

    virtual void resetView();

    void setPlayer(AudioPlayer *player, int position);
    void disablePlayer(int position);

    void EnableLeftButton();
    void DisableLeftButton();
    void EnableRightButton();
    void DisableRightButton();

    void ShowLoading();
    void HideLoading();

    void unselectPlayers();

    void setEditMode();
    void unsetEditMode();
    bool isEditMode() { return in_edit_mode; } // return true if in edit mode

    virtual string getTitle() { return "Musique"; }

    sigc::signal<void> button_left_click;
    sigc::signal<void> button_right_click;

    sigc::signal<void, AudioPlayer *> player_selected;
};

#endif // ACTIVITYAUDIOLISTVIEW_H

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
#include "ApplicationMain.h"
#include "ActivityAudioListView.h"
#include "GenlistItems/GenlistItemSimple.h"
#include "GenlistItems/GenlistItemAlbum.h"
#include "GenlistItems/GenlistItemAlbumHeader.h"
#include "GenlistItems/GenlistItemTrack.h"
#include "GenlistItems/GenlistItemArtist.h"
#include "GenlistItems/GenlistItemYear.h"
#include "GenlistItems/GenlistItemGenre.h"
#include "GenlistItems/GenlistItemBrowserPlaylist.h"
#include "GenlistItems/GenlistItemPlaylistHeader.h"
#include "GenlistItems/GenlistItemRadio.h"
#include "ActivityIntl.h"
#include <FileDownloader.h>

ActivityAudioListView::ActivityAudioListView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/media/audio_list"),
    player_current(NULL),
    in_edit_mode(false)
{
    players.reserve(3);
    for (int i = 0;i < 3;i++)
    {
        ActivityPlayerObject ac(evas, parent);
        players.push_back(ac);
        players[players.size() - 1].createEdjeObject(theme, evas);
        Swallow(players[players.size() - 1].object, "player.swallow." + Utils::to_string(i + 1));
        players[players.size() - 1].player_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::playerSelected));
    }

    Evas_Object *btn = edje_object_part_external_object_get(edje, "button.browser.back");
    elm_object_text_set(btn, _("Back"));

    btn = edje_object_part_external_object_get(edje, "button.browser.root");
    elm_object_text_set(btn, _("My Music"));

    addCallback("button.browser.back", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserButtonBack));
    addCallback("button.browser.root", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserButtonRoot));

    gplaylist = elm_genlist_add(parent);
    Swallow(gplaylist, "playlist.swallow");
    elm_object_style_set(gplaylist, "calaos");
    elm_genlist_homogeneous_set(gplaylist, true);
    evas_object_show(gplaylist);

    pager_browser = elm_naviframe_add(parent);
    evas_object_show(pager_browser);
    Swallow(pager_browser, "browser.swallow");

    addCallback("audio", "*", sigc::mem_fun(*this, &ActivityAudioListView::EdjeCallback));
}

ActivityAudioListView::~ActivityAudioListView()
{
    for (unsigned int i = 0;i < players.size();i++)
    {
        players[i].unsetPlayer();
        DELETE_NULL(players[i].object);
    }

    elm_genlist_clear(gplaylist);
    DELETE_NULL_FUNC(evas_object_del, gplaylist);

    DELETE_NULL_FUNC(evas_object_del, pager_browser);

    for_each(browser_root_buttons.begin(), browser_root_buttons.end(), Delete());
}

void ActivityAudioListView::createRootBrowserPage()
{
    browser_root = new EdjeObject(theme, evas);
    browser_root->LoadEdje("calaos/audio/browser/root");
    browser_root->setAutoDelete(true);

    EdjeObject *obj;
    Params &stats = player_current->getPlayer()->getDBStats();

    obj = createRootButton(_("My Albums"), _("Access to your albums."), stats["albums"], 0, 0);
    obj->addCallback("button", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserShowAlbums));

    obj = createRootButton(_("Artists"), _("Display by artists."), stats["artists"], 0, 1);
    obj->addCallback("button", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserShowArtists));

    obj = createRootButton(_("Years"), _("Sort songs by year."), stats["years"], 1, 0);
    obj->addCallback("button", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserShowYears));

    obj = createRootButton(_("Genre"), _("Show titles by genre."), stats["genres"], 1, 1);
    obj->addCallback("button", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserShowGenres));

    obj = createRootButton(_("Music Directory"), _("Browse your music library by directory."), "", 2, 0);
    obj->addCallback("button", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserShowFolders));

    obj = createRootButton(_("Playlists"), _("Your saved playlists."), stats["playlists"], 2, 1);
    obj->addCallback("button", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserShowPlaylists));

    obj = createRootButton(_("Radios"), _("Web radios web and online service."), "", 3, 0);
    obj->addCallback("button", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserShowRadios));

    obj = createRootButton(_("Search..."), _("Search into your music library."), "", 3, 1);
    obj->addCallback("button", "pressed", sigc::mem_fun(*this, &ActivityAudioListView::browserShowSearch));

    it_browser_root = elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, browser_root->getEvasObject(), "calaos");
}

EdjeObject *ActivityAudioListView::createRootButton(string title, string subtitle, string total, int row, int col)
{
    EdjeObject *obj = new EdjeObject(theme, evas);
    obj->LoadEdje("calaos/audio/browser/button");
    obj->setPartText("title", title);
    obj->setPartText("subtitle", subtitle);
    obj->setPartText("total", total);
    obj->Show();

    int w, h;
    edje_object_size_min_get(obj->getEvasObject(), &w, &h);
    obj->Resize(w, h);
    evas_object_size_hint_min_set(obj->getEvasObject(), w, h);

    browser_root_buttons.push_back(obj);

    if (!edje_object_part_table_pack(browser_root->getEvasObject(), "table", obj->getEvasObject(),
                                     col, row, 1, 1))
        cCritical() <<  "ActivityAudioListView::createRootButton(), failed to pack object into table !";

    return obj;
}

void ActivityAudioListView::resetView()
{
}

void ActivityAudioListView::setPlayer(AudioPlayer *player, int position)
{
    players[position].object->setPartText("player.name", player->params["name"]);
    players[position].object->EmitSignal("player,active", "calaos");
    players[position].setPlayer(player);
}

void ActivityAudioListView::disablePlayer(int position)
{
    players[position].unsetPlayer();
    players[position].object->EmitSignal("player,inactive", "calaos");
    players[position].resetPlayer();
}

void ActivityAudioListView::EnableLeftButton()
{
    EmitSignal("enable,left", "calaos");
}

void ActivityAudioListView::DisableLeftButton()
{
    EmitSignal("disable,left", "calaos");
}

void ActivityAudioListView::EnableRightButton()
{
    EmitSignal("enable,right", "calaos");
}

void ActivityAudioListView::DisableRightButton()
{
    EmitSignal("disable,right", "calaos");
}

void ActivityAudioListView::ShowLoading()
{
    EmitSignal("show,loading", "calaos");
}

void ActivityAudioListView::HideLoading()
{
    EmitSignal("hide,loading", "calaos");
}

void ActivityAudioListView::EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (emission == "page,right")
    {
        button_right_click.emit();
    }
    else if (emission == "page,left")
    {
        button_left_click.emit();
    }
}

void ActivityAudioListView::playerSelected(ActivityPlayerObject *obj)
{
    if (obj == &players[0]) EmitSignal("select,player,1", "calaos");
    if (obj == &players[1]) EmitSignal("select,player,2", "calaos");
    if (obj == &players[2]) EmitSignal("select,player,3", "calaos");

    player_current = obj;

    obj->handlePlaylist(parent, gplaylist, this);
    obj->object->EmitSignal("player,select", "calaos");

    player_selected.emit(obj->getPlayer());
}

void ActivityAudioListView::unselectPlayers()
{
    EmitSignal("unselect,players", "calaos");

    for (uint i = 0;i < players.size();i++)
    {
        players[i].object->EmitSignal("player,unselect", "calaos");
        players[i].unsetPlaylist();
    }

    player_current = NULL;
}

void ActivityAudioListView::setEditMode()
{
    EmitSignal("open,edit,playlist", "calaos");
    in_edit_mode = true;

    Params &stats = player_current->getPlayer()->getDBStats();

    string s = _("<small><blue>Media library : </blue>") +
               stats["albums"] + _(" albums with ") +
               stats["tracks"] + _(" tracks by ") +
               stats["artists"] + _(" artists.</small>");
    setPartText("browser.message", s);

    createRootBrowserPage();
}

void ActivityAudioListView::unsetEditMode()
{
    EmitSignal("close,edit,playlist", "calaos");
    in_edit_mode = false;

    for_each(browser_root_buttons.begin(), browser_root_buttons.end(), Delete());
    browser_root_buttons.clear();

    elm_naviframe_item_pop(pager_browser);
}

void ActivityAudioListView::browserButtonBack(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;
    EmitSignal("browser,loading,stop", "calaos");
    if (elm_naviframe_top_item_get(pager_browser) == it_browser_root) return;

    elm_naviframe_item_pop(pager_browser);
}

void ActivityAudioListView::browserButtonRoot(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;
    EmitSignal("browser,loading,stop", "calaos");

    while (elm_naviframe_top_item_get(pager_browser) != it_browser_root)
        elm_naviframe_item_pop(pager_browser);
}

void ActivityAudioListView::browserShowAlbums(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;

    Params &p = player_current->getPlayer()->getDBStats();
    int count;
    from_string(p["albums"], count);

    CREATE_GENLIST_HELPER(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemAlbum *item = new GenlistItemAlbum(evas, parent, player_current->getPlayer(), i, GenlistItemAlbum::ALBUM_LIST);
        item->Append(glist);
        item->setUserData(item);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::albumSelected));
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::albumSelected(void *data)
{
    GenlistItemAlbum *item_album = reinterpret_cast<GenlistItemAlbum *>(data);
    if (!data) return;

    cDebug() << "Click on Item #" << item_album->getItemId();
    cDebug() << "Item infos: " << item_album->getItemInfos().toString();

    if (!player_current) return;

    EmitSignal("browser,loading,start", "calaos");

    int album_id;
    from_string(item_album->getItemInfos().get_param("id"), album_id);

    player_current->getPlayer()->getDBAlbumTrackCount(album_id,
                                                      sigc::bind(sigc::mem_fun(*this, &ActivityAudioListView::browserShowAlbumTracks),
                                                                 album_id,
                                                                 item_album->getItemInfos()));
}

void ActivityAudioListView::browserShowAlbumTracks(Params &infos, int album_id, Params album_infos)
{
    if (!infos.Exists("count")) return;

    EmitSignal("browser,loading,stop", "calaos");

    album_infos.Add("count", infos["count"]);
    int count;
    from_string(infos["count"], count);

    CREATE_GENLIST_HELPER(glist);

    GenlistItemAlbumHeader *header = new GenlistItemAlbumHeader(evas, parent, player_current->getPlayer(), album_infos, album_id);
    header->Append(glist);
    for (int i = 0;i < count;i++)
    {
        GenlistItemTrack *item = new GenlistItemTrack(evas, parent, player_current->getPlayer(), i, GenlistItemTrack::TRACK_ALBUM, album_id);
        item->Append(glist, NULL);
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::browserShowArtists(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;

    Params &p = player_current->getPlayer()->getDBStats();
    int count;
    from_string(p["artists"], count);

    CREATE_GENLIST_HELPER(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemArtist *item = new GenlistItemArtist(evas, parent, player_current->getPlayer(), i, GenlistItemArtist::ARTIST_LIST);
        item->Append(glist);
        item->setUserData(item);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::artistSelected));
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::artistSelected(void *data)
{
    GenlistItemArtist *item_artist = reinterpret_cast<GenlistItemArtist *>(data);
    if (!data) return;

    cDebug() << "Click on Item #" << item_artist->getItemId();
    cDebug() << "Item infos: " << item_artist->getItemInfos().toString();

    if (!player_current) return;

    EmitSignal("browser,loading,start", "calaos");

    int artist_id;
    from_string(item_artist->getItemInfos().get_param("id"), artist_id);

    player_current->getPlayer()->getDBArtistAlbumCount(artist_id,
                                                       sigc::bind(sigc::mem_fun(*this, &ActivityAudioListView::browserShowArtistAlbum),
                                                                  item_artist->getItemInfos()));
}

void ActivityAudioListView::browserShowArtistAlbum(Params &infos, Params artist_infos)
{
    if (!infos.Exists("count")) return;

    EmitSignal("browser,loading,stop", "calaos");

    cDebug() << "RESULT infos: " << artist_infos.toString();

    artist_infos.Add("count", infos["count"]);
    int count;
    from_string(infos["count"], count);

    int artist_id;
    from_string(artist_infos["id"], artist_id);

    CREATE_GENLIST_HELPER(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemAlbum *item = new GenlistItemAlbum(evas, parent, player_current->getPlayer(), i, GenlistItemAlbum::ALBUM_ARTIST, artist_id);
        item->Append(glist);
        item->setUserData(item);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::albumSelected));
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::browserShowGenres(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;

    Params &p = player_current->getPlayer()->getDBStats();
    int count;
    from_string(p["genres"], count);

    CREATE_GENLIST_HELPER(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemGenre *item = new GenlistItemGenre(evas, parent, player_current->getPlayer(), i);
        item->Append(glist);
        item->setUserData(item);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::genreSelected));
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::genreSelected(void *data)
{
    GenlistItemGenre *item_genre = reinterpret_cast<GenlistItemGenre *>(data);
    if (!data) return;

    cDebug() << "Click on Item #" << item_genre->getItemId();
    cDebug() << "Item infos: " << item_genre->getItemInfos().toString();

    if (!player_current) return;

    EmitSignal("browser,loading,start", "calaos");

    int genre_id;
    from_string(item_genre->getItemInfos().get_param("id"), genre_id);

    player_current->getPlayer()->getDBGenreArtistCount(genre_id,
                                                       sigc::bind(sigc::mem_fun(*this, &ActivityAudioListView::browserShowGenreArtist),
                                                                  item_genre->getItemInfos()));
}

void ActivityAudioListView::browserShowGenreArtist(Params &infos, Params genre_infos)
{
    if (!infos.Exists("count")) return;

    EmitSignal("browser,loading,stop", "calaos");

    cDebug() << "RESULT infos: " << genre_infos.toString();

    genre_infos.Add("count", infos["count"]);
    int count;
    from_string(infos["count"], count);

    int genre_id;
    from_string(genre_infos["id"], genre_id);

    CREATE_GENLIST_HELPER(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemArtist *item = new GenlistItemArtist(evas, parent, player_current->getPlayer(), i, GenlistItemArtist::ARTIST_GENRE, genre_id);
        item->Append(glist);
        item->setUserData(item);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::artistSelected));
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::browserShowYears(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;

    Params &p = player_current->getPlayer()->getDBStats();
    int count;
    from_string(p["years"], count);

    CREATE_GENLIST_HELPER(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemYear *item = new GenlistItemYear(evas, parent, player_current->getPlayer(), i);
        item->Append(glist);
        item->setUserData(item);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::yearSelected));
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::yearSelected(void *data)
{
    GenlistItemYear *item_year = reinterpret_cast<GenlistItemYear *>(data);
    if (!data) return;

    cDebug() << "Click on Item #" << item_year->getItemId();
    cDebug() << "Item infos: " << item_year->getItemInfos().toString();

    if (!player_current) return;

    EmitSignal("browser,loading,start", "calaos");

    int year_id;
    from_string(item_year->getItemInfos().get_param("year"), year_id);

    player_current->getPlayer()->getDBYearAlbumCount(year_id,
                                                     sigc::bind(sigc::mem_fun(*this, &ActivityAudioListView::browserShowYearAlbum),
                                                                item_year->getItemInfos()));
}

void ActivityAudioListView::browserShowYearAlbum(Params &infos, Params year_infos)
{
    if (!infos.Exists("count")) return;

    EmitSignal("browser,loading,stop", "calaos");

    cDebug() << "RESULT infos: " << year_infos.toString();

    year_infos.Add("count", infos["count"]);
    int count;
    from_string(infos["count"], count);

    int year_id;
    from_string(year_infos["year"], year_id);

    CREATE_GENLIST_HELPER(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemAlbum *item = new GenlistItemAlbum(evas, parent, player_current->getPlayer(), i, GenlistItemAlbum::ALBUM_YEAR, year_id);
        item->Append(glist);
        item->setUserData(item);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::albumSelected));
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::browserShowFolders(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;

    //load root folder
    loadFolderList("");
}

void ActivityAudioListView::loadFolderList(string folder_id)
{
    EmitSignal("browser,loading,start", "calaos");

    player_current->getPlayer()->getDBFolder(folder_id,
                                             sigc::mem_fun(*this, &ActivityAudioListView::itemListLoaded));
}

void ActivityAudioListView::itemListLoaded(list<Params> &infos)
{
    list<Params>::iterator it = infos.begin();

    CREATE_GENLIST_HELPER(glist);

    for (;it != infos.end();it++)
    {
        Params &pitem = *it;

        if (pitem["type"] == "track")
        {
            int track_id;
            from_string(pitem["id"], track_id);
            GenlistItemTrack *item = new GenlistItemTrack(evas, parent, player_current->getPlayer(), track_id, GenlistItemTrack::TRACK_ID, 0);
            item->Append(glist, NULL);
        }
        else if (pitem["type"] == "folder")
        {
            GenlistItemSimple *item = new GenlistItemSimple(evas, parent, pitem["name"], true);
            item->setIcon("calaos/icons/genlist/folder");
            item->Append(glist);
            item->setUserData(item);
            item->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &ActivityAudioListView::folderSelected),
                                                   pitem["id"]));
        }
        else if (pitem["type"] == "playlist")
        {
            GenlistItemBrowserPlaylist *item = new GenlistItemBrowserPlaylist(evas, parent, player_current->getPlayer(), pitem);
            item->Append(glist);
            item->setUserData(item);
            item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::playlistSelected));
        }
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");

    EmitSignal("browser,loading,stop", "calaos");
}

void ActivityAudioListView::folderSelected(void *data, string folder_id)
{
    loadFolderList(folder_id);
}

void ActivityAudioListView::browserShowPlaylists(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;

    Params &p = player_current->getPlayer()->getDBStats();
    int count;
    from_string(p["playlists"], count);

    CREATE_GENLIST_HELPER(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemBrowserPlaylist *item = new GenlistItemBrowserPlaylist(evas, parent, player_current->getPlayer(), i);
        item->Append(glist);
        item->setUserData(item);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityAudioListView::playlistSelected));
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::playlistSelected(void *data)
{
    GenlistItemBrowserPlaylist *item_pl = reinterpret_cast<GenlistItemBrowserPlaylist *>(data);
    if (!data) return;

    cDebug() << "Click on Item #" << item_pl->getItemId();
    cDebug() << "Item infos: " << item_pl->getItemInfos().toString();

    if (!player_current) return;

    EmitSignal("browser,loading,start", "calaos");

    int pl_id;
    from_string(item_pl->getItemInfos().get_param("id"), pl_id);

    player_current->getPlayer()->getDBPlaylistTrackCount(pl_id,
                                                         sigc::bind(sigc::mem_fun(*this, &ActivityAudioListView::browserShowPlaylistTracks),
                                                                    item_pl->getItemInfos()));
}

void ActivityAudioListView::browserShowPlaylistTracks(Params &infos, Params pl_infos)
{
    if (!infos.Exists("count")) return;

    EmitSignal("browser,loading,stop", "calaos");

    cDebug() << "RESULT infos: " << pl_infos.toString();

    pl_infos.Add("count", infos["count"]);
    int count;
    from_string(infos["count"], count);

    int pl_id;
    from_string(pl_infos["id"], pl_id);

    CREATE_GENLIST_HELPER(glist);

    GenlistItemPlaylistHeader *header = new GenlistItemPlaylistHeader(evas, parent, player_current->getPlayer(), pl_infos, pl_id);
    header->Append(glist);

    for (int i = 0;i < count;i++)
    {
        GenlistItemTrack *item = new GenlistItemTrack(evas, parent, player_current->getPlayer(), i, GenlistItemTrack::TRACK_PLAYLIST, pl_id);
        item->Append(glist);
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");
}

void ActivityAudioListView::browserShowRadios(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player_current) return;

    EmitSignal("browser,loading,start", "calaos");

    player_current->getPlayer()->getDBAllRadio(sigc::mem_fun(*this, &ActivityAudioListView::itemRadioLoaded));
}

void ActivityAudioListView::radioSelected(void *data, string rid, string subitem_id)
{
    EmitSignal("browser,loading,start", "calaos");

    if (subitem_id == "")
        radio_id = rid;

    player_current->getPlayer()->getDBRadio(rid, subitem_id, sigc::mem_fun(*this, &ActivityAudioListView::itemRadioLoaded));
}

void ActivityAudioListView::itemRadioLoaded(list<Params> &infos)
{
    list<Params>::iterator it = infos.begin();

    CREATE_GENLIST_HELPER(glist);
    elm_genlist_homogeneous_set(glist, false);
    elm_genlist_mode_set(glist, ELM_LIST_COMPRESS);

    for (;it != infos.end();it++)
    {
        Params &pitem = *it;

        cDebug() << "item type: " << pitem.toString();

        if (pitem["type"] == "xmlbrowser")
        {
            GenlistItemSimple *item = new GenlistItemSimple(evas, parent, pitem["name"], true);
            item->setIcon("calaos/icons/genlist/radio");
            item->Append(glist);
            item->setUserData(item);
            item->item_selected.connect(sigc::bind(
                                            sigc::mem_fun(*this, &ActivityAudioListView::radioSelected),
                                            pitem["id"], ""));
        }
        else
        {
            if (pitem["isaudio"] == "1")
            {
                pitem.Add("radio_id", radio_id);
                GenlistItemRadio *item = new GenlistItemRadio(evas, parent, player_current->getPlayer(), pitem);
                item->Append(glist);

                if (pitem["hasitems"] == "1")
                {
                    item->setUserData(item);
                    item->item_selected.connect(sigc::bind(
                                                    sigc::mem_fun(*this, &ActivityAudioListView::radioSelected),
                                                    radio_id ,pitem["id"]));
                }
            }
            else
            {
                if (pitem["hasitems"] == "1" && pitem["type"] != "search" && pitem["type"] != "xmlbrowser_search")
                {
                    GenlistItemSimple *item;
                    if (pitem["type"] == "text" || pitem["type"] == "textarea")
                        item = new GenlistItemSimple(evas, parent, pitem["name"], true, true);
                    else
                        item = new GenlistItemSimple(evas, parent, pitem["name"], true);
                    item->setIcon("calaos/icons/genlist/folder");
                    item->Append(glist);
                    item->setUserData(item);
                    item->item_selected.connect(sigc::bind(
                                                    sigc::mem_fun(*this, &ActivityAudioListView::radioSelected),
                                                    radio_id ,pitem["id"]));
                }
                else
                {
                    GenlistItemSimple *item = new GenlistItemSimple(evas, parent, pitem["name"], false, true);
                    item->Append(glist);

                    if (pitem["type"] == "search" || pitem["type"] == "xmlbrowser_search")
                    {
                        item->setIcon("calaos/icons/genlist/search");
                        item->item_selected.connect(sigc::bind(
                                                        sigc::mem_fun(*this, &ActivityAudioListView::searchRadioSelected),
                                                        radio_id ,pitem["id"]));
                    }
                }
            }
        }
    }

    elm_naviframe_item_push(pager_browser, NULL, NULL, NULL, glist, "calaos");

    EmitSignal("browser,loading,stop", "calaos");
}

void ActivityAudioListView::searchRadioSelected(void *data, string radioid, string subitem_id)
{
    ApplicationMain::Instance().ShowKeyboard(_("Search into radios"), sigc::bind(
                                                 sigc::mem_fun(*this, &ActivityAudioListView::searchRadioKeyboard_cb),
                                                 radioid, subitem_id), false);
}

void ActivityAudioListView::searchRadioKeyboard_cb(string text, string radioid, string subitem_id)
{
    EmitSignal("browser,loading,start", "calaos");

    player_current->getPlayer()->getDBRadioSearch(radioid, subitem_id, text, sigc::mem_fun(*this, &ActivityAudioListView::itemRadioLoaded));
}

void ActivityAudioListView::browserShowSearch(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    ApplicationMain::Instance().ShowKeyboard(_("Search into your music library"),
                                             sigc::mem_fun(*this, &ActivityAudioListView::searchDBKeyboard_cb),
                                             false);
}

void ActivityAudioListView::searchDBKeyboard_cb(string text)
{
    if (!player_current) return;

    EmitSignal("browser,loading,start", "calaos");

    player_current->getPlayer()->getDBSearch(text, sigc::mem_fun(*this, &ActivityAudioListView::itemListLoaded));
}

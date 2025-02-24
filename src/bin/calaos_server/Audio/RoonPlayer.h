/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#pragma once

#include "Calaos.h"
#include "AudioPlayer.h"
#include "Timer.h"
#include "ExternProc.h"
#include <json.hpp>
#include <optional>
#include "Json_Addition.h"

namespace Calaos
{

struct Volume {
    std::string type;
    int min;
    int max;
    int value;
    int step;
    bool is_muted;
    int hard_limit_min;
    int hard_limit_max;
    int soft_limit;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Volume, type, min, max, value, step, is_muted,
                                    hard_limit_min, hard_limit_max, soft_limit)
};

struct SourceControl {
    std::string control_key;
    std::string display_name;
    bool supports_standby;
    std::string status;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SourceControl, control_key, display_name,
                                    supports_standby, status)
};

struct Output {
    std::string output_id;
    std::string zone_id;
    std::vector<std::string> can_group_with_output_ids;
    std::string display_name;
    Volume volume;
    std::vector<SourceControl> source_controls;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Output, output_id, zone_id, can_group_with_output_ids,
                                    display_name, volume, source_controls)
};

struct Settings {
    std::string loop;
    bool shuffle;
    bool auto_radio;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Settings, loop, shuffle, auto_radio)
};

struct OneLine {
    std::string line1;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(OneLine, line1)
};

struct TwoLine {
    std::string line1;
    std::string line2;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TwoLine, line1, line2)
};

struct ThreeLine {
    std::string line1;
    std::string line2;
    std::string line3;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ThreeLine, line1, line2, line3)
};

struct NowPlaying {
    std::optional<int> seek_position;
    std::optional<int> length;
    OneLine one_line;
    TwoLine two_line;
    ThreeLine three_line;
    std::string image_key;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(NowPlaying, seek_position, length, one_line, two_line,
                                    three_line, image_key)
};

struct RoonPlayerState {
    std::string zone_id;
    std::string display_name;
    std::vector<Output> outputs;
    std::string state;
    bool is_next_allowed;
    bool is_previous_allowed;
    bool is_pause_allowed;
    bool is_play_allowed;
    bool is_seek_allowed;
    int queue_items_remaining;
    int queue_time_remaining;
    Settings settings;
    NowPlaying now_playing;
    std::string cover_url;
    std::optional<int> seek_position;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RoonPlayerState, zone_id, display_name, outputs, state,
                                    is_next_allowed, is_previous_allowed, is_pause_allowed,
                                    is_play_allowed, is_seek_allowed, queue_items_remaining,
                                    queue_time_remaining, settings, now_playing, cover_url, seek_position)
};

class RoonCtrl: public sigc::trackable
{
public:
    static shared_ptr<RoonCtrl> Instance(const string &host, int port);

    // Prevent copying
    RoonCtrl(const RoonCtrl&) = delete;
    RoonCtrl& operator=(const RoonCtrl&) = delete;

    ~RoonCtrl();

    void subscribeZone(const string &zoneId, std::function<void(const RoonPlayerState &)> cb);
    void sendMessage(const string &msg);

private:
    explicit RoonCtrl(const string &host, int port);

    ExternProcServer *process;
    string exe;

    std::map<string, std::function<void(const RoonPlayerState &)>> subscribeCb;

    void processNewMessage(const string &msg);
};

class RoonPlayer: public AudioPlayer, public sigc::trackable
{
public:
    RoonPlayer(Params &p);
    virtual ~RoonPlayer();

    virtual void Play();
    virtual void Pause();
    virtual void Stop();
    virtual void Next();
    virtual void Previous();

    virtual void get_volume(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void set_volume(int vol);

    //Until we have a real implementation, just return false
    virtual bool canPlaylist() { return false; }
    virtual bool canDatabase() { return false; }

    virtual Params getOptions()
    {
        Params p;
        p.Add("sync", "false");
        p.Add("sleep", "false");
        return p;
    }

    virtual void get_songinfo(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_current_time(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void set_current_time(double seconds);
    virtual void get_album_cover(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());

    //no implemented yet
    virtual void get_playlist_current(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_playlist_size(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());

    virtual void get_status(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());

    virtual Params getDatabaseCapabilities()
    {
        Params p;
        p.Add("artist", "false");
        p.Add("album", "false");
        p.Add("genre", "false");
        p.Add("year", "false");
        p.Add("music folder", "false");
        p.Add("playlist", "false");
        p.Add("radio", "false");
        p.Add("search", "false");
        return p;
    }

private:
    std::string zoneId, host, id;
    int port;

    RoonPlayerState playerState;

    void state_update_cb(const RoonPlayerState &state);

    void get_current_time_cb(bool status, string request, string result, AudioPlayerData data);
    void get_volume_cb(bool status, string request, string result, AudioPlayerData data);
    void get_songinfo_cb(bool status, string request, string result, AudioPlayerData data);
    void get_status_cb(bool status, string request, string result, AudioPlayerData data);
    void get_album_cover_cb(bool status, string request, string result, AudioPlayerData data);
    void get_playlist_current_cb(bool status, string request, string result, AudioPlayerData data);
    void get_playlist_size_cb(bool status, string request, string result, AudioPlayerData data);
};

}
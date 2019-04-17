'use strict';

function getHTML(command) {
    if (window.XMLHttpRequest) {
        var http = new XMLHttpRequest();
        http.open(command, $('#urlinput').val(), true);

        var s = JSON.parse(document.commandform.messagebody.value).action;
        if (s == "camera")
            http.responseType = 'blob';

        http.onreadystatechange = function() {
            if(http.readyState === 4) {
                if(http.status === 200) {
                    console.log("HTTP Content-Type: " + http.getResponseHeader('Content-Type'));
                    if (http.getResponseHeader('Content-Type') == "image/jpeg") {

                        $('#answer').html('<img id="campic" />');

                        var img = document.getElementById('campic');

                        var blob = http.response; // new Blob([http.response], { type: 'image/jpeg' });

                        img.src = window.URL.createObjectURL(blob);
                        img.onload = function() {
                            window.URL.revokeObjectURL(img.src);
                        }
                    }
                    else {
                        $('#answer').html(JSON.stringify(JSON.parse(http.responseText), null, '    '));
                    }
                }
                else {
                    $('#answer').html('Error ' + http.status);
                }

                $('pre code').each(function(i, block) {
                  hljs.highlightBlock(block);
                });
            }
        };

        http.send(document.commandform.messagebody.value);
    }
    return false;
}

var apiList = [
    '{ }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "get_home" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "get_state", "items": ["input_0", "output_1"], "audio_players": ["0"] }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "get_states", "id": "input_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "query", "id": "input_0", "param": "input_sources" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "get_param", "id": "input_0", "param": "name" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "set_param", "id": "input_0", "param": "name", "value": "New name" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "del_param", "id": "input_0", "param": "param_to_delete" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "get_timerange", "id": "input_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "set_timerange", "id": "input_0", "months": "000000000000", "ranges": [ { "day": "1", "start_hour": "12", "start_min": "0", "start_sec": "0", "start_type": "0", "start_offset": "1", "end_hour": "13", "end_min": "30", "end_sec": "0", "end_type": "0", "end_offset": "1" } ] }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "get_io", "items": ["input_0", "output_0"] }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "set_state", "id": "output_0", "value": "true" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "autoscenario", "type": "list" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "autoscenario", "type": "get", "id": "input_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "autoscenario", "type": "create", "name": "New scenario", "visible": "true", "room_name": "Room", "room_type": "bedroom", "cycle": "true", "steps": [ { "step_pause": "61000", "step_type": "standard", "actions": [ { "id": "output_0", "action": "true"}, { "id": "output_1", "action": "set 50" } ] }, { "step_type": "end", "actions": [ { "id": "output_0", "action": "false" } ] } ] }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "autoscenario", "type": "delete", "id": "input_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "autoscenario", "type": "modify", "id": "input_0", "name": "scenario modified", "visible": "true", "room_name": "Room", "room_type": "bedroom", "cycle": "true", "steps": [ { "step_pause": "61000", "step_type": "standard", "actions": [ { "id": "output_0", "action": "true"}, { "id": "output_1", "action": "set 50" } ] }, { "step_type": "end", "actions": [ { "id": "output_0", "action": "false" } ] } ] }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "autoscenario", "type": "add_schedule", "id": "input_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "autoscenario", "type": "del_schedule", "id": "input_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "get_playlist", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "poll_listen", "type": "register" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "poll_listen", "type": "unregister", "uuid": "XXX" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "poll_listen", "type": "get", "uuid": "XXX" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "config", "type": "get" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "config", "type": "put", "config_files": { "io.xml": "<xml ...... >", "rules.xml": "<xml ...... >" } }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_stats", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio", "audio_action": "get_playlist_size", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio", "audio_action": "get_time", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio", "audio_action": "get_playlist_item", "item": "0", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio", "audio_action": "get_cover_url", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio", "audio_action": "get_cover", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_albums", "from": "0", "count": "1", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_artists", "from": "0", "count": "1", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_years", "from": "0", "count": "1", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_genres", "from": "0", "count": "1", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_playlists", "from": "0", "count": "1", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_music_folder", "from": "0", "count": "1", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_search", "from": "0", "count": "1", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_radios", "from": "0", "count": "1", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_radio_items", "from": "0", "count": "1", "radio_id": "0", "item_id": "1", "search": "", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_artist_album", "from": "0", "count": "1", "artist_id": "0", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_year_albums", "from": "0", "count": "1", "year": "0", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_genre_artists", "from": "0", "count": "1", "genre": "0", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_album_titles", "from": "0", "count": "1", "album_id": "0", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_playlist_titles", "from": "0", "count": "1", "playlist_id": "0", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "audio_db", "audio_action": "get_track_infos", "from": "0", "count": "1", "track_id": "0", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "camera", "type": "get_picture", "id": "output_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "get_cover", "id": "audio_id_0" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "eventlog", "page": 0, "per_page": 100, "uuid": "<uuid for single event>" }',
    '{ "cn_user": "USERNAME", "cn_pass": "PASSWORD", "action": "event_picture", "pic_uid": "<UUID>" }'
];

$(document).ready(function() {
    var i = 0;

    $('#api_list').append($('<option />').val(i++).html('Custom request'));
    for (var c = 1;c < apiList.length;c++) {
        var s = JSON.parse(apiList[c]).action;
        if (s == "poll_listen" || s == "config" || s == "autoscenario")
	    s = s + " " + JSON.parse(apiList[c]).type;
	if (s == "audio" || s == "audio_db")
	    s = s + " " + JSON.parse(apiList[c]).audio_action;

        $('#api_list').append($('<option />').val(i++).html(s));
    }

    $('#api_list').change(function() {
        var j = $('#api_list option:selected').val();
        var s = apiList[parseInt(j)];
        if ($('#username').val() != "")
            s = s.replace("USERNAME", $('#username').val());
        if ($('#passwd').val() != "")
            s = s.replace("PASSWORD", $('#passwd').val());
        $('#message').val(JSON.stringify(JSON.parse(s), null, '    '));
    });
});

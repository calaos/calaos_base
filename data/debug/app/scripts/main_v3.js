'use strict';

var apiList = [
    '{ "msg": "" }',
    '{ "msg": "login", "msg_id": "1234", "data": { "cn_user": "USERNAME", "cn_pass": "PASSWORD" } }',
    '{ "msg": "get_home", "msg_id": "1234" }',
    '{ "msg": "get_state", "msg_id": "1234", "data": { "items": ["input_0", "output_0"] } }',
    '{ "msg": "get_states", "msg_id": "1234", "data": { "id": "input_0" } }',
    '{ "msg": "query", "msg_id": "1234", "data": { "id": "input_0", "param": "input_sources" } }',
    '{ "msg": "get_param", "msg_id": "1234", "data": { "id": "input_0", "param": "name" } }',
    '{ "msg": "set_param", "msg_id": "1234", "data": { "id": "input_0", "param": "name", "value": "New name" } }',
    '{ "msg": "del_param", "msg_id": "1234", "data": { "id": "input_0", "param": "param_to_delete" } }',
    '{ "msg": "get_timerange", "msg_id": "1234", "data": { "id": "input_0" } }',
    '{ "msg": "set_timerange", "msg_id": "1234", "data": { "id": "input_0", "months": "000000000000", "ranges": [ { "day": "1", "start_hour": "12", "start_min": "0", "start_sec": "0", "start_type": "0", "start_offset": "1", "end_hour": "13", "end_min": "30", "end_sec": "0", "end_type": "0", "end_offset": "1" } ] } }',
    '{ "msg": "set_state", "msg_id": "1234", "data": { "id": "output_0", "value": "true" } }',
    '{ "msg": "autoscenario", "msg_id": "1234", "data": { "type": "list" } }',
    '{ "msg": "autoscenario", "msg_id": "1234", "data": { "type": "get", "id": "input_0" } }',
    '{ "msg": "autoscenario", "msg_id": "1234", "data": { "type": "create", "name": "New scenario", "visible": "true", "room_name": "Room", "room_type": "bedroom", "cycle": "true", "steps": [ { "step_pause": "61000", "step_type": "standard", "actions": [ { "id": "output_0", "action": "true"}, { "id": "output_1", "action": "set 50" } ] }, { "step_type": "end", "actions": [ { "id": "output_0", "action": "false" } ] } ] } }',
    '{ "msg": "autoscenario", "msg_id": "1234", "data": { "type": "delete", "id": "input_0" } }',
    '{ "msg": "autoscenario", "msg_id": "1234", "data": { "type": "modify", "id": "input_0", "name": "scenario modified", "visible": "true", "room_name": "Room", "room_type": "bedroom", "cycle": "true", "steps": [ { "step_pause": "61000", "step_type": "standard", "actions": [ { "id": "output_0", "action": "true"}, { "id": "output_1", "action": "set 50" } ] }, { "step_type": "end", "actions": [ { "id": "output_0", "action": "false" } ] } ] } }',
    '{ "msg": "autoscenario", "msg_id": "1234", "data": { "type": "add_schedule", "id": "input_0" } }',
    '{ "msg": "autoscenario", "msg_id": "1234", "data": { "type": "del_schedule", "id": "input_0" } }',
    '{ "msg": "get_playlist", "msg_id": "1234", "data": { "id": "output_0" } }',
    '{ "msg": "get_io", "msg_id": "1234", "data": { "items": ["input_0", "output_0"] } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_stats", "id": "output_0" } }',
    '{ "msg": "audio", "msg_id": "1234", "data": { "audio_action": "get_playlist_size", "id": "output_0" } }',
    '{ "msg": "audio", "msg_id": "1234", "data": { "audio_action": "get_time", "id": "output_0" } }',
    '{ "msg": "audio", "msg_id": "1234", "data": { "audio_action": "get_playlist_item", "item": "0", "id": "output_0" } }',
    '{ "msg": "audio", "msg_id": "1234", "data": { "audio_action": "get_cover_url", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_albums", "from": "0", "count": "1", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_artists", "from": "0", "count": "1", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_years", "from": "0", "count": "1", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_genres", "from": "0", "count": "1", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_playlists", "from": "0", "count": "1", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_music_folder", "from": "0", "count": "1", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_search", "from": "0", "count": "1", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_radios", "from": "0", "count": "1", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_radio_items", "from": "0", "count": "1", "radio_id": "0", "item_id": "1", "search": "", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_artist_album", "from": "0", "count": "1", "artist_id": "0", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_year_albums", "from": "0", "count": "1", "year": "0", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_genre_artists", "from": "0", "count": "1", "genre": "0", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_album_titles", "from": "0", "count": "1", "album_id": "0", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_playlist_titles", "from": "0", "count": "1", "playlist_id": "0", "id": "output_0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_track_infos", "from": "0", "count": "1", "track_id": "0", "id": "output_0" } }',
    '{ "msg": "eventlog", "msg_id": "1234", "data": { "page": 0, "per_page": 100, "uuid": "<uuid for single event>" } }'
];

function popuplateApiList() {
    var i = 0;
    $('#api_list').append($('<option />').val(i++).html('Custom request'));
    for (var c = 1;c < apiList.length;c++) {
        var s = JSON.parse(apiList[c]).msg;
        if (s == "autoscenario")
            s = s + " " + JSON.parse(apiList[c]).data["type"];
        if (s == "audio" || s == "audio_db")
            s = s + " " + JSON.parse(apiList[c]).data["audio_action"];

        $('#api_list').append($('<option />').val(i++).html(s));
    }

    $('#api_list').change(function() {
        var j = $('#api_list option:selected').val();
        var s = apiList[parseInt(j)];
        if ($('#username').val() != "")
            s = s.replace("USERNAME", $('#username').val());
        if ($('#passwd').val() != "")
            s = s.replace("PASSWORD", $('#passwd').val());
        var formatted = JSON.stringify(JSON.parse(s), null, '    ');
        if (window.editor && typeof window.editor.setValue === 'function') {
            window.editor.setValue(formatted);
        } else {
            $('#message').val(formatted);
        }
    });

    $('#api_list').change();
}

var ws = new Object();
var loginfos = [];

function addLog(o, sending) {
    sending = typeof sending !== 'undefined' ? sending : false;
    var id = loginfos.length;
    var newItem = $('<p></p>');
    loginfos.push(o);
    $('.log').append(newItem);
    var icon = '';
    var text = '';
    if (o.type == 'connected') {
        icon = '✔️ ';
        text = 'Connected';
    } else if (o.type == 'disconnected') {
        icon = '✖️ ';
        text = 'Disconnected';
    } else if (o.type == 'error') {
        icon = '⚠️ ';
        text = 'Error';
    } else if (o.type === 'event') {
        icon = '🔔 ';
        text = 'Event';
    } else if (sending) {
        icon = '➡️ ';
        text = o.type || 'Empty "msg"';
    } else {
        icon = '⬅️ ';
        text = o.type || 'Empty "msg"';
    }
    newItem.text(icon + text);
    $('#badge_count').html(loginfos.length);
    newItem.on('click', function () {
        var data = loginfos[id];
        var txt;
        try {
            txt = JSON.stringify(JSON.parse(data.json), null, 4);
        } catch(ex) {
            txt = data.json;
        }
        $('#answer').text(txt);
        $('pre code').each(function(i, block) {
            hljs.highlightElement(block);
        });
    });
}

function clearLog() {
    loginfos = [];
    $('.log').html('');
    $('#badge_count').html('0');
}

$(document).ready(function() {

    popuplateApiList();

    $('#urlinput').val('ws://' + window.location.host + '/api');

    $('#btdisconnect').on('click', function () {
        ws.close();
    });

    $('#btsend').on('click', function () {

        var m = (window.editor && typeof window.editor.getValue === 'function')
            ? window.editor.getValue()
            : $('#message').val();

        if ('readyState' in ws &&
             ws.readyState == ws.OPEN) {
            ws.send(m);
        }
        else {
            console.log('sending error');
            addLog({
                type: 'error',
                msg_id: '',
                json: 'Websocket connection closed'
            });
            return;
        }

        var json = JSON.parse(m);

        var id = '';
        var msg = 'UNKNOWN!!';
        if (json.hasOwnProperty('msg_id'))
            id = json.msg_id;
        if (json.hasOwnProperty('msg'))
            msg = json.msg;

        addLog({
            type: msg,
            msg_id: id,
            json: m
        }, true);
    });

    $('#btclear').on('click', function () {
        clearLog();
    });

    $('#btconnect').on('click', function () {
        ws = new WebSocket($('#urlinput').val());
        ws.onopen = function(evt) {
            $('#status_con').removeClass('bg-danger');
            $('#status_con').addClass('bg-success');
            $('#status_con').html('Connected');

            addLog({
                type: 'connected',
                msg_id: '',
                json: 'OPEN'
            });
        };
        ws.onclose = function(evt) {
            $('#status_con').removeClass('bg-success');
            $('#status_con').addClass('bg-danger');
            $('#status_con').html('Disconnected');

            addLog({
                type: 'disconnected',
                msg_id: '',
                json: 'CLOSED'
            });
        };
        ws.onmessage = function(evt) {
            console.log(evt.data);

            var json = JSON.parse(evt.data);

            var id = '';
            var msg = 'UNKNOWN!!';
            if (json.hasOwnProperty('msg_id'))
                id = json.msg_id;
            if (json.hasOwnProperty('msg'))
                msg = json.msg;

            addLog({
                type: msg,
                msg_id: id,
                json: evt.data
            });
        };
        ws.onerror = function(evt) {
            $('#status_con').removeClass('bg-success');
            $('#status_con').addClass('bg-danger');
            $('#status_con').html('Disconnected');

            addLog({
                type: 'error',
                msg_id: '',
                json: evt.data
            });
        };
    });
});

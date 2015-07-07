'use strict';

var apiList = [
    '{ "msg": "" }',
    '{ "msg": "login", "msg_id": "1234", "data": { "cn_user": "USERNAME", "cn_pass": "PASSWORD" } }',
    '{ "msg": "get_home", "msg_id": "1234" }',
    '{ "msg": "get_state", "msg_id": "1234", "data": { "inputs": ["input_0"], "outputs": ["output_0", "output_1"], "audio_players": ["0"] } }',
    '{ "msg": "set_state", "msg_id": "1234", "data": { "type": "output", "id": "output_0", "value": "true" } }',
    '{ "msg": "set_state", "msg_id": "1234", "data": { "type": "input", "id": "input_0", "value": "true" } }',
    '{ "msg": "set_state", "msg_id": "1234", "data": { "type": "audio", "player_id": "0", "value": "volume 75" } }',
    '{ "msg": "set_state", "msg_id": "1234", "data": { "type": "camera", "camera_id": "0", "camera_action": "move", "value": "left" } }',
    '{ "msg": "get_playlist", "msg_id": "1234", "data": { "player_id": "0" } }',
    '{ "msg": "get_io", "msg_id": "1234", "data": { "inputs": ["input_0"], "outputs": ["output_0", "output_1"] } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_stats", "player_id": "0" } }',
    '{ "msg": "audio", "msg_id": "1234", "data": { "audio_action": "get_playlist_size", "player_id": "0" } }',
    '{ "msg": "audio", "msg_id": "1234", "data": { "audio_action": "get_time", "player_id": "0" } }',
    '{ "msg": "audio", "msg_id": "1234", "data": { "audio_action": "get_playlist_item", "item": "0", "player_id": "0" } }',
    '{ "msg": "audio", "msg_id": "1234", "data": { "audio_action": "get_cover_url", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_albums", "from": "0", "count": "1", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_artists", "from": "0", "count": "1", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_years", "from": "0", "count": "1", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_genres", "from": "0", "count": "1", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_playlists", "from": "0", "count": "1", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_music_folder", "from": "0", "count": "1", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_search", "from": "0", "count": "1", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_radios", "from": "0", "count": "1", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_radio_items", "from": "0", "count": "1", "radio_id": "0", "item_id": "1", "search": "", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_artist_album", "from": "0", "count": "1", "artist_id": "0", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_year_albums", "from": "0", "count": "1", "year": "0", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_genre_artists", "from": "0", "count": "1", "genre": "0", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_album_titles", "from": "0", "count": "1", "album_id": "0", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_playlist_titles", "from": "0", "count": "1", "playlist_id": "0", "player_id": "0" } }',
    '{ "msg": "audio_db", "msg_id": "1234", "data": { "audio_action": "get_track_infos", "from": "0", "count": "1", "track_id": "0", "player_id": "0" } }',
];

function popuplateApiList() {
    var i = 0;
    $('#api_list').append($('<option />').val(i++).html('Custom request'));
    for (var c = 1;c < apiList.length;c++) {
        var s = JSON.parse(apiList[c]).msg;
        if (s == "set_state")
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
        $('#message').val(JSON.stringify(JSON.parse(s), null, '    '));
    });

    $('#api_list').change();
}

var ws = new Object();
var loginfos = [];

function addLog(o, sending) {

    sending = typeof sending !== 'undefined' ? sending : false;

    // <p><span class="glyphicon glyphicon-import"></span>log test</p>
    // <p class="pselected"><span class="glyphicon glyphicon-import"></span>log test2</p>
    var id = loginfos.length;
    var newItem = $('<p></p>');
    loginfos.push(o);

    $('.log').append(newItem);
    if (o.type == 'connected') {
        newItem.append($('<span class="glyphicon glyphicon-ok">'));
        newItem.append('Connected');
    }
    else if (o.type == 'disconnected') {
        newItem.append($('<span class="glyphicon glyphicon-remove">'));
        newItem.append('Disconnected');
    }
    else if (o.type == 'error') {
        newItem.append($('<span class="glyphicon glyphicon-remove-sign">'));
        newItem.append(o.type);
    }
    else if (sending) {
        newItem.append($('<span class="glyphicon glyphicon-export">'));
        if (o.type == '')
            newItem.append('Empty "msg"');
        else
            newItem.append(o.type);
    }
    else {
        newItem.append($('<span class="glyphicon glyphicon-import">'));
        if (o.type == '')
            newItem.append('Empty "msg"');
        else
            newItem.append(o.type);
    }

    $('.badge').html(loginfos.length);

    newItem.on('click', function () {
        var data = loginfos[id];
        var txt;
        try {
            txt = JSON.stringify(JSON.parse(data.json), null, '    ');
        }
        catch(ex) {
            txt = data.json;
        }

        $('#answer').html(txt);

        $('pre code').each(function(i, block) {
          hljs.highlightBlock(block);
        });
    });
}

function clearLog() {
    loginfos = [];
    $('.log').html('');
    $('.badge').html('0');
}

$(document).ready(function() {

    popuplateApiList();

    $('#urlinput').val('ws://' + window.location.host + '/api');

    $('#btdisconnect').on('click', function () {
        ws.close();
    });

    $('#btsend').on('click', function () {

        var m = $('#message').val();

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
            $('#status_con').removeClass('label-danger');
            $('#status_con').addClass('label-success');
            $('#status_con').html('Connected');

            addLog({
                type: 'connected',
                msg_id: '',
                json: 'OPEN'
            });
        };
        ws.onclose = function(evt) {
            $('#status_con').removeClass('label-success');
            $('#status_con').addClass('label-danger');
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
            $('#status_con').removeClass('label-success');
            $('#status_con').addClass('label-danger');
            $('#status_con').html('Disconnected');

            addLog({
                type: 'error',
                msg_id: '',
                json: evt.data
            });
        };
    });
});

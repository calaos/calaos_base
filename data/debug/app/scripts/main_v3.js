'use strict';

var apiList = [
    '{ "msg": "" }',
    '{ "msg": "login", "msg_id": "1234", "data": { "cn_user": "USERNAME", "cn_pass": "PASSWORD" } }',
    '{ "msg": "get_home", "msg_id": "1234" }',
];

function popuplateApiList() {
    var i = 0;
    $('#api_list').append($('<option />').val(i++).html('Custom request'));
    $('#api_list').append($('<option />').val(i++).html('login'));
    $('#api_list').append($('<option />').val(i++).html('get_home'));

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

    $('#urlinput').val('ws://' + window.location.host + '/api/v3');

    $('#btdisconnect').on('click', function () {
        ws.close();
    });

    $('#btsend').on('click', function () {

        var m = $('#message').val();

        if (ws.hasOwnProperty('readyState') && ws.readyState == ws.OPEN) {
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

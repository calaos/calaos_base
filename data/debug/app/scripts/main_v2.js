'use strict';

function getHTML(command) {
    if (window.XMLHttpRequest) {
        var http = new XMLHttpRequest();
        http.open(command, document.commandform.commandurl.value, true);
        http.onreadystatechange = function() {
            if(http.readyState === 4) {
                if(http.status === 200) {
                    $('#answer').html(JSON.stringify(JSON.parse(http.responseText), null, '    '));
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

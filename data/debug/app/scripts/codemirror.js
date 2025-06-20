'use strict';

// Initialize CodeMirror for the JSON textarea and expose it globally
window.editor = CodeMirror.fromTextArea(document.getElementById('message'), {
    mode: { name: 'javascript', json: true },
    lineNumbers: true,
    theme: 'default',
    viewportMargin: Infinity
});

// Add JSON validation indicator
const validationIndicator = document.createElement('div');
validationIndicator.id = 'json-validation';
validationIndicator.className = 'mt-2 small';
validationIndicator.style.display = 'none';
document.querySelector('#message').parentNode.appendChild(validationIndicator);

// Function to validate JSON and show status
function validateJSON() {
    const content = window.editor.getValue();
    const indicator = document.getElementById('json-validation');

    if (content.trim() === '') {
        indicator.style.display = 'none';
        return;
    }

    try {
        JSON.parse(content);
        indicator.innerHTML = '';
        indicator.style.display = 'block';
        window.editor.getWrapperElement().style.borderColor = '';
    } catch (e) {
        indicator.innerHTML = '<span class="text-danger">✗ Invalid JSON: ' + e.message + '</span>';
        indicator.style.display = 'block';
        window.editor.getWrapperElement().style.borderColor = '#dc3545';
    }
}

// Validate on content change
window.editor.on('change', function() {
    clearTimeout(window.jsonValidationTimeout);
    window.jsonValidationTimeout = setTimeout(validateJSON, 500);
});

// Initial validation
setTimeout(validateJSON, 100);
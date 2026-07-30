Upstream submission for 0001-allow-ctrl-c-interception-to-be-disabled.patch
===========================================================================

Target : https://github.com/jonoton/cpp-tui
Status : to be sent (issue + PR). Not yet submitted.

Once an equivalent knob ships upstream, delete the patch, re-vendor the
upstream header and delete this file.

Text to paste
-------------

**Title:** Allow `App`'s mandatory Ctrl+C exit to be opted out of

Hi, and thank you for cpp-tui — it is a pleasure to build on a single header
with this much in it.

**Use case**

We are writing a configuration editor as a TUI. It edits a file, so it has the
usual "you have unsaved changes, really quit?" confirmation: any quit request
must be able to open a dialog rather than take effect immediately.

**What blocks us**

`App::run()` handles Ctrl+C as a "Mandatory Global Exit" at the very top of the
event dispatch loop: the event sets `running = false` and breaks out, before
registered keys (`register_key`), before the focused widget, and before the
registered exit keys. The one exception is when a widget currently holds a text
selection, where Ctrl+C correctly falls through and means Copy.

The consequence is that an application cannot observe Ctrl+C at all. There is
no way to register for it, no way for a focused widget or an open dialog to see
it, and therefore no way to confirm before discarding the user's work. For an
editor, Ctrl+C at the wrong moment silently throws away everything the user
typed.

**Proposal**

Make the interception opt-out rather than unconditional, defaulting to today's
behaviour so nothing changes for existing applications:

```cpp
// class App
void set_intercept_ctrl_c(bool intercept);   // default true
bool intercepts_ctrl_c() const;
```

The mandatory-exit branch becomes:

```cpp
bool is_ctrl_c = (event.is_key_event()) && event.is_copy();
if (is_ctrl_c && intercept_ctrl_c_) {
  ...
}
```

That is the whole change, plus the two accessors and one `bool` member
defaulted to `true`. Applications that never call the setter behave exactly as
they do today. Applications that call `set_intercept_ctrl_c(false)` opt into
handling Ctrl+C themselves, and it then flows down the normal path: contextual
copy, registered keys, focused widget, exit keys. Copy-on-selection keeps
working in both modes — with interception on it is the existing
`handled_as_copy` fall-through, with interception off it is the same
contextual-copy step plus the widgets' own `is_copy()` handling.

We are happy to open a PR with exactly this if the direction suits you. We have
the patch already, tested on Linux against a real pty: default behaviour
unchanged, and with the opt-out a `register_key` callback receives Ctrl+C while
the terminal is still restored correctly on exit.

Two smaller notes from the same investigation, in case they are useful:

* On Unix, Ctrl+C is decoded by the input parser into `key == 'c'` with
  `ctrl == true` (control chars 1..26 are remapped to `c + 96`), so an
  application opting out must register `('c', ctrl=true)`; `key == 3` never
  reaches `key_events_`, even though `Event::is_copy()` also accepts it.
  Perhaps worth a line of documentation.
* Opting out means the application takes responsibility for offering a way to
  quit; that seems like the right trade for an explicit, non-default call, but
  it may deserve a warning in the doc comment.

Thanks again!

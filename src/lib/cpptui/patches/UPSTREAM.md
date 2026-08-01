Upstream submissions
====================

Target : https://github.com/jonoton/cpp-tui

One section per local patch, in the same numeric order as patches/. Each is
self-contained and can be pasted as-is. When one of them ships upstream, delete
the corresponding patch and *its section here*, and re-vendor the header;
delete this file only when the last section is gone.

  1. 0001-allow-ctrl-c-interception-to-be-disabled.patch — feature request
  2. 0002-copy-registered-key-before-invoking-callback.patch — bug report

Of the two, 2 is the one to send first: it is a plain memory-safety bug with a
two-line fix and no API or behaviour change to argue about.


1. Allow the mandatory Ctrl+C exit to be opted out of
====================================================

For : 0001-allow-ctrl-c-interception-to-be-disabled.patch
Status : to be sent (issue + PR). Not yet submitted.

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


2. Use-after-free when a registered key callback unregisters its own key
========================================================================

For : 0002-copy-registered-key-before-invoking-callback.patch
Status : to be sent (issue, with the fix inline). Not yet submitted.

Text to paste
-------------

**Title:** Use-after-free in `App::run()` when a `register_key` callback unregisters its own key

Hi! While building a configuration editor on cpp-tui we hit an
AddressSanitizer report that comes from the key dispatch in `App::run()`. It is
a small one and the fix is two lines, so here it is with a reproducer.

**The problem**

Step "1.7 Registered Key Events" invokes the callback *through* the
`unordered_map` iterator, and then reads the same node again:

```cpp
auto it = key_events_.find(kv);
if (it != key_events_.end()) {
  it->second.callback();
  if (it->second.consume) {
    ...
  }
}
```

`key_events_` is `std::unordered_map<KeyBinding, RegisteredKey, KeyBindingHash>`
and `unregister_key()` does `key_events_.erase(...)`. So a callback that
unregisters its own key — which is a natural thing to do: a shortcut that opens
a modal state in which that shortcut must stop firing — destroys the map node it
is currently running from. Two separate faults follow:

1. The node owns the `std::function`, so the closure is freed while its
   `operator()` is still on the stack. Every access the callback makes to its
   own captures after the `unregister_key()` call is a use-after-free.
2. `it->second.consume` is then read from the freed node, inside `run()`
   itself.

`register_key()` called from a callback is the same hazard by another route: an
insert can rehash the table and invalidate `it` even though nothing was erased.

**Reproducer**

```cpp
#include <cpptui.hpp>

int main() {
  cpptui::App app;
  auto root = std::make_shared<cpptui::Static>(cpptui::StyledText("repro"));
  app.register_key('x', [&app]() { app.unregister_key('x'); });
  app.register_exit_key('q');
  app.run(root);
}
```

`g++ -std=c++17 -fsanitize=address -g`, run on a pty, press `x`:

```
ERROR: AddressSanitizer: heap-use-after-free on address ... 
READ of size 1 at ... thread T0
    #0 ... in cpptui::App::run(std::shared_ptr<cpptui::Widget>) cpptui.hpp:16650
freed by thread T0 here:
    #0 ... in operator delete(void*, unsigned long)
    ...
    #9 ... in cpptui::App::unregister_key(int, bool, bool, bool) cpptui.hpp:16201
    #10 ... in operator() repro.cpp:5
    ...
    #21 ... in cpptui::App::run(std::shared_ptr<cpptui::Widget>) cpptui.hpp:16649
```

That is fault 2 (the `consume` read at :16650). Give the lambda a capture large
enough that `std::function` heap-allocates the closure, and touch that capture
after the `unregister_key()` call, and fault 1 shows up as well, with the free
coming from `RegisteredKey::~RegisteredKey` -> `std::function::~function`
while the same `std::function::operator()` frame is still on the stack.

(Line numbers are from `9543ee3c056583eea1fc44491c6c240f0df0b570`.)

**Fix**

Copy the binding out of the map before invoking it, and read `consume` from the
copy. `RegisteredKey` is `{ std::function<void()>; bool; }`, so the copy keeps
the closure alive for the duration of the call whatever the callback does to
`key_events_`:

```cpp
auto it = key_events_.find(kv);
if (it != key_events_.end()) {
  // The callback may register or unregister keys, including its own, which
  // erases this node or rehashes the map: that would destroy the
  // std::function while it runs and leave `it` dangling for the consume read
  // below. Work on a copy.
  RegisteredKey binding = it->second;
  binding.callback();
  if (binding.consume) {
    needs_render = true;
    continue;
  }
  needs_render = true;
}
```

Dispatch order and `consume` semantics are unchanged; the cost is one
`std::function` copy per handled keypress. Taking the copy before the call is
the point — copying only the callback and reading `consume` afterwards still
touches the dead node, and a second `find()` after the call would look up an
entry the callback may have just removed or replaced.

With this applied the reproducer above is silent under ASan and exits cleanly.

**One nearby remark, not part of this report**

`App::run()` step "1. Handle Additional Timers" has the same shape in a
`std::vector`: `for (auto &t : timers_) { ... t.callback(); ... t.last_fire =
now; }`, while `add_timer()`/`remove_timer()` mutate `timers_`. A timer
callback that adds or removes a timer can therefore reallocate the vector under
the loop, which dangles both `t` and the iterator. We have not hit it and the
fix is less obvious than the one above (index-based iteration plus a decision
about what a timer added mid-loop should do), so we are only mentioning it —
happy to open a separate issue if it is useful.

We are glad to send this as a PR if you prefer. Thanks for cpp-tui!

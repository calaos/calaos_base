#pragma once

#ifdef _WIN32
#define NOMINMAX
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cpptui {

constexpr int VERSION_MAJOR = 1;
constexpr int VERSION_MINOR = 12;
constexpr int VERSION_PATCH = 0;

inline std::string version() {
  return std::to_string(VERSION_MAJOR) + "." + std::to_string(VERSION_MINOR) +
         "." + std::to_string(VERSION_PATCH);
}

class App;
class Dialog;

/// @brief Unique identifier for scheduled timer callbacks
struct TimerId {
  int id;  ///< The unique ID of the timer

  /// @brief Compare two TimerIds for equality
  bool operator==(const TimerId &other) const { return id == other.id; }
  /// @brief Compare two TimerIds for inequality
  bool operator!=(const TimerId &other) const { return id != other.id; }
};

/// @brief Rectangle geometry used for layout, bounds checking, and clipping
struct Rect {
  int x;       ///< X coordinate of the top-left corner
  int y;       ///< Y coordinate of the top-left corner
  int width;   ///< Width of the rectangle
  int height;  ///< Height of the rectangle

  /// @brief Check if a point is contained within the rectangle
  /// @param px X coordinate of the point
  /// @param py Y coordinate of the point
  /// @return true if the point is inside, false otherwise
  bool contains(int px, int py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }

  /// @brief Calculate the intersection with another rectangle
  /// @param other The other rectangle
  /// @return A new Rect representing the intersection area
  Rect intersect(const Rect &other) const {
    int nx = std::max(x, other.x);
    int ny = std::max(y, other.y);
    int nw = std::min(x + width, other.x + other.width) - nx;
    int nh = std::min(y + height, other.y + other.height) - ny;
    if (nw < 0) nw = 0;
    if (nh < 0) nh = 0;
    return {nx, ny, nw, nh};
  }
};

inline volatile std::sig_atomic_t g_resize_pending = 0;

inline void handle_winch([[maybe_unused]] int sig) { g_resize_pending = 1; }

inline std::function<void()> quit_app;

// Cross-thread wakeup mechanism for update()/post()
#ifdef _WIN32
inline HANDLE g_wakeup_event = NULL;
#else
inline int g_wakeup_pipe[2] = {-1, -1};
#endif

// ========================================================================
// UTF-8 and Wide Character Utilities
// ========================================================================

/// @brief Helper structure containing pre-processed character information
struct CharInfo {
  std::string content;  ///< The UTF-8 string content of the character
  int display_width;    ///< The display width (0, 1, or 2)
};

/// @brief Centralized helper for text manipulation, UTF-8 processing, and
/// selection logic
struct TextHelper {
  /// @brief Decode a UTF-8 character from a string
  static bool utf8_decode_codepoint(const std::string &s, size_t pos,
                                    uint32_t &out_codepoint, int &out_len) {
    if (pos >= s.size()) return false;

    unsigned char c = static_cast<unsigned char>(s[pos]);

    if ((c & 0x80) == 0) {
      out_codepoint = c;
      out_len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      if (pos + 1 >= s.size()) return false;
      out_codepoint = (c & 0x1F) << 6;
      out_codepoint |= (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
      out_len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      if (pos + 2 >= s.size()) return false;
      out_codepoint = (c & 0x0F) << 12;
      out_codepoint |= (static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 6;
      out_codepoint |= (static_cast<unsigned char>(s[pos + 2]) & 0x3F);
      out_len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      if (pos + 3 >= s.size()) return false;
      out_codepoint = (c & 0x07) << 18;
      out_codepoint |= (static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 12;
      out_codepoint |= (static_cast<unsigned char>(s[pos + 2]) & 0x3F) << 6;
      out_codepoint |= (static_cast<unsigned char>(s[pos + 3]) & 0x3F);
      out_len = 4;
    } else {
      out_codepoint = c;
      out_len = 1;
    }
    return true;
  }

  /// @brief Get display width of a codepoint
  static int char_display_width(uint32_t codepoint) {
    if (codepoint == 0 || codepoint < 0x20 || codepoint == 0x7F ||
        (codepoint >= 0x80 && codepoint < 0xA0))
      return 0;
    if (codepoint == 0x00AD) return 0;  // Soft hyphen

    // Combining marks
    if ((codepoint >= 0x0300 && codepoint <= 0x036F) ||
        (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||
        (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||
        (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||
        (codepoint >= 0xFE20 && codepoint <= 0xFE2F))
      return 0;

    // Explicit Narrow Overrides for ambiguous symbols often used as icons
    // Checkmark (2713), Ballot X (2717), Warning Sign (26A0)
    if (codepoint == 0x2713 || codepoint == 0x2717 || codepoint == 0x26A0)
      return 1;

    // Zero-width
    if ((codepoint >= 0x200B && codepoint <= 0x200F) ||
        (codepoint >= 0x2028 && codepoint <= 0x202F) ||
        (codepoint >= 0x2060 && codepoint <= 0x206F) || codepoint == 0xFEFF)
      return 0;

    if ((codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||
        (codepoint >= 0xE0100 && codepoint <= 0xE01EF))
      return 0;

    // Wide characters
    if ((codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
        (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
        (codepoint >= 0x20000 && codepoint <= 0x2A6DF) ||
        (codepoint >= 0x2A700 && codepoint <= 0x2B73F) ||
        (codepoint >= 0x2B740 && codepoint <= 0x2B81F) ||
        (codepoint >= 0x2B820 && codepoint <= 0x2CEAF) ||
        (codepoint >= 0x2CEB0 && codepoint <= 0x2EBEF) ||
        (codepoint >= 0x30000 && codepoint <= 0x3134F) ||
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
        (codepoint >= 0x2F800 && codepoint <= 0x2FA1F) ||
        (codepoint >= 0xFF01 && codepoint <= 0xFF60) ||
        (codepoint >= 0xFFE0 && codepoint <= 0xFFE6) ||
        (codepoint >= 0xAC00 && codepoint <= 0xD7AF) ||
        (codepoint >= 0x1100 && codepoint <= 0x11FF) ||
        (codepoint >= 0x3130 && codepoint <= 0x318F) ||
        (codepoint >= 0xA960 && codepoint <= 0xA97F) ||
        (codepoint >= 0xD7B0 && codepoint <= 0xD7FF) ||
        (codepoint >= 0x3040 && codepoint <= 0x309F) ||
        (codepoint >= 0x30A0 && codepoint <= 0x30FF) ||
        (codepoint >= 0x31F0 && codepoint <= 0x31FF) ||
        (codepoint >= 0x3000 && codepoint <= 0x303F) ||
        (codepoint >= 0x3200 && codepoint <= 0x32FF) ||
        (codepoint >= 0x3300 && codepoint <= 0x33FF) ||
        (codepoint >= 0xFE30 && codepoint <= 0xFE4F) ||
        (codepoint >= 0x1F300 && codepoint <= 0x1F9FF) ||
        (codepoint >= 0x1FA00 && codepoint <= 0x1FAFF) ||
        (codepoint >= 0x2600 && codepoint <= 0x26FF) ||
        (codepoint >= 0x2700 && codepoint <= 0x27BF)) {
      return 2;
    }
    return 1;
  }

  /// @brief Calculate total display width of a string
  static int utf8_display_width(const std::string &s) {
    int width = 0;
    size_t pos = 0;
    while (pos < s.size()) {
      uint32_t codepoint;
      int len;
      if (utf8_decode_codepoint(s, pos, codepoint, len)) {
        width += char_display_width(codepoint);
        pos += len;
      } else {
        pos++;
      }
    }
    return width;
  }

  /// @brief Check if a codepoint is a word character
  static bool is_word_char(uint32_t cp) {
    if (cp == '_') return true;
    if (cp >= 'a' && cp <= 'z') return true;
    if (cp >= 'A' && cp <= 'Z') return true;
    if (cp >= '0' && cp <= '9') return true;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
    return false;
  }

  /// @brief Pre-process text into characters for rendering
  static std::vector<CharInfo> prepare_text_for_render(
      const std::string &text) {
    std::vector<CharInfo> chars;
    size_t pos = 0;
    while (pos < text.size()) {
      uint32_t codepoint;
      int byte_len;
      if (utf8_decode_codepoint(text, pos, codepoint, byte_len)) {
        CharInfo ci;
        ci.content = text.substr(pos, byte_len);
        ci.display_width = char_display_width(codepoint);
        if (ci.display_width < 0) ci.display_width = 0;
        chars.push_back(ci);
        pos += byte_len;
      } else {
        pos++;
      }
    }
    return chars;
  }

  /// @brief Convert visual X to character index
  static int visual_to_char_pos(const std::vector<CharInfo> &chars,
                                int visual_x) {
    int current_vx = 0;
    for (size_t i = 0; i < chars.size(); ++i) {
      int cw = chars[i].display_width;
      if (current_vx + cw / 2 >= visual_x) return (int)i;
      current_vx += cw;
    }
    return (int)chars.size();
  }

  /// @brief Convert character index to byte offset
  static size_t char_to_byte_pos(const std::string &text, size_t char_idx) {
    size_t pos = 0;
    size_t count = 0;
    while (pos < text.size() && count < char_idx) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(text, pos, cp, len)) {
        pos += len;
        count++;
      } else {
        pos++;
      }
    }
    return pos;
  }

  /// @brief Find word boundaries at a given character position
  static void select_word_at(const std::vector<CharInfo> &chars, int pos,
                             int &start, int &end) {
    if (pos < 0 || pos >= (int)chars.size()) {
      if (pos < 0) pos = 0;
      if (pos > (int)chars.size()) pos = (int)chars.size();
      start = pos;
      end = pos;
      return;
    }

    std::vector<uint32_t> codepoints;
    codepoints.reserve(chars.size());
    for (const auto &c : chars) {
      uint32_t cp;
      int len;
      utf8_decode_codepoint(c.content, 0, cp, len);
      codepoints.push_back(cp);
    }

    if (!is_word_char(codepoints[pos])) {
      start = pos;
      end = pos + 1;
      return;
    }

    start = pos;
    while (start > 0 && is_word_char(codepoints[start - 1])) start--;

    end = pos;
    while (end < (int)codepoints.size() && is_word_char(codepoints[end])) end++;
  }

  // --- New Helper Methods ---

  /// @brief Count number of UTF-8 codepoints in a string
  static size_t count_codepoints(const std::string &text) {
    size_t count = 0;
    size_t pos = 0;
    while (pos < text.size()) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(text, pos, cp, len)) {
        count++;
        pos += len;
      } else {
        pos++;
      }
    }
    return count;
  }

  /// @brief Safe UTF-8 substring
  static std::string utf8_substr(const std::string &text, size_t start_char_idx,
                                 size_t count = std::string::npos) {
    size_t byte_start = char_to_byte_pos(text, start_char_idx);
    if (count == std::string::npos) {
      return text.substr(byte_start);
    }
    size_t byte_end = char_to_byte_pos(text, start_char_idx + count);
    return text.substr(byte_start, byte_end - byte_start);
  }

  /// @brief Find word boundaries (generic string version)
  static void find_word_boundaries(const std::string &text, int char_idx,
                                   int &start, int &end) {
    auto chars = prepare_text_for_render(text);
    select_word_at(chars, char_idx, start, end);
  }
};

/// @brief Reusable helper for managing text selection state
struct SelectionState {
  int start = -1;
  int end = -1;
  bool mouse_down = false;
  int drag_start_idx = -1;
  bool inclusive_drag = true;  // Default to inclusive (Border/Label style)

  int click_count = 0;

  std::chrono::steady_clock::time_point last_click_time;
  int last_click_idx = -1;

  /// @brief reset selection
  void clear() {
    start = -1;
    end = -1;
    mouse_down = false;
    drag_start_idx = -1;
  }

  /// @brief check if selection is active and valid
  bool has_selection() const {
    return start != -1 && end != -1 && start != end;
  }

  /// @brief Get the sorted selection range
  void get_range(int &s, int &e) const {
    if (start <= end) {
      s = start;
      e = end;
    } else {
      s = end;
      e = start;
    }
  }

  /// @brief check if a specific character index is selected
  bool is_selected(int idx) const {
    return has_selection() && idx >= start && idx < end;
  }

  /// @brief Handle mouse press event
  /// @return true if event handled (selection started)
  bool handle_mouse_press(const std::vector<CharInfo> &chars, int char_idx,
                          int click_count_from_event = 0) {
    if (click_count_from_event > 0) {
      click_count = click_count_from_event;
    } else {
      auto now = std::chrono::steady_clock::now();
      auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - last_click_time)
                      .count();

      if (diff < 500 && last_click_idx == char_idx) {
        click_count++;
      } else {
        click_count = 1;
      }
      last_click_time = now;
      last_click_idx = char_idx;
    }

    if (click_count == 2) {
      // Double click - Select word
      int s = 0, e = 0;
      TextHelper::select_word_at(chars, char_idx, s, e);
      start = s;
      end = e;
      drag_start_idx = end;  // Anchor for drag extension
      mouse_down = true;
    } else if (click_count == 3) {
      // Triple click - Select All (or paragraph if we had line info)
      start = 0;
      end = (int)chars.size();
      drag_start_idx = end;
      mouse_down = true;
    } else {
      // Single click
      mouse_down = true;
      drag_start_idx = char_idx;
      start = char_idx;
      end = char_idx;
    }

    return true;
  }

  /// @brief Handle mouse drag event
  bool handle_mouse_drag(int char_idx) {
    if (mouse_down) {
      start = std::min(drag_start_idx, char_idx);
      end = std::max(drag_start_idx, char_idx);

      if (inclusive_drag) end += 1;  // Inclusive of currently dragged-over char

      return true;
    }
    return false;
  }

  /// @brief Handle mouse release
  bool handle_mouse_release() {
    if (mouse_down) {
      mouse_down = false;
      return true;
    }
    return false;
  }

  /// @brief Get selected text
  std::string get_selected_text(const std::vector<CharInfo> &chars) const {
    if (!has_selection()) return "";
    std::string res;
    for (int i = start; i < end && i < (int)chars.size(); ++i) {
      res += chars[i].content;
    }
    return res;
  }
};

// Backward compatibility wrappers
inline bool utf8_decode_codepoint(const std::string &s, size_t pos,
                                  uint32_t &out_codepoint, int &out_len) {
  return TextHelper::utf8_decode_codepoint(s, pos, out_codepoint, out_len);
}
inline int char_display_width(uint32_t codepoint) {
  return TextHelper::char_display_width(codepoint);
}
inline int utf8_display_width(const std::string &s) {
  return TextHelper::utf8_display_width(s);
}
inline bool is_word_char(uint32_t cp) { return TextHelper::is_word_char(cp); }
inline int utf8_char_byte_length(const std::string &s, size_t pos) {
  // Helper to determine char byte length
  unsigned char c = static_cast<unsigned char>(s[pos]);
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}
inline std::vector<CharInfo> prepare_text_for_render(const std::string &text) {
  return TextHelper::prepare_text_for_render(text);
}
/// @brief Base64 encoding for clipboard operations (OSC 52)
/// @param input The string to encode
/// @return Base64 encoded string
inline std::string base64_encode(const std::string &input) {
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i < input.size()) {
    uint32_t octet_a = i < input.size() ? (unsigned char)input[i++] : 0;
    uint32_t octet_b = i < input.size() ? (unsigned char)input[i++] : 0;
    uint32_t octet_c = i < input.size() ? (unsigned char)input[i++] : 0;
    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

    output.push_back(table[(triple >> 18) & 0x3F]);
    output.push_back(table[(triple >> 12) & 0x3F]);
    output.push_back((i > input.size() + 1) ? '='
                                            : table[(triple >> 6) & 0x3F]);
    output.push_back((i > input.size()) ? '=' : table[triple & 0x3F]);
  }
  return output;
}

/// @brief Internal clipboard buffer for fallback operation
inline std::string g_internal_clipboard = "";

/// @brief Copy text to system clipboard using native methods and OSC 52
/// fallback
inline void copy_to_clipboard(const std::string &text) {
  if (text.empty()) return;

  // Always save to internal clipboard first
  g_internal_clipboard = text;

  bool success = false;
#ifdef _WIN32
  // Windows: clip command
  FILE *pipe = _popen("clip", "w");
  if (pipe) {
    fwrite(text.c_str(), 1, text.size(), pipe);
    _pclose(pipe);
    success = true;
  }
#else
  // Linux: Try common clipboard utilities
  // 1. wl-copy (Wayland)
  if (std::getenv("WAYLAND_DISPLAY")) {
    FILE *pipe = popen("wl-copy", "w");
    if (pipe) {
      fwrite(text.c_str(), 1, text.size(), pipe);
      pclose(pipe);
      success = true;
    }
  }

  // 2. xclip (X11)
  if (!success) {
    FILE *pipe = popen("xclip -selection clipboard -i 2>/dev/null", "w");
    if (pipe) {
      fwrite(text.c_str(), 1, text.size(), pipe);
      if (pclose(pipe) == 0) success = true;
    }
  }

  // 3. xsel (X11 fallback)
  if (!success) {
    FILE *pipe = popen("xsel -b -i 2>/dev/null", "w");
    if (pipe) {
      fwrite(text.c_str(), 1, text.size(), pipe);
      if (pclose(pipe) == 0) success = true;
    }
  }
#endif

  // Always use OSC 52 as a reliable fallback for remote terminals / SSH
  std::string encoded = base64_encode(text);
  // OSC 52 ; c ; <base64> BEL
  std::cout << "\033]52;c;" << encoded << "\x07" << std::flush;
}

/// @brief Retrieve text from system clipboard using native tools
/// @return The string content of the clipboard, or empty if failed
inline std::string paste_from_clipboard() {
  std::string result;
  bool success = false;
  char buffer[128];

#ifdef _WIN32
  // Windows: clip is write-only. Use powershell as a fallback for standard
  // windows terminals. Try powershell as fallback for standard windows
  // terminals
  FILE *pipe = _popen("powershell.exe -NoProfile -Command Get-Clipboard", "r");
  if (pipe) {
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
      result += buffer;
    }
    _pclose(pipe);
    // Remove trailing newline often added by powershell
    if (!result.empty() && result.back() == '\n') result.pop_back();
    if (!result.empty() && result.back() == '\r') result.pop_back();
    success = true;
  }
#else
  // Linux: Try common clipboard utilities

  // 1. wl-paste (Wayland)
  if (!success && std::getenv("WAYLAND_DISPLAY")) {
    FILE *pipe = popen("wl-paste --no-newline 2>/dev/null", "r");
    if (pipe) {
      while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
        success = true;
      }
      pclose(pipe);
    }
  }

  // 2. xclip (X11)
  if (!success) {
    FILE *pipe = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (pipe) {
      while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
        success = true;
      }
      if (pclose(pipe) == 0)
        success = true;  // Only mark success if exit code 0
      else
        result.clear();  // Clear if command failed
    }
  }

  // 3. xsel (X11 fallback)
  if (!success) {
    FILE *pipe = popen("xsel -b -o 2>/dev/null", "r");
    if (pipe) {
      while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
        success = true;
      }
      if (pclose(pipe) == 0)
        success = true;
      else
        result.clear();
    }
  }
#endif

  // Fallback to internal clipboard if system clipboard failed or returned
  // nothing
  if (!success || result.empty()) {
    return g_internal_clipboard;
  }

  return result;
}

/// @brief alignment options for text and widgets
enum class Alignment {
  Left,    ///< Align to the left
  Center,  ///< Align to the center
  Right    ///< Align to the right
};

/// @brief Types of events that can occur
enum class EventType {
  None,    ///< No event
  Key,     ///< Keyboard key press
  Mouse,   ///< Mouse movement or click
  Resize,  ///< Terminal resize
  Quit,    ///< Application quit request
  Paste,   ///< Paste from clipboard (bracketed paste)
  Wakeup   ///< Cross-thread wakeup (from update()/post())
};

/// @brief Represents an input event (keyboard, mouse, or system)
struct Event {
  EventType type = EventType::None;  ///< The type of event
  int key = 0;                       ///< ASCII or Key code for Key events
  int x = 0;                         ///< X coordinate for Mouse events
  int y = 0;                         ///< Y coordinate for Mouse events
  int button = -1;                   ///< Raw button code for Mouse events
  int click_count = 0;     ///< Number of clicks (1=single, 2=double, 3=triple)
  std::string paste_text;  ///< Text content for Paste events

  // Helpers for standard 1003 mouse tracking encoding
  bool mouse_left() const { return (button & 3) == 0 && (button & 64) == 0; }
  bool mouse_middle() const { return (button & 3) == 1 && (button & 64) == 0; }
  bool mouse_right() const { return (button & 3) == 2 && (button & 64) == 0; }
  bool mouse_release() const { return (button & 3) == 3; }
  bool mouse_motion() const { return (button & 32) != 0; }
  bool mouse_drag() const { return mouse_motion() && !mouse_release(); }
  bool mouse_move() const { return mouse_motion() && mouse_release(); }
  bool mouse_wheel() const {
    return (button & 64) != 0;
  }  // Bit 6 typically indicates mouse wheel events

  bool shift = false;  ///< True if Shift modifier is active
  bool ctrl = false;   ///< True if Ctrl modifier is active
  bool alt = false;    ///< True if Alt modifier is active

  /// @brief Check if the event is a copy command
  bool is_copy() const {
    return (ctrl && shift && (key == 'c' || key == 'C')) ||
           (ctrl && (key == 'c' || key == 'C' || key == 3));
  }

  bool is_cut() const {
    return (ctrl && shift && (key == 'x' || key == 'X')) ||
           (ctrl && (key == 'x' || key == 'X' || key == 24));
  }

  bool is_paste() const {
    return (ctrl && shift && (key == 'v' || key == 'V')) ||
           (ctrl && (key == 'v' || key == 'V' || key == 22));
  }

  bool is_select_all() const { return ctrl && (key == 'a' || key == 1); }

  bool is_undo() const {
    return (ctrl && !shift && (key == 'z' || key == 'Z' || key == 26));
  }

  bool is_redo() const {
    return (ctrl && shift && (key == 'z' || key == 'Z')) ||
           (ctrl && !shift && (key == 'y' || key == 'Y' || key == 25));
  }

  bool is_enter() const { return key == 10 || key == 13; }
  bool is_tab() const { return key == 9; }
  bool is_backspace() const { return key == 8 || key == 127; }
  bool is_delete() const { return key == 1005; }

  bool is_nav_up() const { return key == 1065; }
  bool is_nav_down() const { return key == 1066; }
  bool is_nav_left() const { return key == 1068; }
  bool is_nav_right() const { return key == 1067; }
  bool is_nav_home() const { return key == 1003; }
  bool is_nav_end() const { return key == 1004; }
  bool is_nav_pgup() const { return key == 1001; }
  bool is_nav_pgdn() const { return key == 1002; }
  bool is_insert() const { return key == 1006; }

  // Helpers for read-only view scrolling (includes '5', '6', Delete, Insert)
  bool is_view_scroll_up() const {
    return is_nav_pgup() || is_delete() || key == 53;
  }
  bool is_view_scroll_down() const {
    return is_nav_pgdn() || is_insert() || key == 54;
  }

  // Type checking helpers
  bool is_key_event() const { return type == EventType::Key; }
  bool is_mouse_event() const { return type == EventType::Mouse; }

  // Additional key helpers
  bool is_space() const { return key == ' ' || key == 32; }
  bool is_escape() const { return key == 27; }
  bool is_activate() const { return is_enter() || is_space(); }
  bool is_printable() const {
    return (unsigned char)key >= 32 && key != 127 && key < 1000;
  }

  // Mouse wheel direction helpers
  bool mouse_wheel_up() const { return button == 64; }
  bool mouse_wheel_down() const { return button == 65; }

  // Navigation combo helpers (for list/grid navigation)
  bool is_nav_prev() const { return is_nav_up() || is_nav_left(); }
  bool is_nav_next() const { return is_nav_down() || is_nav_right(); }
};

/// @brief Represents an RGB color
struct Color {
  uint8_t r = 255, g = 255, b = 255;
  bool is_default = true;  ///< If true, uses the terminal's default color

  Color() = default;
  Color(uint8_t r, uint8_t g, uint8_t b, bool is_default = false)
      : r(r), g(g), b(b), is_default(is_default) {}

  static Color White() { return {255, 255, 255, false}; }
  static Color Black() { return {0, 0, 0, false}; }
  static Color Red() { return {255, 0, 0, false}; }
  static Color Green() { return {0, 255, 0, false}; }
  static Color Blue() { return {0, 0, 255, false}; }
  static Color Yellow() { return {255, 255, 0, false}; }
  static Color Cyan() { return {0, 255, 255, false}; }
  static Color Magenta() { return {255, 0, 255, false}; }

  bool operator==(const Color &other) const {
    if (is_default && other.is_default) return true;
    return r == other.r && g == other.g && b == other.b &&
           is_default == other.is_default;
  }
  bool operator!=(const Color &other) const { return !(*this == other); }

  /// @brief Returns this color if not default, otherwise returns the fallback
  /// @param fallback The color to use if this color is_default
  /// @return This color or fallback
  Color resolve(const Color &fallback) const {
    return is_default ? fallback : *this;
  }

  /// @brief Calculate optimal contrasting text color (black or white) for a
  /// background Uses relative luminance formula for accessibility
  /// @param bg The background color
  /// @return Black or White, whichever provides better contrast
  static Color contrast_color(const Color &bg) {
    // Proper sRGB to linear conversion with gamma correction
    auto srgb_to_linear = [](double c) -> double {
      if (c <= 0.04045) return c / 12.92;
      return std::pow((c + 0.055) / 1.055, 2.4);
    };

    double r_lin = srgb_to_linear(bg.r / 255.0);
    double g_lin = srgb_to_linear(bg.g / 255.0);
    double b_lin = srgb_to_linear(bg.b / 255.0);

    // Relative luminance formula (ITU-R BT.709)
    double luminance = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin;

    // Return black for light backgrounds, white for dark backgrounds
    // Threshold of 0.4 provides better contrast for mid-tone colors
    return luminance > 0.4 ? Black() : White();
  }

  /// @brief Convert HSV to RGB color
  /// @param h Hue (0.0 - 1.0)
  /// @param s Saturation (0.0 - 1.0)
  /// @param v Value/brightness (0.0 - 1.0)
  /// @return RGB Color
  static Color hsv_to_rgb(float h, float s, float v) {
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    float r = 0, g = 0, b = 0;
    switch (i % 6) {
      case 0:
        r = v;
        g = t;
        b = p;
        break;
      case 1:
        r = q;
        g = v;
        b = p;
        break;
      case 2:
        r = p;
        g = v;
        b = t;
        break;
      case 3:
        r = p;
        g = q;
        b = v;
        break;
      case 4:
        r = t;
        g = p;
        b = v;
        break;
      case 5:
        r = v;
        g = p;
        b = q;
        break;
    }

    return Color{(uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255)};
  }

  /// @brief Convert RGB to HSV
  /// @param c The RGB color
  /// @param h Output: Hue (0.0 - 1.0)
  /// @param s Output: Saturation (0.0 - 1.0)
  /// @param v Output: Value/brightness (0.0 - 1.0)
  static void rgb_to_hsv(const Color &c, float &h, float &s, float &v) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;

    float max_c = std::max({r, g, b});
    float min_c = std::min({r, g, b});
    float delta = max_c - min_c;

    v = max_c;

    if (delta < 0.00001f) {
      s = 0;
      h = 0;
      return;
    }

    s = (max_c > 0.0f) ? (delta / max_c) : 0.0f;

    if (r >= max_c) {
      h = (g - b) / delta;
    } else if (g >= max_c) {
      h = 2.0f + (b - r) / delta;
    } else {
      h = 4.0f + (r - g) / delta;
    }

    h /= 6.0f;
    if (h < 0.0f) h += 1.0f;
  }
};

// Forward declaration for Buffer
class Buffer;

/// @brief Render UTF-8 text to buffer with proper wide character handling
/// @param buffer The buffer to render to
/// @param text The UTF-8 text to render
/// @param start_x Starting X coordinate
/// @param start_y Y coordinate
/// @param max_width Maximum width to render (0 = no limit)
/// @param fg Foreground color
/// @param bg Background color
/// @param bold Bold text
/// @param italic Italic text
/// @param underline Underlined text
/// @return Number of cells consumed
inline int render_utf8_text(Buffer &buffer, const std::string &text,
                            int start_x, int start_y, int max_width, Color fg,
                            Color bg, bool bold = false, bool italic = false,
                            bool underline = false);

/// @brief Defines a color palette for the application
struct Theme {
  // Base
  Color background;  ///< Global background color
  Color foreground;  ///< Default text color

  // Components
  Color panel_bg;      ///< Background for panels/containers
  Color panel_fg;      ///< Text color for panels
  Color border;        ///< Border color
  Color border_focus;  ///< Border color when focused

  // Input
  Color input_bg;  ///< Background for input fields
  Color input_fg;  ///< Text color for input fields

  // Interaction
  Color hover;      ///< Color when hovering over an element
  Color selection;  ///< Color for selected items

  // Accents
  Color primary;    ///< Primary accent color
  Color secondary;  ///< Secondary accent color
  Color success;    ///< Success status color
  Color warning;    ///< Warning status color
  Color error;      ///< Error status color

  // Widget Specifics
  Color scrollbar_track;
  Color scrollbar_thumb;
  Color input_placeholder;

  // Table Specifics
  Color table_header_bg;
  Color table_header_fg;
  Color table_header_bg_focus;
  Color table_header_fg_focus;

  // MenuBar Specifics
  Color menubar_bg;
  Color menubar_fg;

  /// @brief Access the current global theme
  static Theme &current() {
    static Theme instance = Dark();
    return instance;
  }

  /// @brief Set the global theme
  static void set_theme(const Theme &t) { current() = t; }

  /// @brief Default Dark Theme (Catppuccin Mocha)
  /// Soothing pastel dark theme - https://catppuccin.com
  static Theme Dark() {
    Theme t;
    t.background = {30, 30, 46};       // Base #1E1E2E
    t.foreground = {205, 214, 244};    // Text #CDD6F4
    t.panel_bg = {49, 50, 68};         // Surface0 #313244
    t.panel_fg = {205, 214, 244};      // Text #CDD6F4
    t.border = {69, 71, 90};           // Surface1 #45475A
    t.border_focus = {137, 180, 250};  // Blue #89B4FA

    t.input_bg = {49, 50, 68};     // Surface0 #313244
    t.input_fg = {205, 214, 244};  // Text #CDD6F4

    t.hover = {69, 71, 90};       // Surface1 #45475A
    t.selection = {88, 91, 112};  // Surface2 #585B70

    t.primary = {137, 180, 250};    // Blue #89B4FA
    t.secondary = {203, 166, 247};  // Mauve #CBA6F7
    t.success = {166, 227, 161};    // Green #A6E3A1
    t.warning = {249, 226, 175};    // Yellow #F9E2AF
    t.error = {243, 139, 168};      // Red #F38BA8

    t.scrollbar_track = {24, 24, 37};       // Mantle #181825
    t.scrollbar_thumb = {108, 112, 134};    // Overlay0 #6C7086
    t.input_placeholder = {108, 112, 134};  // Overlay0 #6C7086

    t.table_header_bg = t.border;
    t.table_header_fg = t.foreground;
    t.table_header_bg_focus = t.border_focus;
    t.table_header_fg_focus = t.background;

    t.menubar_bg = {24, 24, 37};  // Mantle #181825
    t.menubar_fg = t.foreground;
    return t;
  }

  /// @brief Default Light Theme (Catppuccin Latte)
  /// Soothing pastel light theme - https://catppuccin.com
  static Theme Light() {
    Theme t;
    t.background = {239, 241, 245};   // Base #EFF1F5
    t.foreground = {76, 79, 105};     // Text #4C4F69
    t.panel_bg = {230, 233, 239};     // Mantle #E6E9EF
    t.panel_fg = {76, 79, 105};       // Text #4C4F69
    t.border = {188, 192, 204};       // Surface1 #BCC0CC
    t.border_focus = {30, 102, 245};  // Blue #1E66F5

    t.input_bg = {204, 208, 218};  // Surface0 #CCD0DA
    t.input_fg = {76, 79, 105};    // Text #4C4F69

    t.hover = {188, 192, 204};      // Surface1 #BCC0CC
    t.selection = {114, 135, 253};  // Lavender #7287FD

    t.primary = {30, 102, 245};    // Blue #1E66F5
    t.secondary = {136, 57, 239};  // Mauve #8839EF
    t.success = {64, 160, 43};     // Green #40A02B
    t.warning = {223, 142, 29};    // Yellow #DF8E1D
    t.error = {210, 15, 57};       // Red #D20F39

    t.scrollbar_track = {220, 224, 232};    // Crust #DCE0E8
    t.scrollbar_thumb = {156, 160, 176};    // Overlay0 #9CA0B0
    t.input_placeholder = {140, 143, 161};  // Overlay1 #8C8FA1

    t.table_header_bg = t.border;
    t.table_header_fg = t.foreground;
    t.table_header_bg_focus = t.border_focus;
    t.table_header_fg_focus = t.background;

    t.menubar_bg = {220, 224, 232};  // Crust #DCE0E8
    t.menubar_fg = t.foreground;
    return t;
  }

  /// @brief Nord Theme - Arctic, north-bluish color palette
  /// Clean and elegant design - https://nordtheme.com
  static Theme Nord() {
    Theme t;
    t.background = {46, 52, 64};       // Nord0 #2E3440
    t.foreground = {216, 222, 233};    // Nord4 #D8DEE9
    t.panel_bg = {59, 66, 82};         // Nord1 #3B4252
    t.panel_fg = {216, 222, 233};      // Nord4 #D8DEE9
    t.border = {67, 76, 94};           // Nord2 #434C5E
    t.border_focus = {136, 192, 208};  // Nord8 #88C0D0

    t.input_bg = {59, 66, 82};     // Nord1 #3B4252
    t.input_fg = {229, 233, 240};  // Nord5 #E5E9F0

    t.hover = {67, 76, 94};       // Nord2 #434C5E
    t.selection = {76, 86, 106};  // Nord3 #4C566A

    t.primary = {136, 192, 208};    // Nord8 #88C0D0
    t.secondary = {180, 142, 173};  // Nord15 #B48EAD
    t.success = {163, 190, 140};    // Nord14 #A3BE8C
    t.warning = {235, 203, 139};    // Nord13 #EBCB8B
    t.error = {191, 97, 106};       // Nord11 #BF616A

    t.scrollbar_track = {46, 52, 64};     // Nord0 #2E3440
    t.scrollbar_thumb = {76, 86, 106};    // Nord3 #4C566A
    t.input_placeholder = {76, 86, 106};  // Nord3 #4C566A

    t.table_header_bg = t.border;
    t.table_header_fg = t.foreground;
    t.table_header_bg_focus = t.border_focus;
    t.table_header_fg_focus = t.background;

    t.menubar_bg = {46, 52, 64};  // Nord0 #2E3440
    t.menubar_fg = t.foreground;
    return t;
  }

  /// @brief Tokyo Night Theme - Dark theme inspired by Tokyo's nightscape
  /// Modern aesthetic with vibrant accents
  static Theme TokyoNight() {
    Theme t;
    t.background = {26, 27, 38};       // #1A1B26
    t.foreground = {169, 177, 214};    // #A9B1D6
    t.panel_bg = {36, 40, 59};         // #24283B
    t.panel_fg = {169, 177, 214};      // #A9B1D6
    t.border = {41, 46, 66};           // #292E42
    t.border_focus = {122, 162, 247};  // #7AA2F7

    t.input_bg = {36, 40, 59};     // #24283B
    t.input_fg = {189, 199, 240};  // #BDC7F0

    t.hover = {41, 46, 66};      // #292E42
    t.selection = {51, 59, 81};  // #333B51

    t.primary = {122, 162, 247};    // #7AA2F7
    t.secondary = {187, 154, 247};  // #BB9AF7
    t.success = {158, 206, 106};    // #9ECE6A
    t.warning = {224, 175, 104};    // #E0AF68
    t.error = {247, 118, 142};      // #F7768E

    t.scrollbar_track = {26, 27, 38};     // #1A1B26
    t.scrollbar_thumb = {65, 72, 104};    // #414868
    t.input_placeholder = {65, 72, 104};  // #414868

    t.table_header_bg = t.border;
    t.table_header_fg = t.foreground;
    t.table_header_bg_focus = t.border_focus;
    t.table_header_fg_focus = t.background;

    t.menubar_bg = {26, 27, 38};  // #1A1B26
    t.menubar_fg = t.foreground;
    return t;
  }

  /// @brief Solarized Light Theme - Classic scientific color palette
  /// Designed by Ethan Schoonover - https://ethanschoonover.com/solarized
  static Theme SolarizedLight() {
    Theme t;
    t.background = {253, 246, 227};   // Base3 #FDF6E3
    t.foreground = {101, 123, 131};   // Base00 #657B83
    t.panel_bg = {238, 232, 213};     // Base2 #EEE8D5
    t.panel_fg = {88, 110, 117};      // Base01 #586E75
    t.border = {147, 161, 161};       // Base1 #93A1A1
    t.border_focus = {38, 139, 210};  // Blue #268BD2

    t.input_bg = {238, 232, 213};  // Base2 #EEE8D5
    t.input_fg = {88, 110, 117};   // Base01 #586E75

    t.hover = {238, 232, 213};     // Base2 #EEE8D5
    t.selection = {38, 139, 210};  // Blue #268BD2

    t.primary = {38, 139, 210};     // Blue #268BD2
    t.secondary = {108, 113, 196};  // Violet #6C71C4
    t.success = {133, 153, 0};      // Green #859900
    t.warning = {181, 137, 0};      // Yellow #B58900
    t.error = {220, 50, 47};        // Red #DC322F

    t.scrollbar_track = {238, 232, 213};    // Base2 #EEE8D5
    t.scrollbar_thumb = {147, 161, 161};    // Base1 #93A1A1
    t.input_placeholder = {147, 161, 161};  // Base1 #93A1A1

    t.table_header_bg = t.border;
    t.table_header_fg = t.foreground;
    t.table_header_bg_focus = t.border_focus;
    t.table_header_fg_focus = t.background;

    t.menubar_bg = {238, 232, 213};  // Base2 #EEE8D5
    t.menubar_fg = t.foreground;
    return t;
  }
};

/// @brief A single terminal cell containing a character and styling attributes
struct Cell {
  std::string content = " ";  // UTF-8 supported by using string
  Color fg_color;
  Color bg_color;
  bool bold = false;       ///< Render text in bold
  bool italic = false;     ///< Render text in italics
  bool underline = false;  ///< Render text with underline

  bool operator==(const Cell &other) const {
    return content == other.content && fg_color == other.fg_color &&
           bg_color == other.bg_color && bold == other.bold &&
           italic == other.italic && underline == other.underline;
  }
  bool operator!=(const Cell &other) const { return !(*this == other); }
};

/// @brief A 2D grid of cells representing the terminal screen with clipping
/// support
class Buffer {
 public:
  /// @brief Construct a new Buffer with given dimensions
  /// @param width Width of the buffer
  /// @param height Height of the buffer
  Buffer(int width, int height) : width_(width), height_(height) {
    cells_.resize(height * width);
    // Default clip is full buffer
    clip_stack_.push_back({0, 0, width, height});
  }

  /// @brief Resize the buffer, clearing current content
  /// @param width New width
  /// @param height New height
  void resize(int width, int height) {
    width_ = width;
    height_ = height;
    cells_.assign(width * height, Cell{});
    clip_stack_.clear();
    clip_stack_.push_back({0, 0, width, height});
  }

  /// @brief Push a new clipping rectangle onto the stack
  /// The new clip region is the intersection of the current clip and the new
  /// rect.
  /// @param r The new clipping rectangle
  void push_clip(Rect r) {
    if (clip_stack_.empty()) {
      clip_stack_.push_back(r);
    } else {
      clip_stack_.push_back(clip_stack_.back().intersect(r));
    }
  }

  /// @brief Pop the last clipping rectangle from the stack
  void pop_clip() {
    if (clip_stack_.size() > 1) {  // Always keep root clip
      clip_stack_.pop_back();
    }
  }

  /// @brief Push full buffer size as clip, bypassing parent clipping.
  /// Useful for overlay widgets like Notification that need to render at screen
  /// coordinates.
  void push_full_clip() { clip_stack_.push_back({0, 0, width_, height_}); }

  /// @brief Get the current active clipping rectangle
  /// @return The current clipping Rect
  Rect current_clip() const {
    if (clip_stack_.empty()) return {0, 0, width_, height_};
    return clip_stack_.back();
  }

  /// @brief Set a cell at a specific location, respecting the clipping region
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @param cell The cell data to set
  void set(int x, int y, const Cell &cell) {
    // Apply clipping
    Rect c = current_clip();
    if (x >= c.x && x < c.x + c.width && y >= c.y && y < c.y + c.height) {
      if (x >= 0 && x < width_ && y >= 0 && y < height_) {
        cells_[y * width_ + x] = cell;
      }
    }
  }

  /// @brief Set just the character content of a cell, preserving style
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @param ch The UTF-8 character string
  void set_char(int x, int y, const std::string &ch) {
    Rect c = current_clip();
    if (x >= c.x && x < c.x + c.width && y >= c.y && y < c.y + c.height) {
      if (x >= 0 && x < width_ && y >= 0 && y < height_) {
        cells_[y * width_ + x].content = ch;
      }
    }
  }

  /// @brief Get the cell at a specific location
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @return Const reference to the cell (or static empty cell if out of
  /// bounds)
  const Cell &get(int x, int y) const {
    static Cell empty;
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
      return cells_[y * width_ + x];
    }
    return empty;
  }

  /// @brief Get the width of the buffer
  int width() const { return width_; }
  /// @brief Get the height of the buffer
  int height() const { return height_; }

  /// @brief Clear the entire buffer with a specific cell
  /// @param fill_cell The cell to fill with (default is empty/default style)
  void clear(const Cell &fill_cell = Cell{}) {
    std::fill(cells_.begin(), cells_.end(), fill_cell);
  }

 private:
  int width_ = 0;
  int height_ = 0;
  std::vector<Cell> cells_;
  std::vector<Rect> clip_stack_;
};

/// @brief Implementation of render_utf8_text helper
inline int render_utf8_text(Buffer &buffer, const std::string &text,
                            int start_x, int start_y, int max_width, Color fg,
                            Color bg, bool bold, bool italic, bool underline) {
  int cell_x = 0;
  size_t pos = 0;

  while (pos < text.size()) {
    // Check width limit
    if (max_width > 0 && cell_x >= max_width) break;

    uint32_t codepoint;
    int byte_len;
    if (utf8_decode_codepoint(text, pos, codepoint, byte_len)) {
      Cell c;
      c.content = text.substr(pos, byte_len);
      c.fg_color = fg;
      c.bg_color = bg;
      c.bold = bold;
      c.italic = italic;
      c.underline = underline;
      buffer.set(start_x + cell_x, start_y, c);

      int dw = char_display_width(codepoint);
      // Handle wide characters (CJK, emoji) by placing empty cell in next
      // position
      if (dw == 2 && (max_width == 0 || cell_x + 1 < max_width)) {
        Cell skip;
        skip.content = "";
        skip.bg_color = bg;
        buffer.set(start_x + cell_x + 1, start_y, skip);
      }
      cell_x += (dw > 0 ? dw : 1);
      pos += byte_len;
    } else {
      pos++;  // Skip invalid byte
    }
  }
  return cell_x;
}

/**
 * @brief Helper to render a scrollbar (vertical or horizontal)
 */
inline void render_scrollbar(Buffer &buffer, int x, int y, int length,
                             int offset, int content_length, bool vertical,
                             Color track_color = Color(),
                             Color thumb_color = Color(),
                             std::string track_char = "",
                             std::string thumb_char = "") {
  if (content_length <= length || length <= 0) return;

  int max_scroll = content_length - length;
  int thumb_size = std::max(1, length * length / content_length);
  int thumb_pos = (int)((float)offset / max_scroll * (length - thumb_size));

  Color track = track_color.resolve(Theme::current().scrollbar_track);
  Color thumb = thumb_color.resolve(Theme::current().scrollbar_thumb);

  if (track_char.empty()) track_char = vertical ? " " : "\u2581";
  if (thumb_char.empty()) thumb_char = vertical ? " " : "\u2584";

  for (int i = 0; i < length; ++i) {
    Cell c;
    bool is_thumb = (i >= thumb_pos && i < thumb_pos + thumb_size);

    if (vertical) {
      c.content = is_thumb ? thumb_char : track_char;
      if (is_thumb) {
        c.bg_color = thumb;
        c.fg_color = thumb;  // Ensure solid appearance
      } else {
        c.bg_color = track;
        c.fg_color = track;  // Ensure solid appearance
      }
      buffer.set(x, y + i, c);
    } else {
      c.content = is_thumb ? thumb_char : track_char;
      c.fg_color = is_thumb ? thumb : track;
      c.bg_color = Theme::current().panel_bg;
      buffer.set(x + i, y, c);
    }
  }
}

/// @brief Shared handler for scrollbar events (click, drag, wheel)
/// @return true if event handled (scroll changed or interaction occurred)
inline bool handle_scrollbar_event(
    const Event &event, int x, int y, int width, int height, int content_size,
    int &scroll_offset, bool &is_dragging, bool horizontal,
    std::function<void()> on_interact = nullptr) {
  int view_size = horizontal ? width : height;
  if (content_size <= view_size) return false;

  // 1. Wheel Support
  if (event.mouse_wheel()) {
    if (event.x >= x && event.x < x + width && event.y >= y &&
        event.y < y + height) {
      bool handled = false;
      if (horizontal) {
        if (event.mouse_wheel_up()) {
          scroll_offset--;
          handled = true;
        }  // Left
        else if (event.mouse_wheel_down()) {
          scroll_offset++;
          handled = true;
        }  // Right
      } else {
        if (event.mouse_wheel_up()) {
          scroll_offset--;
          handled = true;
        }  // Up
        else if (event.mouse_wheel_down()) {
          scroll_offset++;
          handled = true;
        }  // Down
      }

      if (handled) {
        if (on_interact) on_interact();
        // Clamp immediately
        int max_scroll = std::max(0, content_size - view_size);
        if (scroll_offset < 0) scroll_offset = 0;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
        return true;
      }
    }
  }

  // 2. Click / Drag
  int clamp_start = horizontal ? x : y;
  int sb_pos_main = horizontal
                        ? y + height - 1
                        : x + width - 1;  // Standard position (bottom or right)

  if (event.mouse_release()) {
    is_dragging = false;
  }

  if (event.mouse_left() || event.mouse_drag()) {
    bool on_scrollbar = false;
    // Tolerance: allow clicking specific scrollbar areas
    if (horizontal) {
      // Horizontal bar at bottom
      on_scrollbar = ((event.y == sb_pos_main || event.y == sb_pos_main - 1) &&
                      event.x >= x && event.x < x + width);
    } else {
      // Vertical bar at right
      on_scrollbar = ((event.x == sb_pos_main || event.x == sb_pos_main - 1) &&
                      event.y >= y && event.y < y + height);
    }

    if (on_scrollbar || is_dragging) {
      if (on_scrollbar && event.mouse_left()) {
        is_dragging = true;
        if (on_interact) on_interact();
      }

      if (is_dragging) {
        int mouse_val = horizontal ? event.x : event.y;
        int rel_pos = mouse_val - clamp_start;
        int max_scroll = std::max(0, content_size - view_size);

        if (max_scroll > 0 && view_size > 1) {
          if (rel_pos < 0) rel_pos = 0;
          if (rel_pos >= view_size) rel_pos = view_size - 1;

          scroll_offset = (int)((float)rel_pos / (view_size - 1) * max_scroll);
          return true;
        }
      }
    }
  }
  return false;
}

/// @brief Parser for VT/ANSI escape sequences handling keyboard and mouse input
class VTParser {
 public:
  /// @brief Check if the parser is currently processing an escape sequence
  /// @return true if inside a sequence, false otherwise
  bool in_sequence() const { return state_ != State::Start; }

  /// @brief Process a single byte of input
  /// @param c The character to process
  /// @return An Event if a complete sequence or key was parsed, otherwise
  /// EventType::None
  Event process(char c) {
    Event event;
    event.type = EventType::None;

    // buffer management
    if (state_ == State::Start) {
      if (c == 27) {  // ESC
        state_ = State::Escape;
        buffer_.clear();
        buffer_.push_back(c);
      } else {
        // Regular key
        event.type = EventType::Key;
        event.key = c;

        // Handle Ctrl+Space (produces NUL = 0 on Unix)
        if (c == 0) {
          event.ctrl = true;
          event.key = ' ';
        }
        // Decode Ctrl+Char (1-26) if checks pass
        else if (c >= 1 && c <= 26 && c != 8 && c != 9 && c != 10 && c != 13 &&
                 c != 27) {
          event.ctrl = true;
          event.key = c + 96;
        }
      }
    } else if (state_ == State::Escape) {
      buffer_.push_back(c);
      if (c == '[') {
        state_ = State::CSI;
      } else if (c == 'O') {
        state_ = State::SS3;
      } else {
        // Alt + Key
        state_ = State::Start;
        event.type = EventType::Key;
        event.key = c;
        event.alt = true;
      }
    } else if (state_ == State::SS3) {
      // SS3 sequences like \033OP (F1)
      buffer_.push_back(c);
      state_ = State::Start;  // simplified, usually just one char
      event.type = EventType::Key;
      // Map F-keys (basic mapping)
      // O + P = F1, Q=F2, R=F3, S=F4
      if (c == 'P')
        event.key = 1011;  // F1
      else if (c == 'Q')
        event.key = 1012;  // F2
      else if (c == 'R')
        event.key = 1013;  // F3
      else if (c == 'S')
        event.key = 1014;  // F4
    } else if (state_ == State::CSI) {
      buffer_.push_back(c);
      if (c == 'M') {
        // X10 Mouse \033[M...
        state_ = State::MouseX10;
        byte_counter_ = 0;
      } else if (c == '<') {
        // SGR Mouse \033[<...
        state_ = State::MouseSGR;
      } else if (isdigit(c) || c == ';') {
        // Parameter bytes
      } else {
        // Final byte
        state_ = State::Start;
        return parseCSI(buffer_);
      }
    } else if (state_ == State::MouseX10) {
      buffer_.push_back(c);
      byte_counter_++;
      if (byte_counter_ == 3) {
        state_ = State::Start;
        return parseX10(buffer_);
      }
    } else if (state_ == State::MouseSGR) {
      buffer_.push_back(c);
      if (c == 'M' || c == 'm') {
        state_ = State::Start;
        return parseSGR(buffer_);
      }
    } else if (state_ == State::BracketedPaste) {
      // Collect paste content until we see ESC
      if (c == 27) {  // ESC - might be end of paste
        paste_escape_buffer_.clear();
        paste_escape_buffer_.push_back(c);
        paste_escape_state_ = 1;
      } else if (paste_escape_state_ == 1 && c == '[') {
        paste_escape_buffer_.push_back(c);
        paste_escape_state_ = 2;
      } else if (paste_escape_state_ == 2 && c == '2') {
        paste_escape_buffer_.push_back(c);
        paste_escape_state_ = 3;
      } else if (paste_escape_state_ == 3 && c == '0') {
        paste_escape_buffer_.push_back(c);
        paste_escape_state_ = 4;
      } else if (paste_escape_state_ == 4 && c == '1') {
        paste_escape_buffer_.push_back(c);
        paste_escape_state_ = 5;
      } else if (paste_escape_state_ == 5 && c == '~') {
        // End of bracketed paste: \e[201~
        state_ = State::Start;
        paste_escape_state_ = 0;
        event.type = EventType::Paste;
        event.paste_text = paste_buffer_;
        paste_buffer_.clear();
        paste_escape_buffer_.clear();
        return event;
      } else {
        // Not the end sequence - add any partial escape to paste buffer
        if (paste_escape_state_ > 0) {
          paste_buffer_ += paste_escape_buffer_;
          paste_escape_buffer_.clear();
          paste_escape_state_ = 0;
        }
        paste_buffer_.push_back(c);
      }
    }

    return event;
  }

 private:
  enum class State {
    Start,
    Escape,
    CSI,
    SS3,
    MouseX10,
    MouseSGR,
    BracketedPaste  ///< Collecting pasted text between \e[200~ and \e[201~
  };
  State state_ = State::Start;
  std::string buffer_;
  std::string paste_buffer_;  ///< Buffer for bracketed paste content
  std::string
      paste_escape_buffer_;     ///< Buffer for detecting paste end sequence
  int paste_escape_state_ = 0;  ///< State for detecting \e[201~ end sequence
  int byte_counter_ = 0;

  Event parseCSI(const std::string &seq) {
    Event event;
    event.type = EventType::Key;

    // Format: ESC [ p1 ; p2 cmd
    // strip ESC [
    std::string body = seq.substr(2);
    char cmd = body.back();
    body.pop_back();

    int p1 = 1;
    int p2 = 1;

    if (!body.empty()) {
      size_t semi = body.find(';');
      if (semi != std::string::npos) {
        try {
          p1 = std::stoi(body.substr(0, semi));
        } catch (...) {
        }
        try {
          p2 = std::stoi(body.substr(semi + 1));
        } catch (...) {
        }
      } else {
        try {
          p1 = std::stoi(body);
        } catch (...) {
        }
      }
    }

    // Map Modifiers (p2)
    // 1=None, 2=Shift, 3=Alt, 4=Shift+Alt, 5=Ctrl, 6=Shift+Ctrl, 7=Alt+Ctrl,
    // 8=All This is roughly: Shift(+1), Alt(+2), Ctrl(+4) to a base of 1.
    int mods = p2 - 1;
    if (mods & 1) event.shift = true;
    if (mods & 2) event.alt = true;
    if (mods & 4) event.ctrl = true;

    switch (cmd) {
      case 'A':
        event.key = 1065;
        break;  // Up (was 65)
      case 'B':
        event.key = 1066;
        break;  // Down (was 66)
      case 'C':
        event.key = 1067;
        break;  // Right (was 67)
      case 'D':
        event.key = 1068;
        break;  // Left (was 68)
      case 'Z':
        event.key = 9;
        event.shift = true;
        break;  // Shift+Tab
      case 'H':
        event.key = 1003;
        break;  // Home
      case 'F':
        event.key = 1004;
        break;  // End
      case '~':
        if (p1 == 5)
          event.key = 1001;  // PageUp
        else if (p1 == 6)
          event.key = 1002;  // PageDown
        else if (p1 == 3)
          event.key = 1005;  // Delete
        else if (p1 == 2)
          event.key = 1006;  // Insert
        else if (p1 == 200) {
          // Bracketed Paste Start \e[200~
          state_ = State::BracketedPaste;
          paste_buffer_.clear();
          paste_escape_buffer_.clear();
          paste_escape_state_ = 0;
          event.type = EventType::None;  // No event yet, waiting for content
        }
        break;
      default:
        event.key = 0;
        break;
    }
    return event;
  }

  Event parseX10(const std::string &seq) {
    // \033[M b x y
    // b = button+32, x=x+32, y=y+32
    Event event;
    event.type = EventType::Mouse;
    if (seq.size() < 6) return event;

    unsigned char b = (unsigned char)seq[3] - 32;
    unsigned char x = (unsigned char)seq[4] - 32;
    unsigned char y = (unsigned char)seq[5] - 32;

    event.x = x - 1;
    event.y = y - 1;
    // X10 doesn't support 1006 SGR extra bits well, but typically:
    event.button = b & 3;
    if (b & 64) {  // Wheel
      event.button = (b & 65) == 64 ? 64 : 65;
    }
    return event;
  }

  Event parseSGR(const std::string &seq) {
    // \033[< b ; x ; y M (or m)
    Event event;
    event.type = EventType::Mouse;

    size_t start = 3;  // ESC [ <
    size_t end = seq.size() - 1;
    char type = seq.back();  // M or m

    std::string body = seq.substr(start, end - start);
    int b = 0, x = 0, y = 0;
    sscanf(body.c_str(), "%d;%d;%d", &b, &x, &y);

    event.x = x - 1;
    event.y = y - 1;

    event.shift = (b & 4) != 0;
    event.alt = (b & 8) != 0;
    event.ctrl = (b & 16) != 0;

    if (type == 'm') {
      event.button = 3;  // Release
    } else {
      event.button = b & ~(4 | 8 | 16);  // Strip mods
    }
    return event;
  }
};

/// @brief Low-level terminal I/O handling raw mode, rendering, and event
/// polling
class Terminal {
 public:
  /// @brief Check if the terminal environment supports UTF-8
  /// @return true if UTF-8 is supported
  static bool has_utf8() {
#ifdef _WIN32
    return GetConsoleOutputCP() == 65001;
#else
    const char *lang = std::getenv("LANG");
    const char *lc_all = std::getenv("LC_ALL");
    const char *lc_ctype = std::getenv("LC_CTYPE");

    auto check = [](const char *s) {
      if (!s) return false;
      std::string str(s);
      return str.find("UTF-8") != std::string::npos ||
             str.find("utf8") != std::string::npos;
    };

    if (check(lc_all)) return true;
    if (check(lc_ctype)) return true;
    if (check(lang)) return true;

    return false;
#endif
  }

  /// @brief Construct the terminal interface and enable raw mode
  Terminal() { enableRawMode(); }

  /// @brief Destructor restores the terminal to canonical mode
  ~Terminal() { disableRawMode(); }

  /// @brief Enable raw mode, allowing direct input processing and disabling
  /// echo
  void enableRawMode() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    originalOutMode = dwMode;
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);

    originalOutCP = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    // Verify VT output support

    bool vt_out = false;

    DWORD outModeCheck = 0;
    GetConsoleMode(hOut, &outModeCheck);
    if (outModeCheck & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
      vt_out = true;
    }

    // Fallback: check ConEmu environment variables
    if (!vt_out) {
      if (std::getenv("ConEmuBuild") || std::getenv("ConEmuANSI")) {
        vt_out = true;
      }
    }

    if (vt_out) {
      vt_supported_ = true;
      // Request Mouse Reporting (1003 = Any Event/Motion, 1006 = SGR)
      // Request Bracketed Paste Mode (2004)
      write("\033[?1003h\033[?1006h\033[?2004h");
    }

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &dwMode);
    originalInMode = dwMode;

    // Configure input mode

    dwMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE |
                ENABLE_PROCESSED_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT);
    dwMode |= ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT;

    if (SetConsoleMode(
            hIn, dwMode | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT)) {
      vt_input_supported_ = true;
      dwMode |= ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT;
    } else {
      // Fallback to legacy mouse input
      dwMode |= ENABLE_MOUSE_INPUT;
      vt_input_supported_ = false;
    }
    SetConsoleMode(hIn, dwMode);

    // Handle Ctrl+C cleanup
    SetConsoleCtrlHandler(
        [](DWORD fdwCtrlType) -> BOOL {
          switch (fdwCtrlType) {
            case CTRL_C_EVENT:
            case CTRL_CLOSE_EVENT:

            {
              HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
              HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

              std::cout << "\033[?25h";    // Show cursor
              std::cout << "\033[?1049l";  // Disable alternate buffer
            }
              return FALSE;  // Let default handler call ExitProcess
            default:
              return FALSE;
          }
        },
        TRUE);

    originalInCP = GetConsoleCP();
    SetConsoleCP(CP_UTF8);

    // Create wakeup event for cross-thread update()/post()
    if (!g_wakeup_event)
      g_wakeup_event = CreateEvent(NULL, FALSE, FALSE, NULL);  // Auto-reset
#else
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    originalTermios = raw;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN |
                     ISIG);  // Disable ISIG to allow Ctrl+Z as raw input
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
#ifdef IUTF8
    raw.c_iflag |= IUTF8;
#endif
    raw.c_cflag |= (CS8);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    signal(SIGWINCH, handle_winch);

    // Create wakeup pipe for cross-thread update()/post()
    if (g_wakeup_pipe[0] == -1) {
      if (pipe(g_wakeup_pipe) == 0) {
        fcntl(g_wakeup_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(g_wakeup_pipe[1], F_SETFL, O_NONBLOCK);
      }
    }
    write("\033[?1003h\033[?1006h\033[?2004h");  // Enable mouse reporting (all
                                                 // motion) + SGR + Bracketed
                                                 // Paste
#endif
    write("\033[?1049h");  // Enable alternate screen buffer
    hideCursor();          // Hide cursor
  }

  /// @brief Restore the terminal to its original state (canonical mode)
  void disableRawMode() {
    showCursor();          // Show cursor
    write("\033[?1049l");  // Disable alternate screen buffer
#ifdef _WIN32
    if (vt_supported_) {
      write("\033[?1003l\033[?1006l\033[?2004l");
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleMode(hOut, originalOutMode);
    SetConsoleOutputCP(originalOutCP);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hIn, originalInMode | ENABLE_EXTENDED_FLAGS);
    SetConsoleCP(originalInCP);

    // Close wakeup event
    if (g_wakeup_event) {
      CloseHandle(g_wakeup_event);
      g_wakeup_event = NULL;
    }
#else
    write("\033[?1003l\033[?1006l\033[?2004l");  // Disable mouse and bracketed
                                                 // paste
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios);

    // Close wakeup pipe
    if (g_wakeup_pipe[0] != -1) {
      close(g_wakeup_pipe[0]);
      close(g_wakeup_pipe[1]);
      g_wakeup_pipe[0] = -1;
      g_wakeup_pipe[1] = -1;
    }
#endif
  }

  /// @brief Write raw string data to the terminal output
  /// @param data The string to write
  void write(const std::string &data) { std::cout << data; }

  /// @brief Flush the output stream
  void flush() { std::cout.flush(); }

  /// @brief Move the cursor to a specific position
  /// @param x X coordinate (0-based)
  /// @param y Y coordinate (0-based)
  void moveCursor(int x, int y) {
    std::stringstream ss;
    ss << "\033[" << (y + 1) << ";" << (x + 1) << "H";
    write(ss.str());
  }

  /// @brief Hide the terminal cursor
  void hideCursor() { write("\033[?25l"); }
  /// @brief Show the terminal cursor
  void showCursor() { write("\033[?25h"); }
  /// @brief Clear the entire screen
  void clearScreen() { write("\033[2J"); }

  /// @brief Drain any stale events from the input buffer
  /// Call this after initialization to clear spurious events (e.g., resize from
  /// alternate buffer switch)
  void drainInputBuffer() {
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD nEvents = 0;
    while (true) {
      GetNumberOfConsoleInputEvents(hIn, &nEvents);
      if (nEvents == 0) break;
      INPUT_RECORD ir;
      DWORD read;
      ReadConsoleInput(hIn, &ir, 1, &read);
    }
#else
    // On Unix, drain any buffered stdin
    fd_set fds;
    struct timeval tv = {0, 0};
    char c;
    while (true) {
      FD_ZERO(&fds);
      FD_SET(STDIN_FILENO, &fds);
      if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) break;
      if (read(STDIN_FILENO, &c, 1) <= 0) break;
    }
#endif
  }

  /// @brief Get the current terminal size
  /// @return A pair containing {width, height}
  static std::pair<int, int> getSize() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return {csbi.srWindow.Right - csbi.srWindow.Left + 1,
            csbi.srWindow.Bottom - csbi.srWindow.Top + 1};
#else
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return {w.ws_col, w.ws_row};
#endif
  }

  /// @brief Render buffer differences using batched output for optimal
  /// performance
  void render(const Buffer &current, const Buffer &previous) {
    std::string output;
    output.reserve(current.width() * current.height() * 30);

    // Track render state to emit only changed attributes
    Color cur_fg;
    Color cur_bg;
    bool cur_bold = false;
    bool cur_italic = false;
    bool cur_underline = false;
    int cur_x = -1;
    int cur_y = -1;

    // Append cursor move (CSI row;col H)
    auto appendCursorMove = [&output](int x, int y) {
      output += "\033[";
      output += std::to_string(y + 1);
      output += ";";
      output += std::to_string(x + 1);
      output += "H";
    };

    // Append foreground color (SGR 38;2;r;g;b)
    auto appendFgColor = [&output](const Color &c) {
      if (!c.is_default) {
        output += "\033[38;2;";
        output += std::to_string(c.r);
        output += ";";
        output += std::to_string(c.g);
        output += ";";
        output += std::to_string(c.b);
        output += "m";
      } else {
        output += "\033[39m";
      }
    };

    // Append background color (SGR 48;2;r;g;b)
    auto appendBgColor = [&output](const Color &c) {
      if (!c.is_default) {
        output += "\033[48;2;";
        output += std::to_string(c.r);
        output += ";";
        output += std::to_string(c.g);
        output += ";";
        output += std::to_string(c.b);
        output += "m";
      } else {
        output += "\033[49m";
      }
    };

    for (int y = 0; y < current.height(); ++y) {
      for (int x = 0; x < current.width(); ++x) {
        const Cell &currCell = current.get(x, y);
        const Cell &prevCell = previous.get(x, y);

        if (currCell != prevCell) {
          // Position cursor (skip if already there from previous write)
          if (cur_x != x || cur_y != y) {
            appendCursorMove(x, y);
          }

          // Foreground
          if (currCell.fg_color != cur_fg) {
            appendFgColor(currCell.fg_color);
            cur_fg = currCell.fg_color;
          }

          // Background
          if (currCell.bg_color != cur_bg) {
            appendBgColor(currCell.bg_color);
            cur_bg = currCell.bg_color;
          }

          // Style attributes
          if (currCell.bold != cur_bold) {
            output += currCell.bold ? "\033[1m" : "\033[22m";
            cur_bold = currCell.bold;
          }
          if (currCell.italic != cur_italic) {
            output += currCell.italic ? "\033[3m" : "\033[23m";
            cur_italic = currCell.italic;
          }
          if (currCell.underline != cur_underline) {
            output += currCell.underline ? "\033[4m" : "\033[24m";
            cur_underline = currCell.underline;
          }

          // Content
          output += currCell.content;

          // Calculate display width for proper cursor tracking
          int display_width = utf8_display_width(currCell.content);
          if (display_width < 1) display_width = 1;

          // Cursor advances by display width after write
          cur_x = x + display_width;
          cur_y = y;

          // Skip cells occupied by wide character
          if (display_width > 1) {
            x += display_width - 1;
          }
        }
      }
    }

    output += "\033[0m";  // Reset attributes

    // Single write and flush for entire frame
    write(output);
    flush();
  }

 private:
  bool vt_input_supported_ = false;

 public:
  /// @brief Check if a key has been pressed
  /// @return true if input is available
  bool kbhit() {
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (vt_input_supported_) {
      DWORD avail = 0;
      if (!PeekNamedPipe(hIn, NULL, 0, NULL, &avail, NULL)) return false;
      return avail > 0;
    }
    DWORD n;
    GetNumberOfConsoleInputEvents(hIn, &n);
    return n > 0;
#else
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval timeout = {0, 0};
    return select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) > 0;
#endif
  }

  /// @brief Read an event from input
  /// @param timeout_ms Maximum time to wait in milliseconds (-1 for infinite)
  /// @return The read Event (or EventType::None if timeout)
  Event readRawEvent(int timeout_ms = -1) {
    Event event;
    event.type = EventType::None;

#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    // VT Input Path - VT sequences arrive as KEY_EVENT records
    if (vt_input_supported_) {
      // Wait for input or wakeup event
      if (timeout_ms != 0) {
        DWORD waitRes;
        DWORD timeout_val = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
        if (g_wakeup_event) {
          HANDLE handles[2] = {hIn, g_wakeup_event};
          waitRes = WaitForMultipleObjects(2, handles, FALSE, timeout_val);
          if (waitRes == WAIT_OBJECT_0 + 1) {
            // Wakeup event signaled
            event.type = EventType::Wakeup;
            return event;
          }
        } else {
          waitRes = WaitForSingleObject(hIn, timeout_val);
        }
        if (waitRes == WAIT_TIMEOUT) return event;
      }

      DWORD nEvents = 0;
      GetNumberOfConsoleInputEvents(hIn, &nEvents);

      while (nEvents > 0) {
        INPUT_RECORD ir[1];
        DWORD read;
        if (!ReadConsoleInput(hIn, ir, 1, &read) || read == 0) break;

        if (ir[0].EventType == KEY_EVENT && ir[0].Event.KeyEvent.bKeyDown) {
          char ch = ir[0].Event.KeyEvent.uChar.AsciiChar;
          if (ch != 0) {
            event = parser_.process(ch);

            // Continue reading if we're mid-sequence
            while (event.type == EventType::None && parser_.in_sequence()) {
              GetNumberOfConsoleInputEvents(hIn, &nEvents);
              if (nEvents == 0) {
                WaitForSingleObject(hIn, 20);
                GetNumberOfConsoleInputEvents(hIn, &nEvents);
                if (nEvents == 0) break;
              }
              if (!ReadConsoleInput(hIn, ir, 1, &read) || read == 0) break;
              if (ir[0].EventType == KEY_EVENT &&
                  ir[0].Event.KeyEvent.bKeyDown) {
                ch = ir[0].Event.KeyEvent.uChar.AsciiChar;
                if (ch != 0) {
                  event = parser_.process(ch);
                }
              } else if (ir[0].EventType == MOUSE_EVENT) {
                event.type = EventType::Mouse;
                event.x = ir[0].Event.MouseEvent.dwMousePosition.X;
                event.y = ir[0].Event.MouseEvent.dwMousePosition.Y;

                DWORD mods = ir[0].Event.MouseEvent.dwControlKeyState;
                if (mods & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                  event.ctrl = true;
                if (mods & SHIFT_PRESSED) event.shift = true;
                if (mods & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
                  event.alt = true;

                DWORD btn = ir[0].Event.MouseEvent.dwButtonState;
                if (btn & FROM_LEFT_1ST_BUTTON_PRESSED)
                  event.button = 0;
                else if (btn & RIGHTMOST_BUTTON_PRESSED)
                  event.button = 2;
                else if (btn & FROM_LEFT_2ND_BUTTON_PRESSED)
                  event.button = 1;
                else
                  event.button = 3;  // Release

                if (ir[0].Event.MouseEvent.dwEventFlags == MOUSE_MOVED) {
                  event.button |= 32;
                } else if (ir[0].Event.MouseEvent.dwEventFlags ==
                           MOUSE_WHEELED) {
                  short delta =
                      (short)((ir[0].Event.MouseEvent.dwButtonState >> 16) &
                              0xFFFF);
                  if (delta > 0)
                    event.button = 64;
                  else
                    event.button = 65;
                }
                return event;
              } else if (ir[0].EventType == WINDOW_BUFFER_SIZE_EVENT) {
                event.type = EventType::Resize;
                event.x = ir[0].Event.WindowBufferSizeEvent.dwSize.X;
                event.y = ir[0].Event.WindowBufferSizeEvent.dwSize.Y;
                return event;
              }
            }

            if (event.type != EventType::None) {
              // Apply modifier flags from Windows control key state
              DWORD ks = ir[0].Event.KeyEvent.dwControlKeyState;
              if (ks & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                event.ctrl = true;
              if (ks & SHIFT_PRESSED) event.shift = true;
              if (ks & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) event.alt = true;
              return event;
            }
          } else {
            // Handle VK codes when AsciiChar is 0 (e.g., Ctrl+Space, arrows in
            // VT mode)
            WORD vk = ir[0].Event.KeyEvent.wVirtualKeyCode;
            DWORD ks = ir[0].Event.KeyEvent.dwControlKeyState;
            bool handled_vk = true;
            event.type = EventType::Key;
            // Set modifier flags
            if (ks & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
              event.ctrl = true;
            if (ks & SHIFT_PRESSED) event.shift = true;
            if (ks & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) event.alt = true;
            switch (vk) {
              case VK_SPACE:
                event.key = ' ';
                break;  // Ctrl+Space
              case VK_UP:
                event.key = 1065;
                break;
              case VK_DOWN:
                event.key = 1066;
                break;
              case VK_RIGHT:
                event.key = 1067;
                break;
              case VK_LEFT:
                event.key = 1068;
                break;
              case VK_HOME:
                event.key = 1003;
                break;
              case VK_END:
                event.key = 1004;
                break;
              case VK_PRIOR:
                event.key = 1001;
                break;  // PageUp
              case VK_NEXT:
                event.key = 1002;
                break;  // PageDown
              case VK_DELETE:
                event.key = 1005;
                break;
              case VK_INSERT:
                event.key = 1006;
                break;
              default:
                handled_vk = false;
                event.type = EventType::None;
                break;
            }
            if (handled_vk && event.type != EventType::None) {
              return event;
            }
          }
        } else if (ir[0].EventType == MOUSE_EVENT) {
          event.type = EventType::Mouse;
          event.x = ir[0].Event.MouseEvent.dwMousePosition.X;
          event.y = ir[0].Event.MouseEvent.dwMousePosition.Y;

          DWORD mods = ir[0].Event.MouseEvent.dwControlKeyState;
          if (mods & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
            event.ctrl = true;
          if (mods & SHIFT_PRESSED) event.shift = true;
          if (mods & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) event.alt = true;

          DWORD btn = ir[0].Event.MouseEvent.dwButtonState;
          if (btn & FROM_LEFT_1ST_BUTTON_PRESSED)
            event.button = 0;
          else if (btn & RIGHTMOST_BUTTON_PRESSED)
            event.button = 2;
          else if (btn & FROM_LEFT_2ND_BUTTON_PRESSED)
            event.button = 1;
          else
            event.button = 3;  // Release

          if (ir[0].Event.MouseEvent.dwEventFlags == MOUSE_MOVED) {
            event.button |= 32;
          } else if (ir[0].Event.MouseEvent.dwEventFlags == MOUSE_WHEELED) {
            short delta =
                (short)((ir[0].Event.MouseEvent.dwButtonState >> 16) & 0xFFFF);
            if (delta > 0)
              event.button = 64;
            else
              event.button = 65;
          }
          return event;
        } else if (ir[0].EventType == WINDOW_BUFFER_SIZE_EVENT) {
          event.type = EventType::Resize;
          event.x = ir[0].Event.WindowBufferSizeEvent.dwSize.X;
          event.y = ir[0].Event.WindowBufferSizeEvent.dwSize.Y;
          return event;
        }

        GetNumberOfConsoleInputEvents(hIn, &nEvents);
      }

      return event;
    }

    // Standard blocking input
    int legacy_timeout =
        (vt_input_supported_ && timeout_ms != -1) ? 0 : timeout_ms;

    if (legacy_timeout >= 0) {
      DWORD result;
      if (g_wakeup_event) {
        HANDLE handles[2] = {hIn, g_wakeup_event};
        result = WaitForMultipleObjects(2, handles, FALSE, legacy_timeout);
        if (result == WAIT_OBJECT_0 + 1) {
          event.type = EventType::Wakeup;
          return event;
        }
      } else {
        result = WaitForSingleObject(hIn, legacy_timeout);
      }
      if (result == WAIT_TIMEOUT) return event;
    }

    DWORD read;
    INPUT_RECORD ir[1];

    if (ReadConsoleInput(hIn, ir, 1, &read) && read > 0) {
      if (ir[0].EventType == KEY_EVENT && ir[0].Event.KeyEvent.bKeyDown) {
        char ch = ir[0].Event.KeyEvent.uChar.AsciiChar;

        if (ch != 0) {
          event = parser_.process(ch);

          // Handle fragmented VT sequences
          int safety_break = 0;
          while (event.type == EventType::None && parser_.in_sequence() &&
                 safety_break < 50) {
            DWORD n;
            GetNumberOfConsoleInputEvents(hIn, &n);

            if (n == 0) {
              WaitForSingleObject(hIn, 20);
              GetNumberOfConsoleInputEvents(hIn, &n);
              if (n == 0) break;
            }

            if (PeekConsoleInput(hIn, ir, 1, &read) && read > 0 &&
                ir[0].EventType == KEY_EVENT && ir[0].Event.KeyEvent.bKeyDown) {
              ReadConsoleInput(hIn, ir, 1, &read);
              char next_ch = ir[0].Event.KeyEvent.uChar.AsciiChar;
              if (next_ch != 0) {
                event = parser_.process(next_ch);
              }
            } else {
              break;
            }
            safety_break++;
          }

          // Apply modifier flags from Windows control key state
          if (event.type != EventType::None) {
            DWORD ks = ir[0].Event.KeyEvent.dwControlKeyState;
            if (ks & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
              event.ctrl = true;
            if (ks & SHIFT_PRESSED) event.shift = true;
            if (ks & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) event.alt = true;
          }
          return event;
        } else {
          // Handle non-ASCII virtual key codes (arrows, etc)
          WORD vk = ir[0].Event.KeyEvent.wVirtualKeyCode;
          DWORD ks = ir[0].Event.KeyEvent.dwControlKeyState;
          event.type = EventType::Key;
          // Set modifier flags from control key state
          if (ks & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) event.ctrl = true;
          if (ks & SHIFT_PRESSED) event.shift = true;
          if (ks & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) event.alt = true;
          switch (vk) {
            case VK_UP:
              event.key = 1065;
              break;
            case VK_DOWN:
              event.key = 1066;
              break;
            case VK_RIGHT:
              event.key = 1067;
              break;
            case VK_LEFT:
              event.key = 1068;
              break;
            case VK_HOME:
              event.key = 1003;
              break;
            case VK_END:
              event.key = 1004;
              break;
            case VK_PRIOR:
              event.key = 1001;
              break;  // PageUp
            case VK_NEXT:
              event.key = 1002;
              break;  // PageDown
            case VK_DELETE:
              event.key = 1005;
              break;
            case VK_INSERT:
              event.key = 1006;
              break;
            case VK_SPACE:
              event.key = ' ';
              break;  // Handle Ctrl+Space
            default:
              event.type = EventType::None;
              break;
          }
          if (event.type != EventType::None) return event;
        }
      } else if (ir[0].EventType == MOUSE_EVENT) {
        event.type = EventType::Mouse;
        event.x = ir[0].Event.MouseEvent.dwMousePosition.X;
        event.y = ir[0].Event.MouseEvent.dwMousePosition.Y;

        DWORD mods = ir[0].Event.MouseEvent.dwControlKeyState;
        if (mods & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) event.ctrl = true;
        if (mods & SHIFT_PRESSED) event.shift = true;
        if (mods & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) event.alt = true;

        DWORD btn = ir[0].Event.MouseEvent.dwButtonState;
        if (btn & FROM_LEFT_1ST_BUTTON_PRESSED)
          event.button = 0;
        else if (btn & RIGHTMOST_BUTTON_PRESSED)
          event.button = 2;
        else if (btn & FROM_LEFT_2ND_BUTTON_PRESSED)
          event.button = 1;
        else
          event.button = 3;  // Release

        if (ir[0].Event.MouseEvent.dwEventFlags == MOUSE_MOVED) {
          event.button |= 32;
        } else if (ir[0].Event.MouseEvent.dwEventFlags == MOUSE_WHEELED) {
          short delta =
              (short)((ir[0].Event.MouseEvent.dwButtonState >> 16) & 0xFFFF);
          if (delta > 0)
            event.button = 64;
          else
            event.button = 65;
        }
      } else if (ir[0].EventType == WINDOW_BUFFER_SIZE_EVENT) {
        event.type = EventType::Resize;
        event.x = ir[0].Event.WindowBufferSizeEvent.dwSize.X;
        event.y = ir[0].Event.WindowBufferSizeEvent.dwSize.Y;
      }
    }
#else
    // Linux/Mac Implementation
    if (g_resize_pending) {
      g_resize_pending = 0;
      auto size = getSize();
      event.type = EventType::Resize;
      event.x = size.first;
      event.y = size.second;
      return event;
    }

    {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(STDIN_FILENO, &fds);

      // Also watch the wakeup pipe for cross-thread update()/post()
      int nfds = STDIN_FILENO + 1;
      if (g_wakeup_pipe[0] != -1) {
        FD_SET(g_wakeup_pipe[0], &fds);
        if (g_wakeup_pipe[0] >= nfds) nfds = g_wakeup_pipe[0] + 1;
      }

      int ret;
      if (timeout_ms >= 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ret = select(nfds, &fds, NULL, NULL, &tv);
      } else {
        // Block indefinitely until input, wakeup, or signal
        ret = select(nfds, &fds, NULL, NULL, NULL);
      }

      if (ret == 0) return event;
      if (ret < 0) {
        if (errno == EINTR && g_resize_pending) {
          g_resize_pending = 0;
          auto size = getSize();
          event.type = EventType::Resize;
          event.x = size.first;
          event.y = size.second;
          return event;
        }
        return event;
      }

      // Check if wakeup pipe is readable
      if (g_wakeup_pipe[0] != -1 && FD_ISSET(g_wakeup_pipe[0], &fds)) {
        // Drain the pipe
        char buf[64];
        while (read(g_wakeup_pipe[0], buf, sizeof(buf)) > 0) {
        }

        event.type = EventType::Wakeup;
        return event;
      }
    }

    // Loop to consume buffer until event found or empty
    while (true) {
      char c;
      int nread = read(STDIN_FILENO, &c, 1);
      if (nread < 0 && errno == EINTR) {
        if (g_resize_pending) { /*...*/
        }
        continue;
      }
      if (nread <= 0) break;  // End of input (for this poll)

      event = parser_.process(c);
      if (event.type != EventType::None) return event;

      // Partial read; continue, but give a small grace period for the rest of
      // an escape sequence Often terminals send escape sequences in bursts, but
      // network latency (SSH) might split them. We shouldn't block
      // indefinitely, but a tiny sleep or select helps coalesce.
      if (nread > 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        // 50ms buffer for fragmented packets (e.g. lengthy paste data)
        struct timeval tv = {0, 50000};
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) break;
      }
    }
#endif
    return event;
  }

  /// @brief Read an event from input with multi-click detection
  /// @param timeout_ms Maximum time to wait in milliseconds (-1 for infinite)
  /// @return The read Event (or EventType::None if timeout)
  Event readEvent(int timeout_ms = -1) {
    Event event = readRawEvent(timeout_ms);

    // Multi-click detection logic
    if (event.type == EventType::Mouse) {
      // We only care about button presses (not releases, motion, or wheel)
      bool is_press = !event.mouse_release() && !event.mouse_motion() &&
                      !event.mouse_wheel();

      if (is_press) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last_click_time_)
                           .count();

        if (elapsed < 500 && event.button == last_click_button_ &&
            event.x == last_click_x_ && event.y == last_click_y_) {
          current_click_count_++;
          if (current_click_count_ > 3)
            current_click_count_ =
                1;  // Usually 3 is enough (single, double, triple)
        } else {
          current_click_count_ = 1;
        }

        last_click_time_ = now;
        last_click_button_ = event.button;
        last_click_x_ = event.x;
        last_click_y_ = event.y;

        event.click_count = current_click_count_;
      } else if (event.mouse_release()) {
        // Pass current click count to release event too, might be useful
        event.click_count = current_click_count_;
      }
    } else if (event.type == EventType::Key ||
               event.type == EventType::Resize) {
      // Reset click count on other major interactions
      current_click_count_ = 0;
    }

    return event;
  }

 private:
#ifdef _WIN32
  DWORD originalOutMode;
  DWORD originalInMode;
  UINT originalOutCP;
  UINT originalInCP;
  bool vt_supported_ = false;
#else
  struct termios originalTermios;
#endif
  VTParser parser_;

  // Multi-click tracking
  std::chrono::steady_clock::time_point last_click_time_;
  int last_click_button_ = -1;
  int last_click_x_ = -1;
  int last_click_y_ = -1;
  int current_click_count_ = 0;
};

/// @brief Screen width categories for responsive layouts
enum class ScreenSize { Small, Medium, Large };
inline ScreenSize g_screen_size = ScreenSize::Large;

/// @brief Screen height categories for responsive layouts
enum class ScreenHeight { Small, Medium, Large };
inline ScreenHeight g_screen_height_cat = ScreenHeight::Large;

class Tooltip;  // Forward declaration

/// @brief Base class for all visual elements in the TUI framework
class Widget : public std::enable_shared_from_this<Widget> {
 public:
  virtual ~Widget() {
    if (focused_widget_ == this) focused_widget_ = nullptr;
  }

  /// @brief Render the widget to the buffer
  /// @param buffer The target buffer to render into
  virtual void render(Buffer &buffer) = 0;

  /// @brief Handle an input event
  /// @param event The event to process
  /// @return true if the event was consumed, false otherwise
  virtual bool on_event(const Event &) { return false; }

  // Focus management
  bool focusable = false;  ///< If true, can receive focus (via click or tab)
  bool tab_stop = true;    ///< If true, included in Tab navigation cycle

  /// @brief Check if the widget currently has focus
  bool has_focus() const { return focused_; }

  /// @brief Called when the widget receives focus
  virtual void on_focus() { focused_ = true; }

  /// @brief Called when the widget loses focus
  virtual void on_blur() { focused_ = false; }

  /// @brief Set the focus state of the widget
  /// @param f true to focus, false to blur
  void set_focus(bool f) {
    if (f) {
      if (focused_widget_ && focused_widget_ != this)
        focused_widget_->set_focus(false);
      focused_widget_ = this;
      on_focus();
    } else {
      if (focused_widget_ == this) focused_widget_ = nullptr;
      on_blur();
    }
  }

  /// @brief Check if this widget or any of its descendants has focus
  virtual bool has_focus_within() const { return focused_; }

  // Basic properties
  int x = 0;            ///< X coordinate relative to parent (or screen if root)
  int y = 0;            ///< Y coordinate relative to parent
  int width = 0;        ///< Current width
  int height = 0;       ///< Current height
  int fixed_width = 0;  ///< 0 = flexible, >0 = fixed size request
  int fixed_height = 0;  ///< 0 = flexible, >0 = fixed size request
  int min_width = 0;     ///< 0 = no minimum, >0 = minimum width constraint
  int min_height = 0;    ///< 0 = no minimum, >0 = minimum height constraint
  int max_width = 0;     ///< 0 = no maximum, >0 = maximum width constraint
  int max_height = 0;    ///< 0 = no maximum, >0 = maximum height constraint

  /// @brief Clamp a size value by min/max constraints.
  /// When min > max, min wins (CSS convention).
  /// @param size The computed size to clamp
  /// @param min_val Minimum constraint (0 = unconstrained)
  /// @param max_val Maximum constraint (0 = unconstrained)
  /// @return The clamped size
  static int clamp_size(int size, int min_val, int max_val) {
    if (max_val > 0 && size > max_val) size = max_val;
    if (min_val > 0 && size < min_val) size = min_val;  // min wins over max
    return size;
  }

  Color bg_color = Color();  ///< Background color override
  Color fg_color = Color();  ///< Foreground color override

  // Hover callback (called when mouse enters/exits widget bounds)
  std::function<void(bool)> on_hover;  ///< Callback for hover state changes
  bool hovered_ = false;               ///< Internal hover state tracking

  /// @brief Set the hover state and trigger callback
  virtual void set_hovered(bool h) {
    if (h != hovered_) {
      hovered_ = h;
      if (on_hover) on_hover(h);
    }
  }

  // Tooltip (optional)
  std::shared_ptr<Tooltip> tooltip_;

  /// @brief Attach a tooltip to this widget
  void set_tooltip(std::shared_ptr<Tooltip> t);
  /// @brief Set a simple text tooltip
  void set_tooltip(const std::string &text);

  // Visibility
  bool visible = true;  ///< If false, widget is not rendered and ignores events

  // Responsive Logic
  bool is_responsive = false;
  bool responsive_width = false;
  bool responsive_height = false;

  bool visible_sm = true;
  bool visible_md = true;
  bool visible_lg = true;

  bool visible_h_sm = true;
  bool visible_h_md = true;
  bool visible_h_lg = true;

  /// @brief Enable responsive visibility based on screen width
  void set_responsive(bool sm, bool md, bool lg) {
    is_responsive = true;
    responsive_width = true;
    visible_sm = sm;
    visible_md = md;
    visible_lg = lg;
  }

  /// @brief Enable responsive visibility based on screen width
  void set_responsive_width(bool sm, bool md, bool lg) {
    is_responsive = true;
    responsive_width = true;
    visible_sm = sm;
    visible_md = md;
    visible_lg = lg;
  }

  /// @brief Enable responsive visibility based on screen height
  void set_responsive_height(bool sm, bool md, bool lg) {
    is_responsive = true;
    responsive_height = true;
    visible_h_sm = sm;
    visible_h_md = md;
    visible_h_lg = lg;
  }

  /// @brief Update internal visibility based on current global screen size
  virtual void update_responsive() {
    if (is_responsive) {
      bool w_vis = true;
      bool h_vis = true;

      if (responsive_width) {
        if (g_screen_size == ScreenSize::Small)
          w_vis = visible_sm;
        else if (g_screen_size == ScreenSize::Medium)
          w_vis = visible_md;
        else
          w_vis = visible_lg;
      }

      if (responsive_height) {
        if (g_screen_height_cat == ScreenHeight::Small)
          h_vis = visible_h_sm;
        else if (g_screen_height_cat == ScreenHeight::Medium)
          h_vis = visible_h_md;
        else
          h_vis = visible_h_lg;
      }

      visible = w_vis && h_vis;
    }
  }

  /// @brief Helper to check if a point is within bounds
  virtual bool contains(int px, int py) const {
    return visible && px >= x && px < x + width && py >= y && py < y + height;
  }

  /**
   * @brief Check if a point interacts with this widget.
   * Default implementation uses contains(), but subclasses (like Notification)
   * can override this to be hit-transparent in certain areas or states.
   */
  virtual bool hit_test(int px, int py) const { return contains(px, py); }

 protected:
  inline static Widget *focused_widget_ = nullptr;
  bool focused_ = false;
};

/// @brief A styled text segment with formatting options
struct TextSpan {
  std::string text;
  Color color = Color();  ///< Default = inherit from parent
  bool bold = false;
  bool italic = false;
  bool underline = false;

  TextSpan() = default;
  TextSpan(const std::string &t) : text(t) {}
  TextSpan(const std::string &t, Color c) : text(t), color(c) {}
};

/// @brief Builder class for creating styled text with mixed formatting
class StyledText {
 public:
  StyledText() = default;
  StyledText(const std::string &text) { add(text); }
  StyledText(const char *text) { add(std::string(text)); }

  /// Add plain text
  StyledText &add(const std::string &text) {
    spans_.push_back(TextSpan(text));
    invalidate_cache();
    return *this;
  }

  /// Add bold text
  StyledText &bold(const std::string &text) {
    TextSpan span(text);
    span.bold = true;
    spans_.push_back(span);
    invalidate_cache();
    return *this;
  }

  /// Add italic text
  StyledText &italic(const std::string &text) {
    TextSpan span(text);
    span.italic = true;
    spans_.push_back(span);
    invalidate_cache();
    return *this;
  }

  /// Add underlined text
  StyledText &underline(const std::string &text) {
    TextSpan span(text);
    span.underline = true;
    spans_.push_back(span);
    invalidate_cache();
    return *this;
  }

  /// Add bold italic text
  StyledText &bold_italic(const std::string &text) {
    TextSpan span(text);
    span.bold = true;
    span.italic = true;
    spans_.push_back(span);
    invalidate_cache();
    return *this;
  }

  /// Add colored text
  StyledText &colored(const std::string &text, Color c) {
    TextSpan span(text, c);
    spans_.push_back(span);
    invalidate_cache();
    return *this;
  }

  /// Add colored bold text
  StyledText &colored_bold(const std::string &text, Color c) {
    TextSpan span(text, c);
    span.bold = true;
    spans_.push_back(span);
    invalidate_cache();
    return *this;
  }

  /// Calculate total character count
  size_t length() const {
    size_t len = 0;
    for (const auto &span : spans_) {
      len += span.text.length();
    }
    return len;
  }

  /// Get plain text (without formatting)
  const std::string &plain_text() const {
    if (plain_text_dirty_) {
      cached_plain_text_.clear();
      for (const auto &span : spans_) {
        cached_plain_text_ += span.text;
      }
      plain_text_dirty_ = false;
    }
    return cached_plain_text_;
  }

  /// Implicit conversion to std::string for backward compatibility
  operator std::string() const { return plain_text(); }

  /// Get the TextSpan corresponding to a specific UTF-8 character index
  const TextSpan *get_span_at_char(int char_idx) const {
    if (spans_.empty()) return nullptr;

    ensure_cache();

    // Find the span containing char_idx
    // Since spans are typically few, linear search is fine, but we use the
    // cached offsets
    for (size_t i = 0; i < spans_.size(); ++i) {
      int start = cached_offsets_[i];
      int end = (i + 1 < spans_.size()) ? cached_offsets_[i + 1] : total_chars_;
      if (char_idx >= start && char_idx < end) {
        return &spans_[i];
      }
    }
    return nullptr;
  }

  /// Clear all spans
  void clear() {
    spans_.clear();
    invalidate_cache();
  }

  /// Check if empty
  bool empty() const { return spans_.empty(); }

  /// Access to spans for manual iteration
  const std::vector<TextSpan> &spans() const { return spans_; }

 private:
  std::vector<TextSpan> spans_;
  mutable std::vector<int> cached_offsets_;
  mutable std::string cached_plain_text_;
  mutable int total_chars_ = 0;
  mutable bool cache_dirty_ = true;
  mutable bool plain_text_dirty_ = true;

  void invalidate_cache() {
    cache_dirty_ = true;
    plain_text_dirty_ = true;
  }

  void ensure_cache() const {
    if (!cache_dirty_) return;

    cached_offsets_.clear();
    int current_idx = 0;
    for (const auto &span : spans_) {
      cached_offsets_.push_back(current_idx);
      current_idx += (int)TextHelper::prepare_text_for_render(span.text).size();
    }
    total_chars_ = current_idx;
    cache_dirty_ = false;
  }
};

/// @brief A simple widget displaying static text
class Static : public Widget {
 public:
  StyledText styled_text_;

  Static(StyledText text) : styled_text_(text), text_(text.plain_text()) {
    focusable = true;
    tab_stop = false;
  }

  /// @brief Update the text content
  void set_text(const StyledText &text) {
    styled_text_ = text;
    text_ = text.plain_text();
    clear_selection();
  }

  bool selectable = true;  ///< If true, text can be selected and copied

  void render(Buffer &buffer) override {
    int current_x = x;
    int current_y = y;

    std::vector<CharInfo> chars = prepare_text_for_render(text_);
    int char_idx = 0;

    for (const auto &ci : chars) {
      if (ci.content == "\n") {
        current_x = x;
        current_y++;
        char_idx++;
        continue;
      }

      if (current_x >= x && current_x < x + width && current_y >= y &&
          current_y < y + height) {
        Cell cell;
        cell.content = ci.content;
        cell.fg_color = fg_color;

        if (bg_color.is_default)
          cell.bg_color = buffer.get(current_x, current_y).bg_color;
        else
          cell.bg_color = bg_color;

        if (is_char_selected(char_idx)) {
          cell.bg_color = Theme::current().selection;
          cell.fg_color = Color::contrast_color(cell.bg_color);
          cell.bold = true;
        } else if (!styled_text_.empty()) {
          if (const TextSpan *span = styled_text_.get_span_at_char(char_idx)) {
            if (!span->color.is_default) cell.fg_color = span->color;
            cell.bold = span->bold;
            cell.italic = span->italic;
            cell.underline = span->underline;
          }
        }

        buffer.set(current_x, current_y, cell);

        if (ci.display_width == 2 && current_x + 1 < x + width) {
          Cell skip;
          skip.content = "";
          skip.bg_color = cell.bg_color;
          buffer.set(current_x + 1, current_y, skip);
        }
      }
      current_x += ci.display_width;
      char_idx++;
    }
  }

  void on_blur() override {
    selection_state_.clear();
    Widget::on_blur();
  }

  bool on_event(const Event &event) override {
    if (!selectable) return false;
    bool hit = contains(event.x, event.y);

    if (event.is_mouse_event()) {
      if (hit && event.mouse_left() && !event.mouse_motion()) {
        set_focus(true);
        auto chars = prepare_text_for_render(text_);
        int new_pos = visual_to_char_pos(chars, event.x, event.y);
        selection_state_.handle_mouse_press(chars, new_pos, event.click_count);
        return true;
      }
      if (selection_state_.mouse_down && event.mouse_drag()) {
        auto chars = prepare_text_for_render(text_);
        selection_state_.handle_mouse_drag(
            visual_to_char_pos(chars, event.x, event.y));
        return true;
      }
      if (event.mouse_release()) {
        selection_state_.handle_mouse_release();
      }
      return false;
    }

    if (has_focus() && event.is_key_event()) {
      if (event.is_select_all()) {
        selection_state_.start = 0;
        selection_state_.end = (int)text_.length();
        return true;
      }
      if (event.is_copy()) {
        if (has_selection()) copy_to_clipboard(get_selected_text());
        return true;
      }
    }
    return false;
  }

  bool has_selection() const { return selection_state_.has_selection(); }

  std::string get_selected_text() const {
    if (!has_selection()) return "";
    int s, e;
    selection_state_.get_range(s, e);
    return text_.substr(s, e - s);
  }

  bool is_char_selected(int char_idx) const {
    return selection_state_.is_selected(char_idx);
  }

 private:
  std::string text_;
  SelectionState selection_state_;

  void clear_selection() { selection_state_.clear(); }

  int visual_to_char_pos(const std::vector<CharInfo> &chars, int vx,
                         int vy) const {
    int cur_x = x;
    int cur_y = y;
    for (int i = 0; i < (int)chars.size(); ++i) {
      if (chars[i].content == "\n") {
        if (cur_y == vy && vx >= cur_x) return i;
        cur_x = x;
        cur_y++;
        continue;
      }
      if (cur_y == vy && vx >= cur_x && vx < cur_x + chars[i].display_width)
        return i;
      cur_x += chars[i].display_width;
    }
    if (vy > cur_y || (vy == cur_y && vx >= cur_x)) return (int)chars.size();
    return 0;
  }
};

/// @brief A label widget for displaying single-line text
class Label : public Widget {
 public:
  StyledText styled_text_;

  /// @brief Construct a new Label
  /// @param text The text to display
  /// @param fg Optional foreground color
  Label(StyledText text, Color fg = {0, 0, 0, true})
      : styled_text_(text), text_(text.plain_text()) {
    fg_color = fg;
    bg_color = {0, 0, 0, true};  // Transparent
    fixed_height = 1;  // Default to single line to prevent layout expansion
    focusable = true;  // Allow focus for text selection/copy
    tab_stop = false;  // Don't include in Tab navigation cycle
  }

  /// @brief Set the text content
  void set_text(const StyledText &t) {
    styled_text_ = t;
    text_ = t.plain_text();
    clear_selection();
  }
  /// @brief Get the current text content
  const std::string &get_text() const { return text_; }
  bool underline = false;  ///< If true, text will be rendered with an underline
  bool selectable =
      true;  ///< If true, text can be selected with mouse and copyable

  // Selection State
  bool has_selection() const { return selection_state_.has_selection(); }

  std::string get_selected_text() const {
    if (!has_selection()) return "";
    // Use TextHelper for safe UTF-8 substring
    return TextHelper::utf8_substr(
        text_, selection_state_.start,
        selection_state_.end - selection_state_.start);
  }

  void clear_selection() { selection_state_.clear(); }

  bool is_char_selected(int idx) const {
    return selection_state_.is_selected(idx);
  }

  void on_blur() override {
    clear_selection();
    Widget::on_blur();
  }

  bool on_event(const Event &event) override {
    if (!selectable) return false;
    bool hit = contains(event.x, event.y);

    if (event.is_mouse_event()) {
      if (hit && event.mouse_left() && !event.mouse_motion()) {
        set_focus(true);
        auto chars = prepare_text_for_render(text_);
        int char_idx = TextHelper::visual_to_char_pos(chars, event.x - x);
        selection_state_.handle_mouse_press(chars, char_idx, event.click_count);
        return true;
      }
      if (selection_state_.mouse_down && event.mouse_drag()) {
        auto chars = prepare_text_for_render(text_);
        selection_state_.handle_mouse_drag(
            TextHelper::visual_to_char_pos(chars, event.x - x));
        return true;
      }
      if (event.mouse_release()) {
        selection_state_.handle_mouse_release();
      }
      return false;
    }

    if (has_focus() && event.is_key_event()) {
      if (event.is_select_all()) {
        selection_state_.start = 0;
        selection_state_.end = (int)text_.length();
        return true;
      }
      if (event.is_copy()) {
        if (has_selection()) copy_to_clipboard(get_selected_text());
        return true;
      }
    }
    return false;
  }

  void render(Buffer &buffer) override {
    Color fg = fg_color.resolve(Theme::current().foreground);

    std::vector<CharInfo> chars = prepare_text_for_render(text_);

    for (int dy = 0; dy < height; ++dy) {
      for (int dx = 0; dx < width; ++dx) {
        int sx = x + dx;
        int sy = y + dy;
        if (sx < 0 || sx >= buffer.width() || sy < 0 || sy >= buffer.height())
          continue;

        Cell cell;
        cell.content = " ";
        cell.fg_color = fg;
        if (bg_color.is_default)
          cell.bg_color = buffer.get(sx, sy).bg_color;
        else
          cell.bg_color = bg_color;

        if (dy == 0) {
          int char_idx = TextHelper::visual_to_char_pos(chars, dx);
          if (char_idx >= 0 && char_idx < (int)chars.size()) {
            // Re-check if this visual position is actually a char start
            int cur_vx = 0;
            bool is_start = false;
            for (int i = 0; i <= char_idx; ++i) {
              if (cur_vx == dx) {
                is_start = true;
                break;
              }
              cur_vx += chars[i].display_width;
            }

            if (is_start) {
              cell.content = chars[char_idx].content;
              if (selection_state_.is_selected(char_idx)) {
                cell.bg_color = Theme::current().selection;
                cell.fg_color = Color::contrast_color(cell.bg_color);
              }
              if (chars[char_idx].display_width == 0) cell.content = "";
            } else {
              // Handle wide char continuation background
              int prev_idx = TextHelper::visual_to_char_pos(chars, dx - 1);
              if (prev_idx >= 0 && prev_idx < (int)chars.size() &&
                  selection_state_.is_selected(prev_idx)) {
                cell.bg_color = Theme::current().selection;
              }
              cell.content = "";
            }
          }
        }
        buffer.set(sx, sy, cell);
      }
    }
  }

 protected:
  std::string text_;
  SelectionState selection_state_;
};

/// @brief A paragraph widget for displaying multi-line text with optional
/// word-wrapping and indentation
class Paragraph : public Widget {
 public:
  /// Styled content with mixed formatting
  StyledText styled_content;

  /// Plain text content (used if styled_content is empty)
  std::string text;

  /// Indentation settings
  int first_line_indent = 0;  ///< Spaces before first line
  int hanging_indent = 0;     ///< Spaces before subsequent lines

  /// Word wrapping
  bool word_wrap = true;  ///< If true, wrap at word boundaries

  /// When true, preserves original whitespace during wrapping instead of
  /// normalizing to single spaces. Defaults to false for backward
  /// compatibility.
  bool preserve_whitespace = false;

  /// Default text styling (for plain text)
  bool bold = false;       ///< Render in bold
  bool italic = false;     ///< Render in italics
  bool underline = false;  ///< Render with underline

  Paragraph() {
    focusable = true;
    tab_stop = false;
  }
  Paragraph(StyledText st) : styled_content(st), text(st.plain_text()) {
    focusable = true;
    tab_stop = false;
  }

  /// @brief Set plain text content, clearing any styled content
  void set_text(const std::string &t) {
    text = t;
    styled_content.clear();
    selection_state_.clear();
  }

  /// @brief Set styled text content, clearing any plain text
  void set_styled(const StyledText &st) {
    styled_content = st;
    text = st.plain_text();
    selection_state_.clear();
  }

  /// @brief If true, text can be selected and copied
  bool selectable = true;

  void render(Buffer &buffer) override {
    const std::string &plain = text;

    std::vector<std::string> lines;
    if (word_wrap)
      lines = wrap_text_static(plain, width, first_line_indent, hanging_indent,
                               preserve_whitespace);
    else {
      std::istringstream stream(plain);
      std::string line;
      while (std::getline(stream, line)) lines.push_back(line);
    }

    bool has_styled = !styled_content.empty();
    struct SpanPos {
      int first;
      const TextSpan *second;
    };
    std::vector<SpanPos> span_map;
    if (has_styled) {
      int pos = 0;
      for (const auto &span : styled_content.spans()) {
        span_map.push_back({pos, &span});
        pos += (int)span.text.length();
      }
    }

    auto get_span_at = [&](int pos) -> const TextSpan * {
      for (int i = (int)span_map.size() - 1; i >= 0; --i) {
        if (pos >= span_map[i].first) return span_map[i].second;
      }
      return nullptr;
    };

    int char_in_plain_offset = 0;
    for (int line_idx = 0; line_idx < (int)lines.size() && line_idx < height;
         ++line_idx) {
      const std::string &line = lines[line_idx];
      int indent = (line_idx == 0) ? first_line_indent : hanging_indent;
      auto chars = TextHelper::prepare_text_for_render(line);

      for (int dx = 0; dx < width; ++dx) {
        int sx = x + dx;
        int sy = y + line_idx;
        if (sx < 0 || sx >= buffer.width() || sy < 0 || sy >= buffer.height())
          continue;

        int cell_x = dx - indent;

        if (cell_x >= 0 && cell_x < (int)chars.size()) {
          const auto &ci = chars[cell_x];
          int global_char_idx = char_in_plain_offset + cell_x;

          Cell cell;
          cell.content = ci.content;
          cell.fg_color = fg_color;

          if (bg_color.is_default)
            cell.bg_color = buffer.get(sx, sy).bg_color;
          else
            cell.bg_color = bg_color;

          if (selection_state_.is_selected(global_char_idx)) {
            cell.bg_color = Theme::current().selection;
            cell.fg_color = Color::contrast_color(cell.bg_color);
            cell.bold = true;
          } else if (has_styled) {
            if (const TextSpan *span = get_span_at(global_char_idx)) {
              if (!span->color.is_default) cell.fg_color = span->color;
              cell.bold = span->bold;
              cell.italic = span->italic;
              cell.underline = span->underline;
            }
          }

          if (underline) cell.underline = true;
          buffer.set(sx, sy, cell);

          if (ci.display_width == 2 && dx + 1 < width) {
            dx++;
            Cell skip;
            skip.content = "";
            skip.bg_color = cell.bg_color;
            buffer.set(x + dx, sy, skip);
          }
        } else {
          Cell empty;
          if (bg_color.is_default)
            empty.bg_color = buffer.get(sx, sy).bg_color;
          else
            empty.bg_color = bg_color;
          buffer.set(sx, sy, empty);
        }
      }
      char_in_plain_offset += (int)line.length() + 1;  // +1 for newline
    }
  }

  void on_blur() override {
    selection_state_.clear();
    Widget::on_blur();
  }

  bool on_event(const Event &event) override {
    if (!selectable) return false;
    bool hit = contains(event.x, event.y);

    if (event.is_mouse_event()) {
      if (hit && event.mouse_left() && !event.mouse_motion()) {
        set_focus(true);
        int new_pos = visual_to_char_idx(event.x, event.y);
        selection_state_.handle_mouse_press(
            TextHelper::prepare_text_for_render(text), new_pos,
            event.click_count);
        return true;
      }
      if (selection_state_.mouse_down && event.mouse_drag()) {
        selection_state_.handle_mouse_drag(
            visual_to_char_idx(event.x, event.y));
        return true;
      }
      if (event.mouse_release()) {
        selection_state_.handle_mouse_release();
      }
      return false;
    }

    if (has_focus() && event.is_key_event()) {
      if (event.is_select_all()) {
        selection_state_.start = 0;
        selection_state_.end = (int)text.length();
        return true;
      }
      if (event.is_copy()) {
        if (selection_state_.has_selection())
          copy_to_clipboard(get_selected_text());
        return true;
      }
    }
    return false;
  }

  bool has_selection() const { return selection_state_.has_selection(); }

  std::string get_selected_text() const {
    if (!has_selection()) return "";
    int s, e;
    selection_state_.get_range(s, e);
    return text.substr(s, e - s);
  }

  int visual_to_char_idx(int vx, int vy) const {
    const std::string &plain = text;
    std::vector<std::string> lines;
    if (word_wrap)
      lines = wrap_text_static(plain, width, first_line_indent, hanging_indent,
                               preserve_whitespace);
    else {
      std::istringstream stream(plain);
      std::string line;
      while (std::getline(stream, line)) lines.push_back(line);
    }

    int char_offset = 0;
    int rel_y = vy - y;
    if (rel_y < 0) return 0;

    for (int i = 0; i < (int)lines.size(); ++i) {
      int indent = (i == 0) ? first_line_indent : hanging_indent;
      auto chars = TextHelper::prepare_text_for_render(lines[i]);
      if (i == rel_y) {
        int rel_x = vx - x - indent;
        int cur_vx = 0;
        for (int j = 0; j < (int)chars.size(); ++j) {
          if (rel_x < cur_vx + chars[j].display_width / 2)
            return char_offset + j;
          cur_vx += chars[j].display_width;
        }
        return char_offset + (int)chars.size();
      }
      char_offset += (int)lines[i].length() + 1;
    }
    return (int)plain.length();
  }

  void select_word_at(int pos) {
    auto chars = TextHelper::prepare_text_for_render(text);
    if (pos < 0 || pos >= (int)chars.size()) return;
    int s = 0, e = 0;
    TextHelper::select_word_at(chars, pos, s, e);
    selection_state_.start = s;
    selection_state_.end = e;
  }

  static std::vector<std::string> wrap_text_static(const std::string &input,
                                                   int max_width,
                                                   int first_indent,
                                                   int hang_indent,
                                                   bool preserve_ws = false) {
    std::vector<std::string> result;
    int first_width = max_width - first_indent;
    int other_width = max_width - hang_indent;
    if (first_width <= 0) first_width = 1;
    if (other_width <= 0) other_width = 1;

    std::istringstream stream(input);
    std::string paragraph_line;
    while (std::getline(stream, paragraph_line)) {
      if (paragraph_line.empty()) {
        result.push_back("");
        continue;
      }

      /* Legacy mode: use stream extraction which normalizes whitespace. */
      if (!preserve_ws) {
        std::vector<std::string> words;
        std::istringstream word_stream(paragraph_line);
        std::string word;
        while (word_stream >> word) words.push_back(word);

        if (words.empty()) {
          result.push_back("");
          continue;
        }

        std::string current_line;
        int current_width = result.empty() ? first_width : other_width;
        for (const auto &w : words) {
          if (current_line.empty())
            current_line = w;
          else if ((int)(current_line.length() + 1 + w.length()) <=
                   current_width)
            current_line += " " + w;
          else {
            result.push_back(current_line);
            current_line = w;
            current_width = other_width;
          }
        }
        if (!current_line.empty()) result.push_back(current_line);
      }
      /* Preserve mode: tokenize with exact whitespace tracking. */
      else {
        struct WordToken {
          std::string
              word;  ///< Non-whitespace content (with leading ws prepended)
          std::string ws;  ///< Trailing whitespace after the word
        };
        std::vector<WordToken> words;

        size_t i = 0;
        bool first_token = true;
        while (i < paragraph_line.size()) {
          /* Capture leading whitespace and prepend to next word */
          std::string leading_ws;
          if (first_token) {
            size_t ws_start = i;
            while (i < paragraph_line.size() &&
                   std::isspace(static_cast<unsigned char>(paragraph_line[i])))
              ++i;
            leading_ws.assign(paragraph_line, ws_start, i - ws_start);
            first_token = false;
          }

          if (i >= paragraph_line.size()) {
            /* Line is only whitespace */
            words.push_back({"", leading_ws});
            break;
          }

          /* Collect word characters */
          size_t start = i;
          while (i < paragraph_line.size() &&
                 !std::isspace(static_cast<unsigned char>(paragraph_line[i])))
            ++i;
          std::string w(paragraph_line, start, i - start);

          /* Capture trailing whitespace after this word */
          size_t ws_start2 = i;
          while (i < paragraph_line.size() &&
                 std::isspace(static_cast<unsigned char>(paragraph_line[i])))
            ++i;
          std::string trailing_ws(paragraph_line, ws_start2, i - ws_start2);

          words.push_back({leading_ws + w, trailing_ws});
        }

        if (words.empty()) {
          result.push_back("");
          continue;
        }

        /* Rebuild lines preserving original whitespace between words */
        std::string current_line;
        int current_width = result.empty() ? first_width : other_width;
        for (int wi = 0; wi < static_cast<int>(words.size()); ++wi) {
          const auto &w = words[wi].word;
          const auto &ws = words[wi].ws;
          int unit_len = static_cast<int>(w.length() + ws.length());

          if (current_line.empty()) {
            current_line = w + ws;
          } else if ((int)(current_line.length() + unit_len) <= current_width) {
            current_line += w + ws;
          } else {
            result.push_back(current_line);
            /* Strip trailing whitespace when wrapping to avoid lines ending
             * with spaces */
            current_line = w + ws;
            current_width = other_width;
          }
        }
        if (!current_line.empty()) result.push_back(current_line);
      }
    }
    return result;
  }

 private:
  SelectionState selection_state_;
};

/// @brief List style for TextList widget
enum class ListStyle {
  Bullet,   ///< Unordered list with bullet points (-, *, or custom)
  Numbered  ///< Ordered list with numbers (1., 2., 3., etc.)
};

/// @brief A single item in a TextList with optional nesting
struct ListItem {
  StyledText styled_text;
  std::string text;  ///< The text content of the item
  int level = 0;     ///< Nesting level (0 = top level, 1 = first indent, etc.)
  int marker_idx = 0;  // Used internally for numbering

  ListItem() = default;
  ListItem(StyledText t, int l = 0)
      : styled_text(t), text(t.plain_text()), level(l) {}

  // Allow implicit conversion from const char* and string for convenience
  ListItem(const char *t) : ListItem(StyledText(t), 0) {}
};

/// @brief A text list widget for displaying bulleted or numbered lists with
/// optional nesting
class TextList : public Widget {
 public:
  /// List of items to display (with nesting support)
  std::vector<ListItem> items;

  /// The style of list markers (Bullet or Numbered)
  ListStyle style = ListStyle::Bullet;

  /// Bullet markers for each nesting level (default: "- ", "o ", "> ")
  std::vector<std::string> bullet_markers = {"- ", "o ", "> ", "* "};

  /// Starting number for numbered lists (per level)
  int start_number = 1;

  /// Indentation per nesting level (in characters)
  int indent_per_level = 2;

  /// Indentation for wrapped lines (in addition to marker width)
  int wrap_indent = 0;

  /// Enable word wrapping
  bool word_wrap = true;

  /// If true, text can be selected and copied
  bool selectable = true;

  TextList() {
    focusable = true;
    tab_stop = false;
  }
  TextList(const std::vector<ListItem> &items_list) : items(items_list) {
    focusable = true;
    tab_stop = false;
  }
  TextList(std::initializer_list<ListItem> items_list) : items(items_list) {
    focusable = true;
    tab_stop = false;
  }

  /// Add an item to the list at a specific level
  void add_item(const std::string &item, int level = 0) {
    items.push_back(ListItem(item, level));
  }

  /// Clear all items
  void clear() { items.clear(); }

  /// Get the bullet marker for a specific nesting level
  std::string get_bullet_marker(int level) const {
    if (bullet_markers.empty()) return "- ";
    int idx = level % (int)bullet_markers.size();
    return bullet_markers[idx];
  }

  /// @brief Get the marker string for a specific item, handling numbering if
  /// needed
  std::string get_marker(int level, int marker_idx) const {
    if (style == ListStyle::Numbered) {
      return std::to_string(start_number + marker_idx) + ". ";
    }
    return get_bullet_marker(level);
  }

  void render(Buffer &buffer) override {
    int line_offset = 0;
    int char_in_full_text_offset = 0;

    // Group by numbering if needed
    std::vector<int> level_counts(10, 0);

    for (size_t i = 0; i < items.size(); ++i) {
      auto &item = items[i];
      std::string marker = get_marker(item.level, level_counts[item.level]++);

      // Reset lower level counts if this level is higher (less indented)
      for (int l = item.level + 1; l < 10; ++l) level_counts[l] = 0;

      int marker_width = utf8_display_width(marker);
      int base_indent = item.level * indent_per_level;
      int text_width = width - base_indent - marker_width;

      std::vector<std::string> lines;
      if (word_wrap && text_width > 0)
        lines = wrap_text(item.text, text_width);
      else
        lines = {item.text};

      // Prepare styled text mapping if needed
      bool has_styled = !item.styled_text.empty();
      struct SpanPos {
        int first;
        const TextSpan *second;
      };
      std::vector<SpanPos> span_map;
      if (has_styled) {
        int pos = 0;
        for (const auto &span : item.styled_text.spans()) {
          span_map.push_back({pos, &span});
          pos += (int)span.text.length();
        }
      }
      auto get_span_at = [&](int pos) -> const TextSpan * {
        for (int j = (int)span_map.size() - 1; j >= 0; --j) {
          if (pos >= span_map[j].first) return span_map[j].second;
        }
        return nullptr;
      };

      int item_char_offset = 0;
      for (int li = 0; li < (int)lines.size(); ++li) {
        if (line_offset >= height) break;

        int cur_y = y + line_offset;
        // FIX: Indentation for the first line must also include the marker
        // width
        int cur_indent =
            base_indent + marker_width + (li == 0 ? 0 : wrap_indent);

        // Clear line background first
        for (int dx = 0; dx < width; ++dx) {
          int sx = x + dx;
          if (sx < 0 || sx >= buffer.width() || cur_y < 0 ||
              cur_y >= buffer.height())
            continue;

          Cell c;
          c.content = " ";
          c.fg_color = fg_color;
          if (bg_color.is_default)
            c.bg_color = buffer.get(sx, cur_y).bg_color;
          else
            c.bg_color = bg_color;
          buffer.set(sx, cur_y, c);
        }

        // Render marker
        if (li == 0) {
          auto marker_chars = TextHelper::prepare_text_for_render(marker);
          int m_vx = 0;
          for (int mi = 0; mi < (int)marker_chars.size(); ++mi) {
            if (base_indent + m_vx < width) {
              int sx = x + base_indent + m_vx;
              if (sx >= 0 && sx < buffer.width() && cur_y >= 0 &&
                  cur_y < buffer.height()) {
                Cell c = buffer.get(sx, cur_y);
                c.content = marker_chars[mi].content;
                buffer.set(sx, cur_y, c);
                if (marker_chars[mi].display_width == 2 &&
                    base_indent + m_vx + 1 < width) {
                  buffer.set(sx + 1, cur_y, {"", c.fg_color, c.bg_color});
                }
              }
            }
            m_vx += marker_chars[mi].display_width;
          }
        }

        // Render text
        auto chars = TextHelper::prepare_text_for_render(lines[li]);
        int t_vx = 0;
        for (int ci = 0; ci < (int)chars.size(); ++ci) {
          int sx = x + cur_indent + t_vx;
          if (sx < x + width && sx < buffer.width() && cur_y >= 0 &&
              cur_y < buffer.height()) {
            int global_char_idx =
                char_in_full_text_offset + item_char_offset + ci;
            Cell c = buffer.get(sx, cur_y);
            c.content = chars[ci].content;

            if (selection_state_.is_selected(global_char_idx)) {
              c.bg_color = Theme::current().selection;
              c.fg_color = Color::contrast_color(c.bg_color);
            } else if (has_styled) {
              if (const TextSpan *span = get_span_at(item_char_offset + ci)) {
                if (!span->color.is_default) c.fg_color = span->color;
                c.bold = span->bold;
                c.italic = span->italic;
                c.underline = span->underline;
              }
            }

            buffer.set(sx, cur_y, c);
            if (chars[ci].display_width == 2 && cur_indent + t_vx + 1 < width) {
              buffer.set(sx + 1, cur_y, {"", c.fg_color, c.bg_color});
            }
          }
          t_vx += chars[ci].display_width;
        }
        line_offset++;
        item_char_offset += (int)lines[li].length() + 1;
      }
      char_in_full_text_offset += (int)item.text.length() + 1;
    }

    // Fill remaining lines
    for (int lo = line_offset; lo < height; ++lo) {
      int cur_y = y + lo;
      if (cur_y < 0 || cur_y >= buffer.height()) continue;
      for (int dx = 0; dx < width; ++dx) {
        int sx = x + dx;
        if (sx < 0 || sx >= buffer.width()) continue;
        Cell c;
        c.content = " ";
        c.fg_color = fg_color;
        if (bg_color.is_default)
          c.bg_color = buffer.get(sx, cur_y).bg_color;
        else
          c.bg_color = bg_color;
        buffer.set(sx, cur_y, c);
      }
    }
  }

  bool on_event(const Event &event) override {
    if (!selectable) return false;
    bool hit = contains(event.x, event.y);

    if (event.is_mouse_event()) {
      if (hit && event.mouse_left() && !event.mouse_motion()) {
        set_focus(true);
        int new_pos = visual_to_char_idx(event.x, event.y);
        selection_state_.handle_mouse_press(
            TextHelper::prepare_text_for_render(get_full_text()), new_pos,
            event.click_count);

        // Triple click - Select the entire row (list item)
        if (event.click_count == 3) {
          select_item_at(new_pos);
        }
        return true;
      }
      if (selection_state_.mouse_down && event.mouse_drag()) {
        selection_state_.handle_mouse_drag(
            visual_to_char_idx(event.x, event.y));
        return true;
      }
      if (event.mouse_release()) {
        selection_state_.handle_mouse_release();
      }
      return false;
    }

    if (has_focus() && event.is_key_event()) {
      if (event.is_select_all()) {
        selection_state_.start = 0;
        selection_state_.end = (int)get_full_text().length();
        return true;
      }
      if (event.is_copy()) {
        if (selection_state_.has_selection())
          copy_to_clipboard(get_selected_text());
        return true;
      }
    }
    return false;
  }

  void on_blur() override {
    selection_state_.clear();
    Widget::on_blur();
  }

  bool has_selection() const { return selection_state_.has_selection(); }

  std::string get_selected_text() const {
    if (!has_selection()) return "";
    int s, e;
    selection_state_.get_range(s, e);
    std::string plain = get_full_text();
    return plain.substr(s, e - s);
  }

 private:
  SelectionState selection_state_;

  void clear_selection() { selection_state_.clear(); }

  std::string get_full_text() const {
    std::string result;
    for (size_t i = 0; i < items.size(); ++i) {
      if (i > 0) result += "\n";
      result += items[i].text;
    }
    return result;
  }

  int visual_to_char_idx(int vx, int vy) const {
    int line_offset = 0;
    int char_in_full_text_offset = 0;
    std::vector<int> level_counts(10, 0);

    for (size_t i = 0; i < items.size(); ++i) {
      auto &item = items[i];
      std::string marker = get_marker(item.level, level_counts[item.level]++);
      for (int l = item.level + 1; l < 10; ++l) level_counts[l] = 0;

      int marker_width = utf8_display_width(marker);
      int base_indent = item.level * indent_per_level;
      int text_width = width - base_indent - marker_width;

      std::vector<std::string> lines;
      if (word_wrap && text_width > 0)
        lines = wrap_text(item.text, text_width);
      else
        lines = {item.text};

      int item_char_offset = 0;
      for (int li = 0; li < (int)lines.size(); ++li) {
        int cur_y = y + line_offset;
        if (vy == cur_y) {
          int cur_indent =
              base_indent + marker_width + (li == 0 ? 0 : wrap_indent);
          int rel_x = vx - x - cur_indent;
          if (rel_x < 0) return char_in_full_text_offset + item_char_offset;

          auto chars = TextHelper::prepare_text_for_render(lines[li]);
          int t_vx = 0;
          for (int ci = 0; ci < (int)chars.size(); ++ci) {
            if (rel_x < t_vx + chars[ci].display_width / 2)
              return char_in_full_text_offset + item_char_offset + ci;
            t_vx += chars[ci].display_width;
          }
          return char_in_full_text_offset + item_char_offset +
                 (int)chars.size();
        }
        line_offset++;
        item_char_offset += (int)lines[li].length() + 1;
      }
      char_in_full_text_offset += (int)item.text.length() + 1;
    }
    return (int)get_full_text().length();
  }

  void select_word_at(int pos) {
    std::string full = get_full_text();
    auto chars = TextHelper::prepare_text_for_render(full);
    if (pos < 0 || pos >= (int)chars.size()) return;
    int s = 0, e = 0;
    TextHelper::select_word_at(chars, pos, s, e);
    selection_state_.start = s;
    selection_state_.end = e;
  }

  void select_item_at(int pos) {
    // Simple: select the whole item that contains 'pos'
    int char_offset = 0;
    for (const auto &item : items) {
      int next_offset = char_offset + (int)item.text.length();
      if (pos >= char_offset && pos <= next_offset) {
        selection_state_.start = char_offset;
        selection_state_.end = next_offset;
        selection_state_.drag_start_idx = next_offset;
        selection_state_.mouse_down = true;
        return;
      }
      char_offset = next_offset + 1;
    }
  }

  std::vector<std::string> wrap_text(const std::string &input,
                                     int max_width) const {
    std::vector<std::string> result;
    if (input.empty()) {
      result.push_back("");
      return result;
    }
    std::istringstream stream(input);
    std::string line;
    while (std::getline(stream, line)) {
      if (line.empty()) {
        result.push_back("");
        continue;
      }
      std::vector<std::string> words;
      std::istringstream word_stream(line);
      std::string word;
      while (word_stream >> word) words.push_back(word);
      if (words.empty()) {
        result.push_back("");
        continue;
      }

      std::string current_line;
      for (const auto &w : words) {
        if (current_line.empty())
          current_line = w;
        else if ((int)(utf8_display_width(current_line) + 1 +
                       utf8_display_width(w)) <= max_width)
          current_line += " " + w;
        else {
          result.push_back(current_line);
          current_line = w;
        }
      }
      if (!current_line.empty()) result.push_back(current_line);
    }
    return result;
  }
};

/// @brief A clickable button widget
class Button : public Widget {
 public:
  StyledText styled_label_;

  /// @param label The text on the button
  /// @param on_click Callback function to execute on click
  Button(StyledText label, std::function<void()> on_click = {})
      : styled_label_(label), label_(label.plain_text()), on_click_(on_click) {
    focusable = true;
    height = 1;
    fixed_height = 1;
  }

  std::function<void()> on_double_click;
  std::function<void()> on_triple_click;

  /// @brief Get the button label
  const std::string &get_label() const { return label_; }

  /// @brief Set the button label
  void set_label(const StyledText &label) {
    styled_label_ = label;
    label_ = label.plain_text();
  }

  /// @brief Set the on_click callback
  void set_on_click(std::function<void()> on_click) { on_click_ = on_click; }

  // Use defaults from Theme if not manually overridden
  Color bg_color = {0, 0, 0, true};
  Color hover_color = {0, 0, 0, true};
  Color focus_color = {0, 0, 0, true};
  Color text_color = {0, 0, 0, true};

  Alignment alignment =
      Alignment::Center;  ///< Text alignment within the button

  void render(Buffer &buffer) override {
    // 1. Resolve visual state
    Color current_bg = bg_color.resolve(Theme::current().panel_bg);
    Color current_fg =
        text_color.is_default ? Color::contrast_color(current_bg) : text_color;

    if (focused_) {
      Color focus = focus_color.resolve(Theme::current().primary);
      current_bg = pressed_ ? current_fg : focus;
      current_fg = pressed_ ? current_bg : Color::contrast_color(focus);
    } else if (hovered_) {
      Color hover = hover_color.resolve(Theme::current().hover);
      current_bg = pressed_ ? current_fg : hover;
      current_fg = pressed_ ? current_bg : Color::contrast_color(hover);
    }

    // 2. Fill Background
    for (int dy = 0; dy < height; ++dy) {
      for (int dx = 0; dx < width; ++dx) {
        Cell c;
        c.content = " ";
        c.bg_color = current_bg;
        c.fg_color = current_fg;
        buffer.set(x + dx, y + dy, c);
      }
    }

    // 3. Render Text Label
    std::vector<CharInfo> chars = prepare_text_for_render(label_);
    int label_display_width = 0;
    for (const auto &ci : chars) label_display_width += ci.display_width;

    int text_start = 0;
    if (alignment == Alignment::Center)
      text_start = std::max(0, (int)(width - label_display_width) / 2);
    else if (alignment == Alignment::Right)
      text_start = std::max(0, width - label_display_width - 1);
    else
      text_start = 1;

    if (height > 0) {
      int dy = height / 2;
      int current_dx = 0;
      for (size_t i = 0; i < chars.size(); ++i) {
        if (text_start + current_dx >= width) break;

        const auto &ci = chars[i];
        Cell c;
        c.content = ci.content;
        c.bg_color = current_bg;
        c.fg_color = current_fg;

        // Apply span styles
        const TextSpan *span = styled_label_.get_span_at_char((int)i);
        if (span) {
          c.bold = span->bold;
          c.italic = span->italic;
          c.underline = span->underline;
          if (!span->color.is_default) c.fg_color = span->color;
        }

        buffer.set(x + text_start + current_dx, y + dy, c);

        // Handle wide characters by placing an empty skip cell
        if (ci.display_width == 2 && text_start + current_dx + 1 < width) {
          Cell skip = c;
          skip.content = "";
          buffer.set(x + text_start + current_dx + 1, y + dy, skip);
        }

        current_dx += ci.display_width;
      }
    }
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      bool hit = (event.x >= x && event.x < x + width && event.y >= y &&
                  event.y < y + height);

      // Handle Hover
      if (hit != hovered_) {
        set_hovered(hit);
        // If we are active (dragging), update pressed state based on hit
        if (active_) {
          pressed_ = hit;
        }
        return true;  // Re-render
      }

      // Mouse Logic
      if (event.mouse_left()) {
        // Mouse Down
        if (hit) {
          active_ = true;
          pressed_ = true;
          return true;
        }
      } else if (event.mouse_drag()) {
        // Mouse Drag
        if (active_) {
          if (pressed_ != hit) {
            pressed_ = hit;
            return true;
          }
        }
      } else if (event.mouse_release()) {
        // Mouse Up
        bool was_pressed = pressed_;
        active_ = false;
        pressed_ = false;

        if (was_pressed && hit) {
          if (event.click_count == 2 && on_double_click) {
            on_double_click();
          } else if (event.click_count == 3 && on_triple_click) {
            on_triple_click();
          } else if (on_click_) {
            on_click_();
          }
          return true;
        }
        if (was_pressed) return true;  // Re-render to clear pressed state
      }
    } else if (event.is_key_event() && focused_) {
      // Check for Enter or Space
      if (event.is_activate()) {
        if (on_click_) on_click_();
        return true;
      }
    }
    return false;
  }

 private:
  std::string label_;
  std::function<void()> on_click_;
  bool active_ = false;   // Mouse down started here
  bool pressed_ = false;  // Currently visually pressed;
};

/// @brief Generic undo/redo history manager for text input widgets
/// @tparam State The state type to track (e.g., InputState, TextAreaState)
template <typename State>
class UndoRedoHistory {
 public:
  static constexpr size_t MAX_HISTORY = 100;

  /// @brief Push a new state to the history
  /// @param state The state to save
  void push(const State &state) {
    // If we're not at the end of history, truncate redo states
    if (current_idx_ < history_.size()) {
      history_.erase(history_.begin() + current_idx_, history_.end());
    }

    // Add new state
    history_.push_back(state);

    // Enforce max history size
    if (history_.size() > MAX_HISTORY) {
      history_.erase(history_.begin());
    } else {
      current_idx_++;
    }
  }

  /// @brief Check if undo is possible
  bool can_undo() const { return current_idx_ > 0; }

  /// @brief Check if redo is possible
  bool can_redo() const { return current_idx_ < history_.size(); }

  /// @brief Undo to previous state
  /// @param current The current state (saved before returning previous)
  /// @return The previous state, or nullopt if no history
  std::optional<State> undo(const State &current) {
    if (!can_undo()) return std::nullopt;

    // If at the end of history, save current state for redo
    if (current_idx_ == history_.size()) {
      history_.push_back(current);
    }

    current_idx_--;
    return history_[current_idx_];
  }

  /// @brief Redo to next state
  /// @return The next state, or nullopt if no redo available
  std::optional<State> redo() {
    if (!can_redo()) return std::nullopt;

    current_idx_++;
    if (current_idx_ < history_.size()) {
      return history_[current_idx_];
    }
    return std::nullopt;
  }

  /// @brief Clear all history
  void clear() {
    history_.clear();
    current_idx_ = 0;
  }

  /// @brief Check if history is empty
  bool empty() const { return history_.empty(); }

 private:
  std::vector<State> history_;
  size_t current_idx_ = 0;
};

/// @brief An interactive text input field
class Input : public Widget

{
 public:
  Input() {
    focusable = true;
    selection_state_.inclusive_drag = false;
    height = 1;
    fixed_height = 1;
  }

  // Configuration
  std::string placeholder = "";
  std::string empty_char = " ";  // Default filler (space for clean modern look)
  bool accepts_tab =
      false;  ///< If true, Tab key inserts spaces instead of moving focus
  int tab_size = 4;          ///< Number of spaces for Tab key
  bool is_password = false;  ///< If true, obscure input text
  std::string password_char =
      "*";  ///< Character used to obscure input text when is_password is true

  // Colors
  Color fg_color = Color();
  Color bg_color = Color();
  Color focused_fg_color = Color();
  Color focused_bg_color = Color();
  Color placeholder_color = Color();
  Color cursor_color = Color();  // If default uses primary

  // Validation
  std::string regex_pattern = "";
  Color error_fg_color = Color{255, 50, 50};  // Red text on error

  Color error_bg_color = Color();

  std::function<void(std::string)> on_change;

  bool is_valid() const {
    if (regex_pattern.empty()) return true;
    try {
      std::regex re(regex_pattern);
      return std::regex_match(value_, re);
    } catch (...) {
      return false;
    }
  }

  void render(Buffer &buffer) override {
    update_view_offset();
    bool focused = has_focus();
    bool valid = is_valid();

    // Base Style
    Color base_fg = fg_color.resolve(Theme::current().input_fg);
    Color base_bg = bg_color.resolve(Theme::current().input_bg);

    // Focused Style overrides Base
    if (focused) {
      if (!focused_fg_color.is_default) base_fg = focused_fg_color;
      if (!focused_bg_color.is_default) base_bg = focused_bg_color;
    }

    // Text Color (inherits base, but changes on error)
    Color text_fg = base_fg;
    Color text_bg = base_bg;

    if (!valid && !value_.empty()) {
      if (!error_fg_color.is_default) text_fg = error_fg_color;
      if (!error_bg_color.is_default) text_bg = error_bg_color;
    }

    // Pre-compute UTF-8 characters for value
    std::vector<CharInfo> value_chars =
        TextHelper::prepare_text_for_render(value_);

    if (is_password && !password_char.empty()) {
      int mask_width = TextHelper::utf8_display_width(password_char);
      for (auto &ci : value_chars) {
        ci.content = password_char;
        ci.display_width = mask_width;
      }
    }

    // Compute display width up to cursor position
    int cursor_visual_x = 0;
    for (size_t i = 0; i < cursor_char_pos_ && i < value_chars.size(); ++i) {
      cursor_visual_x += value_chars[i].display_width;
    }

    // Pre-compute placeholder chars
    std::vector<CharInfo> placeholder_chars =
        TextHelper::prepare_text_for_render(placeholder);

    int text_y = height / 2;  // Vertical Center as requested for flexed inputs

    for (int dy = 0; dy < height; ++dy) {
      if (dy != text_y) {
        // Empty background lines
        for (int dx = 0; dx < width; ++dx) {
          Cell cell;
          cell.content = " ";
          cell.fg_color = base_fg;
          cell.bg_color = base_bg;
          buffer.set(x + dx, y + dy, cell);
        }
        continue;
      }

      // Text line - proper UTF-8 rendering with scrolling
      int screen_dx = 0;

      if (value_.empty()) {
        // Render placeholder (left-aligned, no scroll)
        size_t ph_idx = 0;
        while (screen_dx < width && ph_idx < placeholder_chars.size()) {
          const auto &ci = placeholder_chars[ph_idx];
          Cell cell;
          cell.bg_color = base_bg;
          cell.content = ci.content;
          cell.fg_color =
              placeholder_color.resolve(Theme::current().input_placeholder);

          buffer.set(x + screen_dx, y + dy, cell);
          if (ci.display_width == 2 && screen_dx + 1 < width) {
            screen_dx++;
            Cell skip;
            skip.content = "";
            skip.bg_color = base_bg;
            buffer.set(x + screen_dx, y + dy, skip);
          }

          screen_dx++;
          ph_idx++;
        }
      } else {
        // Render value with scroll offset
        int current_visual_pos = 0;
        for (size_t v_idx = 0; v_idx < value_chars.size(); ++v_idx) {
          const auto &ci = value_chars[v_idx];
          int char_width = ci.display_width;
          int rel_start = current_visual_pos - view_offset_;

          // If fully or partially visible
          if (rel_start + char_width > 0 && rel_start < width) {
            Cell cell;
            cell.content = ci.content;

            // Check if this character is selected
            bool in_selection = selection_state_.is_selected((int)v_idx);
            if (in_selection) {
              cell.bg_color = Theme::current().selection;
              cell.fg_color = Color::contrast_color(cell.bg_color);
            } else {
              cell.bg_color = text_bg;
              cell.fg_color = text_fg;
            }

            // Support partially visible wide characters on the left edge if
            // needed, but standard terminal is cell-based. If rel_start < 0, we
            // skip.
            if (rel_start >= 0) {
              buffer.set(x + rel_start, y + dy, cell);
              if (char_width == 2 && rel_start + 1 < width) {
                Cell skip;
                skip.content = "";
                skip.bg_color = cell.bg_color;
                buffer.set(x + rel_start + 1, y + dy, skip);
              }
            }
          }
          current_visual_pos += char_width;
        }

        // We need to know where the text ended on screen to fill the rest
        int total_visual_width = 0;
        for (auto &c : value_chars) total_visual_width += c.display_width;
        screen_dx = std::max(0, total_visual_width - view_offset_);
      }

      // Fill remaining space with background
      for (int d = screen_dx; d < width; ++d) {
        Cell cell;
        cell.content = empty_char;
        cell.fg_color = base_fg;
        cell.bg_color = base_bg;
        buffer.set(x + d, y + dy, cell);
      }

      // Cursor Overlay
      if (focused) {
        int cursor_screen_x = cursor_visual_x - view_offset_;
        if (cursor_screen_x >= 0 && cursor_screen_x < width) {
          Color cc = cursor_color.resolve(Theme::current().primary);
          Cell c_cursor;
          c_cursor.bg_color = cc;
          c_cursor.fg_color = Theme::current().background;
          c_cursor.content = " ";
          if (cursor_char_pos_ < value_chars.size()) {
            c_cursor.content = value_chars[cursor_char_pos_].content;
          }
          buffer.set(x + cursor_screen_x, y + dy, c_cursor);
        }
      }
    }
  }

  void on_blur() override {
    clear_selection();
    Widget::on_blur();
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      if (event.x >= x && event.x < x + width && event.y >= y &&
          event.y < y + height) {
        int click_visual_x = event.x - x + view_offset_;
        std::vector<CharInfo> value_chars =
            TextHelper::prepare_text_for_render(value_);
        int char_pos =
            TextHelper::visual_to_char_pos(value_chars, click_visual_x);

        if (event.mouse_left()) {
          set_focus(true);

          // Handle press via SelectionState
          selection_state_.handle_mouse_press(value_chars, char_pos,
                                              event.click_count);

          // Sync cursor
          if (selection_state_.start != selection_state_.end) {
            // Double click happened (range selected)
            cursor_char_pos_ = selection_state_.end;
          } else {
            // Single click
            cursor_char_pos_ = char_pos;
          }

          update_view_offset();
          return true;
        } else if (event.mouse_drag() && selection_state_.mouse_down) {
          selection_state_.handle_mouse_drag(char_pos);
          cursor_char_pos_ = char_pos;
          update_view_offset();
          return true;
        }
      }

      // Handle drag outside
      if (event.mouse_drag() && selection_state_.mouse_down) {
        int click_visual_x = event.x - x + view_offset_;
        std::vector<CharInfo> value_chars = TextHelper::prepare_text_for_render(
            value_);  // Optimization: only if changed?
        // Actually checking bounds
        int char_pos = 0;
        if (click_visual_x < 0)
          char_pos = 0;
        else {
          // Estimate if past end
          // Just use helper, it clamps max
          char_pos = TextHelper::visual_to_char_pos(
              value_chars, std::max(0, click_visual_x));
        }

        selection_state_.handle_mouse_drag(char_pos);
        cursor_char_pos_ = char_pos;
        update_view_offset();
        return true;
      }

      if (event.mouse_release()) {
        if (selection_state_.handle_mouse_release()) return true;
      }

      return false;
    }

    // Handle Key Events
    if (has_focus() && event.is_key_event()) {
      bool changed = false;

      // Handle Ctrl+A
      if (event.is_select_all()) {
        selection_state_.drag_start_idx = 0;
        cursor_char_pos_ = (int)count_chars();
        selection_state_.start = 0;
        selection_state_.end = cursor_char_pos_;
        update_view_offset();
        return true;
      }

      // Handle Undo (Ctrl+Z)
      if (event.is_undo()) {
        auto prev_state = undo_history_.undo(get_current_state());
        if (prev_state) {
          restore_state(*prev_state);
          if (on_change) on_change(value_);
        }
        return true;
      }

      // Handle Redo (Ctrl+Shift+Z or Ctrl+Y)
      if (event.is_redo()) {
        auto next_state = undo_history_.redo();
        if (next_state) {
          restore_state(*next_state);
          if (on_change) on_change(value_);
        }
        return true;
      }

      // Handle Copy
      if (event.is_copy()) {
        if (has_selection()) copy_to_clipboard(get_selected_text());
        return true;
      }

      // Handle Cut
      if (event.is_cut()) {
        if (has_selection()) {
          save_undo_state();
          copy_to_clipboard(get_selected_text());
          delete_selection();
          changed = true;
        }
        update_view_offset();
        if (changed && on_change) on_change(value_);
        return true;
      }

      // Handle Paste
      if (event.is_paste()) {
        std::string text = paste_from_clipboard();
        if (!text.empty()) {
          save_undo_state();
          if (has_selection()) delete_selection();

          std::string filtered;
          for (size_t i = 0; i < text.size();) {
            uint32_t cp;
            int len;
            if (utf8_decode_codepoint(text, i, cp, len)) {
              if (cp != '\n' && cp != '\r')
                filtered.append(text.substr(i, len));
              i += len;
            } else
              i++;
          }

          if (!filtered.empty()) {
            insert_text_at_cursor(filtered);
            if (on_change) on_change(value_);
          }
        }
        return true;
      }

      // Handle Tab
      if (event.is_tab()) {
        if (accepts_tab) {
          save_undo_state();
          if (has_selection()) delete_selection();
          std::string indent(tab_size, ' ');
          insert_text_at_cursor(indent);
          if (on_change) on_change(value_);
          update_view_offset();
          return true;
        }
        return false;
      }

      // Navigation Keys
      bool is_nav_key = event.is_nav_left() || event.is_nav_right() ||
                        event.is_nav_home() || event.is_nav_end();

      if (is_nav_key) {
        if (event.shift)
          start_selection();
        else
          clear_selection();
      }

      // Arrow keys
      if (event.is_nav_left()) {
        if (event.ctrl)
          cursor_char_pos_ = find_prev_word_boundary();
        else if (cursor_char_pos_ > 0)
          cursor_char_pos_--;
      } else if (event.is_nav_right()) {
        if (event.ctrl)
          cursor_char_pos_ = find_next_word_boundary();
        else {
          size_t char_count = count_chars();
          if (cursor_char_pos_ < char_count) cursor_char_pos_++;
        }
      } else if (event.is_nav_home()) {
        cursor_char_pos_ = 0;
      } else if (event.is_nav_end()) {
        cursor_char_pos_ = (int)count_chars();
      }

      // Sync Keyboard Selection
      if (is_nav_key && selection_state_.drag_start_idx != -1) {
        selection_state_.start =
            std::min(selection_state_.drag_start_idx, (int)cursor_char_pos_);
        selection_state_.end =
            std::max(selection_state_.drag_start_idx, (int)cursor_char_pos_);
      }

      if (is_nav_key) {
        update_view_offset();
        return true;
      }

      if (event.is_backspace()) {
        save_undo_state();
        if (has_selection()) {
          delete_selection();
          changed = true;
        } else if (!value_.empty() && cursor_char_pos_ > 0) {
          size_t byte_pos = char_to_byte_pos(cursor_char_pos_);
          size_t prev_byte_pos = char_to_byte_pos(cursor_char_pos_ - 1);
          value_.erase(prev_byte_pos, byte_pos - prev_byte_pos);
          cursor_char_pos_--;
          changed = true;
        }
      } else if (event.is_delete()) {
        save_undo_state();
        if (has_selection()) {
          delete_selection();
          changed = true;
        } else {
          size_t char_count = count_chars();
          if (cursor_char_pos_ < char_count) {
            size_t byte_pos = char_to_byte_pos(cursor_char_pos_);
            size_t next_byte_pos = char_to_byte_pos(cursor_char_pos_ + 1);
            value_.erase(byte_pos, next_byte_pos - byte_pos);
            changed = true;
          }
        }
      } else if (event.is_printable()) {
        save_undo_state();
        if (has_selection()) delete_selection();

        size_t byte_pos = char_to_byte_pos(cursor_char_pos_);
        value_.insert(byte_pos, 1, (char)event.key);
        cursor_char_pos_++;
        clear_selection();
        changed = true;
      }

      if (changed) {
        if (on_change) on_change(value_);
      }

      update_view_offset();
      return true;
    }

    // Handle Paste event (bracketed paste)
    if (has_focus() && event.type == EventType::Paste &&
        !event.paste_text.empty()) {
      save_undo_state();
      if (has_selection()) delete_selection();

      // Insert pasted text at cursor (filter out newlines for single-line
      // input)
      std::string filtered;
      for (char c : event.paste_text) {
        if (c != '\n' && c != '\r') filtered += c;
      }

      size_t byte_pos = char_to_byte_pos(cursor_char_pos_);
      value_.insert(byte_pos, filtered);

      // Advance cursor by number of characters pasted
      size_t pos = 0;
      int char_count = 0;
      while (pos < filtered.size()) {
        uint32_t cp;
        int len;
        if (utf8_decode_codepoint(filtered, pos, cp, len)) {
          char_count++;
          pos += len;
        } else {
          pos++;
        }
      }
      cursor_char_pos_ += char_count;
      clear_selection();
      update_view_offset();

      if (on_change) on_change(value_);
      return true;
    }

    return false;
  }

  /// @brief Get current input value
  std::string get_value() const { return value_; }

  /// @brief Set input value logic, resetting cursor
  void set_value(const std::string &v) {
    if (value_ != v) {
      save_undo_state();
      value_ = v;
      clear_selection();
      // Count characters and set cursor to end
      cursor_char_pos_ = 0;
      size_t pos = 0;
      while (pos < value_.size()) {
        uint32_t cp;
        int len;
        if (utf8_decode_codepoint(value_, pos, cp, len)) {
          cursor_char_pos_++;
          pos += len;
        } else {
          pos++;
        }
      }
      update_view_offset();
      if (on_change) on_change(v);
    }
  }

  /// @brief Alias for set_value
  void set_text(const std::string &v) { set_value(v); }

 private:
  std::string value_;
  size_t cursor_char_pos_ = 0;  // Cursor position in characters (not bytes)
  int view_offset_ = 0;         // Horizontal scroll offset (in visual cells)

  // Undo/Redo state
  struct InputState {
    std::string value;
    size_t cursor_pos;
  };
  UndoRedoHistory<InputState> undo_history_;

  /// @brief Get current state for undo/redo
  InputState get_current_state() const { return {value_, cursor_char_pos_}; }

  /// @brief Restore state from undo/redo
  void restore_state(const InputState &state) {
    value_ = state.value;
    cursor_char_pos_ = state.cursor_pos;
    // Clamp cursor position
    size_t char_count = count_chars();
    if (cursor_char_pos_ > char_count) cursor_char_pos_ = char_count;
    clear_selection();
    update_view_offset();
  }

  /// @brief Save current state before a modifying operation
  void save_undo_state() { undo_history_.push(get_current_state()); }

  void update_view_offset() {
    if (width <= 0) {
      view_offset_ = 0;
      return;
    }
    // Calculate cursor visual position
    int cursor_visual_x = 0;
    size_t pos = 0;
    size_t count = 0;
    while (pos < value_.size() && count < cursor_char_pos_) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(value_, pos, cp, len)) {
        cursor_visual_x += char_display_width(cp);
        pos += len;
        count++;
      } else {
        pos++;
      }
    }

    // Adjust Offset
    if (cursor_visual_x < view_offset_) {
      view_offset_ = cursor_visual_x;
    } else if (cursor_visual_x >= view_offset_ + width) {
      view_offset_ = cursor_visual_x - width + 1;
    }
    if (view_offset_ < 0) view_offset_ = 0;
  }

  /// @brief Count UTF-8 characters in value_
  size_t count_chars() const { return TextHelper::count_codepoints(value_); }

 protected:
  SelectionState selection_state_;

 public:
  /// @brief Clear the current selection
  void clear_selection() { selection_state_.clear(); }

  /// @brief Check if there is an active selection
  bool has_selection() const { return selection_state_.has_selection(); }

  /// @brief Start a selection at the current cursor position (for keyboard)
  void start_selection() {
    if (selection_state_.drag_start_idx < 0) {
      selection_state_.drag_start_idx = (int)cursor_char_pos_;
      // We don't set mouse_down here as it's keyboard initiated.
      // We will manually call handle_mouse_drag in on_event.
    }
  }

  /// @brief Get the selection range in normalized form (start <= end)
  void get_selection_range(int &start, int &end) const {
    if (!has_selection()) {
      start = -1;
      end = -1;
      return;
    }
    start = selection_state_.start;
    end = selection_state_.end;
  }

  /// @brief Delete the selected text
  void delete_selection() {
    if (!has_selection()) return;

    int start = selection_state_.start;
    int end = selection_state_.end;

    // Delete range
    size_t b_start = TextHelper::char_to_byte_pos(value_, start);
    size_t b_end = TextHelper::char_to_byte_pos(value_, end);

    if (b_end > b_start) {
      value_.erase(b_start, b_end - b_start);
      cursor_char_pos_ = start;
      clear_selection();
    }
  }

  /// @brief Get the selected text as a string
  std::string get_selected_text() const {
    if (!has_selection()) return "";
    // Use TextHelper for safe UTF-8 substring.
    // selection_state_.end is exclusive (inclusive_drag=false).
    return TextHelper::utf8_substr(
        value_, selection_state_.start,
        selection_state_.end - selection_state_.start);
  }

 private:
  /// @brief Get byte position from character position
  size_t char_to_byte_pos(size_t char_idx) const {
    return TextHelper::char_to_byte_pos(value_, char_idx);
  }

  // Helper to insert text at cursor
  void insert_text_at_cursor(const std::string &text) {
    size_t byte_pos = char_to_byte_pos(cursor_char_pos_);
    value_.insert(byte_pos, text);

    // Advance cursor by number of characters in inserted text
    // TextHelper doesn't have a direct "count chars" but we can use
    // prepare_text_for_render or a loop. Loop is more efficient than allocating
    // vector<CharInfo>.
    size_t pos = 0;
    int char_count = 0;
    while (pos < text.size()) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(text, pos, cp, len)) {
        char_count++;
        pos += len;
      } else {
        pos++;
      }
    }
    cursor_char_pos_ += char_count;
    update_view_offset();
  }

  /// @brief Find previous word boundary for Ctrl+Left navigation
  size_t find_prev_word_boundary() const {
    if (cursor_char_pos_ == 0) return 0;

    // Parse characters
    std::vector<uint32_t> codepoints;
    size_t pos = 0;
    while (pos < value_.size()) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(value_, pos, cp, len)) {
        codepoints.push_back(cp);
        pos += len;
      } else {
        pos++;
      }
    }

    // Start one position before cursor
    int idx = (int)cursor_char_pos_ - 1;

    // Skip any whitespace
    while (idx > 0 && (codepoints[idx] == ' ' || codepoints[idx] == '\t'))
      idx--;

    // Move back to start of word (alphanumeric run)
    while (idx > 0 && codepoints[idx - 1] != ' ' && codepoints[idx - 1] != '\t')
      idx--;

    return (size_t)std::max(0, idx);
  }

  /// @brief Find next word boundary for Ctrl+Right navigation
  size_t find_next_word_boundary() const {
    // Parse characters
    std::vector<uint32_t> codepoints;
    size_t pos = 0;
    while (pos < value_.size()) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(value_, pos, cp, len)) {
        codepoints.push_back(cp);
        pos += len;
      } else {
        pos++;
      }
    }

    size_t idx = cursor_char_pos_;
    size_t len = codepoints.size();

    // Skip any leading whitespace
    while (idx < len && (codepoints[idx] == ' ' || codepoints[idx] == '\t'))
      idx++;

    // Skip current word to its end
    while (idx < len && codepoints[idx] != ' ' && codepoints[idx] != '\t')
      idx++;

    return idx;
  }
};

/// @brief A multi-line text editor widget
class TextArea : public Widget {
 public:
  TextArea() {
    focusable = true;
    tab_stop = true;
    lines_.push_back("");
  }

  // Configuration
  bool show_line_numbers = true;
  bool word_wrap = false;
  bool show_scrollbar = true;
  bool accepts_tab =
      false;  ///< If true, Tab key inserts spaces instead of moving focus
  int tab_size = 4;  ///< Number of spaces for Tab key
  Color line_number_fg = Color(100, 100, 100);
  Color line_number_bg = Color();
  Color cursor_color = Color();
  Color selection_bg = Color();

  std::function<void(std::string)> on_change;
  std::function<void(int, int)> on_cursor_move;

  void on_blur() override {
    clear_selection();
    Widget::on_blur();
  }

  void set_text(const std::string &text) {
    save_undo_state();
    clear_selection();
    lines_.clear();
    std::string line;
    for (char c : text) {
      if (c == '\n') {
        lines_.push_back(line);
        line.clear();
      } else if (c != '\r') {
        line += c;
      }
    }
    lines_.push_back(line);
    if (lines_.empty()) lines_.push_back("");
    cursor_x_ = 0;
    cursor_y_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    needs_recalc_max_width_ = true;
    cached_max_visual_width_ = -1;
    recalculate_virtual_lines();
    ensure_cursor_visible();
  }

  /// @brief Check if there is an active selection
  bool has_selection() const {
    return sel_anchor_x_ >= 0 && sel_anchor_y_ >= 0 &&
           (sel_anchor_x_ != cursor_x_ || sel_anchor_y_ != cursor_y_);
  }

  void load_from_stream(std::istream &is) {
    clear_selection();
    lines_.clear();
    std::string line;
    while (std::getline(is, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      lines_.push_back(line);
    }
    if (lines_.empty()) lines_.push_back("");

    cursor_x_ = 0;
    cursor_y_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    needs_recalc_max_width_ = true;
    cached_max_visual_width_ = -1;
    recalculate_virtual_lines();
    ensure_cursor_visible();
  }

  std::string get_text() const {
    std::string result;
    for (size_t i = 0; i < lines_.size(); ++i) {
      result += lines_[i];
      if (i < lines_.size() - 1) result += "\n";
    }
    return result;
  }

  void recalculate_virtual_lines() {
    virtual_lines_.clear();
    int ln_width = get_line_number_width();
    int text_width = width - ln_width;
    if (show_scrollbar) text_width -= 1;
    if (text_width <= 0) text_width = 1;

    if (!word_wrap) {
      for (size_t i = 0; i < lines_.size(); ++i) {
        auto chars = prepare_text_for_render(lines_[i]);
        virtual_lines_.push_back({i, 0, (int)chars.size(), false});
      }
      return;
    }

    for (size_t i = 0; i < lines_.size(); ++i) {
      auto chars = prepare_text_for_render(lines_[i]);
      if (chars.empty()) {
        virtual_lines_.push_back({i, 0, 0, false});
        continue;
      }

      int current_line_visual_width = 0;
      int start_char = 0;
      bool is_cont = false;

      for (int c_idx = 0; c_idx < (int)chars.size(); ++c_idx) {
        int cw = chars[c_idx].display_width;
        if (current_line_visual_width + cw > text_width &&
            current_line_visual_width > 0) {
          // Wrap
          virtual_lines_.push_back(
              {i, start_char, c_idx - start_char, is_cont});
          start_char = c_idx;
          current_line_visual_width = cw;
          is_cont = true;
        } else {
          current_line_visual_width += cw;
        }
      }
      // Last segment
      virtual_lines_.push_back(
          {i, start_char, (int)chars.size() - start_char, is_cont});
    }
  }

  void render(Buffer &buffer) override {
    if (width != last_width_ || height != last_height_ ||
        word_wrap != last_word_wrap_ || show_line_numbers != last_show_ln_ ||
        show_scrollbar != last_show_sb_) {
      last_width_ = width;
      last_height_ = height;
      last_word_wrap_ = word_wrap;
      last_show_ln_ = show_line_numbers;
      last_show_sb_ = show_scrollbar;
      recalculate_virtual_lines();
    }

    // Just ensure max width is up to date if needed, though usually
    // recalc_virtual_lines handles it
    if (!word_wrap && needs_recalc_max_width_) {
      get_max_visual_width();
    }

    bool focused = has_focus();
    int ln_width = get_line_number_width();
    int text_width = width - ln_width;
    if (show_scrollbar) text_width -= 1;

    int max_visual_width = 0;
    if (!word_wrap) {
      max_visual_width = get_max_visual_width();
    }

    int visible_height = height;
    bool show_h_scroll =
        (show_scrollbar && !word_wrap && max_visual_width > text_width);
    if (show_h_scroll) visible_height -= 1;

    if (text_width <= 0 || visible_height <= 0) return;

    Color base_fg = Theme::current().input_fg;
    Color base_bg = Theme::current().input_bg;
    Color ln_fg = line_number_fg.resolve(Color(100, 100, 100));
    Color ln_bg = line_number_bg.is_default ? base_bg : line_number_bg;
    Color cc = cursor_color.resolve(Theme::current().primary);

    for (int dy = 0; dy < visible_height; ++dy) {
      int v_line_idx = dy + scroll_y_;
      int sy = y + dy;

      if (v_line_idx < (int)virtual_lines_.size()) {
        const auto &vl = virtual_lines_[v_line_idx];

        // Render Line Number (only if start of logical line)
        if (show_line_numbers) {
          std::string ln_str = "";
          if (!vl.is_continuation) {
            ln_str = std::to_string(vl.logical_line_idx + 1);
          }

          for (int dx = 0; dx < ln_width; ++dx) {
            Cell cell;
            cell.bg_color = ln_bg;
            cell.fg_color = ln_fg;
            cell.content = " ";

            int text_pos = dx - (ln_width - (int)ln_str.length() - 1);
            if (text_pos >= 0 && text_pos < (int)ln_str.length()) {
              cell.content = ln_str.substr(text_pos, 1);
            }
            buffer.set(x + dx, sy, cell);
          }
        }

        // Render Text Segment
        const std::string &logical_text = lines_[vl.logical_line_idx];
        auto chars = prepare_text_for_render(logical_text);

        int current_visual_x = 0;  // Relative to start of visual segment
        for (int i = 0; i < vl.char_count; ++i) {
          int char_idx = vl.start_char_idx + i;
          const auto &ci = chars[char_idx];
          int rel_x = current_visual_x - (word_wrap ? 0 : scroll_x_);

          if (rel_x >= 0 && rel_x < text_width) {
            Cell cell;
            cell.content = ci.content;

            // Check if this character is selected
            bool in_selection = is_char_selected(vl.logical_line_idx, char_idx);
            if (in_selection) {
              cell.bg_color = selection_bg.resolve(Theme::current().selection);
              cell.fg_color = Color::contrast_color(cell.bg_color);
            } else {
              cell.bg_color = base_bg;
              cell.fg_color = base_fg;
            }
            buffer.set(x + ln_width + rel_x, sy, cell);

            if (ci.display_width == 2 && rel_x + 1 < text_width) {
              Cell skip;
              skip.content = "";
              skip.bg_color = cell.bg_color;
              buffer.set(x + ln_width + rel_x + 1, sy, skip);
            }
          }
          current_visual_x += ci.display_width;
        }

        // Fill remaining line background
        int last_visual_x =
            std::max(0, current_visual_x - (word_wrap ? 0 : scroll_x_));
        for (int dx = last_visual_x; dx < text_width; ++dx) {
          Cell cell;
          cell.content = " ";
          cell.bg_color = base_bg;
          cell.fg_color = base_fg;
          buffer.set(x + ln_width + dx, sy, cell);
        }
      } else {
        // Empty lines beyond end
        for (int dx = 0; dx < width - (show_scrollbar ? 1 : 0); ++dx) {
          Cell cell;
          cell.content = " ";
          cell.bg_color =
              (show_line_numbers && dx < ln_width) ? ln_bg : base_bg;
          cell.fg_color = base_fg;
          buffer.set(x + dx, sy, cell);
        }
      }

      // Render Cursor
      if (focused) {
        int v_cursor_idx = find_v_line(cursor_y_, cursor_x_);
        if (v_line_idx == v_cursor_idx &&
            v_cursor_idx < (int)virtual_lines_.size()) {
          const auto &vl = virtual_lines_[v_cursor_idx];
          auto chars = prepare_text_for_render(lines_[cursor_y_]);

          int cursor_rel_char = cursor_x_ - vl.start_char_idx;
          int cursor_visual_x = 0;
          for (int i = 0; i < cursor_rel_char &&
                          (vl.start_char_idx + i) < (int)chars.size();
               ++i) {
            cursor_visual_x += chars[vl.start_char_idx + i].display_width;
          }

          int rel_cursor_x = cursor_visual_x - (word_wrap ? 0 : scroll_x_);
          if (rel_cursor_x >= 0 && rel_cursor_x < text_width) {
            Cell c;
            c.bg_color = cc;
            c.fg_color = Color::contrast_color(cc);
            c.content = (cursor_x_ < (int)chars.size())
                            ? chars[cursor_x_].content
                            : " ";
            if (c.content.empty()) c.content = " ";
            buffer.set(x + ln_width + rel_cursor_x, sy, c);
          }
        }
      }
    }

    // Render Scrollbars
    if (show_scrollbar) {
      if ((int)virtual_lines_.size() > visible_height) {
        render_scrollbar(buffer, x + width - 1, y, visible_height, scroll_y_,
                         (int)virtual_lines_.size(), true);
      }
      if (show_h_scroll) {
        render_scrollbar(buffer, x + ln_width, y + height - 1, text_width,
                         scroll_x_, max_visual_width, false);

        // Paint Bottom-Left Corner (Line Number Gutter intersection)
        if (show_line_numbers) {
          for (int dx = 0; dx < ln_width; ++dx) {
            Cell c;
            c.bg_color = ln_bg;
            c.fg_color = ln_fg;
            c.content = " ";  // Blank filler
            buffer.set(x + dx, y + height - 1, c);
          }
        }

        // Paint Bottom-Right Corner (Scrollbar intersection)
        // The vertical scrollbar stops 1 unit short, leaving this corner empty
        {
          Cell c;
          c.bg_color = base_bg;  // Or standard track color if preferred
          c.content = " ";
          buffer.set(x + width - 1, y + height - 1, c);
        }
      }
    }
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      bool hit = (event.x >= x && event.x < x + width && event.y >= y &&
                  event.y < y + height);

      // Handle Scrollbar Dragging
      if (show_scrollbar && (int)virtual_lines_.size() > height) {
        if (handle_scrollbar_event(event, x + width - 1, y, 1, height,
                                   (int)virtual_lines_.size(), scroll_y_,
                                   is_dragging_scrollbar_, false,
                                   [this]() { set_focus(true); })) {
          return true;
        }
      }

      // Handle Horizontal Scrollbar
      int max_visual_width = 0;
      int ln_width = get_line_number_width();
      int text_width = width - ln_width - (show_scrollbar ? 1 : 0);
      if (!word_wrap) {
        max_visual_width = get_max_visual_width();
      }
      if (show_scrollbar && !word_wrap && max_visual_width > text_width) {
        static bool dragging_h = false;
        if (handle_scrollbar_event(event, x + ln_width, y + height - 1,
                                   text_width, 1, max_visual_width, scroll_x_,
                                   dragging_h, true))
          return true;
      }

      if (hit && event.mouse_left() && !event.mouse_motion()) {
        set_focus(true);
        // Determine cursor position from click
        int ln_width = get_line_number_width();
        int click_x = event.x - (x + ln_width) + (word_wrap ? 0 : scroll_x_);
        int click_v_y = event.y - y + scroll_y_;

        int new_cursor_x = cursor_x_;
        int new_cursor_y = cursor_y_;

        if (click_v_y >= 0 && click_v_y < (int)virtual_lines_.size()) {
          const auto &vl = virtual_lines_[click_v_y];
          new_cursor_y = vl.logical_line_idx;
          new_cursor_x = visual_to_char_pos_in_segment(vl, click_x);
        } else if (click_v_y >= (int)virtual_lines_.size()) {
          new_cursor_y = (int)lines_.size() - 1;
          auto chars = prepare_text_for_render(lines_[new_cursor_y]);
          new_cursor_x = (int)chars.size();
        }

        if (event.ctrl && sel_anchor_x_ >= 0) {
          // Extend selection from existing anchor
          cursor_x_ = new_cursor_x;
          cursor_y_ = new_cursor_y;
        } else {
          if (event.click_count == 2) {
            // Double click - select word
            select_word_at_ta(new_cursor_x, new_cursor_y);
            is_selecting_ = false;
          } else if (event.click_count == 3) {
            // Triple click - select all
            cursor_y_ = (int)lines_.size() - 1;
            cursor_x_ = (int)TextHelper::count_codepoints(lines_.back());
            sel_anchor_y_ = 0;
            sel_anchor_x_ = 0;
            is_selecting_ = false;
          } else {
            // Start new selection
            cursor_x_ = new_cursor_x;
            cursor_y_ = new_cursor_y;
            sel_anchor_x_ = cursor_x_;
            sel_anchor_y_ = cursor_y_;
            is_selecting_ = true;
          }
        }
        ensure_cursor_visible();
        return true;
      }

      // Handle mouse drag for selection
      if (is_selecting_ && event.mouse_drag()) {
        int ln_width = get_line_number_width();
        int click_x = event.x - (x + ln_width) + (word_wrap ? 0 : scroll_x_);
        int click_v_y = event.y - y + scroll_y_;

        if (click_v_y >= 0 && click_v_y < (int)virtual_lines_.size()) {
          const auto &vl = virtual_lines_[click_v_y];
          cursor_y_ = vl.logical_line_idx;
          cursor_x_ = visual_to_char_pos_in_segment(vl, click_x);
        } else if (click_v_y < 0) {
          cursor_y_ = 0;
          cursor_x_ = 0;
        } else {
          cursor_y_ = (int)lines_.size() - 1;
          cursor_x_ = (int)prepare_text_for_render(lines_[cursor_y_]).size();
        }
        ensure_cursor_visible();
        return true;
      }

      // Handle mouse release
      if (event.mouse_release()) {
        is_selecting_ = false;
      }

      // Handle Scroll wheel anywhere in the widget
      if (hit && event.mouse_wheel()) {
        if (event.ctrl && !word_wrap) {
          // Horizontal Scrolling (Ctrl + Wheel)
          if (event.mouse_wheel_up())
            scroll_x_ = std::max(0, scroll_x_ - 3);
          else if (event.mouse_wheel_down())
            scroll_x_ += 3;

          // Dynamic clamping for horizontal scroll
          int ln_width = get_line_number_width();
          int text_width = width - ln_width - (show_scrollbar ? 1 : 0);
          int max_visual_width = 0;
          if (!word_wrap) {
            max_visual_width = get_max_visual_width();
          }
          int max_x = std::max(0, max_visual_width - text_width);
          if (scroll_x_ > max_x) scroll_x_ = max_x;

          if (show_scrollbar && max_visual_width > text_width) return true;
        } else {
          // Vertical Scrolling
          int old_scroll_y = scroll_y_;
          if (event.mouse_wheel_up())
            scroll_y_ = std::max(0, scroll_y_ - 3);
          else if (event.mouse_wheel_down())
            scroll_y_ += 3;

          // Clamp vertical scroll
          int max_y = std::max(0, (int)virtual_lines_.size() - height);
          if (scroll_y_ > max_y) scroll_y_ = max_y;

          if (scroll_y_ != old_scroll_y) return true;
        }
        return false;
      }

      return false;
    }

    if (has_focus() && event.is_key_event()) {
      // Handle Ctrl+A (Select All)
      if (event.is_select_all()) {
        sel_anchor_x_ = 0;
        sel_anchor_y_ = 0;
        cursor_y_ = (int)lines_.size() - 1;
        cursor_x_ = (int)prepare_text_for_render(lines_[cursor_y_]).size();
        ensure_cursor_visible();
        return true;
      }

      // Handle Undo (Ctrl+Z)
      if (event.is_undo()) {
        auto prev_state = undo_history_.undo(get_current_state());
        if (prev_state) {
          restore_state(*prev_state);
          if (on_change) on_change(get_text());
        }
        return true;
      }

      // Handle Redo (Ctrl+Shift+Z or Ctrl+Y)
      if (event.is_redo()) {
        auto next_state = undo_history_.redo();
        if (next_state) {
          restore_state(*next_state);
          if (on_change) on_change(get_text());
        }
        return true;
      }

      // Handle Copy (Ctrl+Shift+C OR Ctrl+C)
      if (event.is_copy()) {
        if (has_selection()) {
          copy_to_clipboard(get_selected_text());
        }
        return true;
      }

      // Handle Cut (Ctrl+Shift+X OR Ctrl+X)
      if (event.is_cut()) {
        if (has_selection()) {
          save_undo_state();
          copy_to_clipboard(get_selected_text());
          delete_selection();
          needs_recalc_max_width_ = true;
          recalculate_virtual_lines();
          if (on_change) on_change(get_text());
        }
        ensure_cursor_visible();
        return true;
      }
    }

    // Handle Paste (Ctrl+Shift+V OR Ctrl+V)
    if (has_focus() && event.is_paste()) {
      std::string text = paste_from_clipboard();
      if (!text.empty()) {
        save_undo_state();
        if (has_selection()) delete_selection();

        size_t i = 0;
        while (i < text.size()) {
          uint32_t cp;
          int len;
          if (utf8_decode_codepoint(text, i, cp, len)) {
            std::string char_str = text.substr(i, len);
            if (cp == '\r') {
              // Skip
            } else if (cp == '\n') {
              std::string &line = lines_[cursor_y_];
              size_t byte_pos = char_to_byte_pos(cursor_y_, cursor_x_);
              std::string suffix = line.substr(byte_pos);
              line = line.substr(0, byte_pos);
              lines_.insert(lines_.begin() + cursor_y_ + 1, suffix);
              cursor_y_++;
              cursor_x_ = 0;
            } else {
              std::string &l = lines_[cursor_y_];
              size_t byte_idx = char_to_byte_pos(cursor_y_, cursor_x_);
              l.insert(byte_idx, char_str);
              cursor_x_++;
            }
            i += len;
          } else {
            i++;
          }
        }

        needs_recalc_max_width_ = true;
        recalculate_virtual_lines();
        ensure_cursor_visible();
        if (on_change) on_change(get_text());
      }
      return true;
    }

    // Handle Paste event (bracketed paste)
    if (has_focus() && event.type == EventType::Paste &&
        !event.paste_text.empty()) {
      save_undo_state();
      if (has_selection()) delete_selection();

      // Split pasted text by lines
      std::vector<std::string> paste_lines;
      std::string current_line;
      for (size_t i = 0; i < event.paste_text.size(); ++i) {
        char c = event.paste_text[i];
        if (c == '\n') {
          paste_lines.push_back(current_line);
          current_line.clear();
        } else if (c == '\r') {
          // Handle \r as newline
          paste_lines.push_back(current_line);
          current_line.clear();

          // Check if followed by \n and skip it if so (handle \r\n)
          if (i + 1 < event.paste_text.size() &&
              event.paste_text[i + 1] == '\n') {
            i++;
          }
        } else {
          current_line += c;
        }
      }
      paste_lines.push_back(current_line);

      if (paste_lines.size() == 1) {
        // Single line paste - insert at cursor
        size_t byte_pos = char_to_byte_pos(cursor_y_, cursor_x_);
        lines_[cursor_y_].insert(byte_pos, paste_lines[0]);

        // Advance cursor
        auto chars = prepare_text_for_render(paste_lines[0]);
        cursor_x_ += (int)chars.size();
      } else {
        // Multi-line paste
        std::string &line = lines_[cursor_y_];
        size_t byte_pos = char_to_byte_pos(cursor_y_, cursor_x_);
        std::string before = line.substr(0, byte_pos);
        std::string after = line.substr(byte_pos);

        // First pasted line appends to current line
        lines_[cursor_y_] = before + paste_lines[0];

        // Insert middle lines
        for (size_t i = 1; i < paste_lines.size() - 1; ++i) {
          lines_.insert(lines_.begin() + cursor_y_ + 1, paste_lines[i]);
          cursor_y_++;
        }

        // Last pasted line gets the remaining content
        std::string last_line = paste_lines.back() + after;
        lines_.insert(lines_.begin() + cursor_y_ + 1, last_line);
        cursor_y_++;

        // Position cursor at end of pasted content
        auto chars = prepare_text_for_render(paste_lines.back());
        cursor_x_ = (int)chars.size();
      }

      clear_selection();
      needs_recalc_max_width_ = true;
      recalculate_virtual_lines();
      ensure_cursor_visible();

      if (on_change) on_change(get_text());
      return true;
    }

    // Handle Key Events
    if (has_focus() && event.is_key_event()) {
      // 1. Handle Tab Key (Indentation)
      if (event.is_tab()) {
        if (accepts_tab) {
          save_undo_state();
          if (has_selection()) delete_selection();

          std::string indent(tab_size, ' ');
          std::string &l = lines_[cursor_y_];
          size_t byte_idx = char_to_byte_pos(cursor_y_, cursor_x_);
          l.insert(byte_idx, indent);
          cursor_x_ += tab_size;

          needs_recalc_max_width_ = true;
          recalculate_virtual_lines();
          ensure_cursor_visible();
          if (on_change) on_change(get_text());
          return true;
        }
        return false;  // Let it bubble for focus navigation
      }

      bool changed = false;
      int current_v_idx = find_v_line(cursor_y_, cursor_x_);

      // Handle Ctrl+A (Select All)
      if (event.is_select_all()) {
        sel_anchor_x_ = 0;
        sel_anchor_y_ = 0;
        cursor_y_ = (int)lines_.size() - 1;
        cursor_x_ = (int)prepare_text_for_render(lines_[cursor_y_]).size();
        ensure_cursor_visible();
        return true;
      }

      // Handle Ctrl+C (Copy)
      if (event.ctrl && event.shift && (event.key == 'c' || event.key == 'C')) {
        if (has_selection()) {
          copy_to_clipboard(get_selected_text());
        }
        return true;
      }

      // Handle Ctrl+X (Cut)
      if (event.ctrl && event.shift && (event.key == 'x' || event.key == 'X')) {
        if (has_selection()) {
          copy_to_clipboard(get_selected_text());
          delete_selection();
          needs_recalc_max_width_ = true;
          recalculate_virtual_lines();
          if (on_change) on_change(get_text());
        }
        ensure_cursor_visible();
        return true;
      }

      // Handle navigation keys with selection
      bool is_nav_key = event.is_nav_up() || event.is_nav_down() ||
                        event.is_nav_left() || event.is_nav_right() ||
                        event.is_nav_pgup() || event.is_nav_pgdn() ||
                        event.is_nav_home() || event.is_nav_end();

      if (is_nav_key) {
        if (event.shift) {
          start_selection();
        } else {
          clear_selection();
        }
      }

      if (event.is_nav_up()) {  // Up
        if (current_v_idx > 0) {
          int vx =
              get_visual_x_in_segment(virtual_lines_[current_v_idx], cursor_x_);
          current_v_idx--;
          const auto &target_vl = virtual_lines_[current_v_idx];
          cursor_y_ = target_vl.logical_line_idx;
          cursor_x_ = char_pos_from_visual_in_segment(target_vl, vx);
        }
      } else if (event.is_nav_down()) {  // Down
        if (current_v_idx < (int)virtual_lines_.size() - 1) {
          int vx =
              get_visual_x_in_segment(virtual_lines_[current_v_idx], cursor_x_);
          current_v_idx++;
          const auto &target_vl = virtual_lines_[current_v_idx];
          cursor_y_ = target_vl.logical_line_idx;
          cursor_x_ = char_pos_from_visual_in_segment(target_vl, vx);
        }
      } else if (event.is_nav_left()) {  // Left
        if (event.ctrl) {
          // Ctrl+Left: Jump to previous word start
          auto [new_y, new_x] = find_prev_word_boundary_ta();
          cursor_y_ = new_y;
          cursor_x_ = new_x;
        } else if (cursor_x_ > 0) {
          cursor_x_--;
        } else if (cursor_y_ > 0) {
          cursor_y_--;
          cursor_x_ = (int)prepare_text_for_render(lines_[cursor_y_]).size();
        }
      } else if (event.is_nav_right()) {  // Right
        if (event.ctrl) {
          // Ctrl+Right: Jump to next word end
          auto [new_y, new_x] = find_next_word_boundary_ta();
          cursor_y_ = new_y;
          cursor_x_ = new_x;
        } else {
          auto chars = prepare_text_for_render(lines_[cursor_y_]);
          if (cursor_x_ < (int)chars.size()) {
            cursor_x_++;
          } else if (cursor_y_ < (int)lines_.size() - 1) {
            cursor_y_++;
            cursor_x_ = 0;
          }
        }
      } else if (event.is_nav_home()) {  // Home
        if (event.ctrl) {
          // Ctrl+Home -> Start of document
          cursor_y_ = 0;
          cursor_x_ = 0;
        } else {
          // Home -> Start of line
          cursor_x_ = 0;
        }
      } else if (event.is_nav_end()) {  // End
        if (event.ctrl) {
          // Ctrl+End -> End of document
          cursor_y_ = (int)lines_.size() - 1;
          if (cursor_y_ < 0) cursor_y_ = 0;  // Safety
          auto chars = prepare_text_for_render(lines_[cursor_y_]);
          cursor_x_ = (int)chars.size();
        } else {
          // End -> End of line
          cursor_x_ = (int)prepare_text_for_render(lines_[cursor_y_]).size();
        }
      } else if ((event.ctrl || event.alt || event.shift) &&
                 event.is_nav_pgup()) {  // Ctrl/Alt/Shift + PageUp -> Scroll
                                         // Left (Horizontal Page Up)
        // Scroll view left by one visual width
        int text_width =
            width - get_line_number_width() - (show_scrollbar ? 1 : 0);
        scroll_x_ = std::max(0, scroll_x_ - text_width);
        return true;
      } else if ((event.ctrl || event.alt || event.shift) &&
                 event.is_nav_pgdn()) {  // Ctrl/Alt/Shift + PageDown -> Scroll
                                         // Right (Horizontal Page Down) Scroll
                                         // view right by one visual width
        int max_visual_width = get_max_visual_width();
        int text_width =
            width - get_line_number_width() - (show_scrollbar ? 1 : 0);
        int max_scroll = std::max(0, max_visual_width - text_width);

        scroll_x_ = std::min(max_scroll, scroll_x_ + text_width);
        return true;
      } else if (event.is_nav_pgup()) {  // PageUp
        int vx =
            get_visual_x_in_segment(virtual_lines_[current_v_idx], cursor_x_);
        current_v_idx = std::max(0, current_v_idx - height);
        const auto &target_vl = virtual_lines_[current_v_idx];
        cursor_y_ = target_vl.logical_line_idx;
        cursor_x_ = char_pos_from_visual_in_segment(target_vl, vx);
      } else if (event.is_nav_pgdn()) {  // PageDown
        int vx =
            get_visual_x_in_segment(virtual_lines_[current_v_idx], cursor_x_);
        current_v_idx =
            std::min((int)virtual_lines_.size() - 1, current_v_idx + height);
        const auto &target_vl = virtual_lines_[current_v_idx];
        cursor_y_ = target_vl.logical_line_idx;
        cursor_x_ = char_pos_from_visual_in_segment(target_vl, vx);
      } else if (event.is_enter()) {  // Enter
        save_undo_state();
        if (has_selection()) {
          delete_selection();
        }
        std::string current_line = lines_[cursor_y_];
        size_t split_byte = char_to_byte_pos(cursor_y_, cursor_x_);
        std::string next_line = current_line.substr(split_byte);
        lines_[cursor_y_] = current_line.substr(0, split_byte);
        lines_.insert(lines_.begin() + cursor_y_ + 1, next_line);
        cursor_y_++;
        cursor_x_ = 0;
        changed = true;
      } else if (event.is_backspace()) {  // Backspace
        save_undo_state();
        if (has_selection()) {
          delete_selection();
          changed = true;
        } else if (cursor_x_ > 0) {
          std::string &l = lines_[cursor_y_];
          size_t byte_idx = char_to_byte_pos(cursor_y_, cursor_x_ - 1);
          size_t next_byte = char_to_byte_pos(cursor_y_, cursor_x_);
          l.erase(byte_idx, next_byte - byte_idx);
          cursor_x_--;
          changed = true;
        } else if (cursor_y_ > 0) {
          cursor_x_ =
              (int)prepare_text_for_render(lines_[cursor_y_ - 1]).size();
          lines_[cursor_y_ - 1] += lines_[cursor_y_];
          lines_.erase(lines_.begin() + cursor_y_);
          cursor_y_--;
          changed = true;
        }
      } else if (event.is_delete()) {  // Delete
        save_undo_state();
        if (has_selection()) {
          delete_selection();
          changed = true;
        } else {
          auto &l = lines_[cursor_y_];
          auto chars = prepare_text_for_render(l);
          if (cursor_x_ < (int)chars.size()) {
            size_t byte_idx = char_to_byte_pos(cursor_y_, cursor_x_);
            size_t next_byte = char_to_byte_pos(cursor_y_, cursor_x_ + 1);
            l.erase(byte_idx, next_byte - byte_idx);
            changed = true;
          } else if (cursor_y_ < (int)lines_.size() - 1) {
            lines_[cursor_y_] += lines_[cursor_y_ + 1];
            lines_.erase(lines_.begin() + cursor_y_ + 1);
            changed = true;
          }
        }
      } else if (event.is_printable()) {
        save_undo_state();
        if (has_selection()) {
          delete_selection();
        }
        std::string &l = lines_[cursor_y_];
        size_t byte_idx = char_to_byte_pos(cursor_y_, cursor_x_);
        l.insert(byte_idx, 1, (char)event.key);
        cursor_x_++;
        clear_selection();  // Prevent stale anchor from forming a selection
        changed = true;
      }

      if (changed) {
        needs_recalc_max_width_ = true;
        recalculate_virtual_lines();
        if (on_change) on_change(get_text());
      }

      ensure_cursor_visible();
      return true;
    }

    return false;
  }

  void ensure_cursor_visible() {
    if (height <= 0) return;
    int v_idx = find_v_line(cursor_y_, cursor_x_);

    // Vertical Scroll
    if (v_idx < scroll_y_)
      scroll_y_ = v_idx;
    else if (v_idx >= scroll_y_ + height)
      scroll_y_ = v_idx - height + 1;

    if (scroll_y_ < 0) scroll_y_ = 0;
    int max_v_scroll = std::max(0, (int)virtual_lines_.size() - height);
    if (scroll_y_ > max_v_scroll) scroll_y_ = max_v_scroll;

    // Horizontal Scroll (only if word_wrap is off)
    if (!word_wrap) {
      int vx = get_visual_x(cursor_y_, cursor_x_);
      int ln_width = get_line_number_width();
      int text_width = width - ln_width - (show_scrollbar ? 1 : 0);
      if (text_width > 0) {
        if (vx < scroll_x_)
          scroll_x_ = vx;
        else if (vx >= scroll_x_ + text_width)
          scroll_x_ = vx - text_width + 1;
        if (scroll_x_ < 0) scroll_x_ = 0;
      }
    } else {
      scroll_x_ = 0;
    }

    if (on_cursor_move) on_cursor_move(cursor_x_, cursor_y_);
  }

 protected:
  int last_width_ = 0;
  int last_height_ = 0;
  bool last_word_wrap_ = false;
  bool last_show_ln_ = true;
  bool last_show_sb_ = true;
  struct VirtualLine {
    size_t logical_line_idx;
    int start_char_idx;
    int char_count;
    bool is_continuation;
  };
  std::vector<VirtualLine> virtual_lines_;
  bool is_dragging_scrollbar_ = false;

  mutable int cached_max_visual_width_ = -1;
  mutable bool needs_recalc_max_width_ = true;

  int get_max_visual_width() const {
    if (word_wrap) return width;

    if (!needs_recalc_max_width_ && cached_max_visual_width_ != -1) {
      return cached_max_visual_width_;
    }

    int max_w = 0;
    for (const auto &line : lines_) {
      max_w = std::max(max_w, calculate_line_width_fast(line));
    }
    cached_max_visual_width_ = max_w;
    needs_recalc_max_width_ = false;
    return max_w;
  }

  int calculate_line_width_fast(const std::string &text) const {
    int width = 0;
    size_t pos = 0;
    while (pos < text.size()) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(text, pos, cp, len)) {
        width += char_display_width(cp);
        pos += len;
      } else {
        pos++;
      }
    }
    return width;
  }

  std::vector<std::string> lines_;
  int cursor_x_ = 0;  // Character position in logical line
  int cursor_y_ = 0;  // Logical line index
  int scroll_x_ = 0;
  int scroll_y_ = 0;

  // Undo/Redo state
  struct TextAreaState {
    std::vector<std::string> lines;
    int cursor_x;
    int cursor_y;
  };
  UndoRedoHistory<TextAreaState> undo_history_;

  /// @brief Get current state for undo/redo
  TextAreaState get_current_state() const {
    return {lines_, cursor_x_, cursor_y_};
  }

  /// @brief Restore state from undo/redo
  void restore_state(const TextAreaState &state) {
    lines_ = state.lines;
    cursor_x_ = state.cursor_x;
    cursor_y_ = state.cursor_y;
    // Ensure at least one empty line
    if (lines_.empty()) lines_.push_back("");
    // Clamp cursor position
    if (cursor_y_ >= (int)lines_.size()) cursor_y_ = (int)lines_.size() - 1;
    if (cursor_y_ < 0) cursor_y_ = 0;
    auto chars = prepare_text_for_render(lines_[cursor_y_]);
    if (cursor_x_ > (int)chars.size()) cursor_x_ = (int)chars.size();
    if (cursor_x_ < 0) cursor_x_ = 0;
    clear_selection();
    needs_recalc_max_width_ = true;
    recalculate_virtual_lines();
    ensure_cursor_visible();
  }

  /// @brief Save current state before a modifying operation
  void save_undo_state() { undo_history_.push(get_current_state()); }

  int get_line_number_width() const {
    if (!show_line_numbers) return 0;
    return (int)std::to_string(lines_.size()).length() + 2;
  }

  int get_visual_x(int line_idx, int char_pos) {
    if (line_idx < 0 || line_idx >= (int)lines_.size()) return 0;
    auto chars = prepare_text_for_render(lines_[line_idx]);
    int vx = 0;
    for (int i = 0; i < char_pos && i < (int)chars.size(); ++i) {
      vx += chars[i].display_width;
    }
    return vx;
  }

  int visual_to_char_pos(int line_idx, int target_vx) {
    if (line_idx < 0 || line_idx >= (int)lines_.size()) return 0;
    auto chars = prepare_text_for_render(lines_[line_idx]);
    int current_vx = 0;
    for (size_t i = 0; i < chars.size(); ++i) {
      if (current_vx + chars[i].display_width / 2 >= target_vx) {
        return (int)i;
      }
      current_vx += chars[i].display_width;
    }
    return (int)chars.size();
  }

  size_t char_to_byte_pos(int line_idx, int char_pos) const {
    if (line_idx < 0 || line_idx >= (int)lines_.size()) return 0;
    return TextHelper::char_to_byte_pos(lines_[line_idx], char_pos);
  }

  int find_v_line(int logical_y, int char_x) {
    for (int i = 0; i < (int)virtual_lines_.size(); ++i) {
      const auto &vl = virtual_lines_[i];
      if (vl.logical_line_idx == (size_t)logical_y) {
        if (char_x >= vl.start_char_idx &&
            char_x < vl.start_char_idx + vl.char_count)
          return i;
        // Also handle end of line
        if (char_x == vl.start_char_idx + vl.char_count) {
          // Check if last segment for this logical line
          if (i == (int)virtual_lines_.size() - 1 ||
              virtual_lines_[i + 1].logical_line_idx != (size_t)logical_y)
            return i;
        }
      }
    }
    return 0;
  }

  int get_visual_x_in_segment(const VirtualLine &vl, int char_x) {
    auto chars = prepare_text_for_render(lines_[vl.logical_line_idx]);
    int vx = 0;
    int limit = std::min(char_x, vl.start_char_idx + vl.char_count);
    for (int i = vl.start_char_idx; i < limit; ++i) {
      vx += chars[i].display_width;
    }
    return vx;
  }

  int char_pos_from_visual_in_segment(const VirtualLine &vl, int vx) {
    auto chars = prepare_text_for_render(lines_[vl.logical_line_idx]);
    int current_vx = 0;
    for (int i = 0; i < vl.char_count; ++i) {
      int c_idx = vl.start_char_idx + i;
      int cw = chars[c_idx].display_width;
      if (current_vx + cw / 2 >= vx) return c_idx;
      current_vx += cw;
    }
    return vl.start_char_idx + vl.char_count;
  }

  int visual_to_char_pos_in_segment(const VirtualLine &vl, int visual_x) {
    auto chars = prepare_text_for_render(lines_[vl.logical_line_idx]);
    int current_vx = 0;
    for (int i = 0; i < vl.char_count; ++i) {
      int c_idx = vl.start_char_idx + i;
      int cw = chars[c_idx].display_width;
      if (current_vx + cw > visual_x) return c_idx;
      current_vx += cw;
    }
    return vl.start_char_idx + vl.char_count;
  }

  // Selection state (-1 means no selection)
  int sel_anchor_x_ = -1;  ///< Selection anchor X (where selection started)
  int sel_anchor_y_ =
      -1;  ///< Selection anchor Y (line where selection started)
  bool is_selecting_ = false;  ///< Currently dragging to select
  std::chrono::steady_clock::time_point last_click_time_;
  int last_click_pos_x_ = -1;
  int last_click_pos_y_ = -1;
  int click_count_ = 0;

  void select_word_at_ta(int cx, int cy) {
    if (cy < 0 || cy >= (int)lines_.size()) return;

    // Re-use generic TextHelper logic
    int start, end;
    TextHelper::find_word_boundaries(lines_[cy], cx, start, end);

    sel_anchor_y_ = cy;
    sel_anchor_x_ = start;
    cursor_y_ = cy;
    cursor_x_ = end;
  }
  /// @brief Clear the current selection
  void clear_selection() {
    sel_anchor_x_ = -1;
    sel_anchor_y_ = -1;
    is_selecting_ = false;
  }

  /// @brief Start a selection at the current cursor position
  void start_selection() {
    if (sel_anchor_x_ < 0) {
      sel_anchor_x_ = cursor_x_;
      sel_anchor_y_ = cursor_y_;
    }
  }

  /// @brief Get the selection range in normalized form (start <= end)
  void get_selection_range(int &start_x, int &start_y, int &end_x,
                           int &end_y) const {
    if (sel_anchor_y_ < cursor_y_ ||
        (sel_anchor_y_ == cursor_y_ && sel_anchor_x_ <= cursor_x_)) {
      start_x = sel_anchor_x_;
      start_y = sel_anchor_y_;
      end_x = cursor_x_;
      end_y = cursor_y_;
    } else {
      start_x = cursor_x_;
      start_y = cursor_y_;
      end_x = sel_anchor_x_;
      end_y = sel_anchor_y_;
    }
  }

  /// @brief Get the selected text as a string
  std::string get_selected_text() const {
    if (!has_selection()) return "";
    int sx, sy, ex, ey;
    get_selection_range(sx, sy, ex, ey);

    std::string result;
    for (int line = sy; line <= ey && line < (int)lines_.size(); ++line) {
      auto chars = prepare_text_for_render(lines_[line]);
      int start = (line == sy) ? sx : 0;
      int end = (line == ey) ? ex : (int)chars.size();

      // Convert char positions to byte positions
      size_t byte_start = char_to_byte_pos(line, start);
      size_t byte_end = char_to_byte_pos(line, end);
      result += lines_[line].substr(byte_start, byte_end - byte_start);

      if (line < ey) result += "\n";
    }
    return result;
  }

  /// @brief Delete the selected text
  void delete_selection() {
    if (!has_selection()) return;
    int sx, sy, ex, ey;
    get_selection_range(sx, sy, ex, ey);

    if (sy == ey) {
      // Single line deletion
      size_t byte_start = char_to_byte_pos(sy, sx);
      size_t byte_end = char_to_byte_pos(sy, ex);
      lines_[sy].erase(byte_start, byte_end - byte_start);
    } else {
      // Multi-line deletion
      size_t byte_start = char_to_byte_pos(sy, sx);
      size_t byte_end = char_to_byte_pos(ey, ex);
      std::string new_line =
          lines_[sy].substr(0, byte_start) + lines_[ey].substr(byte_end);
      lines_[sy] = new_line;
      lines_.erase(lines_.begin() + sy + 1, lines_.begin() + ey + 1);
    }

    cursor_y_ = sy;
    cursor_x_ = sx;

    // Clamp cursor_x_ to valid range of the (possibly merged) line
    auto chars = prepare_text_for_render(lines_[cursor_y_]);
    if (cursor_x_ > (int)chars.size()) cursor_x_ = (int)chars.size();

    clear_selection();
  }

  /// @brief Check if a character position is within the selection
  bool is_char_selected(int line_idx, int char_idx) const {
    if (!has_selection()) return false;
    int sx, sy, ex, ey;
    get_selection_range(sx, sy, ex, ey);

    if (line_idx > sy && line_idx < ey) return true;
    if (line_idx == sy && line_idx == ey)
      return char_idx >= sx && char_idx < ex;
    if (line_idx == sy) return char_idx >= sx;
    if (line_idx == ey) return char_idx < ex;
    return false;
  }

  /// @brief Find previous word boundary for Ctrl+Left navigation
  /// @return Pair of (line_idx, char_idx)
  std::pair<int, int> find_prev_word_boundary_ta() const {
    int y = cursor_y_;
    int x = cursor_x_;

    // Parse current line chars
    auto chars = prepare_text_for_render(lines_[y]);

    // At start of line? Go to end of previous line
    if (x == 0) {
      if (y > 0) {
        y--;
        auto prev_chars = prepare_text_for_render(lines_[y]);
        x = (int)prev_chars.size();
      }
      return {y, x};
    }

    x--;  // Move back one

    // Get codepoints for current line
    std::vector<uint32_t> codepoints;
    size_t pos = 0;
    while (pos < lines_[y].size()) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(lines_[y], pos, cp, len)) {
        codepoints.push_back(cp);
        pos += len;
      } else {
        pos++;
      }
    }

    // Skip whitespace
    while (x > 0 && x < (int)codepoints.size() &&
           (codepoints[x] == ' ' || codepoints[x] == '\t'))
      x--;

    // Skip to start of word
    while (x > 0 && codepoints[x - 1] != ' ' && codepoints[x - 1] != '\t') x--;

    return {y, std::max(0, x)};
  }

  /// @brief Find next word boundary for Ctrl+Right navigation
  /// @return Pair of (line_idx, char_idx)
  std::pair<int, int> find_next_word_boundary_ta() const {
    int y = cursor_y_;
    int x = cursor_x_;

    // Get codepoints for current line
    std::vector<uint32_t> codepoints;
    size_t pos = 0;
    while (pos < lines_[y].size()) {
      uint32_t cp;
      int len;
      if (utf8_decode_codepoint(lines_[y], pos, cp, len)) {
        codepoints.push_back(cp);
        pos += len;
      } else {
        pos++;
      }
    }

    // At end of line? Go to start of next line
    if (x >= (int)codepoints.size()) {
      if (y < (int)lines_.size() - 1) {
        return {y + 1, 0};
      }
      return {y, x};
    }

    // Skip leading whitespace
    while (x < (int)codepoints.size() &&
           (codepoints[x] == ' ' || codepoints[x] == '\t'))
      x++;

    // Skip current word to its end
    while (x < (int)codepoints.size() && codepoints[x] != ' ' &&
           codepoints[x] != '\t')
      x++;

    return {y, x};
  }
};

/// @brief A widget that holds and manages other widgets

class Container : public Widget {
 public:
  bool auto_shrink =
      false;  ///< If true, container shrinks to fit its children's constraints;
              ///< if false, grows to fill parent

  /// @brief Add a child widget to the container
  /// @param widget The widget to add
  void add(std::shared_ptr<Widget> widget) { children_.push_back(widget); }

  /// @brief Remove all children from the container
  void clear_children() { children_.clear(); }

  virtual void render(Buffer &buffer) override {
    for (auto &child : children_) {
      if (child->visible) {
        buffer.push_clip({child->x, child->y, child->width, child->height});
        child->render(buffer);
        buffer.pop_clip();
      }
    }
  }

  virtual bool on_event(const Event &event) override {
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
      if ((*it)->visible && (*it)->on_event(event)) return true;
    }
    return false;
  }

  /// @brief Recalculate layout of children. Must be implemented by subclasses.
  virtual void layout() = 0;

  bool has_focus_within() const override {
    if (focused_) return true;
    for (auto &child : get_children()) {
      if (child->has_focus_within()) return true;
    }
    return false;
  }

  virtual const std::vector<std::shared_ptr<Widget>> &get_children() const {
    return children_;
  }

  void update_responsive() override {
    Widget::update_responsive();

    int max_child_fixed_height = 0;
    int sum_child_fixed_height = 0;
    int max_child_fixed_width = 0;
    int sum_child_fixed_width = 0;

    for (const auto &child : children_) {
      if (child && child->visible) {
        child->update_responsive();
        if (child->fixed_height > 0) {
          sum_child_fixed_height += child->fixed_height;
          if (child->fixed_height > max_child_fixed_height) {
            max_child_fixed_height = child->fixed_height;
          }
        }
        if (child->fixed_width > 0) {
          sum_child_fixed_width += child->fixed_width;
          if (child->fixed_width > max_child_fixed_width) {
            max_child_fixed_width = child->fixed_width;
          }
        }
      }
    }

    int target_height = get_child_fixed_height_contribution(
        max_child_fixed_height, sum_child_fixed_height);
    int target_width = get_child_fixed_width_contribution(
        max_child_fixed_width, sum_child_fixed_width);

    int pad_h = get_container_padding_height();
    int pad_w = get_container_padding_width();

    if (auto_shrink) {
      propagate_axis(target_height, pad_h, fixed_height,
                     last_propagated_child_height, fixed_height_is_propagated,
                     min_height, max_height);
      propagate_axis(target_width, pad_w, fixed_width,
                     last_propagated_child_width, fixed_width_is_propagated,
                     min_width, max_width);
    } else {
      revert_axis(pad_h, fixed_height, last_propagated_child_height,
                  fixed_height_is_propagated, min_height, max_height);
      revert_axis(pad_w, fixed_width, last_propagated_child_width,
                  fixed_width_is_propagated, min_width, max_width);
    }
  }

 protected:
  std::vector<std::shared_ptr<Widget>> children_;

  int last_propagated_child_height = 0;
  int last_propagated_child_width = 0;
  bool fixed_height_is_propagated = false;
  bool fixed_width_is_propagated = false;

  /// @brief Propagate a child's aggregate fixed size to this container's
  /// fixed size on one axis. Detects external modifications to avoid
  /// overwriting user-set values. Clamps the result by the container's own
  /// min/max constraints.
  /// @param target The aggregate child fixed size contribution
  /// @param pad The container padding for this axis
  /// @param fixed The container's fixed size (modified in place)
  /// @param last_propagated Tracks the last propagated child value
  /// @param is_propagated Whether the current fixed value was auto-propagated
  /// @param min_val The container's minimum constraint for this axis
  /// @param max_val The container's maximum constraint for this axis
  static void propagate_axis(int target, int pad, int &fixed,
                             int &last_propagated, bool &is_propagated,
                             int min_val, int max_val) {
    // Detect external modification: if fixed was propagated but has since
    // been changed by user code, stop treating it as auto-propagated
    if (is_propagated &&
        fixed != clamp_size(last_propagated + pad, min_val, max_val)) {
      is_propagated = false;
    }

    // Only propagate if no user-set value exists
    if (fixed == 0 || is_propagated) {
      if (target > 0) {
        fixed = clamp_size(target + pad, min_val, max_val);
        last_propagated = target;
        is_propagated = true;
      } else {
        fixed = 0;
        last_propagated = 0;
        is_propagated = false;
      }
    }
  }

  /// @brief Revert a previously propagated fixed size when auto_shrink is
  /// disabled. Only clears the value if it hasn't been externally modified.
  static void revert_axis(int pad, int &fixed, int &last_propagated,
                          bool &is_propagated, int min_val, int max_val) {
    if (is_propagated) {
      if (fixed == clamp_size(last_propagated + pad, min_val, max_val)) {
        fixed = 0;
        last_propagated = 0;
      }
      is_propagated = false;
    }
  }

  virtual int get_child_fixed_height_contribution(int max_h, int sum_h) const {
    return max_h;
  }
  virtual int get_child_fixed_width_contribution(int max_w, int sum_w) const {
    return max_w;
  }
  virtual int get_container_padding_height() const { return 0; }
  virtual int get_container_padding_width() const { return 0; }

  /// @brief Distribute available space among flex children with min/max
  /// constraints. Uses a multi-pass algorithm: distribute evenly, clamp
  /// children that violate constraints, reclaim the difference, and
  /// redistribute to unclamped children until stable.
  /// @param children The children to distribute among
  /// @param available Total available space for flex children
  /// @param use_height If true, uses height constraints; if false, width
  /// @param out_sizes Output: computed size for each child (indexed by
  ///                  position in children vector). Fixed children get their
  ///                  fixed size; invisible children get 0.
  static void distribute_flex(
      const std::vector<std::shared_ptr<Widget>> &children, int available,
      bool use_height, std::vector<int> &out_sizes) {
    int n = (int)children.size();
    out_sizes.resize(n, 0);

    if (available < 0) available = 0;

    // Identify flex children and compute initial available space
    std::vector<bool> is_flex(n, false);
    std::vector<bool> settled(n, false);
    int flex_count = 0;
    int remaining = available;

    for (int i = 0; i < n; ++i) {
      const auto &child = children[i];
      if (!child || !child->visible) {
        settled[i] = true;
        continue;
      }
      int fixed = use_height ? child->fixed_height : child->fixed_width;
      if (fixed > 0) {
        out_sizes[i] = fixed;
        remaining -= fixed;
        settled[i] = true;
      } else {
        is_flex[i] = true;
        flex_count++;
      }
    }

    if (remaining < 0) remaining = 0;

    // Multi-pass distribution with clamping
    // Each pass distributes remaining space evenly among unsettled flex
    // children, then clamps any that violate min/max. If any were clamped,
    // we reclaim the difference and repeat with fewer flex children.
    bool changed = true;
    while (changed && flex_count > 0) {
      changed = false;
      int share = remaining / flex_count;
      int rem = remaining % flex_count;

      for (int i = 0; i < n; ++i) {
        if (!is_flex[i] || settled[i]) continue;

        int size = share;
        if (rem > 0) {
          size++;
          rem--;
        }

        int min_val =
            use_height ? children[i]->min_height : children[i]->min_width;
        int max_val =
            use_height ? children[i]->max_height : children[i]->max_width;
        int clamped = clamp_size(size, min_val, max_val);

        if (clamped != size) {
          // This child was constrained — settle it and reclaim the difference
          out_sizes[i] = clamped;
          remaining -= clamped;
          settled[i] = true;
          flex_count--;
          changed = true;
          break;
        }
      }
    }

    // Final pass: assign remaining space to unsettled flex children
    if (flex_count > 0) {
      int share = remaining / flex_count;
      int rem = remaining % flex_count;
      for (int i = 0; i < n; ++i) {
        if (!is_flex[i] || settled[i]) continue;
        int size = share;
        if (rem > 0) {
          size++;
          rem--;
        }
        out_sizes[i] = size;
      }
    }
  }
};

/// @brief Overlays children on top of each other
class Stack : public Container {
 public:
  void layout() override {
    for (auto &child : children_) {
      if (!child->visible) continue;
      child->x = x;
      child->y = y;
      child->width =
          child->fixed_width > 0
              ? child->fixed_width
              : clamp_size(width, child->min_width, child->max_width);
      child->height =
          child->fixed_height > 0
              ? child->fixed_height
              : clamp_size(height, child->min_height, child->max_height);

      if (auto cont = std::dynamic_pointer_cast<Container>(child))
        cont->layout();
    }
  }
};

/// @brief Layouts children vertically
class Vertical : public Container {
 public:
  /// @brief Perform vertical layout calculation
  void layout() override {
    if (children_.empty()) return;

    // Update responsive state for all children
    for (auto &child : children_) {
      child->update_responsive();
    }

    // Distribute height among children with min/max clamping
    std::vector<int> sizes;
    distribute_flex(children_, height, true, sizes);

    int current_y = y;

    for (size_t i = 0; i < children_.size(); ++i) {
      auto &child = children_[i];
      if (!child->visible) continue;

      child->x = x;
      child->y = current_y;
      // Cross-axis: clamp width by child's min/max
      child->width =
          child->fixed_width > 0
              ? child->fixed_width
              : clamp_size(width, child->min_width, child->max_width);
      child->height = sizes[i];

      // Clipping: Ensure child does not overflow parent
      if (current_y >= y + height) {
        child->height = 0;  // Completely outside
      } else if (current_y + child->height > y + height) {
        child->height = (y + height) - current_y;  // Partial clip
      }

      current_y += child->height;

      if (auto cont = std::dynamic_pointer_cast<Container>(child)) {
        cont->layout();
      }
    }
  }

 protected:
  int get_child_fixed_height_contribution(int max_h, int sum_h) const override {
    return sum_h;
  }
};

/// @brief Layouts children horizontally
class Horizontal : public Container {
 public:
  /// @brief Perform horizontal layout calculation
  void layout() override {
    if (children_.empty()) return;

    // Update responsive state for all children
    for (auto &child : children_) {
      child->update_responsive();
    }

    // Distribute width among children with min/max clamping
    std::vector<int> sizes;
    distribute_flex(children_, width, false, sizes);

    int current_x = x;

    for (size_t i = 0; i < children_.size(); ++i) {
      auto &child = children_[i];
      if (!child->visible) continue;

      child->x = current_x;
      child->y = y;
      // Cross-axis: clamp height by child's min/max
      child->height =
          child->fixed_height > 0
              ? child->fixed_height
              : clamp_size(height, child->min_height, child->max_height);
      child->width = sizes[i];

      current_x += child->width;

      // Recursive layout if child is container
      if (auto cont = std::dynamic_pointer_cast<Container>(child)) {
        cont->layout();
      }
    }
  }

 protected:
  int get_child_fixed_width_contribution(int max_w, int sum_w) const override {
    return sum_w;
  }
};

/// @brief A vertical layout that scrolls if content exceeds height
class ScrollableVertical : public Container {
 public:
  ScrollableVertical() { focusable = true; }

  int scroll_offset = 0;     ///< Current vertical scroll position
  int content_height = 0;    ///< Total height of content
  bool is_dragging = false;  ///< Internal state for scrollbar dragging

  void render(Buffer &buffer) override {
    // Push clip rect for this container
    buffer.push_clip({x, y, width, height});

    Container::render(buffer);

    // Draw Scrollbar if needed
    if (content_height > height && height > 0) {
      render_scrollbar(buffer, x + width - 1, y, height, scroll_offset,
                       content_height, true);
    }

    buffer.pop_clip();
  }

  void layout() override {
    if (children_.empty()) return;

    // Update responsive state for all children
    for (auto &child : children_) {
      child->update_responsive();
    }

    // 1. Calculate Content Height using distribute_flex
    std::vector<int> sizes;
    distribute_flex(children_, height, true, sizes);

    content_height = 0;
    for (size_t i = 0; i < children_.size(); ++i) {
      if (!children_[i] || !children_[i]->visible) continue;
      content_height += sizes[i];
    }

    // 2. Check if Scrollbar Needed
    bool show_scrollbar = false;
    if (content_height > height && height > 0) {
      show_scrollbar = true;
    }

    int effective_width = width;
    if (show_scrollbar) {
      effective_width = width - 1;
      if (effective_width < 0) effective_width = 0;
    }

    // 3. Layout Children with Effective Width
    int current_y = y - scroll_offset;

    for (size_t i = 0; i < children_.size(); ++i) {
      auto &child = children_[i];
      if (!child->visible) continue;

      child->x = x;
      child->y = current_y;
      // Cross-axis: clamp width by child's min/max
      child->width =
          child->fixed_width > 0
              ? child->fixed_width
              : clamp_size(effective_width, child->min_width, child->max_width);
      child->height = sizes[i];

      // Advance using INTENDED height
      current_y += sizes[i];

      if (auto cont = std::dynamic_pointer_cast<Container>(child)) {
        cont->layout();
      }
    }
  }

  bool on_event(const Event &event) override {
    // Ensure non-mouse events (like keyboard navigation/typing) are still
    // delegated
    if (!event.is_mouse_event() && Container::on_event(event)) return true;

    // For mouse click/release/drag/motion events, first check whether the
    // cursor is actually inside this container's bounds. If not, let sibling
    // panes handle it.
    if ((event.mouse_left() || event.mouse_release() || event.mouse_drag() ||
         event.mouse_motion()) &&
        !(event.x >= x && event.x < x + width && event.y >= y &&
          event.y < y + height)) {
      return false;
    }

    // For mouse events, delegate only to visible children whose bounds actually
    // intersect our clipping area. Scrolled-out-of-view children should not
    // consume clicks meant for sibling panes. This fixes unclickable areas
    // correlating with scroll position.
    if (event.is_mouse_event()) {
      bool handled = false;
      int clip_top = y;
      int clip_bottom = y + height - 1;

      for (auto it = children_.rbegin(); it != children_.rend() && !handled;
           ++it) {
        if (!(*it)->visible) continue;
        // Only dispatch to children whose visual area overlaps our clipping
        // region
        int child_top = (*it)->y;
        int child_bottom = (*it)->y + (*it)->height - 1;
        bool in_clip_range =
            !(child_bottom < clip_top || child_top > clip_bottom);
        if (in_clip_range && (*it)->on_event(event)) handled = true;
      }
      if (handled) return true;

      // Handle scrollbar only for events within our bounds
      if (handle_scrollbar_event(event, x, y, width, height, content_height,
                                 scroll_offset, is_dragging, false,
                                 [this]() { set_focus(true); })) {
        return true;
      }

      // Focus on Click only when clicking empty space within this container.
      if (event.mouse_left()) {
        if (event.x >= x && event.x < x + width && event.y >= y &&
            event.y < y + height) {
          set_focus(true);
        }
      }

      // Ensure wheel events anywhere in the widget are handled if children
      // didn't
      if (event.mouse_wheel() && (event.x >= x && event.x < x + width &&
                                  event.y >= y && event.y < y + height)) {
        int old_scroll = scroll_offset;
        if (event.mouse_wheel_up())
          scroll_offset = std::max(0, scroll_offset - 3);
        else if (event.mouse_wheel_down())
          scroll_offset =
              std::min(std::max(0, content_height - height), scroll_offset + 3);

        if (scroll_offset != old_scroll) return true;
      }
    }

    if (event.is_key_event() && (has_focus() || has_focus_within())) {
      bool handled = false;

      // Standard navigation key handling
      if (event.is_nav_up()) {  // Up
        scroll_offset--;
        handled = true;
      } else if (event.is_nav_down()) {  // Down
        scroll_offset++;
        handled = true;
      } else if (event.is_nav_pgup()) {  // PageUp
        scroll_offset -= height;
        handled = true;
      } else if (event.is_nav_pgdn()) {  // PageDown
        scroll_offset += height;
        handled = true;
      } else if (event.is_nav_home()) {  // Home
        scroll_offset = 0;
        handled = true;
      } else if (event.is_nav_end()) {  // End
        // Calculate max_scroll later for clamp
        scroll_offset = content_height;  // Clamped by scroll logic
        handled = true;
      }

      if (handled) {
        // Clamp
        if (scroll_offset < 0) scroll_offset = 0;
        int max_scroll = content_height - height;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
        return true;
      }
    }
    return false;
  }

 public:                                        // Configuration
  Color scrollbar_track_color = Color();        // Default
  Color scrollbar_thumb_color = Color();        // Default
  std::string scrollbar_track_char = "\u2591";  // Unicode Light Shade
  std::string scrollbar_thumb_char = "\u2588";  // Unicode Full Block

 protected:
  int get_child_fixed_height_contribution(int max_h, int sum_h) const override {
    return sum_h;
  }
};

/// @brief A horizontal layout that scrolls if content exceeds width
class ScrollableHorizontal : public Horizontal {
 public:
  int scroll_offset = 0;
  int content_width = 0;
  bool is_dragging = false;

  ScrollableHorizontal() { focusable = true; }

  // Default colors and chars
  Color scrollbar_track_color = Color();
  Color scrollbar_thumb_color = Color();
  std::string scrollbar_track_char = "\u2581";  // Unicode Lower 1/8 Block
  std::string scrollbar_thumb_char = "\u2584";  // Unicode Lower Half Block

  void layout() override {
    if (children_.empty()) return;

    // Update responsive state for all children
    for (auto &child : children_) {
      child->update_responsive();
    }

    // 1. Calculate Content Width using distribute_flex
    std::vector<int> sizes;
    distribute_flex(children_, width, false, sizes);

    content_width = 0;
    for (size_t i = 0; i < children_.size(); ++i) {
      if (!children_[i] || !children_[i]->visible) continue;
      content_width += sizes[i];
    }

    // 2. Check if Scrollbar Needed
    bool show_scrollbar = false;
    if (content_width > width && width > 0) {
      show_scrollbar = true;
    }

    int effective_height = height;
    if (show_scrollbar) {
      effective_height = height - 1;
      if (effective_height < 0) effective_height = 0;
    }

    // 3. Layout Children with Effective Height
    int current_x = x - scroll_offset;

    for (size_t i = 0; i < children_.size(); ++i) {
      auto &child = children_[i];
      if (!child->visible) continue;

      child->x = current_x;
      child->y = y;
      // Cross-axis: clamp height by child's min/max
      child->height = child->fixed_height > 0
                          ? child->fixed_height
                          : clamp_size(effective_height, child->min_height,
                                       child->max_height);
      child->width = sizes[i];

      current_x += sizes[i];

      if (auto cont = std::dynamic_pointer_cast<Container>(child)) {
        cont->layout();
      }
    }
  }
  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      if (handle_scrollbar_event(event, x, y, width, height, content_width,
                                 scroll_offset, is_dragging, true,
                                 [this]() { set_focus(true); })) {
        return true;
      }
    }

    if (Horizontal::on_event(event)) return true;

    if (event.is_mouse_event()) {
      // Focus on Click if not handled by children (Horizontal)
      if (event.mouse_left()) {
        if (event.x >= x && event.x < x + width && event.y >= y &&
            event.y < y + height) {
          set_focus(true);
        }
      }
    }

    if (event.is_key_event() && (has_focus() || has_focus_within())) {
      bool handled = false;

      // Standard Horizontal Keys
      if (event.is_nav_right()) {  // Right
        scroll_offset++;
        handled = true;
      } else if (event.is_nav_left()) {  // Left
        scroll_offset--;
        handled = true;
      } else if (event.is_nav_pgup()) {  // PageUp -> Left Page
        scroll_offset -= width;
        handled = true;
      } else if (event.is_nav_pgdn()) {  // PageDown -> Right Page
        scroll_offset += width;
        handled = true;
      } else if (event.is_nav_home()) {  // Home
        scroll_offset = 0;
        handled = true;
      } else if (event.is_nav_end()) {  // End
        scroll_offset = content_width - width;
        handled = true;
      }

      // Shift + Modifiers (requested explicitly, though PageUp/Down above
      // already act horizontal here)
      if (!handled && event.shift) {
        if (event.is_nav_home()) {  // Shift+Home
          scroll_offset = 0;
          handled = true;
        } else if (event.is_nav_end()) {  // Shift+End
          scroll_offset = content_width - width;
          handled = true;
        } else if (event.is_nav_pgup()) {  // Shift+PageUp
          scroll_offset -= width;
          handled = true;
        } else if (event.is_nav_pgdn()) {  // Shift+PageDown
          scroll_offset += width;
          handled = true;
        }
      }

      if (handled) {
        if (scroll_offset < 0) scroll_offset = 0;
        int max_scroll = content_width - width;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
        return true;
      }
    }

    return false;
  }

  void render(Buffer &buffer) override {
    layout();

    // Clip content to container bounds
    buffer.push_clip({x, y, width, height});

    for (auto &child : children_) child->render(buffer);

    buffer.pop_clip();

    // Draw Horizontal Scrollbar at bottom
    if (content_width > width) {
      render_scrollbar(buffer, x, y + height - 1, width, scroll_offset,
                       content_width, false, scrollbar_track_color,
                       scrollbar_thumb_color, scrollbar_track_char,
                       scrollbar_thumb_char);
    }
  }
};

/// @brief Invisible vertical spacing widget for layout padding
class VerticalSpacer : public Widget {
 public:
  VerticalSpacer(int h = 0) {
    if (h > 0) {
      fixed_height = h;
      height = h;
    }
  }
  void render(Buffer &) override {}
};

/// @brief Invisible horizontal spacing widget for layout padding
class HorizontalSpacer : public Widget {
 public:
  HorizontalSpacer(int w = 0) {
    if (w > 0) {
      fixed_width = w;
      width = w;
    }
  }
  void render(Buffer &) override {}
};

/// @brief Numeric input with optional stepper buttons
class NumberInput : public Horizontal {
 public:
  enum class ButtonPos { Left, Right, Split };

  int step = 1;
  int min_value = 0;
  int max_value = 100;
  bool show_buttons_ = true;
  ButtonPos button_pos = ButtonPos::Right;
  std::function<void(int)> on_change;

  NumberInput(int val = 0, bool show_buttons = true,
              ButtonPos pos = ButtonPos::Right)
      : show_buttons_(show_buttons), button_pos(pos) {
    height = 1;
    fixed_height = 1;

    input_ = std::make_shared<Input>();
    input_->regex_pattern = "^-?\\d*$";
    input_->set_value(std::to_string(val));
    input_->on_change = [this](std::string) {
      if (on_change) on_change(get_value());
    };

    btn_down = std::make_shared<Button>("[-]", [this]() { decrement(); });
    btn_down->fixed_width = 3;
    // Theme the buttons to stand out
    btn_down->bg_color = {60, 60, 70};  // Dark grey button
    btn_down->hover_color = {80, 80, 90};

    btn_up = std::make_shared<Button>("[+]", [this]() { increment(); });
    btn_up->fixed_width = 3;
    btn_up->bg_color = {60, 60, 70};
    btn_up->hover_color = {80, 80, 90};

    rebuild_layout();
  }

  void set_position(ButtonPos pos) {
    button_pos = pos;
    rebuild_layout();
  }

  void rebuild_layout() {
    children_.clear();

    if (!show_buttons_) {
      add(input_);
      return;
    }

    auto gap = std::make_shared<HorizontalSpacer>(1);

    if (button_pos == ButtonPos::Left) {
      add(btn_down);
      add(btn_up);
      add(gap);
      add(input_);
    } else if (button_pos == ButtonPos::Right) {
      add(input_);
      add(gap);
      add(btn_down);
      add(btn_up);
    } else {  // Split
      add(btn_down);
      add(gap);
      add(input_);
      add(gap);
      add(btn_up);
    }
  }

  void increment() {
    try {
      int v = 0;
      if (!input_->get_value().empty()) v = std::stoi(input_->get_value());
      v += step;
      if (v > max_value) v = max_value;
      input_->set_value(std::to_string(v));
      if (on_change) on_change(v);
    } catch (...) {
    }
  }

  void decrement() {
    try {
      int v = 0;
      if (!input_->get_value().empty()) v = std::stoi(input_->get_value());
      v -= step;
      if (v < min_value) v = min_value;
      input_->set_value(std::to_string(v));
      if (on_change) on_change(v);
    } catch (...) {
    }
  }

  int get_value() const {
    try {
      if (input_->get_value().empty()) return min_value;
      return std::stoi(input_->get_value());
    } catch (...) {
      return min_value;
    }
  }

  void set_value(int v) {
    if (v < min_value) v = min_value;
    if (v > max_value) v = max_value;
    input_->set_value(std::to_string(v));
  }

  std::shared_ptr<Input> input_;
  std::shared_ptr<Button> btn_up;
  std::shared_ptr<Button> btn_down;
};

/// @brief Bi-directional scrollable container for panning over large 2D content
class ScrollableContainer : public Container {
 public:
  int scroll_x = 0;
  int scroll_y = 0;
  int content_width = 0;
  int content_height = 0;

  bool is_dragging_v = false;
  bool is_dragging_h = false;

  bool show_v_scrollbar = true;
  bool show_h_scrollbar = true;

  int viewport_w = 0;
  int viewport_h = 0;

  ScrollableContainer() { focusable = true; }

  void layout() override {
    if (children_.empty()) return;

    // Calculate content size based on the first child (content)
    auto &content = children_[0];

    // Determine content dimensions
    if (content->fixed_width > 0)
      content_width = content->fixed_width;
    else
      content_width = width;

    if (content->fixed_height > 0)
      content_height = content->fixed_height;
    else
      content_height = height;

    content->update_responsive();

    // Determine Scrollbar Visibility
    // Multi-pass check for scrollbar mutual dependency.
    bool need_h = (content_width > width);
    bool need_v = (content_height > height);

    if (need_h && !need_v) {
      if (content_height > height - 1) need_v = true;
    }
    if (need_v && !need_h) {
      if (content_width > width - 1) need_h = true;
    }

    viewport_w = width;
    viewport_h = height;

    if (need_v) viewport_w -= 1;
    if (need_h) viewport_h -= 1;
    if (viewport_w < 0) viewport_w = 0;
    if (viewport_h < 0) viewport_h = 0;

    // Update Content Layout
    content->x = x - scroll_x;
    content->y = y - scroll_y;
    content->width = content_width;
    content->height = content_height;

    if (auto cont = std::dynamic_pointer_cast<Container>(content)) {
      cont->layout();
    }
  }

  void render(Buffer &buffer) override {
    layout();

    buffer.push_clip({x, y, viewport_w, viewport_h});

    if (!children_.empty()) {
      children_[0]->render(buffer);
    }

    buffer.pop_clip();

    draw_scrollbars(buffer);
  }
  void draw_scrollbars(Buffer &buffer) {
    bool need_h = show_h_scrollbar && (content_width > width);
    bool need_v = show_v_scrollbar && (content_height > height);

    // Check for mutual dependency
    if (need_h && !need_v && show_v_scrollbar) {
      if (content_height > height - 1) need_v = true;
    }
    if (need_v && !need_h && show_h_scrollbar) {
      if (content_width > width - 1) need_h = true;
    }

    if (need_v) {
      int track_h = height - (need_h ? 1 : 0);
      render_scrollbar(buffer, x + width - 1, y, track_h, scroll_y,
                       content_height, true, Color(), Color(), "\u2591",
                       "\u2588");
    }

    if (need_h) {
      int track_w = width - (need_v ? 1 : 0);
      render_scrollbar(buffer, x, y + height - 1, track_w, scroll_x,
                       content_width, false, Color(), Color(), "\u2581",
                       "\u2584");
    }
  }

  bool on_event(const Event &event) override {
    bool handled = false;
    if (event.is_mouse_event()) {
      // Focus on Click
      if (event.mouse_left()) {
        if (event.x >= x && event.x < x + width && event.y >= y &&
            event.y < y + height) {
          set_focus(true);
        }
      }

      // Wheel
      if (event.mouse_wheel()) {
        // PRIOR to processing wheel, ensure we are actually hovering this
        // container
        if (event.x >= x && event.x < x + width && event.y >= y &&
            event.y < y + height) {
          int step = 3;
          if (event.ctrl) {
            // Ctrl + Wheel -> Horizontal Scroll
            if (event.mouse_wheel_up()) {  // Wheel Up -> Scroll Left
              scroll_x -= step;
              handled = true;
            } else if (event
                           .mouse_wheel_down()) {  // Wheel Down -> Scroll Right
              scroll_x += step;
              handled = true;
            }
          } else {
            // Standard Wheel -> Vertical Scroll
            int old_y = scroll_y;
            if (event.mouse_wheel_up()) {  // Wheel Up -> Scroll Up
              scroll_y -= step;
              handled = true;
            } else if (event.mouse_wheel_down()) {  // Wheel Down -> Scroll Down
              scroll_y += step;
              handled = true;
            }

            if (handled) {
              int vh = (viewport_h > 0) ? viewport_h : height;
              int max_y = std::max(0, content_height - vh);
              if (scroll_y < 0) scroll_y = 0;
              if (scroll_y > max_y) scroll_y = max_y;

              if (scroll_y == old_y) handled = false;
            }
          }
        }
      }
      // Click / Drag
      else if (event.mouse_left() || event.mouse_drag() ||
               event.mouse_release()) {
        // Reset drag flags on release
        if (event.mouse_release()) {
          is_dragging_v = false;
          is_dragging_h = false;
        }

        // Check Vertical Scrollbar
        if (show_v_scrollbar) {
          // Adjust height to avoid overlapping horizontal scrollbar corner
          int track_h = height - (show_h_scrollbar ? 1 : 0);
          int effective_view_h = (viewport_h > 0) ? viewport_h : track_h;
          int adjusted_content_h = content_height - effective_view_h + track_h;

          if (handle_scrollbar_event(
                  event, x, y, width, track_h, adjusted_content_h, scroll_y,
                  is_dragging_v, false, [this]() { set_focus(true); })) {
            handled = true;
          }
        }

        // Check Horizontal Scrollbar (if not handled by V)
        if (!handled && show_h_scrollbar) {
          int track_w = width - (show_v_scrollbar ? 1 : 0);
          int effective_view_w = (viewport_w > 0) ? viewport_w : track_w;
          int adjusted_content_w = content_width - effective_view_w + track_w;

          if (handle_scrollbar_event(
                  event, x, y, track_w, height, adjusted_content_w, scroll_x,
                  is_dragging_h, true, [this]() { set_focus(true); })) {
            handled = true;
          }
        }
      }
    }
    if (event.is_key_event() && has_focus()) {
      int step = 2;  // Slightly faster key scroll
      if (event.is_nav_up()) {
        scroll_y -= step;
        handled = true;
      }  // Up
      if (event.is_nav_down()) {
        scroll_y += step;
        handled = true;
      }  // Down
      if (event.is_nav_right()) {
        scroll_x += step;
        handled = true;
      }  // Right
      if (event.is_nav_left()) {
        scroll_x -= step;
        handled = true;
      }  // Left

      // Ctrl Horizontal Scrolling
      int vw = (viewport_w > 0) ? viewport_w : width;
      if (event.ctrl) {
        if (event.is_nav_home()) {  // Ctrl+Home -> Leftmost
          scroll_x = 0;
          handled = true;
        } else if (event.is_nav_end()) {  // Ctrl+End -> Rightmost
          scroll_x = content_width - vw;
          handled = true;
        } else if (event.is_view_scroll_up()) {  // Ctrl+PageUp -> Left Page
          scroll_x -= vw;
          handled = true;
        } else if (event
                       .is_view_scroll_down()) {  // Ctrl+PageDown -> Right Page
          scroll_x += vw;
          handled = true;
        }
      } else {
        // Standard Vertical Scrolling
        int vh = (viewport_h > 0) ? viewport_h : height;

        if (event.is_nav_home()) {  // Home -> Top
          scroll_y = 0;
          handled = true;
        } else if (event.is_nav_end()) {  // End -> Bottom
          scroll_y = content_height - vh;
          handled = true;
        } else if (event.is_view_scroll_up()) {  // PageUp
          scroll_y -= vh;
          handled = true;
        } else if (event.is_view_scroll_down()) {  // PageDown
          scroll_y += vh;
          handled = true;
        }
      }
    }

    if (handled) {
      clamp_scroll();
      return true;
    }

    // Forward to child
    if (!children_.empty() && children_[0]->on_event(event)) return true;

    return false;
  }

  void clamp_scroll() {
    if (scroll_x < 0) scroll_x = 0;
    if (scroll_y < 0) scroll_y = 0;

    int vw = (viewport_w > 0) ? viewport_w : width;
    int vh = (viewport_h > 0) ? viewport_h : height;

    int max_x = content_width - vw;
    int max_y = content_height - vh;

    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    if (scroll_x > max_x) scroll_x = max_x;
    if (scroll_y > max_y) scroll_y = max_y;
  }
};
// --- Advanced Inputs ---

// --- Toggle Switch ---
/// @brief A modern toggle switch widget
class ToggleSwitch : public Widget {
 public:
  // Style: [ OFF ] / [ ON ] or ( ) / (*) with color
  StyledText styled_label;
  std::string label;
  bool is_on = false;
  std::function<void(bool)> on_change;

  // Styling
  Color active_color;
  Color inactive_color;
  std::string on_label = "[ ON  ]";
  std::string off_label = "[ OFF ]";

  ToggleSwitch(StyledText label, bool initial = false)
      : styled_label(label), label(label.plain_text()), is_on(initial) {
    focusable = true;
    width = utf8_display_width(this->label) + 8;  // "[ OFF ] " + label
    height = 1;
    fixed_height = 1;
  }

  void render(Buffer &buffer) override {
    if (height < 1 || width < 1) return;  // Clipped completely

    // Draw Label
    std::string state_str = is_on ? on_label : off_label;
    std::string full_text = state_str + " " + label;

    Color fg = fg_color.resolve(Theme::current().foreground);
    Color bg = bg_color.resolve(Theme::current().panel_bg);

    if (has_focus()) {
      fg = Theme::current().primary;
    }

    Color active = active_color.resolve(Theme::current().success);
    Color inactive = inactive_color.is_default
                         ? (has_focus() ? fg : Theme::current().border)
                         : inactive_color;

    Color state_fg = is_on ? active : inactive;

    // Get display width of state_str for color boundary
    int state_display_width = utf8_display_width(state_str);

    // Pre-compute UTF-8 characters
    struct CharInfo {
      std::string content;
      int display_width;
    };
    std::vector<CharInfo> chars;
    size_t pos = 0;
    while (pos < full_text.size()) {
      uint32_t codepoint;
      int byte_len;
      if (utf8_decode_codepoint(full_text, pos, codepoint, byte_len)) {
        CharInfo ci;
        ci.content = full_text.substr(pos, byte_len);
        ci.display_width = char_display_width(codepoint);
        if (ci.display_width < 0) ci.display_width = 0;
        chars.push_back(ci);
        pos += byte_len;
      } else {
        pos++;
      }
    }

    int state_chars =
        (int)TextHelper::prepare_text_for_render(state_str + " ").size();
    int cell_x = 0;
    for (size_t i = 0; i < chars.size() && cell_x < width; ++i) {
      const auto &ci = chars[i];
      Cell c;
      c.bg_color = bg;
      c.content = ci.content;

      if (cell_x < state_display_width) {
        c.fg_color = state_fg;
      } else {
        c.fg_color = fg;
        // Apply styling to the label part
        if ((int)i >= state_chars && !styled_label.empty()) {
          if (const TextSpan *span =
                  styled_label.get_span_at_char((int)i - state_chars)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }
      }

      buffer.set(x + cell_x, y, c);

      if (ci.display_width == 2 && cell_x + 1 < width) {
        Cell skip;
        skip.content = "";
        skip.bg_color = bg;
        buffer.set(x + cell_x + 1, y, skip);
      }

      cell_x += ci.display_width;
    }
  }
  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      if (event.x >= x && event.x < x + width && event.y >= y &&
          event.y < y + height) {
        if (event.mouse_left()) {
          toggle();
          return true;
        }
      }
    } else if (event.is_key_event() && has_focus()) {
      if (event.is_activate()) {
        toggle();
        return true;
      }
    }
    return false;
  }

  void toggle() {
    is_on = !is_on;
    if (on_change) on_change(is_on);
  }
};

/// @brief A set of mutually exclusive options (Radio buttons)
class RadioSet : public Widget {
 public:
  // Layout mode
  bool horizontal = false;
  std::vector<StyledText> options;
  int selected_index = 0;
  std::function<void(int)> on_change;
  std::function<void(int)> on_submit;  ///< Triggered on double click or Enter

  int focused_option_idx = 0;
  int hovered_index = -1;  // New Hover State

  // Styling
  Color selected_color;
  Color unselected_color;
  std::string selected_prefix = "(\u25CF) ";
  std::string unselected_prefix = "( ) ";

  RadioSet() { focusable = true; }

  void set_options(const std::vector<StyledText> &opts) {
    options = opts;
    recalculate_size();
    fixed_height = height;  // Enforce calculated height
  }

  void render(Buffer &buffer) override {
    if (height < 1 || width < 1) return;

    int draw_x = x;
    int draw_y = y;

    Color fg = fg_color.resolve(Theme::current().foreground);
    Color bg = bg_color.resolve(Theme::current().panel_bg);
    Color sel_col = Theme::current().input_fg;
    Color sel_bg = Theme::current().selection;

    if (has_focus()) {
      sel_bg = Theme::current().primary;
      sel_col = Theme::current().background;
    }

    for (size_t i = 0; i < options.size(); ++i) {
      bool is_sel = ((int)i == selected_index);

      std::string prefix = is_sel ? selected_prefix : unselected_prefix;

      Color current_fg = fg;
      Color current_bg = bg;

      if (is_sel) {
        current_bg = sel_bg;
        current_fg = sel_col;
      }

      if (is_sel && !selected_color.is_default) current_fg = selected_color;
      if (!is_sel && !unselected_color.is_default)
        current_fg = unselected_color;

      // Render prefix
      auto prefix_chars = TextHelper::prepare_text_for_render(prefix);
      int cell_x = 0;
      for (const auto &ci : prefix_chars) {
        Cell cell;
        cell.content = ci.content;
        cell.fg_color = current_fg;
        cell.bg_color = current_bg;
        if (!is_sel && (int)i == hovered_index)
          cell.bg_color = Theme::current().hover;
        buffer.set(draw_x + cell_x, draw_y, cell);
        cell_x += ci.display_width;
      }

      // Render option label
      auto label_chars =
          TextHelper::prepare_text_for_render(options[i].plain_text());
      for (size_t j = 0; j < label_chars.size(); ++j) {
        const auto &ci = label_chars[j];
        Cell cell;
        cell.content = ci.content;
        cell.fg_color = current_fg;
        cell.bg_color = current_bg;

        // Apply styling
        if (!options[i].empty()) {
          if (const TextSpan *span = options[i].get_span_at_char((int)j)) {
            if (!span->color.is_default) cell.fg_color = span->color;
            cell.bold = span->bold;
            cell.italic = span->italic;
            cell.underline = span->underline;
          }
        }

        if (!is_sel && (int)i == hovered_index)
          cell.bg_color = Theme::current().hover;

        buffer.set(draw_x + cell_x, draw_y, cell);

        if (ci.display_width == 2) {
          Cell skip;
          skip.content = "";
          skip.bg_color = cell.bg_color;
          buffer.set(draw_x + cell_x + 1, draw_y, skip);
        }
        cell_x += ci.display_width;
        if (horizontal && cell_x >= width) break;
      }

      if (horizontal) {
        draw_x += cell_x + 2;  // Spacing
      } else {
        draw_y++;
        draw_x = x;
        if (draw_y >= y + height) break;  // Clip
      }
    }
  }

  void recalculate_size() {
    int pref_w = utf8_display_width(selected_prefix);  // Assume both same width
    if (horizontal) {
      height = 1;
      width = 0;
      for (const auto &s : options)
        width += utf8_display_width(s.plain_text()) + pref_w + 2;
    } else {
      width = 0;
      height = options.size();
      for (const auto &s : options) {
        int w = utf8_display_width(s.plain_text()) + pref_w;
        if (w > width) width = w;
      }
    }
    if (fixed_width > 0) width = fixed_width;
    if (fixed_height > 0) height = fixed_height;
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      if (event.mouse_left() && event.x >= x && event.x < x + width &&
          event.y >= y && event.y < y + height) {
        if (horizontal) {
          int cx = x;
          for (size_t i = 0; i < options.size(); ++i) {
            int w = utf8_display_width(options[i].plain_text()) +
                    utf8_display_width(selected_prefix) + 2;
            if (event.x >= cx && event.x < cx + w) {
              select(i);
              set_focus(true);
              if (event.click_count >= 2 && on_submit) on_submit(i);
              return true;
            }
            cx += w;
          }
        } else {
          int row = event.y - y;
          if (row >= 0 && row < (int)options.size()) {
            select(row);
            set_focus(true);
            if (event.click_count >= 2 && on_submit) on_submit(row);
            return true;
          }
        }
      }
    }
    if (event.is_mouse_event()) {  // Update Hover
      if (event.x >= x && event.x < x + width && event.y >= y &&
          event.y < y + height) {
        if (horizontal) {
          int cx = x;
          for (size_t i = 0; i < options.size(); ++i) {
            int w = utf8_display_width(options[i].plain_text()) +
                    utf8_display_width(selected_prefix) + 2;
            if (event.x >= cx && event.x < cx + w) {
              if (hovered_index != (int)i) {
                hovered_index = (int)i;
                return true;
              }
              return !event.mouse_wheel();
            }
            cx += w;
          }
          hovered_index = -1;
        } else {
          int row = event.y - y;
          if (row >= 0 && row < (int)options.size()) {
            if (hovered_index != row) {
              hovered_index = row;
              return true;
            }
            return !event.mouse_wheel();
          } else
            hovered_index = -1;
        }
      } else {
        if (hovered_index != -1) {
          hovered_index = -1;
          return true;
        }
      }
    }

    if (event.is_key_event() && has_focus()) {
      if (event.is_nav_prev()) {  // Up or Left
        int new_idx = selected_index - 1;
        if (new_idx < 0) new_idx = options.size() - 1;
        select(new_idx);
        return true;
      }
      if (event.is_nav_next()) {  // Down or Right
        int new_idx = selected_index + 1;
        if (new_idx >= (int)options.size()) new_idx = 0;
        select(new_idx);
        return true;
      }
      if (event.is_enter()) {
        if (on_submit) on_submit(selected_index);
        return true;
      }
    }
    return false;
  }

  void select(int idx) {
    if (idx != selected_index) {
      selected_index = idx;
      if (on_change) on_change(selected_index);
    }
  }

  // Helper to update prefixes
  void set_style(const std::string &sel_pre, const std::string &unsel_pre) {
    selected_prefix = sel_pre;
    unselected_prefix = unsel_pre;
    recalculate_size();
  }

 private:
  void layout_internals() { recalculate_size(); }
};

/// @brief A list of options with multiple selection capability
class CheckboxList : public Widget {
 public:
  std::vector<StyledText> options;
  std::vector<bool> checked_states;  // Parallel array
  std::function<void(int, bool)> on_change;
  std::function<void(int)> on_submit;  ///< Triggered on double click or Enter

  int hovered_index = -1;
  int cursor_index = 0;

  // Styling
  Color checked_color;
  Color unchecked_color;
  Color cursor_color;
  std::string checked_prefix = "[x] ";
  std::string unchecked_prefix = "[ ] ";

  CheckboxList() { focusable = true; }

  void set_options(const std::vector<StyledText> &opts) {
    options = opts;
    checked_states.assign(opts.size(), false);
    recalculate_size();
    fixed_height = height;  // Enforce calculated height
  }

  void recalculate_size() {
    height = options.size();
    width = 0;
    int pref_w = utf8_display_width(unchecked_prefix);
    for (const auto &s : options) {
      int w = utf8_display_width(s.plain_text()) + pref_w;
      if (w > width) width = w;
    }
    if (fixed_width > 0) width = fixed_width;
    if (fixed_height > 0) height = fixed_height;
  }

  void render(Buffer &buffer) override {
    int draw_x = x;
    int draw_y = y;

    Color fg = fg_color.resolve(Theme::current().foreground);
    Color bg = bg_color.resolve(Theme::current().panel_bg);

    for (size_t i = 0; i < options.size(); ++i) {
      if (draw_y >= y + height) break;

      bool is_checked = checked_states[i];
      bool is_cursor = (has_focus() && (int)i == cursor_index);

      std::string prefix = is_checked ? checked_prefix : unchecked_prefix;

      Color line_fg = fg;
      if (is_checked) line_fg = checked_color.resolve(Theme::current().success);

      // Render prefix
      auto prefix_chars = TextHelper::prepare_text_for_render(prefix);
      int cell_x = 0;
      for (const auto &ci : prefix_chars) {
        Cell c;
        c.content = ci.content;
        c.fg_color = line_fg;
        c.bg_color = bg;
        if (is_cursor) {
          c.bg_color = Theme::current().primary;
          c.fg_color = Theme::current().background;
        } else if ((int)i == hovered_index)
          c.bg_color = Theme::current().hover;
        buffer.set(draw_x + cell_x, draw_y, c);
        cell_x += ci.display_width;
      }

      // Render option label
      auto label_chars =
          TextHelper::prepare_text_for_render(options[i].plain_text());
      for (size_t j = 0; j < label_chars.size(); ++j) {
        const auto &ci = label_chars[j];
        Cell c;
        c.content = ci.content;
        c.fg_color = line_fg;
        c.bg_color = bg;

        // Apply styling
        if (!options[i].empty()) {
          if (const TextSpan *span = options[i].get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        if (is_cursor) {
          c.bg_color = Theme::current().primary;
          c.fg_color = Theme::current().background;
        } else if ((int)i == hovered_index)
          c.bg_color = Theme::current().hover;

        buffer.set(draw_x + cell_x, draw_y, c);

        if (ci.display_width == 2) {
          Cell skip;
          skip.content = "";
          skip.bg_color = c.bg_color;
          buffer.set(draw_x + cell_x + 1, draw_y, skip);
        }
        cell_x += ci.display_width;
      }
      draw_y++;
    }
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      if (event.x >= x && event.x < x + width && event.y >= y &&
          event.y < y + height) {
        int row = event.y - y;
        if (row >= 0 && row < (int)options.size()) {
          if (event.mouse_move() || event.mouse_drag() ||
              event.mouse_motion()) {
            if (hovered_index != row) {
              hovered_index = row;
              return true;
            }
            return !event.mouse_wheel();
          } else if (event.mouse_left()) {
            toggle(row);
            cursor_index = row;
            set_focus(true);
            if (event.click_count >= 2 && on_submit) on_submit(row);
            return true;
          }
        } else {
          hovered_index = -1;
        }
      } else {
        if (hovered_index != -1) {
          hovered_index = -1;
          return true;
        }
      }
    } else if (event.is_key_event() && has_focus()) {
      if (event.is_nav_up()) {  // Up
        cursor_index--;
        if (cursor_index < 0) cursor_index = options.size() - 1;
        return true;
      }
      if (event.is_nav_down()) {  // Down
        cursor_index++;
        if (cursor_index >= (int)options.size()) cursor_index = 0;
        return true;
      }
      if (event.is_activate()) {
        toggle(cursor_index);
        if (on_submit) on_submit(cursor_index);
        return true;
      }
    }
    return false;
  }

  void toggle(int idx) {
    if (idx >= 0 && idx < (int)checked_states.size()) {
      checked_states[idx] = !checked_states[idx];
      if (on_change) on_change(idx, checked_states[idx]);
    }
  }
};

/// @brief A collapsible dropdown selection menu
class Dropdown : public Widget {
 public:
  std::vector<StyledText> options;
  int selected_index = -1;
  bool is_open = false;
  StyledText placeholder = "Select...";

  std::function<void(int, std::string)> on_change;

  // Popup management
  void toggle();
  void close_popup();
  void close() { close_popup(); }
  Color bg_color = Color();
  Color fg_color = Color();
  Color hover_bg = Color();
  Color hover_fg = Color();

 private:
  std::weak_ptr<Dialog> popup_ref_;
  App *app_ = nullptr;

 public:
  Dropdown(App *app = nullptr) : app_(app) {
    focusable = true;
    height = 1;        // Default closed
    fixed_height = 1;  // Start closed
  }

  void set_options(const std::vector<StyledText> &opts) {
    options = opts;
    if (selected_index >= (int)options.size()) selected_index = -1;
    // fixed_height stays 1 (closed) by default
  }

  void make_selection(int idx) {
    if (idx >= 0 && idx < (int)options.size()) {
      selected_index = idx;
      if (on_change) on_change(idx, options[idx]);
    }
    // close_popup() called by dialog
    is_open = false;
  }

  void render(Buffer &buffer) override {
    // 1. Header
    Color bg = bg_color.resolve(Theme::current().input_bg);
    Color fg = fg_color.resolve(Theme::current().input_fg);

    if (has_focus()) {
      fg = Theme::current().primary;
    } else if (hovered_) {
      bg = hover_bg.resolve(Theme::current().hover);
      fg = hover_fg.resolve(Theme::current().input_fg);
    }

    StyledText styled_text = placeholder;
    if (selected_index >= 0 && selected_index < (int)options.size()) {
      styled_text = options[selected_index];
    }
    std::string text = styled_text.plain_text();

    // Draw Header Box
    std::string arrow = is_open ? "^" : "v";

    int avail_w = width - 2;  // margins
    if (avail_w < 0) avail_w = 0;

    int text_w = utf8_display_width(text);
    if (text_w > avail_w - 2) {
      text = TextHelper::utf8_substr(text, 0, avail_w - 2) + "..";
    }

    for (int i = 0; i < width; ++i) {
      Cell c;
      c.bg_color = bg;
      c.fg_color = fg;
      buffer.set(x + i, y, c);
    }

    auto text_chars = TextHelper::prepare_text_for_render(text);
    int current_dx = 0;
    for (size_t i = 0; i < text_chars.size(); ++i) {
      if (current_dx >= avail_w - 2) break;
      const auto &ci = text_chars[i];
      Cell c;
      c.content = ci.content;
      c.bg_color = bg;
      c.fg_color = fg;

      // Apply styling
      if (!styled_text.empty()) {
        if (const TextSpan *span = styled_text.get_span_at_char((int)i)) {
          if (!span->color.is_default) c.fg_color = span->color;
          c.bold = span->bold;
          c.italic = span->italic;
          c.underline = span->underline;
        }
      }

      buffer.set(x + 1 + current_dx, y, c);
      if (ci.display_width == 2 && current_dx + 1 < avail_w - 2) {
        Cell skip;
        skip.content = "";
        skip.bg_color = bg;
        buffer.set(x + 1 + current_dx + 1, y, skip);
      }
      current_dx += ci.display_width;
    }

    Cell c_arr;
    c_arr.content = arrow;
    c_arr.bg_color = bg;
    c_arr.fg_color = fg;
    buffer.set(x + width - 2, y, c_arr);
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      if (event.x >= x && event.x < x + width) {
        // Header Click/Hover
        if (event.y == y) {
          if (event.mouse_move() || event.mouse_drag() ||
              event.mouse_motion()) {
            set_hovered(true);
          } else if (event.mouse_left()) {
            toggle();
            set_focus(true);  // Focus on click
            return true;
          }
        } else {
          set_hovered(false);
        }
        // List Click is handled by Popup Dialog now
      } else {
        set_hovered(false);
      }
    } else if (event.is_key_event() && has_focus()) {
      if (event.is_activate()) {
        toggle();
        return true;
      }
    }
    // Handle Esc to close popup
    // If popup is open, IT has focus usually (set_focus in toggle).
    // So we don't need to handle Esc here for closing popup if popup handles
    // it.

    return false;
  }
};

struct TreeNode {
  std::string label;
  std::vector<TreeNode> children;
  bool expanded = false;
  bool selected = false;
  std::string id;
  std::string user_data;
  std::string icon;  // Custom icon for this node
  Color color;       // Custom color for this node (default if empty/default)

  TreeNode &add(const std::string &lbl) {
    children.push_back({lbl, {}, false, false, "", "", "", Color()});
    return children.back();
  }

  // Helper to find a node by pointer
  TreeNode *find(TreeNode *target) {
    if (this == target) return this;
    for (auto &c : children) {
      if (auto *res = c.find(target)) return res;
    }
    return nullptr;
  }
};

/// @brief Displays hierarchical data in a tree structure
class TreeView : public ScrollableVertical {
 private:
  int scroll_x_ = 0;

 public:
  std::vector<TreeNode> root_nodes;

  // Styling options
  std::string indent_unit = "  ";
  std::string icon_expanded = "v";
  std::string icon_collapsed = ">";
  std::string icon_leaf = " ";

  Color selected_bg;
  Color selected_fg;
  Color hover_bg;  // Used for mouse hover

  // Callbacks
  std::function<void(TreeNode *)> on_select;
  std::function<void(TreeNode *)> on_submit;  // Enter pressed
  std::function<void(TreeNode *)> on_expand;
  std::function<void(TreeNode *)> on_collapse;

  TreeView() {
    selected_bg = Theme::current().selection;
    selected_fg = Theme::current().foreground;
    hover_bg = Theme::current().hover;
  }

  void layout() override {
    if (children_.empty()) return;

    // 1. Calculate Content Dimensions
    content_height = 0;
    int max_content_width = 0;

    for (auto &child : children_) {
      if (!child->visible) continue;

      if (auto btn = std::dynamic_pointer_cast<Button>(child)) {
        // Measure raw string length for now, assuming 1 char = 1 col usually
        // For better results use utf8 utils if available, but size() is decent
        // approx for ASCII paths
        int w = (int)btn->get_label().size() + 2;  // + padding
        if (w > max_content_width) max_content_width = w;
      }

      int intended_height = (child->fixed_height > 0) ? child->fixed_height : 1;
      content_height += intended_height;
    }

    // 2. Determine Scrollbars
    bool show_v_scroll = (content_height > height && height > 0);
    int effective_width = width - (show_v_scroll ? 1 : 0);
    if (effective_width < 0) effective_width = 0;

    bool show_h_scroll =
        (max_content_width > effective_width && effective_width > 0);
    int effective_height = height - (show_h_scroll ? 1 : 0);
    if (effective_height < 0) effective_height = 0;

    // Re-check V scroll with reduced height (corner case)
    if (content_height > effective_height && effective_height > 0) {
      show_v_scroll = true;
      effective_width = width - 1;
    }

    // 3. Layout Children
    int current_y = y - scroll_offset;
    int final_child_width = std::max(effective_width, max_content_width);

    // Clamp scroll_x_
    int max_scroll_x = std::max(0, max_content_width - effective_width);
    if (scroll_x_ > max_scroll_x) scroll_x_ = max_scroll_x;
    if (scroll_x_ < 0) scroll_x_ = 0;

    for (auto &child : children_) {
      if (!child->visible) continue;

      child->x = x - (show_h_scroll ? scroll_x_ : 0);
      child->y = current_y;
      child->width =
          clamp_size(final_child_width, child->min_width, child->max_width);

      int intended_height = (child->fixed_height > 0) ? child->fixed_height : 1;
      child->height =
          clamp_size(intended_height, child->min_height, child->max_height);

      current_y += child->height;
    }
  }

  void render(Buffer &buffer) override {
    // Push clip rect for this container
    buffer.push_clip({x, y, width, height});

    Container::render(buffer);

    // layout calculated dimensions, let's re-derive for rendering props
    // Ideally we should cache these in layout, but re-calc is cheap enough for
    // now
    content_height = 0;
    int max_content_width = 0;
    for (auto &child : children_) {
      if (child->visible) {
        content_height += (child->fixed_height > 0 ? child->fixed_height : 1);
        if (auto btn = std::dynamic_pointer_cast<Button>(child)) {
          int w = (int)btn->get_label().size() + 2;
          if (w > max_content_width) max_content_width = w;
        }
      }
    }

    bool show_v_scroll = (content_height > height && height > 0);
    int effective_width = width - (show_v_scroll ? 1 : 0);

    bool show_h_scroll =
        (max_content_width > effective_width && effective_width > 0);
    int effective_height = height - (show_h_scroll ? 1 : 0);

    // Re-check V scroll
    if (content_height > effective_height && effective_height > 0) {
      show_v_scroll = true;
      effective_width = width - 1;
      // Update effective height again if V scroll appeared? No, H scroll takes
      // priority on height reduction
    }

    // Draw Vertical Scrollbar
    if (show_v_scroll) {
      render_scrollbar(buffer, x + width - 1, y, effective_height,
                       scroll_offset, content_height, true);
    }

    // Draw Horizontal Scrollbar
    if (show_h_scroll) {
      render_scrollbar(buffer, x, y + height - 1, effective_width, scroll_x_,
                       max_content_width, false);

      // Paint Bottom-Right Corner
      if (show_v_scroll) {
        Cell c;
        c.bg_color = Theme::current().input_bg;
        c.content = " ";
        buffer.set(x + width - 1, y + height - 1, c);
      }
    }

    buffer.pop_clip();
  }

  void refresh() {
    children_.clear();
    visible_nodes_.clear();
    content_height = 0;

    for (auto &node : root_nodes) {
      process_node(node, 0);
    }

    // Re-apply selection state visual if needed (handled in render/widget
    // creation) Handle lost selection if node removed Assumes stable pointers
    // between rebuilds
  }

  TreeNode *get_selected_node() const { return selected_node_; }

  void set_selected_node(TreeNode *node) {
    if (selected_node_) selected_node_->selected = false;
    selected_node_ = node;
    if (selected_node_) selected_node_->selected = true;
    if (on_select && selected_node_) on_select(selected_node_);
    refresh();  // Rebuild to update styles
    ensure_visible(node);
  }

  bool on_event(const Event &event) override {
    if (event.is_key_event()) {
      if (visible_nodes_.empty()) return ScrollableVertical::on_event(event);

      int current_idx = -1;
      for (size_t i = 0; i < visible_nodes_.size(); ++i) {
        if (visible_nodes_[i] == selected_node_) {
          current_idx = (int)i;
          break;
        }
      }

      if (event.is_nav_up()) {  // Up
        if (current_idx > 0) {
          set_selected_node(visible_nodes_[current_idx - 1]);
          return true;
        }
      } else if (event.is_nav_down()) {  // Down
        if (current_idx < (int)visible_nodes_.size() - 1) {
          // If nothing selected, select first
          if (current_idx == -1)
            set_selected_node(visible_nodes_[0]);
          else
            set_selected_node(visible_nodes_[current_idx + 1]);
          return true;
        } else if (current_idx == -1 && !visible_nodes_.empty()) {
          set_selected_node(visible_nodes_[0]);
          return true;
        }
      } else if (event.is_nav_right()) {  // Right
        if (selected_node_) {
          if (!selected_node_->children.empty()) {
            if (!selected_node_->expanded) {
              selected_node_->expanded = true;
              if (on_expand) on_expand(selected_node_);
              refresh();
            } else {
              // Already expanded, move down to first child if exists
              // Next node in visible list is the child
              if (current_idx < (int)visible_nodes_.size() - 1) {
                set_selected_node(visible_nodes_[current_idx + 1]);
              }
            }
          }
          return true;
        }
      } else if (event.is_nav_left()) {  // Left
        if (selected_node_) {
          if (selected_node_->expanded && !selected_node_->children.empty()) {
            selected_node_->expanded = false;
            if (on_collapse) on_collapse(selected_node_);
            refresh();
          } else {
            // Move to parent
            // Find parent to traverse up
            // Simple scan:
            TreeNode *parent = find_parent(selected_node_);
            if (parent) set_selected_node(parent);
          }
          return true;
        }
      } else if (event.is_activate()) {  // Enter/Space
        if (selected_node_) {
          // Toggle expansion if has children
          if (!selected_node_->children.empty()) {
            selected_node_->expanded = !selected_node_->expanded;
            if (selected_node_->expanded && on_expand)
              on_expand(selected_node_);
            if (!selected_node_->expanded && on_collapse)
              on_collapse(selected_node_);
            refresh();
          } else {
            if (on_submit) on_submit(selected_node_);
          }
          return true;
        }
      }
    } else if (event.is_mouse_event()) {
      // Check if mouse is actually over the tree view!
      bool hit = (event.x >= x && event.x < x + width && event.y >= y &&
                  event.y < y + height);
      // Handle Horizontal Scroll (Ctrl + Wheel)
      if (hit && event.mouse_wheel() && event.ctrl) {
        int old_x = scroll_x_;
        if (event.mouse_wheel_up()) scroll_x_ -= 3;    // Left
        if (event.mouse_wheel_down()) scroll_x_ += 3;  // Right
        if (scroll_x_ < 0) scroll_x_ = 0;
        // Clamping for max_x happens in layout/render
        if (scroll_x_ != old_x) return true;
      }
    }

    // Normal scroll handling
    return ScrollableVertical::on_event(event);
  }

 private:
  TreeNode *selected_node_ = nullptr;
  std::vector<TreeNode *> visible_nodes_;

  TreeNode *find_parent(TreeNode *child, std::vector<TreeNode> &nodes) {
    for (auto &n : nodes) {
      for (auto &c : n.children) {
        if (&c == child) return &n;
        TreeNode *res = find_parent(child, n.children);
        if (res) return res;
      }
    }
    return nullptr;
  }

  TreeNode *find_parent(TreeNode *child) {
    return find_parent(child, root_nodes);
  }

  void ensure_visible(TreeNode *node) {
    // Calculate index
    int idx = -1;
    for (size_t i = 0; i < visible_nodes_.size(); ++i) {
      if (visible_nodes_[i] == node) {
        idx = i;
        break;
      }
    }
    if (idx == -1) return;

    // Each item height is 1
    int item_y = idx;  // Relative to top of content

    // Scroll offset mapping
    // ScrollableVertical uses scroll_offset as Y translation.

    if (item_y < scroll_offset) {
      scroll_offset = item_y;
    } else if (item_y >= scroll_offset + height) {
      scroll_offset = item_y - height + 1;
    }
  }

 public:
  std::string get_full_path(TreeNode *node,
                            const std::string &separator = "/") {
    if (!node) return "";
    std::string path;
    if (find_path_recursive(node, root_nodes, path, separator)) {
      return path;
    }
    return "";
  }

 private:
  bool find_path_recursive(TreeNode *target, std::vector<TreeNode> &nodes,
                           std::string &path, const std::string &sep) {
    for (auto &n : nodes) {
      // Determine if we need a separator
      std::string prefix = sep;
      if (path.empty()) {
        prefix = "";
      } else {
        // Check if path ends with sep
        if (path.length() >= sep.length() &&
            path.compare(path.length() - sep.length(), sep.length(), sep) ==
                0) {
          prefix = "";  // Path already ends with separator (e.g. root "/")
        }
      }

      if (&n == target) {
        path += prefix + n.label;
        return true;
      }

      std::string current_segment = prefix + n.label;
      std::string original_path = path;
      path += current_segment;

      if (find_path_recursive(target, n.children, path, sep)) return true;

      path = original_path;  // Backtrack
    }
    return false;
  }

  void process_node(TreeNode &node, int depth) {
    visible_nodes_.push_back(&node);

    std::string indent;
    for (int i = 0; i < depth; ++i) indent += indent_unit;

    std::string icon;
    if (node.icon.empty()) {
      icon = node.children.empty()
                 ? icon_leaf
                 : (node.expanded ? icon_expanded : icon_collapsed);
    } else {
      icon = node.icon;
    }

    std::string text = indent + icon + " " + node.label;

    // Styling
    Color fg = node.color.resolve(Theme::current().foreground);
    Color bg = {0, 0, 0, true};  // Transparent default

    if (node.selected) {
      bg = selected_bg;
      fg = selected_fg;
    }

    TreeNode *ptr = &node;
    auto btn = std::make_shared<Button>(text, [this, ptr]() {
      set_selected_node(ptr);
      if (!ptr->children.empty()) {
        ptr->expanded = !ptr->expanded;
        refresh();
      } else {
        if (on_submit) on_submit(ptr);
      }
    });
    btn->on_double_click = [this, ptr]() {
      if (on_submit) on_submit(ptr);
    };
    btn->fixed_height = 1;
    btn->bg_color = bg;
    btn->text_color = fg;
    btn->width = 100;  // Fill
    btn->alignment = Alignment::Left;

    btn->hover_color = hover_bg;
    if (node.selected)
      btn->hover_color =
          selected_bg;  // Keep selection color on hover if selected

    add(btn);

    if (node.expanded) {
      for (auto &child : node.children) {
        process_node(child, depth + 1);
      }
    }
  }
};

/// @brief A filesystem browser widget using TreeView
class FileExplorer : public Container {
 public:
  FileExplorer(const std::string &root_path = ".") : root_path_(root_path) {
    tree_view_ = std::make_shared<TreeView>();
    add(tree_view_);
    refresh();
  }

  std::function<void(std::string)> on_file_selected;

  void refresh() {
    tree_view_->root_nodes.clear();
    tree_view_->clear_children();

    std::filesystem::path p = std::filesystem::absolute(root_path_);
    TreeNode root;
    root.label = p.filename().string();
    if (root.label.empty()) root.label = root_path_;
    root.user_data = p.string();
    root.expanded = true;
    populate_node_recursive(root, p);
    tree_view_->root_nodes.push_back(root);

    tree_view_->on_submit = [this](TreeNode *node) {
      if (on_file_selected && node && !node->user_data.empty()) {
        if (!std::filesystem::is_directory(node->user_data)) {
          on_file_selected(node->user_data);
        }
      }
    };

    tree_view_->refresh();
  }

  void layout() override {
    if (tree_view_) {
      tree_view_->x = x;
      tree_view_->y = y;
      tree_view_->width = width;
      tree_view_->height = height;
      tree_view_->layout();
    }
  }

 private:
  std::string root_path_;
  std::shared_ptr<TreeView> tree_view_;

  void populate_node_recursive(TreeNode &node,
                               const std::filesystem::path &path) {
    try {
      for (const auto &entry : std::filesystem::directory_iterator(path)) {
        TreeNode child;
        child.label = entry.path().filename().string();
        child.user_data = entry.path().string();
        if (entry.is_directory()) {
          child.icon = "\U0001F4C1";  // Folder icon
          populate_node_recursive(child, entry.path());
        } else {
          child.icon = "\U0001F4C4";  // File icon
        }
        node.children.push_back(child);
      }
    } catch (...) {
    }

    std::sort(node.children.begin(), node.children.end(),
              [](const TreeNode &a, const TreeNode &b) {
                bool a_is_dir = !a.children.empty();
                bool b_is_dir = !b.children.empty();
                if (a_is_dir != b_is_dir) return a_is_dir;
                return a.label < b.label;
              });
  }
};

/// @brief Border drawing styles for bordered containers
enum class BorderStyle {
  ASCII,   ///< Simple ASCII characters (+, -, |)
  Single,  ///< Single-line Unicode box drawing
  Double,  ///< Double-line Unicode box drawing
  Rounded  ///< Rounded corner Unicode box drawing
};

// Alignment moved to top

/// @brief Wraps a child with a visible border
class Border : public Container {
 public:
  /// @brief Construct a new Border
  /// @param style The style of border to draw
  /// @param color The color of the border
  Border(BorderStyle style = BorderStyle::ASCII,
         const cpptui::Color &color = cpptui::Color())
      : style_(style), color_(color) {
    focusable = true;
    tab_stop = false;
  }

  /// @brief Set the border title
  /// @param title The text to display
  /// @param align Alignment of the title (Left, Center, Right)
  void set_title(const std::string &title,
                 Alignment align = Alignment::Center) {
    title_ = title;
    title_align_ = align;
  }
  /// @brief Get the current title
  const std::string &get_title() const { return title_; }

  bool has_selection() const { return selection_state_.has_selection(); }

  void layout() override {
    // All children fill the area minus border
    int inner_w = width > 2 ? width - 2 : 0;
    int inner_h = height > 2 ? height - 2 : 0;
    for (auto &child : children_) {
      child->update_responsive();
      child->x = x + 1;
      child->y = y + 1;
      child->width =
          child->fixed_width > 0
              ? std::min(child->fixed_width, inner_w)
              : clamp_size(inner_w, child->min_width, child->max_width);
      child->height =
          child->fixed_height > 0
              ? std::min(child->fixed_height, inner_h)
              : clamp_size(inner_h, child->min_height, child->max_height);

      if (auto cont = std::dynamic_pointer_cast<Container>(child)) {
        cont->layout();
      }
    }
  }

  void set_color(Color c) { color_ = c; }
  void set_bg_color(Color c) { bg_color_ = c; }
  void set_style(BorderStyle s) { style_ = s; }

  bool on_event(const Event &event) override {
    if (title_.empty()) return Container::on_event(event);

    // Hit test for title
    // Need to replicate layout logic to find title bounds
    auto chars = TextHelper::prepare_text_for_render(title_);
    int title_display_width = 0;
    for (const auto &ci : chars) title_display_width += ci.display_width;
    if (title_display_width > width - 4) title_display_width = width - 4;

    int start_x = x + 2;
    if (title_align_ == Alignment::Center)
      start_x = x + (width - title_display_width) / 2;
    else if (title_align_ == Alignment::Right)
      start_x = x + width - 2 - title_display_width;

    // Handle title selection
    if (event.is_mouse_event()) {
      if (event.y == y && event.x >= start_x &&
          event.x < start_x + title_display_width) {
        int local_x = event.x - start_x;
        int char_idx = TextHelper::visual_to_char_pos(chars, local_x);

        if (event.mouse_drag()) {
          if (selection_state_.handle_mouse_drag(char_idx)) return true;
        } else if (event.mouse_left())  // Press
        {
          set_focus(true);  // Take focus for Copy
          if (selection_state_.handle_mouse_press(chars, char_idx,
                                                  event.click_count))
            return true;
        }
      }

      if (event.mouse_drag()) {
        // Dragging outside title area, still update selection
        // Clamp to title bounds
        int local_x = event.x - start_x;
        if (local_x < 0) local_x = 0;
        if (local_x > title_display_width) local_x = title_display_width;

        int char_idx = TextHelper::visual_to_char_pos(chars, local_x);
        if (selection_state_.handle_mouse_drag(char_idx)) return true;
      }

      if (event.mouse_release()) {
        if (selection_state_.handle_mouse_release()) return true;
      }
    } else if (event.is_key_event()) {
      if (has_focus() && event.is_copy()) {
        std::string text = selection_state_.get_selected_text(chars);
        if (!text.empty()) {
          copy_to_clipboard(text);
          return true;
        }
      }
    }

    return Container::on_event(event);
  }

  void on_blur() override {
    selection_state_.clear();
    Container::on_blur();
  }

  void render(Buffer &buffer) override {
    layout();  // Ensure children are properly sized

    // Define characters based on style
    std::string h_line, v_line, tl, tr, bl, br;

    switch (style_) {
      case BorderStyle::ASCII:
        h_line = "-";
        v_line = "|";
        tl = "+";
        tr = "+";
        bl = "+";
        br = "+";
        break;
      case BorderStyle::Single:
        h_line = "\u2500";
        v_line = "\u2502";
        tl = "\u250C";
        tr = "\u2510";
        bl = "\u2514";
        br = "\u2518";
        break;
      case BorderStyle::Double:
        h_line = "\u2550";
        v_line = "\u2551";
        tl = "\u2554";
        tr = "\u2557";
        bl = "\u255A";
        br = "\u255D";
        break;
      case BorderStyle::Rounded:
        h_line = "\u2500";
        v_line = "\u2502";
        tl = "\u256D";
        tr = "\u256E";
        bl = "\u2570";
        br = "\u256F";
        break;
    }

    // Horizontal
    for (int dx = 0; dx < width; ++dx) {
      Color fg = color_.resolve(Theme::current().border);
      Color bg = bg_color_.resolve(Theme::current().background);

      Cell c;
      c.fg_color = fg;
      c.bg_color = bg;
      if (dx == 0) {
        c.content = tl;
        buffer.set(x, y, c);
        c.content = bl;
        buffer.set(x, y + height - 1, c);
      } else if (dx == width - 1) {
        c.content = tr;
        buffer.set(x + width - 1, y, c);
        c.content = br;
        buffer.set(x + width - 1, y + height - 1, c);
      } else {
        c.content = h_line;
        buffer.set(x + dx, y, c);
        buffer.set(x + dx, y + height - 1, c);
      }
    }
    // Vertical (excluding corners)
    for (int dy = 1; dy < height - 1; ++dy) {
      Color fg = color_.resolve(Theme::current().border);
      Color bg = bg_color_.resolve(Theme::current().background);

      Cell c;
      c.fg_color = fg;
      c.bg_color = bg;
      c.content = v_line;
      buffer.set(x, y + dy, c);
      buffer.set(x + width - 1, y + dy, c);
    }

    // Render children
    buffer.push_clip(
        {x + 1, y + 1, width > 2 ? width - 2 : 0, height > 2 ? height - 2 : 0});
    Container::render(buffer);
    buffer.pop_clip();

    // Render Title
    if (!title_.empty() && width > 4) {
      auto chars = TextHelper::prepare_text_for_render(title_);

      int title_display_width = 0;
      for (const auto &ci : chars) title_display_width += ci.display_width;
      if (title_display_width > width - 4) title_display_width = width - 4;

      int start_x = x + 2;  // Left
      if (title_align_ == Alignment::Center) {
        start_x = x + (width - title_display_width) / 2;
      } else if (title_align_ == Alignment::Right) {
        start_x = x + width - 2 - title_display_width;
      }

      int cell_x = 0;
      for (size_t i = 0; i < chars.size() && cell_x < title_display_width;
           ++i) {
        const auto &ci = chars[i];
        bool selected = selection_state_.is_selected(i);

        if (ci.display_width > 0) {
          Cell c;
          c.content = ci.content;
          c.fg_color = color_.resolve(Theme::current().border);
          c.bg_color = bg_color_.resolve(Theme::current().background);

          if (selected) {
            c.bg_color = Theme::current().selection;
            c.fg_color = Color::contrast_color(c.bg_color);
          }

          buffer.set(start_x + cell_x, y, c);

          // For wide chars, fill next cell with empty
          if (ci.display_width == 2 && cell_x + 1 < title_display_width) {
            Cell skip;
            skip.content = "";
            skip.bg_color = c.bg_color;
            buffer.set(start_x + cell_x + 1, y, skip);
          }
        }
        cell_x += ci.display_width;
      }
    }
  }

 protected:
  BorderStyle style_;
  Color color_;
  Color bg_color_;
  std::string title_;
  Alignment title_align_ = Alignment::Center;

  SelectionState selection_state_;

  int get_container_padding_height() const override { return 2; }
  int get_container_padding_width() const override { return 2; }
};

/// @brief Aligns a single child within the available space
class Align : public Container {
 public:
  /// @brief Horizontal alignment options
  enum class H { Left, Center, Right };
  /// @brief Vertical alignment options
  enum class V { Top, Center, Bottom };

  /// @brief Construct a new Align container
  /// @param h_align Horizontal alignment
  /// @param v_align Vertical alignment
  Align(H h_align = H::Center, V v_align = V::Center)
      : h_align_(h_align), v_align_(v_align) {}

  void layout() override {
    if (children_.empty()) return;
    // Layout each child within the container

    for (auto &child : children_) {
      child->update_responsive();

      // Determine child size.
      // If child is fixed size, use it; otherwise fill and clamp.
      int child_w = child->fixed_width > 0
                        ? child->fixed_width
                        : clamp_size(width, child->min_width, child->max_width);
      int child_h =
          child->fixed_height > 0
              ? child->fixed_height
              : clamp_size(height, child->min_height, child->max_height);

      int pos_x = x;
      int pos_y = y;

      switch (h_align_) {
        case H::Left:
          pos_x = x;
          break;
        case H::Center:
          pos_x = x + (width - child_w) / 2;
          break;
        case H::Right:
          pos_x = x + width - child_w;
          break;
      }

      switch (v_align_) {
        case V::Top:
          pos_y = y;
          break;
        case V::Center:
          pos_y = y + (height - child_h) / 2;
          break;
        case V::Bottom:
          pos_y = y + height - child_h;
          break;
      }

      child->x = pos_x;
      child->y = pos_y;
      child->width = child_w;
      child->height = child_h;

      if (auto cont = std::dynamic_pointer_cast<Container>(child)) {
        cont->layout();
      }
    }
  }

 private:
  H h_align_;
  V v_align_;
};

/// @brief Arranges children in a grid structure
class Grid : public Container {
 public:
  // Grid with manual definition
  // Columns can be fixed or auto

  /// @brief Represents a single item in the grid definition
  struct Item {
    std::shared_ptr<Widget> widget;  ///< The widget to place
    int row;                         ///< Row index (0-based)
    int col;                         ///< Column index (0-based)
    int row_span = 1;                ///< Number of rows to span
    int col_span = 1;                ///< Number of columns to span
  };

  std::vector<int> row_heights;  ///< Explicit row heights (0 = auto)
  std::vector<int> col_widths;   ///< Explicit column widths (0 = auto)

  /// @brief Add a widget to the grid at a specific position
  /// @param w The widget to add
  /// @param row Row index
  /// @param col Column index
  /// @param row_span Number of rows to span
  /// @param col_span Number of columns to span
  void add_item(std::shared_ptr<Widget> w, int row, int col, int row_span = 1,
                int col_span = 1) {
    Item item{w, row, col, row_span, col_span};
    grid_items_.push_back(item);
    // Also add to base children for event/render handling
    children_.push_back(w);
  }

  void layout() override {
    if (grid_items_.empty()) return;

    // 1. Determine Grid Dimensions needed
    int max_row = 0;
    int max_col = 0;
    for (auto &item : grid_items_) {
      if (item.row + item.row_span > max_row)
        max_row = item.row + item.row_span;
      if (item.col + item.col_span > max_col)
        max_col = item.col + item.col_span;
    }

    if (max_row == 0 || max_col == 0) return;

    // 2. Resolve Column Widths
    std::vector<int> final_col_widths;
    if (!col_widths.empty()) {
      final_col_widths = col_widths;
      while ((int)final_col_widths.size() < max_col)
        final_col_widths.push_back(1);
    } else {
      // Auto equal split
      int w = width / max_col;
      int rem = width - (w * max_col);
      for (int i = 0; i < max_col; ++i) {
        final_col_widths.push_back(w + (i < rem ? 1 : 0));
      }
    }

    // 3. Resolve Row Heights
    std::vector<int> final_row_heights;
    if (!row_heights.empty()) {
      final_row_heights = row_heights;
      while ((int)final_row_heights.size() < max_row)
        final_row_heights.push_back(1);
    } else {
      // Auto equal split
      int h = height / max_row;
      int rem = height - (h * max_row);
      for (int i = 0; i < max_row; ++i) {
        final_row_heights.push_back(h + (i < rem ? 1 : 0));
      }
    }

    // 4. Position Items
    for (auto &item : grid_items_) {
      item.widget->update_responsive();
      if (!item.widget->visible) continue;

      int px = x;
      for (int i = 0; i < item.col; ++i) px += final_col_widths[i];

      int py = y;
      for (int i = 0; i < item.row; ++i) py += final_row_heights[i];

      int pw = 0;
      for (int i = 0; i < item.col_span; ++i) {
        if (item.col + i < (int)final_col_widths.size())
          pw += final_col_widths[item.col + i];
      }

      int ph = 0;
      for (int i = 0; i < item.row_span; ++i) {
        if (item.row + i < (int)final_row_heights.size())
          ph += final_row_heights[item.row + i];
      }

      item.widget->x = px;
      item.widget->y = py;
      item.widget->width =
          clamp_size(pw, item.widget->min_width, item.widget->max_width);
      item.widget->height =
          clamp_size(ph, item.widget->min_height, item.widget->max_height);

      if (auto cont = std::dynamic_pointer_cast<Container>(item.widget)) {
        cont->layout();
      }
    }
  }

 private:
  std::vector<Item> grid_items_;
};

// --- Menu System ---

struct MenuItem {
  StyledText styled_label;
  std::string label;
  std::function<void()> action;
  std::vector<MenuItem> sub_items;

  MenuItem(StyledText l = "", std::function<void()> a = {},
           std::vector<MenuItem> s = {})
      : styled_label(l), label(l.plain_text()), action(a), sub_items(s) {}
};

/// @brief A general purpose popup dialog
class Dialog : public Border {
 public:
  bool is_open = false;
  bool modal = false;
  bool shadow = true;
  bool steal_focus = true;  // Default: Dialogs steal focus

  App *app_ = nullptr;

  Dialog(App *app, BorderStyle style = BorderStyle::Single,
         Color color = Color())
      : Border(style, color), app_(app) {
    // Dialogs are typically top-level, but rely on manual placement
  }

  virtual void show(int screen_x, int screen_y) {
    x = screen_x;
    y = screen_y;
    is_open = true;
    layout();  // Ensure children are positioned
  }

  virtual void hide() { is_open = false; }

  // Self-management helpers
  void open();
  void open(int screen_x, int screen_y);
  void close();

  void render(Buffer &buffer) override {
    if (!is_open) return;

    // Fill background before drawing border to support transparency
    Color bg_fill = bg_color_.resolve(Theme::current().panel_bg);
    for (int i = 0; i < height; ++i) {
      for (int j = 0; j < width; ++j) {
        Cell c;
        c.content = " ";
        c.bg_color = bg_fill;
        buffer.set(x + j, y + i, c);
      }
    }

    if (shadow) {
      render_shadow(buffer);
    }

    // Force Border to use Panel BG instead of Global BG
    // This ensures the border lines match the interior fill (panel_bg)
    Color original_bg = bg_color_;
    bool was_default = bg_color_.is_default;

    if (was_default) {
      bg_color_ = Theme::current().panel_bg;
    }

    Border::render(buffer);

    if (was_default) {
      bg_color_ = original_bg;
    }
  }

  bool on_event(const Event &event) override {
    if (!is_open) return false;
    // Delegate to children first
    if (Border::on_event(event)) return true;

    // If modal, we assume we want to capture interaction within bounds
    if (event.is_mouse_event()) {
      if (contains(event.x, event.y) && !event.mouse_wheel()) return true;
    }
    return false;
  }

 protected:
  void render_shadow(Buffer &buffer) {
    auto darken = [](Color &c) {
      if (c.is_default) {
        c.r = 0;
        c.g = 0;
        c.b = 0;
      }
      c.r = c.r / 2;
      c.g = c.g / 2;
      c.b = c.b / 2;
      if (c.r < 30 && c.g < 30 && c.b < 30) {
        c.r = 60;
        c.g = 60;
        c.b = 60;
      }
      c.is_default = false;
    };

    // Bottom Shadow Strip
    for (int j = 1; j < width; ++j) {
      int sx = x + j;
      int sy = y + height;
      if (sx < buffer.width() && sy < buffer.height()) {
        Cell c = buffer.get(sx, sy);
        Color shadow_col = c.bg_color;
        darken(shadow_col);
        c.content = "\u2580";
        c.fg_color = shadow_col;
        buffer.set(sx, sy, c);
      }
    }

    // Right Shadow Strip
    for (int i = 1; i < height; ++i) {
      int sx = x + width;
      int sy = y + i;
      if (sx < buffer.width() && sy < buffer.height()) {
        Cell c = buffer.get(sx, sy);
        Color shadow_col = c.bg_color;
        darken(shadow_col);
        c.content = "\u258C";
        c.fg_color = shadow_col;
        buffer.set(sx, sy, c);
      }
    }

    // Corner
    {
      int sx = x + width;
      int sy = y + height;
      if (sx < buffer.width() && sy < buffer.height()) {
        Cell c = buffer.get(sx, sy);
        Color shadow_col = c.bg_color;
        darken(shadow_col);
        c.content = "\u2598";
        c.fg_color = shadow_col;
        buffer.set(sx, sy, c);
      }
    }
  }
};

/// @brief A popup menu widget (formerly Menu)
class MenuDialog : public Dialog {
 public:
  std::vector<MenuItem> items;
  int selected_index = 0;

  // Interaction Settings
  bool mouse_hover_select = true;

  // Styling inherited from Dialog: title, shadow
  std::string selection_indicator = "> ";
  // Shadow is now managed by Dialog

  Color highlight_bg = Color();  // Default: selection
  Color highlight_fg = Color();  // Default: foreground
  Color normal_bg = Color();     // Default: panel_bg
  Color normal_fg = Color();     // Default: foreground
  Color title_color = Color();   // Default: border

  MenuDialog(App *app) : Dialog(app, BorderStyle::Single) { focusable = true; }

  void show(int screen_x, int screen_y) override {
    // Auto-size logic
    int max_len = 0;
    for (const auto &item : items) {
      int len = utf8_display_width(item.label) +
                utf8_display_width(selection_indicator);
      if (len > max_len) max_len = len;
    }

    // Allow for Title size as well
    int title_len = utf8_display_width(get_title());
    if (title_len > max_len) max_len = title_len;

    if (width < max_len + 4) width = max_len + 4;
    height = items.size() + 2;

    Dialog::show(screen_x, screen_y);  // Will call layout() with new size
  }

  // hide() inherited

  void render(Buffer &buffer) override {
    if (!is_open) return;

    // 1. Draw Shadow & Border via Base
    Dialog::render(buffer);

    Color bg_fill = normal_bg.resolve(Theme::current().panel_bg);
    // We can re-fill just the inside
    for (int i = 1; i < height - 1; ++i) {
      for (int j = 1; j < width - 1; ++j) {
        Cell c;
        c.content = " ";
        c.bg_color = bg_fill;
        buffer.set(x + j, y + i, c);
      }
    }

    // 5. Render Items

    // 5. Render Items
    for (size_t i = 0; i < items.size(); ++i) {
      int item_y = y + 1 + i;
      int item_x = x + 2;

      bool is_sel = (i == (size_t)selected_index);

      std::string prefix =
          is_sel ? selection_indicator
                 : std::string(utf8_display_width(selection_indicator), ' ');
      int content_width = width - 4;

      Color n_fg = normal_fg.resolve(Theme::current().foreground);
      Color n_bg = normal_bg.resolve(Theme::current().panel_bg);
      Color h_fg = highlight_fg.resolve(Theme::current().input_fg);
      Color h_bg = highlight_bg.resolve(Theme::current().selection);

      Color fg = is_sel ? h_fg : n_fg;
      Color bg = is_sel ? h_bg : n_bg;

      // Fill background
      for (int dx = 0; dx < content_width; ++dx) {
        Cell c;
        c.content = " ";
        c.bg_color = bg;
        buffer.set(item_x + dx, item_y, c);
      }

      // Render prefix
      auto prefix_chars = TextHelper::prepare_text_for_render(prefix);
      int current_dx = 0;
      for (const auto &ci : prefix_chars) {
        if (current_dx >= content_width) break;
        Cell c;
        c.content = ci.content;
        c.fg_color = fg;
        c.bg_color = bg;
        buffer.set(item_x + current_dx, item_y, c);
        current_dx += ci.display_width;
      }

      // Render label
      auto label_chars = TextHelper::prepare_text_for_render(items[i].label);
      for (size_t j = 0; j < label_chars.size(); ++j) {
        if (current_dx >= content_width) break;
        const auto &ci = label_chars[j];
        Cell c;
        c.content = ci.content;
        c.fg_color = fg;
        c.bg_color = bg;

        // Apply styling
        if (!items[i].styled_label.empty()) {
          if (const TextSpan *span =
                  items[i].styled_label.get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(item_x + current_dx, item_y, c);
        if (ci.display_width == 2 && current_dx + 1 < content_width) {
          Cell skip;
          skip.content = "";
          skip.bg_color = bg;
          buffer.set(item_x + current_dx + 1, item_y, skip);
        }
        current_dx += ci.display_width;
      }
    }
  }

  bool on_event(const Event &event) override;
};

/// @brief A horizontal Top-level Menu Bar
class MenuBar : public Widget {
 public:
  std::vector<MenuItem> items;
  Color bg_color = Color();
  Color text_color = Color();
  Color hover_bg = Color();

  Color hover_fg = Color();
  std::string selection_indicator = "> ";

  int selected_index = -1;  // -1 none

  App *app_ = nullptr;

  MenuBar(App *app = nullptr) : app_(app) {
    height = 1;
    fixed_height = 1;
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().menubar_bg);
    Color fg = text_color.resolve(Theme::current().menubar_fg);
    Color h_bg = hover_bg.resolve(Theme::current().selection);
    Color h_fg = hover_fg.resolve(Theme::current().input_fg);

    // Fill Bar
    for (int i = 0; i < width; ++i) {
      Cell c;
      c.bg_color = bg;
      c.fg_color = fg;
      c.content = " ";
      buffer.set(x + i, y, c);
    }

    int current_x = x + 1;
    for (size_t i = 0; i < items.size(); ++i) {
      Color c_bg = (i == (size_t)selected_index) ? h_bg : bg;
      Color c_fg = (i == (size_t)selected_index) ? h_fg : fg;

      // Render with padding
      if (current_x < x + width) {
        Cell c;
        c.content = " ";
        c.bg_color = c_bg;
        c.fg_color = c_fg;
        buffer.set(current_x++, y, c);
      }

      auto label_chars = TextHelper::prepare_text_for_render(items[i].label);
      for (size_t j = 0; j < label_chars.size(); ++j) {
        if (current_x >= x + width) break;
        const auto &ci = label_chars[j];
        Cell c;
        c.content = ci.content;
        c.fg_color = c_fg;
        c.bg_color = c_bg;

        // Apply styling
        if (!items[i].styled_label.empty()) {
          if (const TextSpan *span =
                  items[i].styled_label.get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(current_x, y, c);
        if (ci.display_width == 2 && current_x + 1 < x + width) {
          Cell skip;
          skip.content = "";
          skip.bg_color = c_bg;
          buffer.set(current_x + 1, y, skip);
        }
        current_x += ci.display_width;
      }

      if (current_x < x + width) {
        Cell c;
        c.content = " ";
        c.bg_color = c_bg;
        c.fg_color = c_fg;
        buffer.set(current_x++, y, c);
      }
    }
  }

  bool on_event(const Event &event) override;
};

/// @brief Visualizes progress as a bar
class ProgressBar : public Widget {
 public:
  float value = 0.0f;     // 0.0 to 1.0
  Color color = Color();  // Default: primary/success

  // Styling Options
  int max_height = 0;
  std::string char_filled = "█";
  std::string char_empty = "░";
  Color empty_color = Color();  // Default: Theme::secondary

  // Text Options
  bool show_text = false;
  Color text_color = Color();
  Alignment text_alignment = Alignment::Center;
  std::function<std::string(float)> text_formatter;

  ProgressBar(float val = 0.0f) : value(val) {
    height = 1;
    fixed_height = 1;
  }

  void render(Buffer &buffer) override {
    int filled_width = static_cast<int>(width * value);
    if (filled_width < 0) filled_width = 0;
    if (filled_width > width) filled_width = width;

    Color c_col = color.resolve(Theme::current().primary);
    Color e_col = empty_color.resolve(Theme::current().secondary);
    Color t_col = text_color.resolve(Theme::current().panel_fg);

    int render_height = height;
    if (max_height > 0 && render_height > max_height) {
      render_height = max_height;
    }

    // Prepare text if enabled
    std::string text_str;
    int text_start_x = -1;

    if (show_text) {
      if (text_formatter) {
        text_str = text_formatter(value);
      } else {
        text_str = std::to_string(static_cast<int>(value * 100)) + "%";
      }

      int t_len = static_cast<int>(text_str.length());
      if (t_len > width) {
        text_str = text_str.substr(0, width);
        t_len = width;
      }

      if (text_alignment == Alignment::Left) {
        text_start_x = 0;
      } else if (text_alignment == Alignment::Right) {
        text_start_x = width - t_len;
      } else {  // Center
        text_start_x = (width - t_len) / 2;
      }
    }

    int text_y = render_height / 2;

    for (int dy = 0; dy < render_height; ++dy) {
      for (int dx = 0; dx < width; ++dx) {
        // Check if we are drawing a text character at this position on this row
        bool is_text = false;
        char text_char = ' ';

        if (show_text && dy == text_y && dx >= text_start_x &&
            dx < text_start_x + static_cast<int>(text_str.length())) {
          is_text = true;
          text_char = text_str[dx - text_start_x];
        }

        Cell c;
        if (dx < filled_width) {
          // Filled part
          if (is_text) {
            c.content = std::string(1, text_char);
            c.fg_color = t_col;
            c.bg_color = c_col;  // Use filled color as background for text
          } else {
            c.content = char_filled;
            c.fg_color = c_col;
          }
        } else {
          // Empty part
          if (is_text) {
            c.content = std::string(1, text_char);
            c.fg_color = t_col;
            c.bg_color = e_col;  // Use empty color as background for text
          } else {
            c.content = char_empty;
            c.fg_color = e_col;
          }
        }

        buffer.set(x + dx, y + dy, c);
      }
    }
  }
};

/// @brief A checkbox widget
class Checkbox : public Widget {
 public:
  Checkbox(StyledText label, bool checked = false)
      : styled_label_(label), label_(label.plain_text()), checked_(checked) {
    height = 1;
    fixed_height = 1;
    focusable = true;
  }

  // Configuration - defaults to Theme
  Color text_color = {0, 0, 0, true};
  Color focus_color = {0, 0, 0, true};
  Color hover_color = {0, 0, 0, true};

  std::string checked_prefix = "[x] ";
  std::string unchecked_prefix = "[ ] ";

  void render(Buffer &buffer) override {
    std::string prefix = checked_ ? checked_prefix : unchecked_prefix;
    int prefix_chars = (int)TextHelper::prepare_text_for_render(prefix).size();

    std::string full_text = prefix + label_;
    std::vector<CharInfo> chars =
        TextHelper::prepare_text_for_render(full_text);

    Color fg = text_color.resolve(Theme::current().foreground);
    Color focus = focus_color.resolve(Theme::current().primary);
    Color hover = hover_color.resolve(Theme::current().hover);
    Color bg = Theme::current().background;

    int cell_x = 0;
    for (size_t i = 0; i < chars.size() && cell_x < width; ++i) {
      Cell cell;
      cell.content = chars[i].content;
      cell.bg_color = bg;

      if (has_focus())
        cell.fg_color = focus;
      else if (hovered_)
        cell.fg_color = hover;
      else
        cell.fg_color = fg;

      // Apply styling to the label part
      if ((int)i >= prefix_chars && !styled_label_.empty()) {
        if (const TextSpan *span =
                styled_label_.get_span_at_char((int)i - prefix_chars)) {
          if (!span->color.is_default) cell.fg_color = span->color;
          cell.bold = span->bold;
          cell.italic = span->italic;
          cell.underline = span->underline;
        }
      }

      buffer.set(x + cell_x, y, cell);
      if (chars[i].display_width == 2 && cell_x + 1 < width) {
        Cell skip;
        skip.content = "";
        skip.bg_color = bg;
        buffer.set(x + cell_x + 1, y, skip);
      }
      cell_x += chars[i].display_width;
    }
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      bool hit = (event.x >= x && event.x < x + width && event.y >= y &&
                  event.y < y + height);
      if (hit != hovered_ && !event.mouse_wheel()) {
        set_hovered(hit);
        return true;
      }
      if (hit) {
        if (event.mouse_left()) {
          checked_ = !checked_;
          if (on_change) on_change(checked_);
          return true;
        }
        return !event.mouse_wheel();
      }
    } else if (event.is_key_event()) {
      if (has_focus()) {
        if (event.is_activate()) {
          checked_ = !checked_;
          if (on_change) on_change(checked_);
          return true;
        }
      }
    }
    return false;
  }

  bool is_checked() const { return checked_; }
  void set_checked(bool c) { checked_ = c; }

 public:
  std::function<void(bool)> on_change;

 private:
  StyledText styled_label_;
  std::string label_;
  bool checked_;
};

/// @brief Displays a simple line graph of data values
class Sparkline : public Widget {
 public:
  std::vector<float> data;
  Color color = {0, 255, 255, true};

  // Sparkline Enhancements
  bool auto_scale = false;
  float min_val = 0.0f;
  float max_val = 1.0f;

  std::vector<std::pair<float, Color>> color_thresholds;

  bool show_label = false;
  std::string label_format = "%.1f";
  Color label_color = Color();

  void render(Buffer &buffer) override {
    if (data.empty()) return;

    // 1. Auto-scaling calculation
    float actual_min = min_val;
    float actual_max = max_val;
    if (auto_scale) {
      actual_min = std::numeric_limits<float>::max();
      actual_max = std::numeric_limits<float>::lowest();
      for (float v : data) {
        if (v < actual_min) actual_min = v;
        if (v > actual_max) actual_max = v;
      }
      if (actual_min == actual_max) {
        actual_min -= 1.0f;
        actual_max += 1.0f;
      }
    }

    // 2. Handle label reservation
    std::string val_label = "";
    int label_w = 0;
    if (show_label) {
      char buf[64];
      snprintf(buf, sizeof(buf), label_format.c_str(), data.back());
      val_label = " " + std::string(buf);
      label_w = val_label.size();
    }

    int plot_width = width - label_w;
    if (plot_width <= 0) plot_width = width;  // Fallback if too small

    const std::string blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int start_idx = std::max(0, (int)data.size() - plot_width);

    // 3. Multi-line rendering algorithm
    for (int dx = 0; dx < plot_width; ++dx) {
      int data_idx = start_idx + dx;
      if (data_idx >= (int)data.size()) break;

      float raw_val = data[data_idx];
      float val = (actual_max == actual_min)
                      ? 0.0f
                      : (raw_val - actual_min) / (actual_max - actual_min);
      if (val < 0.0f) val = 0.0f;
      if (val > 1.0f) val = 1.0f;

      // Select color based on threshold matching (using raw_val)
      Color c_col = color.resolve(Theme::current().primary);
      for (const auto &thresh : color_thresholds) {
        if (raw_val >= thresh.first) {
          c_col = thresh.second;
        }
      }

      int total_levels = height * 8;
      int filled_levels = static_cast<int>(val * total_levels);

      for (int row = 0; row < height; ++row) {
        int cell_y =
            y + height - 1 - row;  // row = 0 is bottom, height-1 is top
        int level = filled_levels - row * 8;

        Cell c;
        c.fg_color = c_col;
        c.bg_color = bg_color.resolve(Theme::current().background);

        if (level >= 8) {
          c.content = "█";
          buffer.set(x + dx, cell_y, c);
        } else if (level <= 0) {
          c.content = " ";
          buffer.set(x + dx, cell_y, c);
        } else {
          c.content = blocks[level];
          buffer.set(x + dx, cell_y, c);
        }
      }
    }

    // 4. Render Numeric Label (aligned at the right edge)
    if (show_label && label_w > 0) {
      Color lbl_col = label_color.resolve(Theme::current().foreground);
      int label_x = x + plot_width;
      int label_y = y + height - 1;  // bottom row
      for (int i = 0; i < label_w && label_x + i < x + width; ++i) {
        Cell c;
        c.content = std::string(1, val_label[i]);
        c.fg_color = lbl_col;
        c.bg_color = bg_color.resolve(Theme::current().background);
        buffer.set(label_x + i, label_y, c);
      }
    }
  }
};

/// @brief Helper class for high-resolution Braille character rendering in
/// charts
class BrailleCanvas {
 public:
  int width, height;
  std::vector<uint8_t> grid;
  // Dot mask per cell. Virtual size: 2w x 4h

  BrailleCanvas(int w, int h) : width(w), height(h) {
    grid.assign(width * height, 0);
  }

  // Set dot at virtual coordinates (0,0 is top-left)
  void set_dot(int vx, int vy) {
    int cx = vx / 2;
    int cy = vy / 4;

    if (cx < 0 || cx >= width || cy < 0 || cy >= height) return;

    // Braille Unicode Pattern:
    // 1 4
    // 2 5
    // 3 6
    // 7 8
    //
    // Bitmask:
    // 0x01 (1)   0x08 (4)
    // 0x02 (2)   0x10 (5)
    // 0x04 (3)   0x20 (6)
    // 0x40 (7)   0x80 (8)

    int dx = vx % 2;
    int dy = vy % 4;

    uint8_t bit = 0;
    if (dx == 0) {
      if (dy == 0)
        bit = 0x01;  // 1
      else if (dy == 1)
        bit = 0x02;  // 2
      else if (dy == 2)
        bit = 0x04;  // 3
      else if (dy == 3)
        bit = 0x40;  // 7
    } else {
      if (dy == 0)
        bit = 0x08;  // 4
      else if (dy == 1)
        bit = 0x10;  // 5
      else if (dy == 2)
        bit = 0x20;  // 6
      else if (dy == 3)
        bit = 0x80;  // 8
    }

    grid[cy * width + cx] |= bit;
  }

  // Draw a line using subpixel virtual coordinates (Bresenham's algorithm)
  void draw_line(int x0, int y0, int x1, int y1,
                 std::function<void(int, int)> set_pixel = nullptr) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (true) {
      if (set_pixel) {
        set_pixel(x0, y0);
      } else {
        set_dot(x0, y0);
      }
      if (x0 == x1 && y0 == y1) break;
      e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  // Unicode Braille Pattern Start: U+2800
  // UTF-8 encoding for U+28xx:
  // 0xE2 0xA0 0x80 + mask
  std::string get_char(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return " ";
    uint8_t mask = grid[y * width + x];
    if (mask == 0) return "";  // Empty

    // Construct UTF-8 directly
    // U+2800 = 0010 1000 0000 0000
    // + mask (8 bits)
    // Total: 0010 1000 m7 m6 ... m0

    // UTF-8 template: 1110xxxx 10xxxxxx 10xxxxxx
    // Byte 1: 11100010 (0xE2)
    // Byte 2: 101000xx -> 101000(m7 m6) -> 0xA0 | ((mask >> 6) & 0x03)
    // Byte 3: 10xxxxxx -> 10(m5 m4 m3 m2 m1 m0) -> 0x80 | (mask & 0x3F)

    std::string s(3, ' ');
    s[0] = (char)0xE2;
    s[1] = (char)(0xA0 | ((mask >> 6) & 0x03));
    s[2] = (char)(0x80 | (mask & 0x3F));
    return s;
  }
};

/// @brief Base class for chart widgets with shared axis and tooltip
/// functionality
class Tooltip : public Widget {
 public:
  StyledText styled_text;
  std::string text;
  std::shared_ptr<Widget> target;
  int delay_ms = 500;

  enum class Position {
    Top,
    Bottom,
    Left,
    Right,
    Manual  // For charts etc.
  };
  Position position = Position::Top;

  int manual_x = 0;
  int manual_y = 0;

  bool visible_ = false;
  Rect last_bounds;
  SelectionState selection_state_;

  Tooltip(const StyledText &t = StyledText(""))
      : styled_text(t), text(t.plain_text()) {}

  void attach(std::shared_ptr<Widget> w) { target = w; }

  void show() { visible_ = true; }
  void hide() { visible_ = false; }

  bool contains(int px, int py) const override {
    if (!visible_) return false;
    return last_bounds.contains(px, py);
  }

  bool on_event(const Event &event) override {
    if (!visible_) return false;

    if (event.is_mouse_event()) {
      // Strict bounds check from known render position
      bool hit = contains(event.x, event.y);

      // Handle drags outside if we started inside
      if (selection_state_.mouse_down && event.mouse_drag()) {
        int rel_x = event.x - (last_bounds.x + 2);  // +2 for border + padding
        if (event.y != last_bounds.y + 1)
          rel_x = -1000;  // Only select on text line

        auto chars = prepare_text_for_render(text);
        int char_idx = 0;
        int total_w = utf8_display_width(text);

        if (rel_x >= total_w)
          char_idx = (int)chars.size();
        else if (rel_x < 0)
          char_idx = 0;
        else
          char_idx = TextHelper::visual_to_char_pos(chars, rel_x);

        selection_state_.handle_mouse_drag(char_idx);
        return true;
      }

      if (hit) {
        if (event.mouse_left()) {
          // Calculate hit relative to text start
          // Text starts at tip_x + 2, tip_y + 1
          int text_start_x = last_bounds.x + 2;
          int text_start_y = last_bounds.y + 1;

          if (event.y == text_start_y) {
            int rel_x = event.x - text_start_x;
            auto chars = prepare_text_for_render(text);
            int char_idx = TextHelper::visual_to_char_pos(chars, rel_x);
            selection_state_.handle_mouse_press(chars, char_idx);
            return true;
          } else {
            // Clicked on border/padding, clear selection
            selection_state_.clear();
            return true;
          }
        }
      }

      if (event.mouse_release()) {
        if (selection_state_.handle_mouse_release()) return true;
      }
    } else if (event.is_key_event()) {
      if (event.is_copy() && selection_state_.has_selection()) {
        std::string sel_text = TextHelper::utf8_substr(
            text, selection_state_.start,
            selection_state_.end - selection_state_.start);
        copy_to_clipboard(sel_text);
        return true;
      }
    }

    return false;
  }

  void render(Buffer &buffer) override {
    if (!visible_ || text.empty() || !target) return;

    // Bypass parent clipping - tooltips render at screen-absolute coordinates
    buffer.push_full_clip();

    Color bg = Theme::current().panel_bg;
    Color fg = Theme::current().foreground;
    Color border = Theme::current().border;
    Color sel_bg = Theme::current().selection;
    Color sel_fg = Color::White();

    int tip_w = utf8_display_width(text) + 4;
    int tip_h = 3;
    int tip_x, tip_y;

    switch (position) {
      case Position::Top:
        tip_x = target->x + (target->width - tip_w) / 2;
        tip_y = target->y - tip_h;
        break;
      case Position::Bottom:
        tip_x = target->x + (target->width - tip_w) / 2;
        tip_y = target->y + target->height;
        break;
      case Position::Left:
        tip_x = target->x - tip_w;
        tip_y = target->y;
        break;
      case Position::Right:
        tip_x = target->x + target->width;
        tip_y = target->y;
        break;
      case Position::Manual:
        tip_x = manual_x;
        tip_y = manual_y;
        break;
    }

    if (tip_x < 0) tip_x = 0;
    if (tip_y < 0) tip_y = 0;

    // Store bounds for hit testing
    last_bounds.x = tip_x;
    last_bounds.y = tip_y;
    last_bounds.width = tip_w;
    last_bounds.height = tip_h;

    // Draw border
    for (int j = 0; j < tip_h; ++j) {
      for (int i = 0; i < tip_w; ++i) {
        Cell c;
        if (j == 0 && i == 0)
          c.content = "┌";
        else if (j == 0 && i == tip_w - 1)
          c.content = "┐";
        else if (j == tip_h - 1 && i == 0)
          c.content = "└";
        else if (j == tip_h - 1 && i == tip_w - 1)
          c.content = "┘";
        else if (j == 0 || j == tip_h - 1)
          c.content = "─";
        else if (i == 0 || i == tip_w - 1)
          c.content = "│";
        else
          c.content = " ";
        c.fg_color = border;
        c.bg_color = bg;
        buffer.set(tip_x + i, tip_y + j, c);
      }
    }

    // Draw text - UTF-8 safe
    size_t pos = 0;
    int cell_x = 0;
    int char_idx = 0;
    while (pos < text.size() && cell_x < (int)text.size()) {
      uint32_t codepoint;
      int byte_len;
      if (utf8_decode_codepoint(text, pos, codepoint, byte_len)) {
        bool selected = selection_state_.is_selected(char_idx);

        Cell c;
        c.content = text.substr(pos, byte_len);
        c.fg_color = selected ? sel_fg : fg;
        c.bg_color = selected ? sel_bg : bg;

        // Apply styling from styled_text
        if (!selected && !styled_text.empty()) {
          if (const TextSpan *span = styled_text.get_span_at_char(char_idx)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(tip_x + 2 + cell_x, tip_y + 1, c);
        int dw = char_display_width(codepoint);
        if (dw == 2) {
          Cell skip;
          skip.content = "";
          skip.bg_color = c.bg_color;
          buffer.set(tip_x + 2 + cell_x + 1, tip_y + 1, skip);
        }
        cell_x += (dw > 0 ? dw : 1);
        pos += byte_len;
        char_idx++;
      } else {
        pos++;
      }
    }

    // Restore clipping context
    buffer.pop_clip();
  }
};

// Implement Widget tooltip methods now that Tooltip is defined
inline void Widget::set_tooltip(std::shared_ptr<Tooltip> t) { tooltip_ = t; }

inline void Widget::set_tooltip(const std::string &text) {
  // Create a default tooltip
  auto t = std::make_shared<Tooltip>(text);
  t->position = Tooltip::Position::Top;
  tooltip_ = t;
}

class ChartBase : public Widget {
 public:
  // Axis display settings
  bool show_y_tick_labels = false;
  bool show_x_tick_labels = false;
  bool show_y_axis = false;
  bool show_x_axis = false;
  bool show_y_ticks = false;
  bool show_x_ticks = false;

  Color axis_color = Color();
  Color label_color = Color();
  Color grid_color = Color();
  Color zero_axis_color = Color();

  // Tick customization
  int x_tick_count = 10;
  int y_tick_count = 5;
  int x_tick_precision = 1;
  int y_tick_precision = 1;
  std::function<std::string(double)> x_tick_formatter = nullptr;
  std::function<std::string(double)> y_tick_formatter = nullptr;

  // Tooltip settings
  bool show_tooltip = false;
  int tooltip_duration_ms = 1000;

  bool show_legend = false;  ///< Show interactive series legend
  bool show_grid_lines =
      false;                ///< Show background grid lines aligned with ticks
  bool auto_scale = false;  ///< Dynamically scale axis range to fit data series
  std::chrono::steady_clock::time_point
      last_hit_time_;  ///< Last time hover hit occurred

  /// @brief Bounding box tracking for mouse click hits on legend text
  struct LegendHit {
    int x, y;          ///< Coordinate position
    int width;         ///< Bounding width
    int series_index;  ///< Associated series index
  };
  mutable std::vector<LegendHit>
      legend_hits_;  ///< Screen locations of drawn legend items
  int hovered_series_idx_ =
      -1;  ///< Currently hovered legend item index (-1 if none)
  std::unordered_map<std::string, bool>
      series_visibility_;  ///< Persistent series visibility overrides

 protected:
  /// @brief Format a tick label value
  std::string format_tick_label(double val, bool is_x_axis) const {
    if (is_x_axis && x_tick_formatter) return x_tick_formatter(val);
    if (!is_x_axis && y_tick_formatter) return y_tick_formatter(val);

    std::stringstream ss;
    ss.precision(is_x_axis ? x_tick_precision : y_tick_precision);
    ss << std::fixed << val;
    return ss.str();
  }
};

/// @brief A multi-series line chart
class LineChart : public ChartBase {
 public:
  /// @brief Rendering style for the line series
  enum class LineStyle {
    Points,  ///< Draw points only
    Lines,   ///< Draw lines connecting points
    Braille  ///< Use Braille patterns for higher resolution
  };

  /// @brief Data series definition
  struct Series {
    std::vector<double> data;  ///< Data points
    std::string label;         ///< Series label for legend
    Color color;               ///< Drawing color
    std::string marker = "*";  ///< Marker character (for Points style)
    LineStyle style = LineStyle::Lines;  ///< Render style
    bool fill_gaps = true;  ///< Interpolate missing x-values if continuous
    bool fill = false;  ///< Shade/fill area beneath the curve down to baseline
    std::string fill_char = "░";  ///< Filling character used in Lines style
    Color fill_color =
        Color();          ///< Custom filling color (default is series color)
    bool visible = true;  ///< Toggle series visibility in chart and legend
  };

  std::vector<Series> series;
  double min_val = 0.0;
  double max_val = 1.0;

  void add_series(const std::vector<double> &d, const std::string &l,
                  cpptui::Color c, LineStyle s = LineStyle::Lines,
                  std::string m = "*") {
    Series new_series;
    new_series.data = d;
    new_series.label = l;
    new_series.color = c;
    new_series.style = s;
    new_series.marker = m;
    auto it = series_visibility_.find(l);
    if (it != series_visibility_.end()) {
      new_series.visible = it->second;
    } else {
      series_visibility_[l] = new_series.visible;
    }
    series.push_back(new_series);
  }

  // LineChart-specific tick label options
  bool label_all_x_ticks = false;
  bool label_all_y_ticks = false;

  // Tooltip Support (chart-specific PointHit type)
  struct PointHit {
    int x, y;
    int series_index;
    int data_index;
    double value;
  };

  mutable std::vector<PointHit> point_hits_;

  // Custom formatter: Series, Index, Value -> String
  std::function<std::string(const Series &, int, double)> tooltip_formatter;

  bool on_event(const Event &event) override {
    bool requested_update = false;

    if (event.is_mouse_event()) {
      if (contains(event.x, event.y)) {
        // 1. Check Legend Hits
        int prev_hovered = hovered_series_idx_;
        hovered_series_idx_ = -1;  // Reset hover

        for (const auto &hit : legend_hits_) {
          if (event.x >= hit.x && event.x < hit.x + hit.width &&
              event.y == hit.y) {
            hovered_series_idx_ = hit.series_index;
            break;
          }
        }

        if (hovered_series_idx_ != prev_hovered) {
          requested_update = true;
        }

        // Handle Click (toggling)
        if (event.mouse_left() && !event.mouse_motion()) {
          for (const auto &hit : legend_hits_) {
            if (event.x >= hit.x && event.x < hit.x + hit.width &&
                event.y == hit.y) {
              series[hit.series_index].visible =
                  !series[hit.series_index].visible;
              series_visibility_[series[hit.series_index].label] =
                  series[hit.series_index].visible;
              requested_update = true;
              return true;  // Click handled
            }
          }
        }
      } else {
        // If mouse moved outside widget, clear hover highlight
        if (hovered_series_idx_ != -1) {
          hovered_series_idx_ = -1;
          requested_update = true;
        }
      }
    }

    // 2. Handle Tooltips if enabled
    if (show_tooltip && event.is_mouse_event() && !event.mouse_wheel()) {
      bool hit_found = false;
      if (contains(event.x, event.y) && hovered_series_idx_ == -1) {
        for (const auto &hit : point_hits_) {
          if (std::abs(event.x - hit.x) <= 1 &&
              std::abs(event.y - hit.y) <= 0) {
            std::string text;
            if (tooltip_formatter) {
              text = tooltip_formatter(series[hit.series_index], hit.data_index,
                                       hit.value);
            } else {
              std::stringstream ss;
              ss << std::fixed << std::setprecision(2) << hit.value;
              text = series[hit.series_index].label + ": " + ss.str();
            }

            if (!tooltip_) tooltip_ = std::make_shared<Tooltip>();
            tooltip_->text = text;
            tooltip_->position = Tooltip::Position::Manual;
            tooltip_->manual_x = event.x;
            tooltip_->manual_y = event.y - 1;
            tooltip_->visible = true;
            hit_found = true;
            last_hit_time_ = std::chrono::steady_clock::now();
            return true;
          }
        }
      }

      if (hovered_series_idx_ != -1 && tooltip_) {
        tooltip_ = nullptr;
        requested_update = true;
      } else if (!hit_found && tooltip_) {
        if (tooltip_->contains(event.x, event.y)) {
          last_hit_time_ = std::chrono::steady_clock::now();
        } else {
          auto now = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - last_hit_time_)
                             .count();
          if (elapsed >= tooltip_duration_ms) {
            tooltip_ = nullptr;
            requested_update = true;
          }
        }
      }
    }

    return requested_update;
  }

  void render(Buffer &buffer) override {
    for (const auto &s : series) {
      series_visibility_[s.label] = s.visible;
    }
    if (show_tooltip) point_hits_.clear();  // Reset hits for this frame

    if (auto_scale && !series.empty()) {
      double calculated_min = std::numeric_limits<double>::max();
      double calculated_max = std::numeric_limits<double>::lowest();
      bool has_data = false;
      for (const auto &s : series) {
        for (double v : s.data) {
          if (v < calculated_min) calculated_min = v;
          if (v > calculated_max) calculated_max = v;
          has_data = true;
        }
      }
      if (has_data) {
        if (calculated_min == calculated_max) {
          min_val = calculated_min - 1.0;
          max_val = calculated_max + 1.0;
        } else {
          double range = calculated_max - calculated_min;
          min_val = calculated_min - range * 0.05;
          max_val = calculated_max + range * 0.05;
        }
      }
    }

    Color bg = bg_color.resolve(Theme::current().background);
    if (series.empty()) return;

    int draw_x = x;
    int draw_y = y;
    int draw_width = width;
    int draw_height = height;

    Color axis = axis_color.resolve(Theme::current().border);
    Color lbl = label_color.resolve(Theme::current().foreground);
    Color grid_col = grid_color.resolve(Theme::current().border);
    Color zero_axis_col = zero_axis_color.resolve(Theme::current().foreground);

    bool has_x_labels = show_x_tick_labels && height > 2;
    bool has_x_axis = show_x_axis;

    int bottom_reserved = 0;
    if (has_x_labels)
      bottom_reserved = 1;
    else if (has_x_axis)
      bottom_reserved = 1;

    if (show_x_axis && show_x_tick_labels && height > 4) bottom_reserved = 2;

    int plot_height = height - bottom_reserved;

    // 1. Calculate Offsets and Draw Y Elements
    int y_label_width = 6;
    bool has_y_labels = show_y_tick_labels && draw_width > y_label_width + 2;
    bool has_y_axis = show_y_axis;

    int left_offset = 0;
    if (has_y_labels)
      left_offset = y_label_width;
    else if (has_y_axis)
      left_offset = 1;

    // Helper for formatting Y tick labels
    auto format_y_label = [&](double val) -> std::string {
      if (y_tick_formatter) return y_tick_formatter(val);
      std::stringstream ss;
      ss.precision(y_tick_precision);
      ss << std::fixed << val;
      return ss.str();
    };

    // Draw Y Labels
    if (has_y_labels) {
      int line_height = plot_height;

      if (label_all_y_ticks && y_tick_count > 1 && line_height > 2) {
        // Label all tick positions
        for (int t = 0; t < y_tick_count; ++t) {
          int tick_y = y + (int)std::round((double)(line_height - 1) * t /
                                           (y_tick_count - 1));
          // Calculate value at this tick position (inverted: top is max, bottom
          // is min)
          double val = max_val - (max_val - min_val) * t / (y_tick_count - 1);
          std::string s = format_y_label(val);
          for (int i = 0; i < std::min((int)s.size(), y_label_width); ++i) {
            Cell c;
            c.bg_color = bg;
            c.content = std::string(1, s[i]);
            c.fg_color = lbl;
            buffer.set(x + i, tick_y, c);
          }
        }
      } else {
        // Just endpoints (original behavior)
        // Max
        std::string s_max = format_y_label(max_val);
        for (int i = 0; i < std::min((int)s_max.size(), y_label_width); ++i) {
          Cell c;
          c.bg_color = bg;
          c.content = std::string(1, s_max[i]);
          c.fg_color = lbl;
          buffer.set(x + i, y, c);
        }

        // Min
        std::string s_min = format_y_label(min_val);
        int label_bottom_y = y + plot_height - 1;
        if (label_bottom_y < y) label_bottom_y = y;

        for (int i = 0; i < std::min((int)s_min.size(), y_label_width); ++i) {
          Cell c;
          c.bg_color = bg;
          c.content = std::string(1, s_min[i]);
          c.fg_color = lbl;
          buffer.set(x + i, label_bottom_y, c);
        }
      }
    }

    // Draw Y Axis Line & Ticks
    if (has_y_axis || has_y_labels) {
      int line_x = x + left_offset - 1;
      if (!has_y_labels && !has_y_axis) {
      }
      if (has_y_axis && !has_y_labels) line_x = x;

      if (show_y_axis) {
        int line_height = plot_height;

        for (int i = 0; i < line_height; ++i) {
          Cell c;
          c.bg_color = bg;
          c.content = "│";
          c.fg_color = axis;
          buffer.set(line_x, y + i, c);
        }

        // Y Ticks & Grid Lines
        if (y_tick_count > 1) {
          for (int i = 0; i < y_tick_count; ++i) {
            int tick_y = y + (int)std::round((double)(line_height - 1) * i /
                                             (y_tick_count - 1));
            if (show_y_ticks) {
              Cell c;
              c.bg_color = bg;
              c.content = "┼";
              c.fg_color = axis;
              buffer.set(line_x, tick_y, c);
            }
            if (show_grid_lines) {
              int start_gx = x + left_offset;
              int end_gx = x + width;
              for (int gx = start_gx; gx < end_gx; ++gx) {
                Cell gc;
                gc.bg_color = bg;
                gc.content = "─";
                gc.fg_color = grid_col;
                buffer.set(gx, tick_y, gc);
              }
            }
          }
        }
      }
    }

    draw_x += left_offset;
    draw_width -= left_offset;

    // 2. Calculate Offsets and Draw X Elements
    int bottom_y = y + height - 1;
    int axis_y = y + plot_height - 1;

    // Draw X Axis Line & Ticks
    if (show_x_axis) {
      // Determine Axis Y position
      int axis_screen_y = axis_y;
      bool is_zero_axis = false;
      if (min_val <= 0 && max_val >= 0 && max_val != min_val) {
        double zero_norm = (0.0 - min_val) / (max_val - min_val);
        int row_from_bottom = (int)std::round(zero_norm * (plot_height - 1));
        axis_screen_y = (y + plot_height - 1) - row_from_bottom;
        is_zero_axis = true;
      }

      for (int i = 0; i < draw_width; ++i) {
        Cell c;
        c.bg_color = bg;
        c.content = "─";
        c.fg_color = is_zero_axis ? zero_axis_col : axis;
        buffer.set(draw_x + i, axis_screen_y, c);
      }

      // X Ticks & Grid Lines
      if (x_tick_count > 1) {
        for (int i = 0; i < x_tick_count; ++i) {
          int tick_x = draw_x + (draw_width - 1) * i / (x_tick_count - 1);
          if (show_x_ticks) {
            Cell c;
            c.bg_color = bg;
            c.content = "┼";
            c.fg_color = is_zero_axis ? zero_axis_col : axis;
            buffer.set(tick_x, axis_screen_y, c);
          }
          if (show_grid_lines) {
            for (int gy = 0; gy < plot_height; ++gy) {
              int screen_gy = y + gy;
              if (show_x_axis && screen_gy == axis_screen_y) continue;
              if (show_y_axis && tick_x == (x + left_offset - 1)) continue;

              const Cell &existing = buffer.get(tick_x, screen_gy);
              Cell gc;
              gc.bg_color = bg;
              gc.fg_color = grid_col;
              if (existing.content == "─") {
                gc.content = "┼";
              } else {
                gc.content = "│";
              }
              buffer.set(tick_x, screen_gy, gc);
            }
          }
        }
      }

      // Corner Connector
      if (show_y_axis) {
        Cell c;
        c.fg_color = axis;
        c.bg_color = bg;
        int line_x = x + left_offset - 1;
        if (!has_y_labels && has_y_axis) line_x = x;

        if (axis_screen_y == bottom_y || axis_screen_y == axis_y) {
          c.content = "└";
        } else {
          c.content = "├";
        }

        buffer.set(line_x, axis_screen_y, c);
      }

      // Draw vertical mathematical Y-axis line at index 0
      if (show_y_axis) {
        int zero_x_screen = draw_x;
        for (int gy = 0; gy < plot_height; ++gy) {
          int screen_gy = y + gy;
          if (show_x_axis && screen_gy == axis_screen_y) continue;

          Cell c;
          c.bg_color = bg;
          c.content = "│";
          c.fg_color = zero_axis_col;
          buffer.set(zero_x_screen, screen_gy, c);
        }

        if (show_x_axis && axis_screen_y >= y &&
            axis_screen_y < y + plot_height) {
          Cell c;
          c.bg_color = bg;
          c.content = "┼";
          c.fg_color = zero_axis_col;
          buffer.set(zero_x_screen, axis_screen_y, c);
        }
      }
    }

    // Draw X Labels
    if (show_x_tick_labels) {
      int label_y = bottom_y;

      // Helper for formatting X tick labels
      auto format_x_label = [&](double val) -> std::string {
        if (x_tick_formatter) return x_tick_formatter(val);
        std::stringstream ss;
        ss.precision(x_tick_precision);
        ss << std::fixed << val;
        return ss.str();
      };

      // Get the max data size for X range
      size_t max_data_size = 0;
      for (const auto &s : series)
        if (s.data.size() > max_data_size) max_data_size = s.data.size();
      double x_min_val = 0;
      double x_max_val = (double)(max_data_size > 0 ? max_data_size - 1 : 0);

      if (label_all_x_ticks && x_tick_count > 1 && draw_width > 20) {
        // Label all tick positions

        for (int t = 0; t < x_tick_count; ++t) {
          int tick_x = draw_x + (draw_width - 1) * t / (x_tick_count - 1);
          double val =
              x_min_val + (x_max_val - x_min_val) * t / (x_tick_count - 1);
          std::string s = format_x_label(val);

          // Center the label on the tick position
          int label_start = tick_x - (int)s.size() / 2;
          if (t == 0) label_start = tick_x;  // Left-align first label
          if (t == x_tick_count - 1)
            label_start = tick_x - (int)s.size() + 1;  // Right-align last label

          // Bounds check
          if (label_start < draw_x) label_start = draw_x;
          if (label_start + (int)s.size() > draw_x + draw_width)
            continue;  // Skip if would overflow

          for (size_t i = 0; i < s.size(); ++i) {
            Cell c;
            c.bg_color = bg;
            c.content = std::string(1, s[i]);
            c.fg_color = lbl;
            buffer.set(label_start + i, label_y, c);
          }
        }
      } else {
        // Just endpoints (original behavior)
        // Min X (Left) - index 0
        std::string s_min = format_x_label(x_min_val);
        for (int i = 0; i < std::min((int)s_min.size(), draw_width); ++i) {
          Cell c;
          c.bg_color = bg;
          c.content = std::string(1, s_min[i]);
          c.fg_color = lbl;
          buffer.set(draw_x + i, label_y, c);
        }

        // Max X (Right) - max data index
        std::string s_max = format_x_label(x_max_val);
        if ((int)s_max.size() < draw_width) {
          int start_x = draw_x + draw_width - s_max.size();
          if (start_x > draw_x + (int)s_min.size() + 1) {  // Avoid overlap
            for (size_t i = 0; i < s_max.size(); ++i) {
              Cell c;
              c.bg_color = bg;
              c.content = std::string(1, s_max[i]);
              c.fg_color = lbl;
              buffer.set(start_x + i, label_y, c);
            }
          }
        }
      }
    }

    // 3. Plot Data
    draw_height = plot_height;
    if (draw_height <= 0 || draw_width <= 0) return;

    [[maybe_unused]] bool needs_braille = false;
    for (const auto &s : series) {
      if (s.visible && s.style == LineStyle::Braille) needs_braille = true;
    }

    bool use_braille_shared = needs_braille && Terminal::has_utf8();
    std::unique_ptr<BrailleCanvas> shared_bc;
    std::vector<std::vector<int>> cell_dot_counts;

    if (use_braille_shared) {
      shared_bc = std::make_unique<BrailleCanvas>(draw_width, draw_height);
      cell_dot_counts.assign(draw_width * draw_height,
                             std::vector<int>(series.size(), 0));
    }

    auto render_series = [&](int s_idx, BrailleCanvas *target_bc,
                             std::vector<std::vector<int>> *target_dot_counts) {
      const auto &s = series[s_idx];
      if (s.data.empty()) return;
      if (!s.visible) return;

      LineStyle effective_style = s.style;
      if (effective_style == LineStyle::Braille && !Terminal::has_utf8()) {
        effective_style = LineStyle::Lines;
      }

      Color draw_col = s.color;
      if (hovered_series_idx_ != -1 && s_idx != hovered_series_idx_) {
        draw_col = Theme::current().border;
      }

      if (effective_style == LineStyle::Braille && target_bc) {
        // Helper map
        auto map_y = [&](double v) -> int {
          double norm = (v - min_val) / (max_val - min_val);
          if (norm < 0) norm = 0;
          if (norm > 1) norm = 1;
          // 4 sub-pixels per cell height
          int virtual_h = draw_height * 4;
          int row_from_bottom = (int)std::round(norm * (virtual_h - 1));
          return (virtual_h - 1) - row_from_bottom;
        };

        int virtual_baseline_y = draw_height * 4 - 1;
        if (show_x_axis && min_val <= 0 && max_val >= 0 && max_val != min_val) {
          double zero_norm = (0.0 - min_val) / (max_val - min_val);
          int row_from_bottom = (int)std::round(zero_norm * (draw_height - 1));
          int axis_screen_y = (draw_height - 1) - row_from_bottom;
          virtual_baseline_y = axis_screen_y * 4 + 2;
        }

        int prev_vy = -1;

        auto plot_dot = [&](int vx, int vy) {
          target_bc->set_dot(vx, vy);
          int cx = vx / 2;
          int cy = vy / 4;
          if (cx >= 0 && cx < draw_width && cy >= 0 && cy < draw_height &&
              target_dot_counts) {
            (*target_dot_counts)[cy * draw_width + cx][s_idx]++;
          }
        };

        for (int dx = 0; dx < draw_width; ++dx) {
          for (int sub_x = 0; sub_x < 2; ++sub_x) {
            double ratio = 0.0;
            if (draw_width > 0) {
              double vx = dx * 2 + sub_x;
              double total_vx = draw_width * 2;
              if (total_vx > 1) ratio = vx / (total_vx - 1);
            }

            double exact_idx = ratio * (s.data.size() - 1);

            // Linear Interpolation
            int idx0 = (int)exact_idx;
            int idx1 = idx0 + 1;
            if (idx1 >= (int)s.data.size()) idx1 = (int)s.data.size() - 1;

            double frac = exact_idx - idx0;
            double val = s.data[idx0] * (1.0 - frac) + s.data[idx1] * frac;

            int vx = dx * 2 + sub_x;
            int vy = map_y(val);

            // Connect dots using subpixel vector lines
            if (prev_vy != -1 && s.fill_gaps) {
              target_bc->draw_line(vx - 1, prev_vy, vx, vy, plot_dot);
            } else {
              plot_dot(vx, vy);
            }

            if (s.fill) {
              int start_vy = vy;
              int end_vy = virtual_baseline_y;
              if (start_vy > end_vy) std::swap(start_vy, end_vy);
              for (int y_fill = start_vy; y_fill <= end_vy; ++y_fill) {
                plot_dot(vx, y_fill);
              }
            }
            prev_vy = vy;

            // Record Hit
            if (sub_x == 0) {
              int cell_y = vy / 4;
              if (show_tooltip) {
                point_hits_.push_back(
                    {draw_x + dx, draw_y + cell_y, s_idx, idx0, s.data[idx0]});
              }
            }
          }
        }
      } else if (effective_style == LineStyle::Lines) {
        int prev_screen_y = -1;

        for (int dx = 0; dx < draw_width; ++dx) {
          int data_idx = 0;
          if (draw_width > 1) {
            double ratio = (double)dx / (draw_width - 1);
            data_idx = (int)(ratio * (s.data.size() - 1));
          }
          if (data_idx >= (int)s.data.size()) data_idx = (int)s.data.size() - 1;

          double val = s.data[data_idx];
          double norm = (val - min_val) / (max_val - min_val);
          if (norm < 0) norm = 0;
          if (norm > 1) norm = 1;

          int row_from_bottom = (int)std::round(norm * (draw_height - 1));
          int screen_y = (y + draw_height - 1) - row_from_bottom;

          int baseline_y = y + draw_height - 1;
          if (show_x_axis && min_val <= 0 && max_val >= 0 &&
              max_val != min_val) {
            double zero_norm = (0.0 - min_val) / (max_val - min_val);
            int row_from_bottom_zero =
                (int)std::round(zero_norm * (draw_height - 1));
            baseline_y = (y + draw_height - 1) - row_from_bottom_zero;
          }

          if (s.fill) {
            int start_fy = screen_y;
            int end_fy = baseline_y;
            if (start_fy > end_fy) std::swap(start_fy, end_fy);
            Color f_col = s.fill_color.is_default ? draw_col : s.fill_color;
            if (hovered_series_idx_ != -1 && s_idx != hovered_series_idx_) {
              f_col = Theme::current().border;
            }
            for (int fill_y = start_fy; fill_y <= end_fy; ++fill_y) {
              if (fill_y == screen_y) continue;
              Cell fc;
              fc.content = s.fill_char;
              fc.fg_color = f_col;
              fc.bg_color = bg;
              buffer.set(draw_x + dx, fill_y, fc);
            }
          }

          // Plot
          Cell c;
          c.content = s.marker;
          c.fg_color = draw_col;
          c.bg_color = bg;
          buffer.set(draw_x + dx, screen_y, c);

          // Record Hit
          if (show_tooltip) {
            point_hits_.push_back(
                {draw_x + dx, screen_y, s_idx, data_idx, val});
          }

          // Fill Gap
          if (s.style == LineStyle::Lines && s.fill_gaps &&
              prev_screen_y != -1) {
            int dir = (screen_y > prev_screen_y) ? 1 : -1;
            int count = std::abs(screen_y - prev_screen_y);
            for (int k = 1; k < count; ++k) {
              buffer.set(draw_x + dx, prev_screen_y + k * dir,
                         c);  // Vertical fill only
            }
            // Improved vertical fill:
            int y1 = prev_screen_y;
            int y2 = screen_y;
            if (dx > 0) {  // Connect to previous column
              int lower = std::min(y1, y2);
              int upper = std::max(y1, y2);
              for (int iy = lower + 1; iy < upper; ++iy) {
                buffer.set(draw_x + dx, iy, c);
              }
            }
          }
          prev_screen_y = screen_y;
        }
      }
    };

    // Pass 1: Render all background (non-hovered) series
    for (int s_idx = 0; s_idx < (int)series.size(); ++s_idx) {
      if (hovered_series_idx_ != -1 && s_idx == hovered_series_idx_) continue;
      render_series(s_idx, shared_bc.get(), &cell_dot_counts);
    }

    // Blit background BrailleCanvas to Buffer if used
    if (use_braille_shared && shared_bc) {
      for (int by = 0; by < draw_height; ++by) {
        for (int bx = 0; bx < draw_width; ++bx) {
          std::string bchar = shared_bc->get_char(bx, by);
          if (!bchar.empty() && bchar != " ") {
            // Find dominant series index in this cell
            int max_count = 0;
            int best_idx = -1;
            const auto &counts = cell_dot_counts[by * draw_width + bx];
            for (size_t i = 0; i < series.size(); ++i) {
              if (counts[i] > max_count) {
                max_count = counts[i];
                best_idx = i;
              }
            }

            if (best_idx == -1) {
              for (size_t i = 0; i < series.size(); ++i) {
                if (series[i].visible &&
                    series[i].style == LineStyle::Braille) {
                  best_idx = i;
                  break;
                }
              }
            }

            if (best_idx != -1) {
              Color draw_col = series[best_idx].color;
              if (hovered_series_idx_ != -1 &&
                  best_idx != hovered_series_idx_) {
                draw_col = Theme::current().border;
              }

              Cell c;
              c.content = bchar;
              c.fg_color = draw_col;
              c.bg_color = bg;
              buffer.set(draw_x + bx, draw_y + by, c);
            }
          }
        }
      }
    }

    // Pass 2: Render the foreground (hovered) series on top
    if (hovered_series_idx_ != -1 && hovered_series_idx_ < (int)series.size()) {
      auto hover_bc = std::make_unique<BrailleCanvas>(draw_width, draw_height);
      std::vector<std::vector<int>> hover_dot_counts(
          draw_width * draw_height, std::vector<int>(series.size(), 0));
      render_series(hovered_series_idx_, hover_bc.get(), &hover_dot_counts);

      // Blit foreground BrailleCanvas to Buffer if used
      for (int by = 0; by < draw_height; ++by) {
        for (int bx = 0; bx < draw_width; ++bx) {
          std::string bchar = hover_bc->get_char(bx, by);
          if (!bchar.empty() && bchar != " ") {
            Cell c;
            c.content = bchar;
            c.fg_color = series[hovered_series_idx_].color;
            c.bg_color = bg;
            buffer.set(draw_x + bx, draw_y + by, c);
          }
        }
      }
    }

    // 4. Draw Legend
    legend_hits_.clear();
    if (show_legend) {
      int ly = y;
      for (size_t s_idx = 0; s_idx < series.size(); ++s_idx) {
        const auto &s = series[s_idx];
        if (s.label.empty()) continue;
        int legend_len = s.label.size();
        int lx = x + width - legend_len - 2;
        if (lx < x) lx = x;

        Color leg_col = s.color;
        if (!s.visible) {
          leg_col = Theme::current().border;
        } else if (hovered_series_idx_ != -1 &&
                   (int)s_idx != hovered_series_idx_) {
          leg_col = Theme::current().border;
        }

        for (int i = 0; i < legend_len; ++i) {
          Cell c;
          c.content = std::string(1, s.label[i]);
          c.fg_color = leg_col;
          c.bg_color = bg;
          buffer.set(lx + i, ly, c);
        }

        legend_hits_.push_back({lx, ly, legend_len, (int)s_idx});
        ly++;
        if (ly >= y + height) break;
      }
    }

    // 5. Render Tooltip
  }
};

/// @brief A scatter plot chart
class ScatterChart : public ChartBase {
 public:
  /// @brief Data series definition for scatter plot
  struct Series {
    std::vector<std::pair<double, double>> points;  ///< X,Y points
    std::string label;                              ///< Series label
    Color color;                                    ///< Series color
    std::string marker = "*";                       ///< Marker string
    bool use_braille = false;  ///< Use braille for high density (experimental)
    bool visible = true;       ///< Toggle series visibility in chart and legend
  };

  std::vector<Series> series;
  double x_min = 0.0, x_max = 1.0;
  double y_min = 0.0, y_max = 1.0;

  void add_series(const std::vector<std::pair<double, double>> &p,
                  const std::string &l, cpptui::Color c, std::string m = "*",
                  bool braille = false) {
    Series s;
    s.points = p;
    s.label = l;
    s.color = c;
    s.marker = m;
    s.use_braille = braille;
    auto it = series_visibility_.find(l);
    if (it != series_visibility_.end()) {
      s.visible = it->second;
    } else {
      series_visibility_[l] = s.visible;
    }
    series.push_back(s);
  }

  // Backwards compatibility
  void add_series(const std::vector<std::pair<double, double>> &p,
                  const std::string &l, cpptui::Color c, char m) {
    add_series(p, l, c, std::string(1, m));
  }

  // Tooltip Support (chart-specific PointHit type with x,y values)
  struct PointHit {
    int x, y;
    int series_index;
    int data_index;
    double val_x, val_y;
  };

  mutable std::vector<PointHit> point_hits_;

  // Custom formatter: Series, Index, X, Y -> String
  std::function<std::string(const Series &, int, double, double)>
      tooltip_formatter;

  bool on_event(const Event &event) override {
    bool requested_update = false;

    if (event.is_mouse_event()) {
      if (contains(event.x, event.y)) {
        // 1. Check Legend Hits
        int prev_hovered = hovered_series_idx_;
        hovered_series_idx_ = -1;  // Reset hover

        for (const auto &hit : legend_hits_) {
          if (event.x >= hit.x && event.x < hit.x + hit.width &&
              event.y == hit.y) {
            hovered_series_idx_ = hit.series_index;
            break;
          }
        }

        if (hovered_series_idx_ != prev_hovered) {
          requested_update = true;
        }

        // Handle Click (toggling)
        if (event.mouse_left() && !event.mouse_motion()) {
          for (const auto &hit : legend_hits_) {
            if (event.x >= hit.x && event.x < hit.x + hit.width &&
                event.y == hit.y) {
              series[hit.series_index].visible =
                  !series[hit.series_index].visible;
              series_visibility_[series[hit.series_index].label] =
                  series[hit.series_index].visible;
              requested_update = true;
              return true;  // Click handled
            }
          }
        }
      } else {
        // If mouse moved outside widget, clear hover highlight
        if (hovered_series_idx_ != -1) {
          hovered_series_idx_ = -1;
          requested_update = true;
        }
      }
    }

    // 2. Handle Tooltips if enabled
    if (show_tooltip && event.is_mouse_event() && !event.mouse_wheel()) {
      bool hit_found = false;
      if (contains(event.x, event.y) && hovered_series_idx_ == -1) {
        for (const auto &hit : point_hits_) {
          if (std::abs(event.x - hit.x) <= 1 &&
              std::abs(event.y - hit.y) <= 0) {
            std::string text;
            if (tooltip_formatter) {
              text = tooltip_formatter(series[hit.series_index], hit.data_index,
                                       hit.val_x, hit.val_y);
            } else {
              std::stringstream ss;
              ss << std::fixed << std::setprecision(2) << "(" << hit.val_x
                 << ", " << hit.val_y << ")";
              text = series[hit.series_index].label + ": " + ss.str();
            }

            if (!tooltip_) tooltip_ = std::make_shared<Tooltip>();
            tooltip_->text = text;
            tooltip_->position = Tooltip::Position::Manual;
            tooltip_->manual_x = event.x;
            tooltip_->manual_y = event.y - 1;
            tooltip_->visible = true;
            hit_found = true;
            last_hit_time_ = std::chrono::steady_clock::now();
            return true;
          }
        }
      }

      if (hovered_series_idx_ != -1 && tooltip_) {
        tooltip_ = nullptr;
        requested_update = true;
      } else if (!hit_found && tooltip_) {
        if (tooltip_->contains(event.x, event.y)) {
          last_hit_time_ = std::chrono::steady_clock::now();
        } else {
          auto now = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - last_hit_time_)
                             .count();
          if (elapsed >= tooltip_duration_ms) {
            tooltip_ = nullptr;
            requested_update = true;
          }
        }
      }
    }

    return requested_update;
  }

  void render(Buffer &buffer) override {
    for (const auto &s : series) {
      series_visibility_[s.label] = s.visible;
    }
    if (show_tooltip) point_hits_.clear();

    if (auto_scale && !series.empty()) {
      double calculated_x_min = std::numeric_limits<double>::max();
      double calculated_x_max = std::numeric_limits<double>::lowest();
      double calculated_y_min = std::numeric_limits<double>::max();
      double calculated_y_max = std::numeric_limits<double>::lowest();
      bool has_data = false;
      for (const auto &s : series) {
        for (const auto &p : s.points) {
          if (p.first < calculated_x_min) calculated_x_min = p.first;
          if (p.first > calculated_x_max) calculated_x_max = p.first;
          if (p.second < calculated_y_min) calculated_y_min = p.second;
          if (p.second > calculated_y_max) calculated_y_max = p.second;
          has_data = true;
        }
      }
      if (has_data) {
        if (calculated_x_min == calculated_x_max) {
          x_min = calculated_x_min - 1.0;
          x_max = calculated_x_max + 1.0;
        } else {
          double range_x = calculated_x_max - calculated_x_min;
          x_min = calculated_x_min - range_x * 0.05;
          x_max = calculated_x_max + range_x * 0.05;
        }
        if (calculated_y_min == calculated_y_max) {
          y_min = calculated_y_min - 1.0;
          y_max = calculated_y_max + 1.0;
        } else {
          double range_y = calculated_y_max - calculated_y_min;
          y_min = calculated_y_min - range_y * 0.05;
          y_max = calculated_y_max + range_y * 0.05;
        }
      }
    }

    Color bg = bg_color.resolve(Theme::current().background);

    // Resolve defaults
    Color axis = axis_color.resolve(Theme::current().border);
    Color lbl = label_color.resolve(Theme::current().foreground);
    Color grid_col = grid_color.resolve(Theme::current().border);
    Color zero_axis_col = zero_axis_color.resolve(Theme::current().foreground);
    // Draw Axes

    int draw_x = x;
    int draw_y = y;
    int draw_width = width;
    int draw_height = height;

    bool has_x_labels = show_x_tick_labels && height > 2;
    bool has_x_axis = show_x_axis;

    int bottom_reserved = 0;
    if (has_x_labels)
      bottom_reserved = 1;
    else if (has_x_axis)
      bottom_reserved = 1;

    if (show_x_axis && show_x_tick_labels && height > 4) bottom_reserved = 2;

    int plot_height = height - bottom_reserved;

    // 1. Calculate Offsets and Draw Y Elements
    int y_label_width = 6;
    bool has_y_labels = show_y_tick_labels && draw_width > y_label_width + 2;
    bool has_y_axis = show_y_axis;

    int left_offset = 0;
    if (has_y_labels)
      left_offset = y_label_width;
    else if (has_y_axis)
      left_offset = 1;

    // Helper for formatting tick labels
    auto format_y_label = [&](double val) -> std::string {
      if (y_tick_formatter) return y_tick_formatter(val);
      std::stringstream ss;
      ss.precision(y_tick_precision);
      ss << std::fixed << val;
      return ss.str();
    };
    auto format_x_label = [&](double val) -> std::string {
      if (x_tick_formatter) return x_tick_formatter(val);
      std::stringstream ss;
      ss.precision(x_tick_precision);
      ss << std::fixed << val;
      return ss.str();
    };

    // Draw Y Labels
    if (has_y_labels) {
      int line_height = plot_height;

      // Max
      std::string s_max = format_y_label(y_max);
      for (int i = 0; i < std::min((int)s_max.size(), y_label_width); ++i) {
        Cell c;
        c.bg_color = bg;
        c.content = std::string(1, s_max[i]);
        c.fg_color = lbl;
        buffer.set(x + i, y, c);
      }

      // Min
      std::string s_min = format_y_label(y_min);
      int label_bottom_y = y + plot_height - 1;
      if (label_bottom_y < y) label_bottom_y = y;

      for (int i = 0; i < std::min((int)s_min.size(), y_label_width); ++i) {
        Cell c;
        c.bg_color = bg;
        c.content = std::string(1, s_min[i]);
        c.fg_color = lbl;
        buffer.set(x + i, label_bottom_y, c);
      }
    }

    // Draw Y Axis Line & Ticks
    if (has_y_axis || has_y_labels) {
      if (show_y_axis) {
        int line_x = x + left_offset - 1;
        if (has_y_axis && !has_y_labels) line_x = x;

        int line_height = plot_height;

        for (int i = 0; i < line_height; ++i) {
          Cell c;
          c.bg_color = bg;
          c.content = "│";
          c.fg_color = axis;
          buffer.set(line_x, y + i, c);
        }

        // Y Ticks & Grid Lines
        if (y_tick_count > 1) {
          for (int i = 0; i < y_tick_count; ++i) {
            int tick_y = y + (int)std::round((double)(line_height - 1) * i /
                                             (y_tick_count - 1));
            if (show_y_ticks) {
              Cell c;
              c.bg_color = bg;
              c.content = "┼";
              c.fg_color = axis;
              buffer.set(line_x, tick_y, c);
            }
            if (show_grid_lines) {
              int start_gx = x + left_offset;
              int end_gx = x + width;
              for (int gx = start_gx; gx < end_gx; ++gx) {
                Cell gc;
                gc.bg_color = bg;
                gc.content = "─";
                gc.fg_color = grid_col;
                buffer.set(gx, tick_y, gc);
              }
            }
          }
        }
      }
    }

    draw_x += left_offset;
    draw_width -= left_offset;

    // 2. Calculate Offsets and Draw X Elements
    int bottom_y = y + height - 1;
    int axis_y = y + plot_height - 1;

    // Draw X Axis Line & Ticks
    if (show_x_axis) {
      // Determine Axis Y position
      int axis_screen_y = axis_y;
      bool is_zero_axis = false;

      // If 0 is in range [y_min, y_max]
      if (y_min <= 0 && y_max >= 0 && y_max != y_min) {
        double zero_norm = (0.0 - y_min) / (y_max - y_min);
        int row_from_bottom = (int)std::round(zero_norm * (plot_height - 1));
        axis_screen_y = (y + plot_height - 1) - row_from_bottom;
        is_zero_axis = true;
      }

      for (int i = 0; i < draw_width; ++i) {
        Cell c;
        c.bg_color = bg;
        c.content = "─";
        c.fg_color = is_zero_axis ? zero_axis_col : axis;
        buffer.set(draw_x + i, axis_screen_y, c);
      }

      // X Ticks & Grid Lines
      if (x_tick_count > 1) {
        for (int i = 0; i < x_tick_count; ++i) {
          int tick_x = draw_x + (draw_width - 1) * i / (x_tick_count - 1);
          if (show_x_ticks) {
            Cell c;
            c.bg_color = bg;
            c.content = "┼";
            c.fg_color = is_zero_axis ? zero_axis_col : axis;
            buffer.set(tick_x, axis_screen_y, c);
          }
          if (show_grid_lines) {
            for (int gy = 0; gy < plot_height; ++gy) {
              int screen_gy = y + gy;
              if (show_x_axis && screen_gy == axis_screen_y) continue;
              if (show_y_axis && tick_x == (x + left_offset - 1)) continue;

              const Cell &existing = buffer.get(tick_x, screen_gy);
              Cell gc;
              gc.bg_color = bg;
              gc.fg_color = grid_col;
              if (existing.content == "─") {
                gc.content = "┼";
              } else {
                gc.content = "│";
              }
              buffer.set(tick_x, screen_gy, gc);
            }
          }
        }
      }

      // Corner
      if (show_y_axis) {
        Cell c;
        c.fg_color = axis;
        c.bg_color = bg;
        int line_x = x + left_offset - 1;
        if (!has_y_labels && has_y_axis) line_x = x;

        if (axis_screen_y == bottom_y || axis_screen_y == axis_y) {
          c.content = "└";
        } else {
          c.content = "├";
        }

        buffer.set(line_x, axis_screen_y, c);
      }

      // Draw vertical mathematical Y-axis line at x = 0.0
      if (show_y_axis && x_min <= 0 && x_max >= 0 && x_max != x_min) {
        double zero_norm = (0.0 - x_min) / (x_max - x_min);
        int col_from_left = (int)std::round(zero_norm * (draw_width - 1));
        int zero_x_screen = draw_x + col_from_left;

        if (zero_x_screen >= draw_x && zero_x_screen < draw_x + draw_width) {
          for (int gy = 0; gy < plot_height; ++gy) {
            int screen_gy = y + gy;
            // Skip the horizontal axis intersection point
            if (show_x_axis && screen_gy == axis_screen_y) continue;
            // Skip the left border Y-axis line
            if (zero_x_screen == (x + left_offset - 1)) continue;

            Cell c;
            c.bg_color = bg;
            c.content = "│";
            c.fg_color = zero_axis_col;
            buffer.set(zero_x_screen, screen_gy, c);
          }

          // Draw intersection of horizontal and vertical zero axes
          if (show_x_axis && axis_screen_y >= y &&
              axis_screen_y < y + plot_height) {
            Cell c;
            c.bg_color = bg;
            c.content = "┼";
            c.fg_color = zero_axis_col;
            buffer.set(zero_x_screen, axis_screen_y, c);
          }
        }
      }
    }

    // 2. Draw X-Axis Labels (Bottom)
    if (show_x_tick_labels) {
      if (draw_height > 2) {
        // We already calculated layout, just draw

        // Min X (Left)
        std::string s_min = format_x_label(x_min);
        for (int i = 0; i < std::min((int)s_min.size(), draw_width); ++i) {
          Cell c;
          c.bg_color = bg;
          c.content = std::string(1, s_min[i]);
          c.fg_color = lbl;
          buffer.set(draw_x + i, bottom_y, c);
        }

        // Max X (Right)
        std::string s_max = format_x_label(x_max);
        if ((int)s_max.size() < draw_width) {
          int start_x = draw_x + draw_width - s_max.size();
          if (start_x > draw_x + (int)s_min.size() + 1) {  // Avoid overlap
            for (size_t i = 0; i < s_max.size(); ++i) {
              Cell c;
              c.bg_color = bg;
              c.content = std::string(1, s_max[i]);
              c.fg_color = lbl;
              buffer.set(start_x + i, bottom_y, c);
            }
          }
        }
      }
    }

    // 3. Plot Data
    draw_height = plot_height;
    if (draw_height <= 0 || draw_width <= 0) return;

    [[maybe_unused]] bool needs_braille = false;
    for (const auto &s : series) {
      if (s.visible && s.use_braille) needs_braille = true;
    }

    bool use_braille_shared = needs_braille && Terminal::has_utf8();
    std::unique_ptr<BrailleCanvas> shared_bc;
    std::vector<std::vector<int>> cell_dot_counts;

    if (use_braille_shared) {
      shared_bc = std::make_unique<BrailleCanvas>(draw_width, draw_height);
      cell_dot_counts.assign(draw_width * draw_height,
                             std::vector<int>(series.size(), 0));
    }

    auto render_series = [&](int s_idx, BrailleCanvas *target_bc,
                             std::vector<std::vector<int>> *target_dot_counts) {
      const auto &s = series[s_idx];
      if (s.points.empty()) return;
      if (!s.visible) return;

      bool use_braille = s.use_braille;
      if (use_braille && !Terminal::has_utf8()) {
        use_braille = false;
      }

      Color draw_col = s.color;
      if (hovered_series_idx_ != -1 && s_idx != hovered_series_idx_) {
        draw_col = Theme::current().border;
      }

      if (use_braille && target_bc) {
        // Helper map Y
        auto map_y = [&](double v) -> int {
          double norm = (v - y_min) / (y_max - y_min);
          if (norm < 0) norm = 0;
          if (norm > 1) norm = 1;
          // 4 sub-pixels per cell height
          int virtual_h = draw_height * 4;
          int row_from_bottom = (int)std::round(norm * (virtual_h - 1));
          return (virtual_h - 1) - row_from_bottom;
        };

        // Helper map X
        auto map_x = [&](double v) -> int {
          double norm = (v - x_min) / (x_max - x_min);
          if (norm < 0) norm = 0;
          if (norm > 1) norm = 1;
          int virtual_w = draw_width * 2;
          return (int)std::round(norm * (virtual_w - 1));
        };

        for (int i = 0; i < (int)s.points.size(); ++i) {
          const auto &p = s.points[i];
          double px = p.first;
          double py = p.second;
          if (px < x_min || px > x_max || py < y_min || py > y_max) continue;

          int vx = map_x(px);
          int vy = map_y(py);

          target_bc->set_dot(vx, vy);
          int cx = vx / 2;
          int cy = vy / 4;
          if (cx >= 0 && cx < draw_width && cy >= 0 && cy < draw_height &&
              target_dot_counts) {
            (*target_dot_counts)[cy * draw_width + cx][s_idx]++;
          }

          // Record Hit
          if (show_tooltip && draw_width > 0 && draw_height > 0) {
            int hit_x = draw_x + cx;
            int hit_y = draw_y + cy;
            point_hits_.push_back({hit_x, hit_y, s_idx, i, px, py});
          }
        }
      } else {
        for (int i = 0; i < (int)s.points.size(); ++i) {
          const auto &p = s.points[i];
          double px = p.first;
          double py = p.second;

          // Map X
          double norm_x = (px - x_min) / (x_max - x_min);
          if (norm_x < 0 || norm_x > 1) continue;

          // Map Y
          double norm_y = (py - y_min) / (y_max - y_min);
          if (norm_y < 0 || norm_y > 1) continue;

          int screen_x = (int)std::round(norm_x * (draw_width - 1));
          int row_from_bottom = (int)std::round(norm_y * (draw_height - 1));
          int screen_y = (draw_height - 1) - row_from_bottom;

          Cell c;
          c.content = s.marker;
          c.fg_color = draw_col;
          c.bg_color = bg;
          buffer.set(draw_x + screen_x, draw_y + screen_y, c);

          // Record Hit
          if (show_tooltip) {
            point_hits_.push_back(
                {draw_x + screen_x, draw_y + screen_y, s_idx, i, px, py});
          }
        }
      }
    };

    // Pass 1: Render all background (non-hovered) series
    for (int s_idx = 0; s_idx < (int)series.size(); ++s_idx) {
      if (hovered_series_idx_ != -1 && s_idx == hovered_series_idx_) continue;
      render_series(s_idx, shared_bc.get(), &cell_dot_counts);
    }

    // Blit background BrailleCanvas to Buffer if used
    if (use_braille_shared && shared_bc) {
      for (int by = 0; by < draw_height; ++by) {
        for (int bx = 0; bx < draw_width; ++bx) {
          std::string bchar = shared_bc->get_char(bx, by);
          if (!bchar.empty() && bchar != " ") {
            // Find dominant series index in this cell
            int max_count = 0;
            int best_idx = -1;
            const auto &counts = cell_dot_counts[by * draw_width + bx];
            for (size_t i = 0; i < series.size(); ++i) {
              if (counts[i] > max_count) {
                max_count = counts[i];
                best_idx = i;
              }
            }

            if (best_idx == -1) {
              for (size_t i = 0; i < series.size(); ++i) {
                if (series[i].visible && series[i].use_braille) {
                  best_idx = i;
                  break;
                }
              }
            }

            if (best_idx != -1) {
              Color draw_col = series[best_idx].color;
              if (hovered_series_idx_ != -1 &&
                  best_idx != hovered_series_idx_) {
                draw_col = Theme::current().border;
              }

              Cell c;
              c.content = bchar;
              c.fg_color = draw_col;
              c.bg_color = bg;
              buffer.set(draw_x + bx, draw_y + by, c);
            }
          }
        }
      }
    }

    // Pass 2: Render foreground (hovered) series on top
    if (hovered_series_idx_ != -1 && hovered_series_idx_ < (int)series.size()) {
      auto hover_bc = std::make_unique<BrailleCanvas>(draw_width, draw_height);
      std::vector<std::vector<int>> hover_dot_counts(
          draw_width * draw_height, std::vector<int>(series.size(), 0));
      render_series(hovered_series_idx_, hover_bc.get(), &hover_dot_counts);

      // Blit foreground BrailleCanvas to Buffer if used
      for (int by = 0; by < draw_height; ++by) {
        for (int bx = 0; bx < draw_width; ++bx) {
          std::string bchar = hover_bc->get_char(bx, by);
          if (!bchar.empty() && bchar != " ") {
            Cell c;
            c.content = bchar;
            c.fg_color = series[hovered_series_idx_].color;
            c.bg_color = bg;
            buffer.set(draw_x + bx, draw_y + by, c);
          }
        }
      }
    }

    // 4. Draw Legend
    legend_hits_.clear();
    if (show_legend) {
      int ly = y;
      for (size_t s_idx = 0; s_idx < series.size(); ++s_idx) {
        const auto &s = series[s_idx];
        if (s.label.empty()) continue;
        int legend_len = s.label.size();
        int lx = x + width - legend_len - 2;
        if (lx < x) lx = x;

        Color leg_col = s.color;
        if (!s.visible) {
          leg_col = Theme::current().border;
        } else if (hovered_series_idx_ != -1 &&
                   (int)s_idx != hovered_series_idx_) {
          leg_col = Theme::current().border;
        }

        for (int i = 0; i < legend_len; ++i) {
          Cell c;
          c.bg_color = bg;
          c.content = std::string(1, s.label[i]);
          c.fg_color = leg_col;
          buffer.set(lx + i, ly, c);
        }

        legend_hits_.push_back({lx, ly, legend_len, (int)s_idx});
        ly++;
        if (ly >= y + height) break;
      }
    }

    // 5. Render Tooltip
  }
};

/// @brief A vertical bar chart
class BarChart : public ChartBase {
 public:
  struct Series {
    std::vector<double> values;
    StyledText label;
    Color color;
  };

  std::vector<Series> series;
  std::vector<StyledText> categories;  // Shared x-axis category labels

  int bar_gap = 1;    // Gap between category groups
  int group_gap = 0;  // Gap between bars within a group (usually 0)

  bool show_values = false;

  // Tooltip Hit Structure
  struct BarHit {
    int x, y;
    int width, height;
    int series_index;
    int category_index;
    double value;
    StyledText category;
  };

  mutable std::vector<BarHit> bar_hits_;
  mutable std::vector<Rect> bar_rects_;  // For easier hit testing

  // Custom formatter: Series Label, Category, Value -> String
  std::function<std::string(const std::string &, const std::string &, double)>
      tooltip_formatter;

  void add_series(const std::vector<double> &v, const StyledText &l,
                  cpptui::Color c) {
    series.push_back({v, l, c});
  }

  bool on_event(const Event &event) override {
    if (!show_tooltip) return false;

    if (event.is_mouse_event()) {
      if (event.mouse_wheel()) return false;
      bool hit_found = false;

      // Check if mouse is within chart bounds
      if (contains(event.x, event.y)) {
        for (const auto &hit : bar_hits_) {
          // Check if point is inside the bar rect
          if (event.x >= hit.x && event.x < hit.x + hit.width &&
              event.y >= hit.y && event.y < hit.y + hit.height) {
            std::string text;
            StyledText s_label = series[hit.series_index].label;

            if (tooltip_formatter) {
              text = tooltip_formatter(s_label.plain_text(),
                                       hit.category.plain_text(), hit.value);
            } else {
              // Default format
              std::stringstream ss;
              ss << std::fixed << std::setprecision(2) << hit.value;
              text = s_label.plain_text() + " (" + hit.category.plain_text() +
                     "): " + ss.str();
            }

            if (!tooltip_) tooltip_ = std::make_shared<Tooltip>();

            tooltip_->text = text;
            tooltip_->position = Tooltip::Position::Manual;
            tooltip_->manual_x = hit.x + hit.width / 2;
            tooltip_->manual_y = hit.y - 1;
            tooltip_->visible = true;

            hit_found = true;
            last_hit_time_ = std::chrono::steady_clock::now();
            return true;  // Request redraw
          }
        }
      }

      if (!hit_found) {
        if (tooltip_) {
          if (tooltip_->contains(event.x, event.y)) {
            last_hit_time_ = std::chrono::steady_clock::now();
          } else {
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_hit_time_)
                    .count();
            if (elapsed >= tooltip_duration_ms) {
              tooltip_ = nullptr;
              return true;  // Request redraw
            }
          }
        }
      }
    }
    return false;
  }

  void render(Buffer &buffer) override {
    if (show_tooltip) bar_hits_.clear();

    Color bg = bg_color.resolve(Theme::current().background);
    Color axis_col = axis_color.resolve(Theme::current().border);
    Color lbl_col = label_color.resolve(Theme::current().foreground);
    Color grid_col = grid_color.resolve(Theme::current().border);

    if (series.empty() || categories.empty()) return;

    // 1. Calc Max
    double max_val = 0;
    for (const auto &s : series) {
      for (double d : s.values)
        if (d > max_val) max_val = d;
    }
    if (max_val <= 0) max_val = 1;

    // 2. Layout
    int draw_x = x;
    int draw_y = y;
    int draw_width = width;
    [[maybe_unused]] int draw_height = height;

    // Y-Axis Labels & Margins
    int y_label_width = 6;
    bool has_y_labels = show_y_tick_labels && draw_width > y_label_width + 2;
    bool has_y_axis = show_y_axis;

    int left_offset = 0;
    if (has_y_labels)
      left_offset = y_label_width;
    else if (has_y_axis)
      left_offset = 1;

    // Helper for formatting Y tick labels
    auto format_y_label = [&](double val) -> std::string {
      if (y_tick_formatter) return y_tick_formatter(val);
      std::stringstream ss;
      ss.precision(y_tick_precision);
      ss << std::fixed << val;
      return ss.str();
    };

    // Draw Y Labels
    if (has_y_labels) {
      // Max
      std::string s_max = format_y_label(max_val);
      for (int i = 0; i < std::min((int)s_max.size(), y_label_width); ++i) {
        Cell c;
        c.bg_color = bg;
        c.content = std::string(1, s_max[i]);
        c.fg_color = lbl_col;
        buffer.set(x + i, y, c);
      }
    }

    draw_x += left_offset;
    draw_width -= left_offset;

    // Reserve space for X labels at bottom
    int bottom_reserved = 1;  // Always assume 1 for categories
    int content_height = height - bottom_reserved;

    if (content_height <= 0) return;

    // Draw Y Axis Line
    if (show_y_axis) {
      int line_x = x + left_offset - 1;
      if (has_y_axis && !has_y_labels) line_x = x;

      for (int i = 0; i < content_height; ++i) {
        Cell c;
        c.bg_color = bg;
        c.content = "│";
        c.fg_color = axis_col;
        buffer.set(line_x, y + i, c);
      }

      // Y Ticks & Grid Lines
      if (y_tick_count > 1) {
        for (int i = 0; i < y_tick_count; ++i) {
          int tick_y = y + (content_height - 1) * i / (y_tick_count - 1);
          if (show_y_ticks) {
            Cell c;
            c.bg_color = bg;
            c.content = "┼";
            c.fg_color = axis_col;
            buffer.set(line_x, tick_y, c);
          }
          if (show_grid_lines) {
            int start_gx = x + left_offset;
            int end_gx = x + width;
            for (int gx = start_gx; gx < end_gx; ++gx) {
              Cell gc;
              gc.bg_color = bg;
              gc.content = "─";
              gc.fg_color = grid_col;
              buffer.set(gx, tick_y, gc);
            }
          }
        }
      }
    }
    // Draw 0 label at correct Y
    if (has_y_labels) {
      int zero_y = y + content_height - 1;
      std::string s_min = format_y_label(0.0);
      for (int i = 0; i < std::min((int)s_min.size(), y_label_width); ++i) {
        Cell c;
        c.bg_color = bg;
        c.content = std::string(1, s_min[i]);
        c.fg_color = lbl_col;
        buffer.set(x + i, zero_y, c);
      }
    }

    // Draw X Axis Line
    if (show_x_axis) {
      int axis_y = y + content_height - 1;
      for (int i = 0; i < draw_width; ++i) {
        Cell c;
        c.bg_color = bg;
        c.content = "─";
        c.fg_color = axis_col;
        buffer.set(draw_x + i, axis_y, c);
      }

      // Corner
      if (show_y_axis) {
        Cell c;
        c.fg_color = axis_col;
        c.bg_color = bg;
        int line_x = x + left_offset - 1;
        if (!has_y_labels && has_y_axis) line_x = x;
        c.content = "└";
        buffer.set(line_x, axis_y, c);
      }
    }

    // 3. Render Bars
    int num_categories = categories.size();
    int num_series = series.size();

    if (num_categories == 0) return;
    int slot_width = draw_width / num_categories;
    if (slot_width < 1) return;  // Too cramped

    int active_slot_width = slot_width - bar_gap;
    if (active_slot_width < num_series)
      active_slot_width = num_series;  // Min 1 char per bar

    int final_bar_width = active_slot_width / num_series;
    if (final_bar_width < 1) final_bar_width = 1;

    // X Ticks logic prep
    int axis_y = y + content_height - 1;

    // Render Categories
    for (int c_idx = 0; c_idx < num_categories; ++c_idx) {
      int slot_x = draw_x + c_idx * slot_width;
      if (slot_x >= draw_x + draw_width) break;

      // Center the group within the slot
      int group_width = num_series * final_bar_width;
      int group_start_x = slot_x + (slot_width - group_width) / 2;

      // X Tick (between categories) & Grid Lines
      int tick_x = slot_x + slot_width / 2;
      if (tick_x < draw_x + draw_width) {
        if (show_x_ticks && show_x_axis) {
          Cell c;
          c.bg_color = bg;
          c.content = "┴";
          c.fg_color = axis_col;
          buffer.set(tick_x, axis_y, c);
        }
        if (show_grid_lines) {
          for (int gy = 0; gy < content_height; ++gy) {
            int screen_gy = y + gy;
            if (show_x_axis && screen_gy == axis_y) continue;
            if (show_y_axis && tick_x == (x + left_offset - 1)) continue;

            const Cell &existing = buffer.get(tick_x, screen_gy);
            Cell gc;
            gc.bg_color = bg;
            gc.fg_color = grid_col;
            if (existing.content == "─") {
              gc.content = "┼";
            } else {
              gc.content = "│";
            }
            buffer.set(tick_x, screen_gy, gc);
          }
        }
      }

      // Draw Bars in Group
      for (int s_idx = 0; s_idx < num_series; ++s_idx) {
        const auto &s = series[s_idx];
        if (c_idx >= (int)s.values.size()) continue;

        double val = s.values[c_idx];
        double ratio = val / max_val;
        int bar_h = (int)(ratio * (content_height - 1));

        int bx = group_start_x + s_idx * final_bar_width;
        int bottom_plot_y = axis_y - 1;
        int bar_top_y = bottom_plot_y - bar_h + 1;

        // Store Hit
        if (show_tooltip) {
          BarHit hit;
          hit.x = bx;
          hit.y = bar_top_y;
          hit.width = final_bar_width;
          hit.height = bar_h;
          hit.series_index = s_idx;
          hit.category_index = c_idx;
          hit.value = val;
          hit.category = categories[c_idx];
          bar_hits_.push_back(hit);
        }

        // Draw Column
        for (int h = 0; h < bar_h; ++h) {
          int pixel_y = bottom_plot_y - h;

          for (int w = 0; w < final_bar_width; ++w) {
            Cell c;
            c.bg_color = s.color;  // Use BG for solid block
            c.content = " ";
            buffer.set(bx + w, pixel_y, c);
          }
        }
      }

      // Draw Category Label
      StyledText styled_cat = categories[c_idx];
      std::string cat = styled_cat.plain_text();
      int cat_dw = utf8_display_width(cat);

      int label_x = slot_x + (slot_width - cat_dw) / 2;
      int label_y = draw_y + height - 1;

      Color lbl = label_color.resolve(Theme::current().foreground);

      auto text_chars = TextHelper::prepare_text_for_render(cat);
      int cell_x = 0;
      for (size_t j = 0; j < text_chars.size(); ++j) {
        if (cell_x >= slot_width) break;
        const auto &ci = text_chars[j];
        Cell c;
        c.content = ci.content;
        c.fg_color = lbl;
        c.bg_color = bg;

        // Apply styling
        if (!styled_cat.empty()) {
          if (const TextSpan *span = styled_cat.get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(label_x + cell_x, label_y, c);
        cell_x += ci.display_width;
      }
    }

    // Legend
    if (show_legend) {
      int ly = draw_y;
      for (const auto &s : series) {
        std::string label = s.label.plain_text();
        int lx = draw_x + draw_width - utf8_display_width(label) - 2;
        if (lx < draw_x) lx = draw_x;

        auto text_chars = TextHelper::prepare_text_for_render(label);
        int cell_x = 0;
        for (size_t i = 0; i < text_chars.size(); ++i) {
          const auto &ci = text_chars[i];
          Cell c;
          c.content = ci.content;
          c.fg_color = s.color;
          c.bg_color = bg;

          // Apply styling
          if (!s.label.empty()) {
            if (const TextSpan *span = s.label.get_span_at_char((int)i)) {
              if (!span->color.is_default) c.fg_color = span->color;
              c.bold = span->bold;
              c.italic = span->italic;
              c.underline = span->underline;
            }
          }

          buffer.set(lx + cell_x, ly, c);
          cell_x += ci.display_width;
        }
        ly++;
      }
    }
  }
};

/// @brief A horizontal/vertical slider for range selection
class Slider : public Widget {
 public:
  double value = 50.0;
  double min_value = 0.0;
  double max_value = 100.0;
  double step = 1.0;
  bool vertical = false;

  Color track_color = Color();
  Color thumb_color = Color();
  Color fill_color = Color();

  std::string thumb_char = "●";
  std::string track_char = "─";
  std::string track_char_v = "│";
  std::string fill_char = "━";
  std::string fill_char_v = "┃";

  std::function<void(double)> on_change;

  Slider(double val = 50.0, double min_v = 0.0, double max_v = 100.0)
      : value(val), min_value(min_v), max_value(max_v) {
    focusable = true;
    fixed_height = 1;
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().background);
    Color track = track_color.resolve(Theme::current().border);
    Color thumb = thumb_color.resolve(Theme::current().primary);
    Color fill = fill_color.resolve(Theme::current().secondary);

    if (focused_) thumb = Theme::current().primary;

    double ratio = (value - min_value) / (max_value - min_value);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    if (!vertical) {
      int track_len = width;
      int fill_pos = (int)(ratio * (track_len - 1));

      for (int i = 0; i < track_len; ++i) {
        Cell c;
        c.bg_color = bg;
        if (i < fill_pos) {
          c.content = fill_char;
          c.fg_color = fill;
        } else if (i == fill_pos) {
          c.content = thumb_char;
          c.fg_color = thumb;
        } else {
          c.content = track_char;
          c.fg_color = track;
        }
        buffer.set(x + i, y, c);
      }
    } else {
      int track_len = height;
      int fill_pos = (int)((1.0 - ratio) * (track_len - 1));

      for (int i = 0; i < track_len; ++i) {
        Cell c;
        c.bg_color = bg;
        if (i > fill_pos) {
          c.content = fill_char_v;
          c.fg_color = fill;
        } else if (i == fill_pos) {
          c.content = thumb_char;
          c.fg_color = thumb;
        } else {
          c.content = track_char_v;
          c.fg_color = track;
        }
        buffer.set(x, y + i, c);
      }
    }
  }

  bool on_event(const Event &event) override {
    if (event.is_key_event() && focused_) {
      double old_val = value;
      if (!vertical) {
        if (event.is_nav_right()) {
          value += step;
        }
        if (event.is_nav_left()) {
          value -= step;
        }
      } else {
        if (event.is_nav_up()) {
          value += step;
        }
        if (event.is_nav_down()) {
          value -= step;
        }
      }
      if (value < min_value) value = min_value;
      if (value > max_value) value = max_value;
      if (value != old_val && on_change) on_change(value);
      return value != old_val;
    }

    if (event.is_mouse_event() && contains(event.x, event.y) &&
        !event.mouse_wheel()) {
      if (event.mouse_left() || event.mouse_drag()) {
        set_focus(true);
        double old_val = value;
        if (!vertical) {
          double ratio = (double)(event.x - x) / (width - 1);
          value = min_value + ratio * (max_value - min_value);
        } else {
          double ratio = 1.0 - (double)(event.y - y) / (height - 1);
          value = min_value + ratio * (max_value - min_value);
        }
        value = std::round(value / step) * step;
        if (value < min_value) value = min_value;
        if (value > max_value) value = max_value;
        if (value != old_val && on_change) on_change(value);
        return true;
      }
    }
    return false;
  }
};

/// @brief Compact inline status indicator (badge/tag)
class Badge : public Widget {
 public:
  StyledText styled_text;
  std::string text;
  Color badge_bg = Color();
  Color text_color = Color();

  enum class Style { Pill, Square, Outline };
  Style style = Style::Pill;

  Badge(const StyledText &t = StyledText(""), const Color &bg = Color(),
        const Color &fg = Color())
      : styled_text(t), text(t.plain_text()), badge_bg(bg), text_color(fg) {
    fixed_height = 1;
  }

  void render(Buffer &buffer) override {
    Color bg = badge_bg.resolve(Theme::current().primary);
    Color fg = text_color.is_default ? Color::contrast_color(bg) : text_color;

    std::string prefix = "";
    std::string suffix = "";
    if (style == Style::Pill) {
      prefix = " ";
      suffix = " ";
    } else if (style == Style::Square) {
      prefix = "[";
      suffix = "]";
    } else {
      prefix = "(";
      suffix = ")";
    }

    int prefix_chars = (int)TextHelper::prepare_text_for_render(prefix).size();
    std::string full_text = prefix + text + suffix;
    std::vector<CharInfo> chars =
        TextHelper::prepare_text_for_render(full_text);

    if (style == Style::Outline) {
      bg = Theme::current().background;
      fg = badge_bg.resolve(Theme::current().primary);
    }

    int cell_x = 0;
    for (size_t i = 0; i < chars.size() && cell_x < width; ++i) {
      Cell c;
      c.content = chars[i].content;
      c.fg_color = fg;
      c.bg_color = bg;

      // Apply styling to the text part
      if ((int)i >= prefix_chars &&
          (int)i < prefix_chars +
                       (int)TextHelper::prepare_text_for_render(text).size()) {
        if (!styled_text.empty()) {
          if (const TextSpan *span =
                  styled_text.get_span_at_char((int)i - prefix_chars)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }
      }

      buffer.set(x + cell_x, y, c);
      if (chars[i].display_width == 2 && cell_x + 1 < width) {
        Cell skip;
        skip.content = "";
        skip.bg_color = bg;
        buffer.set(x + cell_x + 1, y, skip);
      }
      cell_x += chars[i].display_width;
    }
  }
};

/// @brief Formatted keyboard shortcut display bar
class ShortcutBar : public Widget {
 public:
  struct Shortcut {
    std::string key;
    StyledText styled_description;
    std::string description;
  };

  std::vector<Shortcut> items;
  int spacing = 2;

  Color key_bg = Color();
  Color key_fg = Color();
  Color desc_fg = Color();

  ShortcutBar() { fixed_height = 1; }

  void add(const std::string &key, StyledText desc) {
    items.push_back({key, desc, desc.plain_text()});
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().background);
    Color k_bg = key_bg.resolve(Theme::current().foreground);
    Color k_fg = key_fg.resolve(Theme::current().background);
    Color d_fg = desc_fg.resolve(Theme::current().foreground);

    int cx = x;
    for (const auto &item : items) {
      // Draw key (inverted) - UTF-8 safe
      size_t pos = 0;
      while (pos < item.key.size() && cx < x + width) {
        uint32_t codepoint;
        int byte_len;
        if (utf8_decode_codepoint(item.key, pos, codepoint, byte_len)) {
          Cell c;
          c.content = item.key.substr(pos, byte_len);
          c.fg_color = k_fg;
          c.bg_color = k_bg;
          buffer.set(cx, y, c);
          int dw = char_display_width(codepoint);
          if (dw == 2 && cx + 1 < x + width) {
            Cell skip;
            skip.content = "";
            skip.bg_color = k_bg;
            buffer.set(cx + 1, y, skip);
          }
          cx += (dw > 0 ? dw : 1);
          pos += byte_len;
        } else {
          pos++;
        }
      }
      // Space
      if (cx < x + width) {
        Cell sp;
        sp.content = " ";
        sp.bg_color = bg;
        buffer.set(cx++, y, sp);
      }
      // Draw description - UTF-8 safe
      pos = 0;
      int char_idx = 0;
      while (pos < item.description.size() && cx < x + width) {
        uint32_t codepoint;
        int byte_len;
        if (utf8_decode_codepoint(item.description, pos, codepoint, byte_len)) {
          Cell c;
          c.content = item.description.substr(pos, byte_len);
          c.fg_color = d_fg;
          c.bg_color = bg;

          // Apply styling
          if (!item.styled_description.empty()) {
            if (const TextSpan *span =
                    item.styled_description.get_span_at_char(char_idx)) {
              if (!span->color.is_default) c.fg_color = span->color;
              c.bold = span->bold;
              c.italic = span->italic;
              c.underline = span->underline;
            }
          }

          buffer.set(cx, y, c);
          int dw = char_display_width(codepoint);
          if (dw == 2 && cx + 1 < x + width) {
            Cell skip;
            skip.content = "";
            skip.bg_color = bg;
            buffer.set(cx + 1, y, skip);
          }
          cx += (dw > 0 ? dw : 1);
          pos += byte_len;
          char_idx++;
        } else {
          pos++;
        }
      }
      // Spacing
      for (int s = 0; s < spacing && cx < x + width; ++s) {
        Cell sp;
        sp.content = " ";
        sp.bg_color = bg;
        buffer.set(cx++, y, sp);
      }
    }
  }
};

/// @brief Multi-section status bar
class StatusBar : public Widget {
 public:
  struct Section {
    StyledText styled_content;
    int fixed_width = 0;  // 0 = flexible
    Alignment align = Alignment::Left;
  };

  std::vector<Section> sections;
  std::string separator = " │ ";  // Unicode vertical line (U+2502)

  StatusBar() { fixed_height = 1; }

  void add_section(StyledText content, int width = 0,
                   Alignment align = Alignment::Left) {
    sections.push_back({content, width, align});
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().panel_bg);
    Color fg = fg_color.resolve(Theme::current().foreground);
    Color sep_fg = Theme::current().border;

    // Fill background
    for (int i = 0; i < width; ++i) {
      Cell c;
      c.content = " ";
      c.bg_color = bg;
      buffer.set(x + i, y, c);
    }

    int cx = x;
    auto sep_chars = TextHelper::prepare_text_for_render(separator);
    int sep_width = 0;
    for (const auto &sc : sep_chars) sep_width += sc.display_width;

    for (size_t idx = 0; idx < sections.size(); ++idx) {
      const auto &sec = sections[idx];
      auto chars =
          TextHelper::prepare_text_for_render(sec.styled_content.plain_text());
      int content_display_width = 0;
      for (const auto &ci : chars) content_display_width += ci.display_width;

      int sec_width =
          sec.fixed_width > 0 ? sec.fixed_width : content_display_width;
      if (cx + sec_width > x + width) sec_width = x + width - cx;
      if (sec_width <= 0) break;

      int text_start_dx = 0;
      if (sec.align == Alignment::Center)
        text_start_dx = (sec_width - content_display_width) / 2;
      else if (sec.align == Alignment::Right)
        text_start_dx = sec_width - content_display_width;

      if (text_start_dx < 0) text_start_dx = 0;

      int current_dx = 0;
      for (size_t i = 0; i < chars.size(); ++i) {
        if (text_start_dx + current_dx >= sec_width) break;

        Cell c;
        c.content = chars[i].content;
        c.fg_color = fg;
        c.bg_color = bg;

        // Apply styling
        if (!sec.styled_content.empty()) {
          if (const TextSpan *span =
                  sec.styled_content.get_span_at_char((int)i)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(cx + text_start_dx + current_dx, y, c);
        if (chars[i].display_width == 2 &&
            text_start_dx + current_dx + 1 < sec_width) {
          Cell skip;
          skip.content = "";
          skip.bg_color = bg;
          buffer.set(cx + text_start_dx + current_dx + 1, y, skip);
        }
        current_dx += chars[i].display_width;
      }

      cx += sec_width;

      // Draw separator
      if (idx < sections.size() - 1 && cx + sep_width <= x + width) {
        int sep_cx = 0;
        for (const auto &sc : sep_chars) {
          Cell c;
          c.content = sc.content;
          c.fg_color = sep_fg;
          c.bg_color = bg;
          buffer.set(cx + sep_cx, y, c);
          sep_cx += sc.display_width;
        }
        cx += sep_width;
      }
    }
  }
};

/// @brief Semi-circular gauge/dial progress meter
class Gauge : public Widget {
 public:
  double value = 0.5;  // 0.0 to 1.0
  std::string min_label = "0";
  std::string max_label = "100";
  bool show_value = true;
  std::string value_format = "%d%%";

  Color arc_color = Color();
  Color fill_color = Color();
  Color text_color = Color();

  Gauge(double v = 0.5) : value(v) { fixed_height = 4; }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().background);
    Color arc = arc_color.resolve(Theme::current().border);
    Color fill = fill_color.resolve(Theme::current().primary);
    Color txt = text_color.resolve(Theme::current().foreground);

    if (value < 0) value = 0;
    if (value > 1) value = 1;

    // Simple ASCII arc representation
    int arc_width = std::min(width, 20);
    int center_x = x + width / 2;
    int arc_y = y + 1;

    // Draw arc segments (simplified)
    std::string arc_chars = "╭───────────────────╮";
    int num_segments = arc_width - 2;
    int filled = (int)(value * num_segments);

    // Top arc
    for (int i = 0; i < arc_width && center_x - arc_width / 2 + i < x + width;
         ++i) {
      Cell c;
      c.bg_color = bg;
      int pos = center_x - arc_width / 2 + i;
      if (i == 0)
        c.content = "╭";
      else if (i == arc_width - 1)
        c.content = "╮";
      else {
        c.content = "─";
        c.fg_color = (i - 1 < filled) ? fill : arc;
      }
      if (i == 0 || i == arc_width - 1) c.fg_color = arc;
      buffer.set(pos, arc_y, c);
    }

    // Value display
    if (show_value && height > 2) {
      char val_buf[32];
      snprintf(val_buf, sizeof(val_buf), value_format.c_str(),
               (int)(value * 100));
      std::string val_str = val_buf;
      int val_x = center_x - val_str.size() / 2;
      for (size_t i = 0; i < val_str.size() && val_x + (int)i < x + width;
           ++i) {
        Cell c;
        c.content = std::string(1, val_str[i]);
        c.fg_color = txt;
        c.bg_color = bg;
        buffer.set(val_x + i, arc_y + 1, c);
      }
    }

    // Min/Max labels
    if (height > 3) {
      int label_y = arc_y + 2;
      for (size_t i = 0; i < min_label.size(); ++i) {
        Cell c;
        c.content = std::string(1, min_label[i]);
        c.fg_color = txt;
        c.bg_color = bg;
        buffer.set(center_x - arc_width / 2 + i, label_y, c);
      }
      int max_start = center_x + arc_width / 2 - max_label.size();
      for (size_t i = 0; i < max_label.size(); ++i) {
        Cell c;
        c.content = std::string(1, max_label[i]);
        c.fg_color = txt;
        c.bg_color = bg;
        buffer.set(max_start + i, label_y, c);
      }
    }
  }
};

/// @brief Pie chart for proportional data
class ProportionalBar : public Widget {
 public:
  struct Segment {
    double value;
    std::string label;
    Color color;
  };

  std::vector<Segment> segments;
  bool show_legend = true;
  bool show_percentages = false;

  void add_segment(double val, const std::string &label, cpptui::Color col) {
    segments.push_back({val, label, col});
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().background);

    if (segments.empty()) return;

    double total = 0;
    for (const auto &s : segments) total += s.value;
    if (total <= 0) return;

    // Simple text-based pie representation
    int pie_size = std::min(width - 15, height * 2);
    if (pie_size < 4) pie_size = 4;
    int pie_x = x + 2;
    int pie_y = y + 1;

    // Draw simple bar representation (true circular is complex in text)
    int bar_width = pie_size;
    double acc = 0;
    int bar_y = pie_y;

    for (size_t i = 0; i < segments.size() && bar_y < y + height - 1; ++i) {
      const auto &seg = segments[i];
      double pct = seg.value / total;

      int start_col = (int)(acc * bar_width);
      acc += pct;
      int end_col = (int)(acc * bar_width);
      int seg_width = end_col - start_col;
      if (seg_width < 1) seg_width = 1;

      for (int j = 0; j < seg_width && pie_x + start_col + j < x + width - 15;
           ++j) {
        Cell c;
        c.content = "█";
        c.fg_color = seg.color;
        c.bg_color = bg;
        buffer.set(pie_x + start_col + j, bar_y, c);
      }
    }

    // Legend
    if (show_legend) {
      int legend_x = x + pie_size + 5;
      int legend_y = y;
      for (const auto &seg : segments) {
        if (legend_y >= y + height) break;

        Cell marker;
        marker.content = "■";
        marker.fg_color = seg.color;
        marker.bg_color = bg;
        buffer.set(legend_x, legend_y, marker);

        std::string lbl = " " + seg.label;
        if (show_percentages) {
          char pct[16];
          snprintf(pct, sizeof(pct), " %.1f%%", (seg.value / total) * 100);
          lbl += pct;
        }
        // UTF-8 safe rendering
        size_t pos = 0;
        int cell_x = 0;
        while (pos < lbl.size() && legend_x + 1 + cell_x < x + width) {
          uint32_t codepoint;
          int byte_len;
          if (utf8_decode_codepoint(lbl, pos, codepoint, byte_len)) {
            Cell c;
            c.content = lbl.substr(pos, byte_len);
            c.fg_color = Theme::current().foreground;
            c.bg_color = bg;
            buffer.set(legend_x + 1 + cell_x, legend_y, c);
            int dw = char_display_width(codepoint);
            if (dw == 2 && legend_x + 1 + cell_x + 1 < x + width) {
              Cell skip;
              skip.content = "";
              skip.bg_color = bg;
              buffer.set(legend_x + 1 + cell_x + 1, legend_y, skip);
            }
            cell_x += (dw > 0 ? dw : 1);
            pos += byte_len;
          } else {
            pos++;
          }
        }
        legend_y++;
      }
    }
  }
};

/// @brief Circular Pie and Donut chart widget supporting Braille subpixel
/// rendering and character block fallbacks
class PieChart : public Widget {
 public:
  struct Segment {
    double value;
    std::string label;
    Color color;
  };

  std::vector<Segment> segments;  ///< Proportional chart segments
  bool show_legend = true;        ///< Show the segment labels side legend
  bool show_percentages =
      true;            ///< Display segment share percentage next to labels
  bool donut = false;  ///< Render as a donut chart with a central hole
  double inner_radius_ratio =
      0.4;  ///< Ratio of the donut central hole radius (0.0 to 1.0)
  double aspect_ratio =
      2.0;  ///< Horizontal stretch ratio for character block fallback
  double radius_scale =
      1.0;  ///< Circle radius scaling factor inside widget footprint

  void add_segment(double val, const std::string &label, Color col) {
    segments.push_back({val, label, col});
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().background);
    if (segments.empty()) return;

    double total = 0.0;
    for (const auto &s : segments) total += s.value;
    if (total <= 0.0) return;

    // Define bounds for the circle
    int legend_width = show_legend ? 20 : 0;
    const double PI = 3.14159265358979323846;
    int circle_width = 0;

    bool use_braille = Terminal::has_utf8();

    if (use_braille) {
      int max_radius_vx = (width - legend_width) * 2 / 2 - 2;
      int radius_v = std::min(height * 2 - 2, max_radius_vx);
      if (radius_v < 4) radius_v = 4;

      circle_width = (radius_v / 2) * 2 + 4;  // Bounding box cells
      int virtual_w = circle_width * 2;
      int virtual_h = height * 4;

      int center_vx = virtual_w / 2;
      int center_vy = virtual_h / 2;

      double r_outer = radius_v * radius_scale;
      double r_inner = donut ? r_outer * inner_radius_ratio : 0.0;

      BrailleCanvas bc(circle_width, height);

      // Track segment counts per cell to resolve cell colors
      struct CellColorTracker {
        std::vector<int> counts;
      };
      std::vector<CellColorTracker> color_trackers(circle_width * height);
      for (auto &t : color_trackers) t.counts.assign(segments.size(), 0);

      for (int vy = 0; vy < virtual_h; ++vy) {
        for (int vx = 0; vx < virtual_w; ++vx) {
          double dx = vx - center_vx;
          double dy = vy - center_vy;
          double dist = std::sqrt(dx * dx + dy * dy);

          if (dist <= r_outer && dist >= r_inner) {
            double angle = std::atan2(dy, dx);
            if (angle < 0) angle += 2.0 * PI;

            // Find matching segment
            double acc = 0.0;
            int matched_idx = -1;
            for (size_t i = 0; i < segments.size(); ++i) {
              double seg_start = 2.0 * PI * (acc / total);
              acc += segments[i].value;
              double seg_end = 2.0 * PI * (acc / total);
              if (angle >= seg_start && angle < seg_end) {
                matched_idx = i;
                break;
              }
            }

            if (matched_idx != -1) {
              bc.set_dot(vx, vy);
              int cx = vx / 2;
              int cy = vy / 4;
              if (cx >= 0 && cx < circle_width && cy >= 0 && cy < height) {
                color_trackers[cy * circle_width + cx].counts[matched_idx]++;
              }
            }
          }
        }
      }

      // Blit Braille Canvas to buffer
      for (int cy = 0; cy < height; ++cy) {
        for (int cx = 0; cx < circle_width; ++cx) {
          std::string bchar = bc.get_char(cx, cy);
          if (!bchar.empty() && bchar != " ") {
            // Find dominant segment color in this cell
            int max_count = 0;
            int best_idx = 0;
            const auto &tracker = color_trackers[cy * circle_width + cx];
            for (size_t i = 0; i < segments.size(); ++i) {
              if (tracker.counts[i] > max_count) {
                max_count = tracker.counts[i];
                best_idx = i;
              }
            }

            Cell c;
            c.content = bchar;
            c.fg_color = segments[best_idx].color;
            c.bg_color = bg;
            buffer.set(x + cx, y + cy, c);
          }
        }
      }
    } else {
      // Fallback: Block rendering
      int max_radius_x = (width - legend_width) / 2 - 1;
      int radius_x = std::min(height - 1, max_radius_x);
      if (radius_x < 2) radius_x = 2;

      int center_x = radius_x + 2;
      int center_y = height / 2;

      double r_outer = radius_x * radius_scale;
      double r_inner = donut ? r_outer * inner_radius_ratio : 0.0;
      circle_width = radius_x * 2 + 4;  // Bounding box width

      for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < circle_width; ++sx) {
          double dx = sx - center_x;
          double dy = (sy - center_y) * aspect_ratio;
          double dist = std::sqrt(dx * dx + dy * dy);

          if (dist <= r_outer && dist >= r_inner) {
            double angle = std::atan2(dy, dx);
            if (angle < 0) angle += 2.0 * PI;

            // Find matching segment
            double acc = 0.0;
            int matched_idx = -1;
            for (size_t i = 0; i < segments.size(); ++i) {
              double seg_start = 2.0 * PI * (acc / total);
              acc += segments[i].value;
              double seg_end = 2.0 * PI * (acc / total);
              if (angle >= seg_start && angle < seg_end) {
                matched_idx = i;
                break;
              }
            }

            if (matched_idx != -1) {
              Cell c;
              c.content = "█";
              c.fg_color = segments[matched_idx].color;
              c.bg_color = bg;
              buffer.set(x + sx, y + sy, c);
            }
          }
        }
      }
    }

    // Draw Legend right next to the circle
    if (show_legend && legend_width > 0) {
      int legend_x = x + circle_width + 4;
      int legend_y = y + (height - (int)segments.size()) / 2;
      if (legend_y < y) legend_y = y;

      for (const auto &seg : segments) {
        if (legend_y >= y + height) break;

        Cell marker;
        marker.content = "■";
        marker.fg_color = seg.color;
        marker.bg_color = bg;
        buffer.set(legend_x, legend_y, marker);

        std::string lbl = " " + seg.label;
        if (show_percentages) {
          char pct[16];
          snprintf(pct, sizeof(pct), " %.1f%%", (seg.value / total) * 100);
          lbl += pct;
        }

        // Render label text safely
        int cell_x = 0;
        size_t pos = 0;
        while (pos < lbl.size() && legend_x + 1 + cell_x < x + width) {
          uint32_t codepoint;
          int byte_len;
          if (utf8_decode_codepoint(lbl, pos, codepoint, byte_len)) {
            Cell c;
            c.content = lbl.substr(pos, byte_len);
            c.fg_color = Theme::current().foreground;
            c.bg_color = bg;
            buffer.set(legend_x + 1 + cell_x, legend_y, c);
            int dw = char_display_width(codepoint);
            cell_x += (dw > 0 ? dw : 1);
            pos += byte_len;
          } else {
            pos++;
          }
        }
        legend_y++;
      }
    }
  }
};

/// @brief Box and Whisker Plot for statistical distributions
class BoxWhiskerPlot : public Widget {
 public:
  double min_val = 0.0;     ///< Minimum data value
  double q1_val = 0.0;      ///< First quartile (25th percentile) value
  double median_val = 0.0;  ///< Median (50th percentile) value
  double q3_val = 0.0;      ///< Third quartile (75th percentile) value
  double max_val = 0.0;     ///< Maximum data value

  double range_min = 0.0;  ///< Fixed scale minimum when auto_scale is false
  double range_max = 1.0;  ///< Fixed scale maximum when auto_scale is false
  bool auto_scale = true;  ///< Auto-scale plot bounds based on min/max data

  Color box_color =
      Color();  ///< Custom color for the IQR box (default primary)
  Color whisker_color =
      Color();  ///< Custom color for the whiskers and caps (default foreground)
  Color median_color =
      Color();  ///< Custom color for the median line (default secondary)

  std::string label;  ///< Optional descriptive label shown next to the plot

  BoxWhiskerPlot() { fixed_height = 3; }

  /// @brief Set the statistical data points for the box-whisker plot
  /// @param min Minimum value
  /// @param q1 First quartile value
  /// @param med Median value
  /// @param q3 Third quartile value
  /// @param max Maximum value
  void set_data(double min, double q1, double med, double q3, double max) {
    min_val = min;
    q1_val = q1;
    median_val = med;
    q3_val = q3;
    max_val = max;
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().background);
    Color box_col = box_color.is_default ? Theme::current().primary : box_color;
    Color whisker_col =
        whisker_color.is_default ? Theme::current().foreground : whisker_color;
    Color median_col =
        median_color.is_default ? Theme::current().secondary : median_color;

    double actual_min = range_min;
    double actual_max = range_max;
    if (auto_scale) {
      actual_min = min_val;
      actual_max = max_val;
    }

    if (actual_max == actual_min) {
      actual_max += 1.0;
      actual_min -= 1.0;
    }

    int left_offset = label.empty() ? 2 : (int)label.size() + 2;
    int plot_width = width - left_offset - 2;
    if (plot_width < 10) return;

    int draw_x = x + left_offset;
    int draw_y = y;

    auto map_val = [&](double val) -> int {
      double norm = (val - actual_min) / (actual_max - actual_min);
      if (norm < 0) norm = 0;
      if (norm > 1) norm = 1;
      return draw_x + (int)std::round(norm * (plot_width - 1));
    };

    int c_min = map_val(min_val);
    int c_q1 = map_val(q1_val);
    int c_med = map_val(median_val);
    int c_q3 = map_val(q3_val);
    int c_max = map_val(max_val);

    // 1. Draw Label
    if (!label.empty()) {
      int label_x = x;
      int label_y = y + 1;
      for (size_t i = 0; i < label.size() && label_x + (int)i < draw_x; ++i) {
        Cell c;
        c.content = std::string(1, label[i]);
        c.fg_color = Theme::current().foreground;
        c.bg_color = bg;
        buffer.set(label_x + i, label_y, c);
      }
    }

    // 2. Draw Top of Box
    if (c_q3 > c_q1) {
      buffer.set(c_q1, draw_y, {"┌", box_col, bg});
      buffer.set(c_q3, draw_y, {"┐", box_col, bg});
      for (int col = c_q1 + 1; col < c_q3; ++col) {
        buffer.set(col, draw_y, {"─", box_col, bg});
      }
    }

    // 3. Draw Bottom of Box
    if (c_q3 > c_q1) {
      buffer.set(c_q1, draw_y + 2, {"└", box_col, bg});
      buffer.set(c_q3, draw_y + 2, {"┘", box_col, bg});
      for (int col = c_q1 + 1; col < c_q3; ++col) {
        buffer.set(col, draw_y + 2, {"─", box_col, bg});
      }
    }

    // 4. Draw Middle Row (Whiskers, Median, Box Interior)
    // Draw whiskers
    for (int col = c_min; col < c_q1; ++col) {
      buffer.set(col, draw_y + 1, {"─", whisker_col, bg});
    }
    for (int col = c_q3 + 1; col <= c_max; ++col) {
      buffer.set(col, draw_y + 1, {"─", whisker_col, bg});
    }
    // Whisker caps
    buffer.set(c_min, draw_y + 1, {"│", whisker_col, bg});
    buffer.set(c_max, draw_y + 1, {"│", whisker_col, bg});

    // Box sides
    buffer.set(c_q1, draw_y + 1, {"│", box_col, bg});
    buffer.set(c_q3, draw_y + 1, {"│", box_col, bg});

    // Box interior shading
    for (int col = c_q1 + 1; col < c_q3; ++col) {
      if (col == c_med) continue;
      buffer.set(col, draw_y + 1, {"░", box_col, bg});
    }

    // Median line
    buffer.set(c_med, draw_y + 1, {"┃", median_col, bg});
  }
};

/// @brief Collapsible content sections
class Accordion : public Container {
 public:
  struct Section {
    StyledText title;
    std::shared_ptr<Widget> content;
    bool expanded = false;
  };

  std::vector<Section> sections;
  bool allow_multiple = false;
  int selected_index = 0;

  Color header_bg = Color();
  Color header_fg = Color();
  std::string expand_icon = "▼";
  std::string collapse_icon = "▶";

  Accordion() { focusable = true; }

  void add_section(const StyledText &title, std::shared_ptr<Widget> content,
                   bool expanded = false) {
    sections.push_back({title, content, expanded});
    children_.push_back(content);
  }

  void toggle(int idx) {
    if (idx < 0 || idx >= (int)sections.size()) return;
    if (!allow_multiple) {
      for (auto &s : sections) s.expanded = false;
    }
    sections[idx].expanded = !sections[idx].expanded;
  }

  void layout() override {
    int cy = y;
    for (size_t i = 0; i < sections.size(); ++i) {
      auto &sec = sections[i];
      cy++;  // Header line
      if (sec.expanded && sec.content) {
        sec.content->x = x + 1;
        sec.content->y = cy;
        sec.content->width = width - 2;
        int content_h =
            sec.content->fixed_height > 0 ? sec.content->fixed_height : 3;
        sec.content->height = content_h;
        sec.content->visible = true;
        if (auto cont = std::dynamic_pointer_cast<Container>(sec.content)) {
          cont->layout();
        }
        cy += content_h;
      } else if (sec.content) {
        sec.content->visible = false;
      }
    }
  }

  void render(Buffer &buffer) override {
    Color h_bg = header_bg.resolve(Theme::current().panel_bg);
    Color h_fg = header_fg.resolve(Theme::current().foreground);

    int cy = y;
    for (size_t i = 0; i < sections.size(); ++i) {
      auto &sec = sections[i];
      bool is_sel = ((int)i == selected_index) && focused_;

      // Draw header
      Color eff_bg = is_sel ? Theme::current().selection : h_bg;
      for (int j = 0; j < width; ++j) {
        Cell c;
        c.content = " ";
        c.bg_color = eff_bg;
        buffer.set(x + j, cy, c);
      }

      std::string icon = sec.expanded ? expand_icon : collapse_icon;
      Cell ic;
      ic.content = icon;
      ic.fg_color = h_fg;
      ic.bg_color = eff_bg;
      buffer.set(x + 1, cy, ic);

      // UTF-8 safe title rendering with styling
      auto title_chars =
          TextHelper::prepare_text_for_render(sec.title.plain_text());
      int cell_x = 0;
      for (size_t j = 0; j < title_chars.size(); ++j) {
        if (x + 3 + cell_x >= x + width) break;
        const auto &ci = title_chars[j];
        Cell c;
        c.content = ci.content;
        c.fg_color = h_fg;
        c.bg_color = eff_bg;

        // Apply styling
        if (!sec.title.empty()) {
          if (const TextSpan *span = sec.title.get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(x + 3 + cell_x, cy, c);
        if (ci.display_width == 2 && x + 3 + cell_x + 1 < x + width) {
          Cell skip;
          skip.content = "";
          skip.bg_color = eff_bg;
          buffer.set(x + 3 + cell_x + 1, cy, skip);
        }
        cell_x += ci.display_width;
      }
      cy++;

      // Draw content if expanded
      if (sec.expanded && sec.content && sec.content->visible) {
        sec.content->render(buffer);
        cy += sec.content->height;
      }
    }
  }

  bool on_event(const Event &event) override {
    // Mouse click support - detect clicks on section headers
    if (event.is_mouse_event() && event.button == 0) {
      // Check if click is within accordion bounds
      if (event.x >= x && event.x < x + width && event.y >= y) {
        // Calculate which row was clicked and map to section header
        int cy = y;
        for (size_t i = 0; i < sections.size(); ++i) {
          auto &sec = sections[i];
          // Check if click is on this section's header row
          if (event.y == cy) {
            selected_index = i;
            toggle(i);
            layout();
            return true;
          }
          cy++;  // Move past header
          // Skip content area if expanded
          if (sec.expanded && sec.content) {
            cy += sec.content->height;
          }
        }
      }
    }

    if (event.is_key_event() && focused_) {
      if (event.is_nav_up()) {  // Up
        if (selected_index > 0) selected_index--;
        return true;
      }
      if (event.is_nav_down()) {  // Down
        if (selected_index < (int)sections.size() - 1) selected_index++;
        return true;
      }
      if (event.is_activate()) {  // Space/Enter
        toggle(selected_index);
        layout();
        return true;
      }
    }

    // Pass to expanded content
    for (auto &sec : sections) {
      if (sec.expanded && sec.content && sec.content->on_event(event))
        return true;
    }
    return false;
  }
};

/// @brief Navigation breadcrumb trail
class Breadcrumb : public Widget {
 public:
  struct Item {
    StyledText label;
    std::function<void()> on_click;
  };

  std::vector<Item> items;
  std::string separator = " > ";
  int hover_index = -1;
  int selected_index = 0;

  Color separator_color = Color();
  Color item_color = Color();
  Color current_color = Color();

  Breadcrumb() {
    fixed_height = 1;
    focusable = true;
    tab_stop = true;
  }

  void add(StyledText label, std::function<void()> on_click = nullptr) {
    items.push_back({label, on_click});
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().background);
    Color sep = separator_color.resolve(Theme::current().border);
    Color item = item_color.resolve(Theme::current().primary);
    Color curr = current_color.resolve(Theme::current().foreground);

    int cx = x;
    for (size_t i = 0; i < items.size(); ++i) {
      bool is_last = (i == items.size() - 1);
      bool is_hover = ((int)i == hover_index);
      bool is_selected = focused_ && ((int)i == selected_index);
      Color fg =
          is_last ? curr
                  : (is_hover || is_selected ? Theme::current().primary : item);
      Color item_bg = is_selected ? Theme::current().selection : bg;

      // UTF-8 safe label rendering
      StyledText styled_lbl = items[i].label;
      auto text_chars =
          TextHelper::prepare_text_for_render(styled_lbl.plain_text());
      for (size_t j = 0; j < text_chars.size(); ++j) {
        if (cx >= x + width) break;
        const auto &ci = text_chars[j];
        Cell c;
        c.content = ci.content;
        c.fg_color = fg;
        c.bg_color = item_bg;
        c.underline = (is_hover || is_selected) && !is_last;

        // Apply styling
        if (!styled_lbl.empty()) {
          if (const TextSpan *span = styled_lbl.get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline |= span->underline;
          }
        }

        buffer.set(cx, y, c);
        if (ci.display_width == 2 && cx + 1 < x + width) {
          Cell skip;
          skip.content = "";
          skip.bg_color = item_bg;
          buffer.set(cx + 1, y, skip);
        }
        cx += ci.display_width;
      }

      // UTF-8 safe separator rendering
      if (!is_last && cx + utf8_display_width(separator) <= x + width) {
        size_t sep_pos = 0;
        while (sep_pos < separator.size()) {
          uint32_t codepoint;
          int byte_len;
          if (utf8_decode_codepoint(separator, sep_pos, codepoint, byte_len)) {
            Cell c;
            c.content = separator.substr(sep_pos, byte_len);
            c.fg_color = sep;
            c.bg_color = bg;
            buffer.set(cx, y, c);

            int dw = char_display_width(codepoint);
            if (dw == 2 && cx + 1 < x + width) {
              Cell skip;
              skip.content = "";
              skip.bg_color = bg;
              buffer.set(cx + 1, y, skip);
            }
            cx += (dw > 0 ? dw : 1);
            sep_pos += byte_len;
          } else {
            sep_pos++;
          }
        }
      }
    }
  }

  bool on_event(const Event &event) override {
    // Keyboard navigation when focused
    if (event.is_key_event() && focused_ && !items.empty()) {
      if (event.is_nav_left()) {  // Left arrow
        if (selected_index > 0) selected_index--;
        return true;
      }
      if (event.is_nav_right()) {  // Right arrow
        if (selected_index < (int)items.size() - 1) selected_index++;
        return true;
      }
      if (event.is_activate()) {  // Space/Enter
        if (selected_index >= 0 && selected_index < (int)items.size()) {
          if (items[selected_index].on_click) {
            items[selected_index].on_click();
            return true;
          }
        }
      }
    }

    if (event.is_mouse_event() && contains(event.x, event.y)) {
      // Find which item was clicked
      int cx = x;
      for (size_t i = 0; i < items.size(); ++i) {
        int item_end = cx + utf8_display_width(items[i].label);
        if (event.x >= cx && event.x < item_end) {
          hover_index = i;
          selected_index = i;
          if (event.mouse_left() && items[i].on_click) {
            items[i].on_click();
            return true;
          }
          return false;
        }
        cx = item_end + utf8_display_width(separator);
      }
    }
    if (!contains(event.x, event.y)) hover_index = -1;
    return false;
  }
};

/// @brief 2D color intensity grid (heatmap)
class Heatmap : public Widget {
 public:
  std::vector<std::vector<double>> data;  // 2D data grid, values 0-1
  std::vector<StyledText> row_labels;
  std::vector<StyledText> col_labels;
  bool show_values = false;

  Color low_color = Color(0, 0, 128);
  Color high_color = Color(255, 0, 0);

  // Tooltip Support
  bool show_tooltip = false;
  int tooltip_duration_ms = 1000;
  std::chrono::steady_clock::time_point last_hit_time_;
  std::function<std::string(int, int, double)> tooltip_formatter;

  struct HeatmapHit {
    int x, y;
    int width, height;
    int row_index;
    int col_index;
    double value;
  };
  mutable std::vector<HeatmapHit> heatmap_hits_;

  bool on_event(const Event &event) override {
    if (!show_tooltip) return false;

    if (event.is_mouse_event()) {
      if (event.mouse_wheel()) return false;
      bool hit_found = false;

      if (contains(event.x, event.y)) {
        for (const auto &hit : heatmap_hits_) {
          if (event.x >= hit.x && event.x < hit.x + hit.width &&
              event.y >= hit.y && event.y < hit.y + hit.height) {
            std::string text;
            if (tooltip_formatter) {
              text = tooltip_formatter(hit.row_index, hit.col_index, hit.value);
            } else {
              std::stringstream ss;
              ss << std::fixed << std::setprecision(2) << hit.value;
              std::string r_lbl = (hit.row_index < (int)row_labels.size())
                                      ? row_labels[hit.row_index].plain_text()
                                      : std::to_string(hit.row_index);
              std::string c_lbl = (hit.col_index < (int)col_labels.size())
                                      ? col_labels[hit.col_index].plain_text()
                                      : std::to_string(hit.col_index);
              text = r_lbl + ", " + c_lbl + ": " + ss.str();
            }

            if (!tooltip_) tooltip_ = std::make_shared<Tooltip>();

            tooltip_->text = text;
            tooltip_->position = Tooltip::Position::Manual;
            tooltip_->manual_x = event.x;
            tooltip_->manual_y = event.y - 1;
            tooltip_->visible = true;

            hit_found = true;
            last_hit_time_ = std::chrono::steady_clock::now();
            return true;
          }
        }
      }

      if (!hit_found) {
        if (tooltip_) {
          if (tooltip_->contains(event.x, event.y)) {
            last_hit_time_ = std::chrono::steady_clock::now();
          } else {
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_hit_time_)
                    .count();
            if (elapsed >= tooltip_duration_ms) {
              tooltip_ = nullptr;
              return true;  // Request redraw
            }
          }
        }
      }
    }
    return false;
  }

  void set_data(const std::vector<std::vector<double>> &d) { data = d; }

  void render(Buffer &buffer) override {
    if (show_tooltip) heatmap_hits_.clear();
    Color bg = bg_color.resolve(Theme::current().background);

    if (data.empty()) return;
    int rows = data.size();
    int cols = data[0].size();

    int label_width = 0;
    for (const auto &lbl : row_labels) {
      int w = utf8_display_width(lbl.plain_text());
      if (w > label_width) label_width = w;
    }
    label_width = std::min(label_width + 1, 10);

    int cell_w = std::max(1, (width - label_width) / std::max(1, cols));
    int cell_h = std::max(1, (height - 1) / std::max(1, rows));

    for (int r = 0; r < rows && y + r * cell_h < y + height; ++r) {
      // Row label - UTF-8 safe
      if (r < (int)row_labels.size()) {
        StyledText styled_lbl = row_labels[r];
        auto text_chars =
            TextHelper::prepare_text_for_render(styled_lbl.plain_text());
        int cell_x = 0;
        for (size_t j = 0; j < text_chars.size(); ++j) {
          if (cell_x >= label_width) break;
          const auto &ci = text_chars[j];
          Cell c;
          c.content = ci.content;
          c.fg_color = Theme::current().foreground;
          c.bg_color = bg;

          // Apply styling
          if (!styled_lbl.empty()) {
            if (const TextSpan *span = styled_lbl.get_span_at_char((int)j)) {
              if (!span->color.is_default) c.fg_color = span->color;
              c.bold = span->bold;
              c.italic = span->italic;
              c.underline = span->underline;
            }
          }

          buffer.set(x + cell_x, y + r * cell_h, c);
          if (ci.display_width == 2 && cell_x + 1 < label_width) {
            Cell skip;
            skip.content = "";
            skip.bg_color = bg;
            buffer.set(x + cell_x + 1, y + r * cell_h, skip);
          }
          cell_x += ci.display_width;
        }
      }

      for (int c_idx = 0;
           c_idx < cols && x + label_width + c_idx * cell_w < x + width;
           ++c_idx) {
        double val = data[r][c_idx];
        if (val < 0) val = 0;
        if (val > 1) val = 1;

        if (show_tooltip) {
          heatmap_hits_.push_back({x + label_width + c_idx * cell_w,
                                   y + r * cell_h, cell_w, cell_h, r, c_idx,
                                   val});
        }

        // Interpolate color
        uint8_t cr =
            (uint8_t)(low_color.r + val * (high_color.r - low_color.r));
        uint8_t cg =
            (uint8_t)(low_color.g + val * (high_color.g - low_color.g));
        uint8_t cb =
            (uint8_t)(low_color.b + val * (high_color.b - low_color.b));
        Color cell_color(cr, cg, cb);

        for (int cy = 0; cy < cell_h; ++cy) {
          for (int cx = 0; cx < cell_w; ++cx) {
            Cell cell;
            cell.content = show_values && cy == 0 && cx == 0
                               ? std::to_string((int)(val * 100)).substr(0, 2)
                               : " ";
            cell.bg_color = cell_color;
            cell.fg_color = Color::White();
            buffer.set(x + label_width + c_idx * cell_w + cx,
                       y + r * cell_h + cy, cell);
          }
        }
      }
    }
  }
};

/// @brief Resizable split pane container
class SplitPane : public Container {
 public:
  bool vertical = false;  // false = horizontal split (left|right)
  double ratio = 0.5;     // 0-1, position of divider
  int min_size1 = 5;
  int min_size2 = 5;

  std::shared_ptr<Widget> pane1;
  std::shared_ptr<Widget> pane2;

  Color divider_color = Color();
  std::string divider_char_h = "│";
  std::string divider_char_v = "─";

  bool dragging_ = false;

  void set_panes(std::shared_ptr<Widget> p1, std::shared_ptr<Widget> p2) {
    pane1 = p1;
    pane2 = p2;
    children_.clear();
    if (p1) children_.push_back(p1);
    if (p2) children_.push_back(p2);
  }

  void layout() override {
    if (!pane1 || !pane2) return;

    if (!vertical) {
      int div_x = x + (int)(ratio * width);
      div_x =
          std::max(x + min_size1, std::min(x + width - min_size2 - 1, div_x));

      pane1->x = x;
      pane1->y = y;
      pane1->width = clamp_size(div_x - x, pane1->min_width, pane1->max_width);
      pane1->height =
          pane1->fixed_height > 0
              ? pane1->fixed_height
              : clamp_size(height, pane1->min_height, pane1->max_height);

      pane2->x = div_x + 1;
      pane2->y = y;
      pane2->width =
          clamp_size(x + width - div_x - 1, pane2->min_width, pane2->max_width);
      pane2->height =
          pane2->fixed_height > 0
              ? pane2->fixed_height
              : clamp_size(height, pane2->min_height, pane2->max_height);
    } else {
      int div_y = y + (int)(ratio * height);
      div_y =
          std::max(y + min_size1, std::min(y + height - min_size2 - 1, div_y));

      pane1->x = x;
      pane1->y = y;
      pane1->width =
          pane1->fixed_width > 0
              ? pane1->fixed_width
              : clamp_size(width, pane1->min_width, pane1->max_width);
      pane1->height =
          clamp_size(div_y - y, pane1->min_height, pane1->max_height);

      pane2->x = x;
      pane2->y = div_y + 1;
      pane2->width =
          pane2->fixed_width > 0
              ? pane2->fixed_width
              : clamp_size(width, pane2->min_width, pane2->max_width);
      pane2->height = clamp_size(y + height - div_y - 1, pane2->min_height,
                                 pane2->max_height);
    }

    if (auto c = std::dynamic_pointer_cast<Container>(pane1)) c->layout();
    if (auto c = std::dynamic_pointer_cast<Container>(pane2)) c->layout();
  }

  void render(Buffer &buffer) override {
    layout();  // Ensure panes are properly sized before rendering

    // Clear entire SplitPane area to prevent ghost content
    Color bg = Theme::current().background;
    for (int dy = 0; dy < height; ++dy) {
      for (int dx = 0; dx < width; ++dx) {
        Cell c;
        c.content = " ";
        c.bg_color = bg;
        buffer.set(x + dx, y + dy, c);
      }
    }

    Color div = divider_color.resolve(Theme::current().border);

    if (pane1) pane1->render(buffer);
    if (pane2) pane2->render(buffer);

    // Draw divider - use pane dimensions which are already clamped by min_size
    if (!vertical) {
      int div_x = pane1->x + pane1->width;  // Divider is right after pane1
      for (int i = 0; i < height; ++i) {
        Cell c;
        c.content = divider_char_h;
        c.fg_color = div;
        c.bg_color = Theme::current().background;
        buffer.set(div_x, y + i, c);
      }
    } else {
      int div_y = pane1->y + pane1->height;  // Divider is below pane1
      for (int i = 0; i < width; ++i) {
        Cell c;
        c.content = divider_char_v;
        c.fg_color = div;
        c.bg_color = Theme::current().background;
        buffer.set(x + i, div_y, c);
      }
    }
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      // Use the same clamped divider position as layout(), so that event
      // handling matches where the visual divider actually renders. Otherwise
      // clicks near the unclamped position are consumed as "near_divider" even
      // though they're visually inside a child pane (causing dead zones where
      // children don't receive events).
      int div_pos =
          !vertical
              ? std::max(x + min_size1, std::min(x + width - min_size2 - 1,
                                                 x + (int)(ratio * width)))
              : std::max(y + min_size1, std::min(y + height - min_size2 - 1,
                                                 y + (int)(ratio * height)));
      int mouse_pos = !vertical ? event.x : event.y;
      int other_pos = !vertical ? event.y : event.x;
      int other_start = !vertical ? y : x;
      int other_end = !vertical ? y + height : x + width;

      // Check if mouse is within bounds of the splitpane
      bool in_bounds = other_pos >= other_start && other_pos < other_end;
      bool near_divider = mouse_pos == div_pos;

      if (event.mouse_left() && near_divider && in_bounds) {
        dragging_ = true;
        return true;
      }
      if (event.mouse_release()) {
        dragging_ = false;
      }
      if (dragging_ &&
          (event.mouse_drag() || event.mouse_left() || event.mouse_motion())) {
        if (!vertical) {
          ratio = (double)(event.x - x) / width;
        } else {
          ratio = (double)(event.y - y) / height;
        }
        ratio = std::max(0.1, std::min(0.9, ratio));
        layout();
        return true;
      }
    }

    if (pane1 && pane1->on_event(event)) return true;
    if (pane2 && pane2->on_event(event)) return true;
    return false;
  }
};

/// @brief Month-view calendar widget with button navigation
class Calendar : public Container {
 public:
  int year = 2024;
  int month = 1;  // 1-12
  int selected_day = 1;
  std::vector<int> highlighted_days;

  std::function<void(int, int, int)> on_select;  // year, month, day

  Color header_color = Color();
  Color day_color = Color();
  Color selected_color = Color();
  Color highlight_color = Color();

  // Border options
  bool show_border = false;
  Color border_color = Color();

  std::shared_ptr<Button> prev_btn;
  std::shared_ptr<Button> next_btn;
  std::shared_ptr<Widget> grid_focus;  // Focusable element for day grid
  bool day_grid_focused_ = false;

  /// @brief Calculate the number of rows needed for the day grid
  int calculate_rows() const {
    int start_dow = first_weekday();
    int num_days = days_in_month();
    int total_cells = start_dow + num_days;
    return (total_cells + 6) / 7;  // ceil division
  }

  Calendar() {
    focusable = false;  // Buttons take focus, not the container
    tab_stop = false;
    fixed_width = 22;
    year = 2024;
    month = 12;
    selected_day = 19;
    // Height will be dynamically adjusted in layout()

    // Create navigation buttons
    prev_btn = std::make_shared<Button>("<", [this]() {
      if (--month < 1) {
        month = 12;
        year--;
      }
      selected_day = std::min(selected_day, days_in_month());
      update_header();
    });
    prev_btn->fixed_width = 1;
    prev_btn->fixed_height = 1;

    next_btn = std::make_shared<Button>(">", [this]() {
      if (++month > 12) {
        month = 1;
        year++;
      }
      selected_day = std::min(selected_day, days_in_month());
      update_header();
    });
    next_btn->fixed_width = 1;
    next_btn->fixed_height = 1;

    // Create focusable element for day grid (sized to cover grid area)
    grid_focus = std::make_shared<Static>("");
    grid_focus->focusable = true;
    grid_focus->tab_stop = true;

    add(prev_btn);
    add(next_btn);
    add(grid_focus);
  }

  void on_focus() override {
    // When Calendar gets focus via container, delegate to first button
  }

  void on_blur() override { day_grid_focused_ = false; }

  int days_in_month() const {
    int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int d = days[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
      d = 29;
    return d;
  }

  int first_weekday() const {
    int m = month, y = year;
    if (m < 3) {
      m += 12;
      y--;
    }
    int k = y % 100, j = y / 100;
    int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
  }

  void update_header() {
    // Update button labels if needed
  }

  void update_responsive() override {
    int num_rows = calculate_rows();
    int content_height = 2 + num_rows;
    fixed_height = show_border ? content_height + 2 : content_height;
    fixed_width = show_border ? 23 : 21;
    Container::update_responsive();
  }

  void layout() override {
    // Calculate dynamic height: header + day names + grid rows + optional
    // border
    int num_rows = calculate_rows();
    int content_height = 2 + num_rows;  // header + day names + grid
    fixed_height = show_border ? content_height + 2 : content_height;
    fixed_width = show_border ? 23 : 21;  // grid is 21, border adds 2

    // Border offset
    int bx = show_border ? 1 : 0;
    int by = show_border ? 1 : 0;

    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char month_str[16];
    snprintf(month_str, sizeof(month_str), "%s %d", months[month - 1], year);
    int month_len = strlen(month_str);
    int grid_width = 21;            // 7 days * 3 chars
    int hdr_width = month_len + 4;  // "< month_str >"
    int hdr_x = x + bx + (grid_width - hdr_width) / 2;

    // Position prev button
    prev_btn->x = hdr_x;
    prev_btn->y = y + by;
    prev_btn->width = 1;
    prev_btn->height = 1;

    // Position next button
    next_btn->x = hdr_x + 2 + month_len + 1;
    next_btn->y = y + by;
    next_btn->width = 1;
    next_btn->height = 1;

    // Position grid_focus to cover the day grid area
    grid_focus->x = x + bx;
    grid_focus->y = y + by + 2;
    grid_focus->width = 21;  // 7 days * 3 chars
    grid_focus->height = num_rows;
  }

  void render(Buffer &buffer) override {
    layout();

    Color bg = bg_color.resolve(Theme::current().background);
    Color hdr = header_color.resolve(Theme::current().primary);
    Color day_c = day_color.resolve(Theme::current().foreground);
    Color sel = selected_color.resolve(Theme::current().selection);
    Color hlt = highlight_color.resolve(Theme::current().secondary);
    Color bdr = border_color.resolve(Theme::current().border);

    // Border offset
    int bx = show_border ? 1 : 0;
    int by = show_border ? 1 : 0;
    int num_rows = calculate_rows();
    int content_h = 2 + num_rows;
    int content_w = 21;

    // Draw border if enabled
    if (show_border) {
      int total_w = content_w + 2;
      int total_h = content_h + 2;

      for (int row_idx = 0; row_idx < total_h; ++row_idx) {
        for (int col_idx = 0; col_idx < total_w; ++col_idx) {
          Cell c;
          c.bg_color = bg;
          c.fg_color = bdr;

          if (row_idx == 0 && col_idx == 0)
            c.content = "┌";
          else if (row_idx == 0 && col_idx == total_w - 1)
            c.content = "┐";
          else if (row_idx == total_h - 1 && col_idx == 0)
            c.content = "└";
          else if (row_idx == total_h - 1 && col_idx == total_w - 1)
            c.content = "┘";
          else if (row_idx == 0 || row_idx == total_h - 1)
            c.content = "─";
          else if (col_idx == 0 || col_idx == total_w - 1)
            c.content = "│";
          else
            continue;  // Interior cells are rendered separately

          buffer.set(x + col_idx, y + row_idx, c);
        }
      }
    }

    // Render buttons
    prev_btn->render(buffer);
    next_btn->render(buffer);

    // Month and year label
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char month_str[16];
    snprintf(month_str, sizeof(month_str), "%s %d", months[month - 1], year);
    int month_len = strlen(month_str);
    int label_x = prev_btn->x + 2;

    for (int i = 0; i < month_len; ++i) {
      Cell c;
      c.content = std::string(1, month_str[i]);
      c.fg_color = hdr;
      c.bg_color = bg;
      buffer.set(label_x + i, y + by, c);
    }

    // Day headers
    const char *days[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    for (int i = 0; i < 7; ++i) {
      Cell c1;
      c1.content = std::string(1, days[i][0]);
      c1.fg_color = hdr;
      c1.bg_color = bg;
      Cell c2;
      c2.content = std::string(1, days[i][1]);
      c2.fg_color = hdr;
      c2.bg_color = bg;
      buffer.set(x + bx + i * 3, y + by + 1, c1);
      buffer.set(x + bx + i * 3 + 1, y + by + 1, c2);
    }

    // Days grid
    int start_dow = first_weekday();
    int num_days = days_in_month();
    int day = 1;

    for (int row = 0; row < num_rows && day <= num_days; ++row) {
      for (int col = 0; col < 7 && day <= num_days; ++col) {
        if (row == 0 && col < start_dow) continue;

        bool is_sel = (day == selected_day) && grid_focus->has_focus();
        bool is_hlt =
            std::find(highlighted_days.begin(), highlighted_days.end(), day) !=
            highlighted_days.end();

        char day_str[4];
        snprintf(day_str, sizeof(day_str), "%2d", day);

        Color fg = is_sel ? Color::White() : (is_hlt ? hlt : day_c);
        Color cell_bg = is_sel ? sel : bg;

        for (int i = 0; i < 2; ++i) {
          Cell c;
          c.content = std::string(1, day_str[i]);
          c.fg_color = fg;
          c.bg_color = cell_bg;
          buffer.set(x + bx + col * 3 + i, y + by + 2 + row, c);
        }
        day++;
      }
    }
  }

  bool on_event(const Event &event) override {
    // Let buttons handle their events first
    if (prev_btn->contains(event.x, event.y) && event.is_mouse_event()) {
      return prev_btn->on_event(event);
    }
    if (next_btn->contains(event.x, event.y) && event.is_mouse_event()) {
      return next_btn->on_event(event);
    }

    // Handle keyboard events for day navigation (when grid is focused)
    if (event.is_key_event() && grid_focus->has_focus()) {
      int num_days = days_in_month();
      if (event.is_nav_left()) {  // Left
        if (selected_day > 1) selected_day--;
        return true;
      }
      if (event.is_nav_right()) {  // Right
        if (selected_day < num_days) selected_day++;
        return true;
      }
      if (event.is_nav_up()) {  // Up
        if (selected_day > 7) selected_day -= 7;
        return true;
      }
      if (event.is_nav_down()) {  // Down
        if (selected_day + 7 <= num_days) selected_day += 7;
        return true;
      }
      if (event.is_enter()) {  // Enter
        if (on_select) on_select(year, month, selected_day);
        return true;
      }
    }

    // Mouse click on days grid
    if (event.is_mouse_event() && event.mouse_left()) {
      int bx = show_border ? 1 : 0;
      int by = show_border ? 1 : 0;
      int mx = event.x - x - bx;
      int my = event.y - y - by;
      int num_rows = calculate_rows();

      if (my >= 2 && my < 2 + num_rows) {
        int col = mx / 3;
        int row = my - 2;

        if (col >= 0 && col < 7 && mx >= 0) {
          int start_dow = first_weekday();
          int num_days = days_in_month();
          int cell_index = row * 7 + col;
          int clicked_day = cell_index - start_dow + 1;

          if (clicked_day >= 1 && clicked_day <= num_days) {
            selected_day = clicked_day;
            grid_focus->set_focus(true);
            if (on_select) on_select(year, month, selected_day);
            return true;
          }
        }
      }
    }

    return false;
  }

  bool has_focus_within() const override {
    return grid_focus->has_focus() || prev_btn->has_focus() ||
           next_btn->has_focus();
  }
};

/// @brief Input with autocomplete suggestions

/// @brief Tooltip popup for contextual help

/// @brief Toast notification that auto-dismisses
class Notification : public Widget {
 public:
  enum class Type { Info, Success, Warning, Error };
  enum class Position {
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft,
    TopCenter,
    BottomCenter
  };

  struct Message {
    StyledText styled_text;
    std::string text;
    Type type;
    int duration_ms;
    std::chrono::steady_clock::time_point created;
  };

  std::vector<Message> queue;
  int max_visible = 3;
  Position position = Position::TopRight;

  // Selection and Interaction Support
  std::vector<Rect> toast_bounds;
  SelectionState selection_state_;
  int selected_msg_index = -1;
  bool hovered_on_toast = false;

  /**
   * @brief Override hit_test to be transparent when no toasts are visible.
   * This prevents empty notification widgets from blocking interaction with
   * widgets behind them.
   */
  bool hit_test(int px, int py) const override {
    if (queue.empty()) return false;

    // Check if point is inside any visible toast
    for (const auto &bk : toast_bounds) {
      if (bk.contains(px, py)) return true;
    }
    return false;
  }

  void show(StyledText text, Type type = Type::Info, int duration_ms = 3000) {
    queue.push_back({text, text.plain_text(), type, duration_ms,
                     std::chrono::steady_clock::now()});
  }

  void update() {
    // If mouse is hovering over any toast, do NOT expire messages
    if (hovered_on_toast) {
      // Update created time to "pause" the timer
      // Effectively we slide the creation window forward so duration doesn't
      // run out
      auto now = std::chrono::steady_clock::now();
      for (auto &m : queue) {
        // This is a simple trick: if we are hovering, we just keep bumping the
        // created time or we could track "paused" state. A simpler approach for
        // the user experience is: If hovered, extend lifetime.
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - m.created)
                           .count();
        if (elapsed >= m.duration_ms - 100) {  // If about to expire
          m.created = now - std::chrono::milliseconds(
                                m.duration_ms - 500);  // Give it 500ms more
        }
      }
      return;
    }

    auto now = std::chrono::steady_clock::now();
    queue.erase(std::remove_if(
                    queue.begin(), queue.end(),
                    [&](const Message &m) {
                      auto elapsed =
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - m.created)
                              .count();
                      return elapsed >= m.duration_ms;
                    }),
                queue.end());

    // If queue changed and selection was active on a removed item, clear
    // selection
    if (selected_msg_index >= (int)queue.size()) {
      selected_msg_index = -1;
      selection_state_.clear();
    }
  }

  bool on_event(const Event &event) override {
    // Update hover state
    if (event.is_mouse_event()) {
      hovered_on_toast = hit_test(event.x, event.y);

      if (hovered_on_toast) {
        // Find which toast we are on
        int hit_index = -1;
        // toast_bounds corresponds to the currently rendered visible messages.
        // The render loop iterates: i=0 to count-1.
        // toast_bounds are pushed in order.
        // However, queue logic in render is: queue[queue.size() - count + i]
        // We need to map visual index back to queue index.

        int count = std::min((int)queue.size(), max_visible);
        for (size_t i = 0; i < toast_bounds.size(); ++i) {
          if (toast_bounds[i].contains(event.x, event.y)) {
            hit_index = (int)(queue.size() - count + i);
            break;
          }
        }

        if (event.mouse_left()) {
          if (hit_index != -1) {
            if (hit_index != selected_msg_index) {
              selected_msg_index = hit_index;
              selection_state_.clear();
            }

            // Handle click for selection
            // Calculate local X relative to text start
            // Text starts at toast_x + 4
            int rx =
                event.x -
                (toast_bounds[selected_msg_index - (queue.size() - count)].x +
                 4);

            auto &msg = queue[hit_index];
            auto chars = prepare_text_for_render(msg.text);
            int char_idx = TextHelper::visual_to_char_pos(chars, rx);

            selection_state_.handle_mouse_press(chars, char_idx);
            return true;
          }
        } else if (event.mouse_drag() && selection_state_.mouse_down &&
                   hit_index == selected_msg_index) {
          // Handle drag
          int rx =
              event.x -
              (toast_bounds[selected_msg_index - (queue.size() - count)].x + 4);
          auto &msg = queue[hit_index];
          auto chars = prepare_text_for_render(msg.text);
          // Clamp visual pos
          int char_idx = TextHelper::visual_to_char_pos(chars, rx);
          selection_state_.handle_mouse_drag(char_idx);
          return true;
        }
      }

      if (event.mouse_release()) {
        if (selection_state_.handle_mouse_release()) return true;
      }
    } else if (event.is_key_event()) {
      if (event.is_copy() && selection_state_.has_selection() &&
          selected_msg_index != -1 && selected_msg_index < (int)queue.size()) {
        const auto &msg = queue[selected_msg_index];
        std::string sel_text = TextHelper::utf8_substr(
            msg.text, selection_state_.start,
            selection_state_.end - selection_state_.start);
        copy_to_clipboard(sel_text);
        return true;
      }
    }

    return false;
  }

  void render(Buffer &buffer) override {
    update();

    toast_bounds.clear();  // Reset visual bounds frame

    if (queue.empty()) return;

    // Bypass parent clipping - notifications render at screen-absolute
    // coordinates
    buffer.push_full_clip();

    Color bg_info = Theme::current().panel_bg;
    Color bg_success = Theme::current().success;
    Color bg_warning = Theme::current().warning;
    Color bg_error = Theme::current().error;
    Color sel_bg = Theme::current().selection;
    Color sel_fg = Color::White();

    int toast_w = 40;
    int toast_h = 1;

    // Use buffer dimensions for absolute screen positioning
    int screen_w = buffer.width();
    int screen_h = buffer.height();

    int count = std::min((int)queue.size(), max_visible);
    for (int i = 0; i < count; ++i) {
      int msg_idx = queue.size() - count + i;
      const auto &msg = queue[msg_idx];

      Color bg;
      switch (msg.type) {
        case Type::Info:
          bg = bg_info;
          break;
        case Type::Success:
          bg = bg_success;
          break;
        case Type::Warning:
          bg = bg_warning;
          break;
        case Type::Error:
          bg = bg_error;
          break;
      }

      Color fg = Color::contrast_color(bg);

      // Calculate position based on setting
      int toast_x, toast_y;
      switch (position) {
        case Position::TopRight:
          toast_x = screen_w - toast_w - 1;
          toast_y = 1 + i;
          break;
        case Position::TopLeft:
          toast_x = 1;
          toast_y = 1 + i;
          break;
        case Position::BottomRight:
          toast_x = screen_w - toast_w - 1;
          toast_y = screen_h - count + i;
          break;
        case Position::BottomLeft:
          toast_x = 1;
          toast_y = screen_h - count + i;
          break;
        case Position::TopCenter:
          toast_x = (screen_w - toast_w) / 2;
          toast_y = 1 + i;
          break;
        case Position::BottomCenter:
          toast_x = (screen_w - toast_w) / 2;
          toast_y = screen_h - count + i;
          break;
      }

      // Store bounds
      Rect r;
      r.x = toast_x;
      r.y = toast_y;
      r.width = toast_w;
      r.height = toast_h;
      toast_bounds.push_back(r);

      // Background
      for (int j = 0; j < toast_w; ++j) {
        Cell c;
        c.content = " ";
        c.bg_color = bg;
        buffer.set(toast_x + j, toast_y, c);
      }

      // Icon
      std::string icon;
      switch (msg.type) {
        case Type::Info:
          icon = "ℹ";
          break;
        case Type::Success:
          icon = "✓";
          break;
        case Type::Warning:
          icon = "⚠";
          break;
        case Type::Error:
          icon = "✗";
          break;
      }
      Cell ic;
      ic.content = icon;
      ic.fg_color = fg;
      ic.bg_color = bg;
      buffer.set(toast_x + 1, toast_y, ic);

      // Handle wide icons (e.g. checkmark) by clearing the next cell
      uint32_t icon_cp;
      int icon_len;
      if (utf8_decode_codepoint(icon, 0, icon_cp, icon_len)) {
        if (char_display_width(icon_cp) == 2) {
          Cell skip;
          skip.content = " ";  // Use space instead of empty string for safety
          skip.bg_color = bg;
          buffer.set(toast_x + 2, toast_y, skip);
        }
      }

      // Text rendering using standardized helper to match selection logic
      auto text_chars = prepare_text_for_render(msg.text);
      int cell_x = 0;
      int max_text_w = toast_w - 5;
      bool is_msg_selected = (msg_idx == selected_msg_index);

      for (int char_idx = 0; char_idx < (int)text_chars.size(); ++char_idx) {
        const auto &ci = text_chars[char_idx];

        if (cell_x >= max_text_w) break;

        bool selected =
            is_msg_selected && selection_state_.is_selected(char_idx);

        Cell c;
        c.content = ci.content;
        c.fg_color = selected ? sel_fg : fg;
        c.bg_color = selected ? sel_bg : bg;

        // Apply styling
        if (!selected && !msg.styled_text.empty()) {
          if (const TextSpan *span =
                  msg.styled_text.get_span_at_char(char_idx)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(toast_x + 4 + cell_x, toast_y, c);

        if (ci.display_width == 2) {
          if (cell_x + 1 < max_text_w) {
            Cell skip;
            skip.content =
                "";  // Skip cell for text still uses empty to avoid overwriting
            skip.bg_color = selected ? sel_bg : bg;
            buffer.set(toast_x + 4 + cell_x + 1, toast_y, skip);
          }
        }

        cell_x += (ci.display_width > 0 ? ci.display_width : 1);
      }
    }

    // Restore clipping context
    buffer.pop_clip();
  }
};

// --- Carousel ---

/// @brief Displays one child (page) at a time
class Carousel : public Container {
  /// @brief Internal helper for carousel pagination dots
  class PaginationDots : public Widget {
   public:
    Carousel *parent;
    PaginationDots(Carousel *p) : parent(p) { focusable = true; }

    void render(Buffer &buffer) override {
      if (!parent || !parent->show_dots || parent->children_.empty()) return;

      int n_dots = parent->children_.size();
      int spacing = 3;

      // Use parent's focus state so dots reflect focus anywhere in carousel
      Color active =
          parent->active_dot_color.is_default
              ? (parent->has_focus_within() ? Theme::current().primary
                                            : Theme::current().secondary)
              : parent->active_dot_color;
      Color inactive = parent->dot_color.resolve(Theme::current().border);

      for (int i = 0; i < n_dots; ++i) {
        Cell c;
        bool is_active = (i == parent->current_index);
        c.fg_color = is_active ? active : inactive;

        // Transparency: Inherit background from buffer (where Carousel content
        // was already rendered)
        int sx = x + i * spacing;
        if (sx >= 0 && sx < buffer.width() && y >= 0 && y < buffer.height()) {
          c.bg_color = buffer.get(sx, y).bg_color;
        } else {
          c.bg_color = Theme::current().background;
        }

        c.content = parent->dot_char;
        buffer.set(sx, y, c);
      }
    }

    bool on_event(const Event &event) override {
      if (!parent) return false;
      if (event.is_mouse_event()) {
        // Strict bounds check + ignore drags
        bool hit = (event.x >= x && event.x < x + width && event.y >= y &&
                    event.y < y + height);
        if (!hit) return false;

        if (event.mouse_left() && !event.mouse_drag()) {
          int dx = event.x - x;
          int spacing = 3;
          int idx = dx / spacing;
          if (idx >= 0 && idx < (int)parent->children_.size()) {
            parent->current_index = idx;
            return true;
          }
        }
      }
      if (focused_ && event.is_key_event()) {
        if (event.is_nav_left()) {
          parent->prev();
          return true;
        }  // Left Arrow
        if (event.is_nav_right()) {
          parent->next();
          return true;
        }  // Right Arrow
        if (event.key == '[' || event.key == '{') {
          parent->prev();
          return true;
        }
        if (event.key == ']' || event.key == '}') {
          parent->next();
          return true;
        }
      }
      return false;
    }
  };

 public:
  int current_index = 0;

  Color active_dot_color = {0, 0, 0, true};
  Color dot_color = {0, 0, 0, true};
  std::string dot_char = "●";  // Standard Circle (Rounder)

  // Options
  bool show_dots = true;
  bool show_arrows = false;

  Carousel() {
    focusable = true;
    tab_stop = false;  // Page content and arrows are the tab stops
    prev_btn = std::make_shared<Button>("<", [this]() { prev(); });
    next_btn = std::make_shared<Button>(">", [this]() { next(); });
    dots_widget = std::make_shared<PaginationDots>(this);
  }

  void add_page(std::shared_ptr<Widget> widget) { add(widget); }

  void next() {
    if (children_.empty()) return;
    current_index = (current_index + 1) % children_.size();
  }

  void prev() {
    if (children_.empty()) return;
    current_index = (current_index - 1 + children_.size()) % children_.size();
  }

  const std::vector<std::shared_ptr<Widget>> &get_children() const override {
    // Update combined list for focus/event dispatch
    combined_children_.clear();

    // 1. Current Page
    if (!children_.empty() && current_index < (int)children_.size()) {
      combined_children_.push_back(children_[current_index]);
    }

    // 2. Navigation Controls
    if (show_arrows) combined_children_.push_back(prev_btn);
    if (show_dots) combined_children_.push_back(dots_widget);
    if (show_arrows) combined_children_.push_back(next_btn);

    return combined_children_;
  }

  void layout() override {
    // Layout only the current child to fill the carousel
    if (children_.empty()) return;
    if (current_index >= (int)children_.size()) current_index = 0;

    auto &child = children_[current_index];
    child->update_responsive();
    child->x = x;
    child->y = y;
    child->width = child->fixed_width > 0 ? child->fixed_width : width;

    int content_height = height;
    // Reserve 3 lines for clean footer: [Space, Controls, Space]
    if (children_.size() > 1 && (show_dots || show_arrows) &&
        content_height > 3) {
      content_height -= 3;
    }

    child->height =
        child->fixed_height > 0 ? child->fixed_height : content_height;

    if (auto cont = std::dynamic_pointer_cast<Container>(child)) {
      cont->layout();
    }

    // Layout Footer Controls
    if (show_dots || show_arrows) {
      int center_x = x + width / 2;
      int spacing = 3;
      int n_dots = children_.size();
      int dots_width = n_dots * spacing;
      int start_x = center_x - (dots_width) / 2;
      int dots_y = y + height - 2;

      if (show_dots) {
        dots_widget->x = start_x;
        dots_widget->y = dots_y;
        dots_widget->width = dots_width;
        dots_widget->height = 1;
        dots_widget->visible = true;
      } else {
        dots_widget->visible = false;
      }

      if (show_arrows) {
        prev_btn->x = start_x - 3;
        prev_btn->y = dots_y;
        prev_btn->width = 1;
        prev_btn->height = 1;
        prev_btn->visible = true;

        next_btn->x = start_x + (n_dots * spacing) - spacing + 3;
        next_btn->y = dots_y;
        next_btn->width = 1;
        next_btn->height = 1;
        next_btn->visible = true;
      } else {
        prev_btn->visible = false;
        next_btn->visible = false;
      }
    }
  }

  void render(Buffer &buffer) override {
    if (children_.empty()) return;
    // Only render current
    if (current_index >= (int)children_.size()) current_index = 0;
    children_[current_index]->render(buffer);

    if (children_.size() <= 1) return;
    if (!show_dots && !show_arrows) return;

    if (show_dots) dots_widget->render(buffer);

    if (show_arrows) {
      prev_btn->render(buffer);
      next_btn->render(buffer);
    }
  }

  // Input
  bool on_event(const Event &event) override {
    if (children_.empty()) return false;

    // 1. Pass to footer controls
    if (show_arrows) {
      if (prev_btn->on_event(event)) return true;
      if (next_btn->on_event(event)) return true;
    }
    if (show_dots) {
      if (dots_widget->on_event(event)) return true;
    }

    // Keyboard Navigation (if this container has focus directly)
    if (focused_ && event.is_key_event()) {
      if (event.is_nav_left()) {  // Left Arrow
        prev();
        return true;
      }
      if (event.is_nav_right()) {  // Right Arrow
        next();
        return true;
      }
      if (event.key == '[' || event.key == '{') {
        prev();
        return true;
      }
      if (event.key == ']' || event.key == '}') {
        next();
        return true;
      }
    }

    // 2. Pass to current child
    bool consumed = children_[current_index]->on_event(event);
    if (consumed) return true;

    return false;
  }

 private:
  std::shared_ptr<Button> prev_btn;
  std::shared_ptr<Button> next_btn;
  std::shared_ptr<PaginationDots> dots_widget;
  mutable std::vector<std::shared_ptr<Widget>> combined_children_;
};

// --- Tabs ---
/// @brief Tabbed interface container with focusable tab buttons
class Tabs : public Container {
 public:
  std::vector<StyledText> tab_names;  ///< Labels for each tab
  int current_tab = 0;                ///< Index of the currently active tab

  // Overflow Handling
  int scroll_offset_ = 0;  ///< Horizontal scroll offset for tab buttons
  bool show_nav_buttons_ =
      false;  ///< Show navigation buttons (< >) if tabs overflow

  // Customization
  std::string prev_marker = "<";  ///< Label for previous button
  std::string next_marker = ">";  ///< Label for next button

  // Callback when tab changes
  std::function<void(int)>
      on_change;  ///< Callback fired when active tab changes

  Tabs() {
    focusable = true;
    tab_stop = false;  // Tab buttons are the tab stops

    // Create overflow navigation buttons
    prev_btn_ = std::make_shared<Button>(prev_marker, [this]() {
      if (scroll_offset_ > 0) scroll_offset_--;
    });
    next_btn_ = std::make_shared<Button>(next_marker, [this]() {
      if (scroll_offset_ < (int)tab_names.size() - 1) scroll_offset_++;
    });
  }

  // Add a tab with a name and content widget
  void add_tab(StyledText name, std::shared_ptr<Widget> content) {
    tab_names.push_back(name);
    add(content);

    // Create a button for this tab
    int tab_idx = tab_names.size() - 1;
    auto btn = std::make_shared<Button>(
        name, [this, tab_idx]() { select_tab(tab_idx); });
    btn->focusable = true;
    btn->tab_stop = true;
    tab_buttons_.push_back(btn);
  }

  void select_tab(int index) {
    if (index >= 0 && index < (int)children_.size()) {
      int old_tab = current_tab;
      current_tab = index;
      // Ensure the active tab is within visibility bounds
      if (current_tab < scroll_offset_) {
        scroll_offset_ = current_tab;
      }
      // Update button states
      update_button_states();
      if (old_tab != current_tab) {
        if (on_change) on_change(current_tab);
      }
    }
  }

  void set_tab(int index) { select_tab(index); }

  void next_tab() {
    if (children_.empty()) return;
    select_tab((current_tab + 1) % children_.size());
  }

  void prev_tab() {
    if (children_.empty()) return;
    select_tab((current_tab - 1 + children_.size()) % children_.size());
  }

  const std::vector<std::shared_ptr<Widget>> &get_children() const override {
    // Update combined list for focus/event dispatch
    combined_children_.clear();

    // 1. Overflow prev button (if visible)
    if (show_nav_buttons_ && prev_btn_->visible) {
      combined_children_.push_back(prev_btn_);
    }

    // 2. Visible tab buttons
    for (auto &btn : tab_buttons_) {
      if (btn->visible) {
        combined_children_.push_back(btn);
      }
    }

    // 3. Overflow next button (if visible)
    if (show_nav_buttons_ && next_btn_->visible) {
      combined_children_.push_back(next_btn_);
    }

    // 4. Current tab content
    if (!children_.empty() && current_tab < (int)children_.size()) {
      combined_children_.push_back(children_[current_tab]);
    }

    return combined_children_;
  }

  void layout() override {
    if (children_.empty()) return;
    if (current_tab >= (int)children_.size()) current_tab = 0;

    int header_height = 2;

    // Calculate total width needed
    int total_needed = 0;
    for (const auto &name : tab_names) {
      total_needed += utf8_display_width(name.plain_text()) + 2 +
                      1;  // " " + name + " " + gap
    }

    int max_w = width;
    if (max_w < 5) max_w = 5;

    bool overflow = total_needed > max_w;
    show_nav_buttons_ = overflow;

    // Layout tab buttons in header row
    int btn_x = x;
    int btn_y = y;

    if (overflow) {
      // Layout prev button
      prev_btn_->x = x;
      prev_btn_->y = btn_y;
      prev_btn_->width = utf8_display_width(prev_marker);
      prev_btn_->height = 1;
      prev_btn_->visible = true;
      btn_x += prev_btn_->width;

      // Layout next button
      next_btn_->width = utf8_display_width(next_marker);
      next_btn_->height = 1;
      next_btn_->x = x + width - next_btn_->width;
      next_btn_->y = btn_y;
      next_btn_->visible = true;
    } else {
      prev_btn_->visible = false;
      next_btn_->visible = false;
    }

    int max_btn_x = x + width - (overflow ? next_btn_->width : 0);
    int visible_start = overflow ? scroll_offset_ : 0;

    // Layout visible tab buttons
    for (size_t i = 0; i < tab_buttons_.size(); ++i) {
      auto &btn = tab_buttons_[i];
      int btn_w = utf8_display_width(tab_names[i]) + 2;  // " " + name + " "

      if ((int)i < visible_start) {
        btn->visible = false;
        continue;
      }

      if (btn_x + btn_w > max_btn_x) {
        btn->visible = false;
        continue;
      }

      btn->x = btn_x;
      btn->y = btn_y;
      btn->width = btn_w;
      btn->height = 1;
      btn->visible = true;

      btn_x += btn_w + 1;  // +1 for gap
    }

    update_button_states();

    // Layout content children
    for (size_t i = 0; i < children_.size(); ++i) {
      auto &child = children_[i];
      child->update_responsive();
      if ((int)i == current_tab) {
        child->visible = true;
        child->x = x;
        child->y = y + header_height;
        child->width = width;
        child->height = height > header_height ? height - header_height : 0;

        if (auto cont = std::dynamic_pointer_cast<Container>(child)) {
          cont->layout();
        }
      } else {
        child->visible = false;
        child->width = 0;
        child->height = 0;
      }
    }
  }

  void render(Buffer &buffer) override {
    Color active_bg = Theme::current().panel_bg;
    Color inactive_bg = Theme::current().background;

    // Calculate Active Tab Geometry (for the connector line)
    int active_tab_start = -1;
    int active_tab_end = -1;

    // Render tab buttons
    for (size_t i = 0; i < tab_buttons_.size(); ++i) {
      auto &btn = tab_buttons_[i];
      if (!btn->visible) continue;

      bool is_active = ((int)i == current_tab);

      if (is_active) {
        active_tab_start = btn->x;
        active_tab_end = btn->x + btn->width;
        btn->bg_color = active_bg;
        btn->text_color = Theme::current().foreground;
      } else {
        btn->bg_color = inactive_bg;
        btn->text_color = Theme::current().border;
      }

      btn->render(buffer);
    }

    // Render overflow buttons
    if (show_nav_buttons_) {
      prev_btn_->bg_color = inactive_bg;
      next_btn_->bg_color = inactive_bg;
      prev_btn_->render(buffer);
      next_btn_->render(buffer);
    }

    // Divider line
    for (int i = 0; i < width; ++i) {
      int screen_x = x + i;
      bool is_under_active =
          (active_tab_start != -1) &&
          (screen_x >= active_tab_start && screen_x < active_tab_end);

      Cell c;
      if (is_under_active) {
        c.content = " ";
        c.fg_color = Theme::current().foreground;
        c.bg_color = inactive_bg;
      } else {
        c.content = "─";
        c.fg_color = Theme::current().border;
        c.bg_color = inactive_bg;
      }
      buffer.set(screen_x, y + 1, c);
    }

    // Render Content
    if (children_.empty()) return;
    if (current_tab >= (int)children_.size()) current_tab = 0;

    // Clear content area
    Color clear_bg = Theme::current().background;
    int content_y = y + 2;
    int content_h = height - 2;
    for (int dy = 0; dy < content_h; ++dy) {
      for (int dx = 0; dx < width; ++dx) {
        Cell c;
        c.content = " ";
        c.bg_color = clear_bg;
        buffer.set(x + dx, content_y + dy, c);
      }
    }

    children_[current_tab]->render(buffer);
  }

  bool on_event(const Event &event) override {
    // Pass events to navigation buttons first
    if (show_nav_buttons_) {
      if (prev_btn_->on_event(event)) return true;
      if (next_btn_->on_event(event)) return true;
    }

    // Pass events to visible tab buttons
    for (auto &btn : tab_buttons_) {
      if (btn->visible && btn->on_event(event)) return true;
    }

    // Keyboard navigation when any tab button is focused
    if (event.is_key_event()) {
      bool any_tab_focused = false;
      int focused_idx = -1;
      for (size_t i = 0; i < tab_buttons_.size(); ++i) {
        if (tab_buttons_[i]->has_focus()) {
          any_tab_focused = true;
          focused_idx = i;
          break;
        }
      }

      if (any_tab_focused) {
        // Left/Right arrow to switch focus between tabs
        if (event.is_nav_left()) {  // Left Arrow
          if (focused_idx > 0) {
            tab_buttons_[focused_idx]->set_focus(false);
            tab_buttons_[focused_idx - 1]->set_focus(true);
            // Ensure visible
            if (focused_idx - 1 < scroll_offset_)
              scroll_offset_ = focused_idx - 1;
            return true;
          }
        }
        if (event.is_nav_right()) {  // Right Arrow
          if (focused_idx < (int)tab_buttons_.size() - 1) {
            tab_buttons_[focused_idx]->set_focus(false);
            tab_buttons_[focused_idx + 1]->set_focus(true);
            return true;
          }
        }
      }

      // Global tab switching shortcuts
      if (event.key == '[' || event.key == 'p') {
        prev_tab();
        return true;
      }
      if (event.key == ']' || event.key == 'n' || event.key == '/') {
        next_tab();
        return true;
      }
    }

    // Pass to current child content
    if (!children_.empty() && current_tab < (int)children_.size()) {
      return children_[current_tab]->on_event(event);
    }
    return false;
  }

 private:
  std::vector<std::shared_ptr<Button>> tab_buttons_;
  std::shared_ptr<Button> prev_btn_;
  std::shared_ptr<Button> next_btn_;
  mutable std::vector<std::shared_ptr<Widget>> combined_children_;

  void update_button_states() {
    // Update visual state of buttons based on current selection
    for (size_t i = 0; i < tab_buttons_.size(); ++i) {
      bool is_active = ((int)i == current_tab);
      if (is_active) {
        tab_buttons_[i]->bg_color = Theme::current().panel_bg;
      } else {
        tab_buttons_[i]->bg_color = Theme::current().background;
      }
    }
  }
};

/// @brief Displays tabular data with pagination controls
class TablePaginated : public Container {
 public:
  std::vector<StyledText> columns;            ///< Column headers
  std::vector<std::vector<StyledText>> rows;  ///< Data rows

  int page_size = 5;  ///< Number of rows per page
  bool auto_page_size =
      false;             ///< If true, calculate page size based on height
  int current_page = 0;  ///< Current page index (0-based)

  // Helper to add a row of strings
  void add_row(const std::vector<std::string> &row) {
    std::vector<StyledText> styled_row;
    styled_row.reserve(row.size());
    for (const auto &s : row) styled_row.emplace_back(s);
    rows.push_back(std::move(styled_row));
  }

  // Visuals
  Color header_bg_color = Color();  ///< Header background color
  Color header_fg_color = Color();  ///< Header text color

  Color selected_bg_color = Color();  ///< Selected row background
  Color selected_fg_color = Color();  ///< Selected row foreground

  Color row_color_a = Color();  // Very Light Grey
  Color row_color_b = Color();  // Slightly Darker

  Color border_color = Color();  // Dark Grey Borders

  bool alternate_colors = true;
  int selected_row_ = 0;  // Global selection index
  int hovered_row_ = -1;  // Hover state

  // Interactive Controls
  std::shared_ptr<Button> btn_first;
  std::shared_ptr<Button> btn_prev;
  std::shared_ptr<Button> btn_next;
  std::shared_ptr<Button> btn_last;
  std::shared_ptr<Input> input_page;
  std::shared_ptr<Label> label_static;  // " of X "

  TablePaginated() {
    focusable = true;  // Allow focus for row selection (blurs when other
                       // widgets clicked)
    tab_stop = true;   // Allow tabbing to table for row navigation

    // Initialize Controls
    btn_first = std::make_shared<Button>("<<", [this]() {
      current_page = 0;
      update_input();
    });
    btn_prev = std::make_shared<Button>("<", [this]() {
      if (current_page > 0) current_page--;
      update_input();
    });

    input_page = std::make_shared<Input>();
    input_page->width = 4;

    btn_next = std::make_shared<Button>(">", [this]() {
      int ps = get_page_size();
      int total_pages = (rows.size() + ps - 1) / ps;
      if (current_page < total_pages - 1) current_page++;
      update_input();
    });

    btn_last = std::make_shared<Button>(">>", [this]() {
      int ps = get_page_size();
      int total_pages = (rows.size() + ps - 1) / ps;
      if (total_pages > 0) current_page = total_pages - 1;
      update_input();
    });

    // Default buttons are dark (CoolGreyDark), which contrasts well with light
    // table.

    label_static = std::make_shared<Label>(" of 1 ", Color{100, 100, 100});

    // Register children for tab navigation
    add(btn_first);
    add(btn_prev);
    add(input_page);
    add(btn_next);
    add(btn_last);
  }

  int get_page_size() const {
    if (auto_page_size) {
      // Height - Header(1) - HeaderSep(1) - FooterSep(1) - FooterControls(1) =
      // Height - 4
      int ps = height - 4;
      if (ps < 1) ps = 1;
      return ps;
    }
    return page_size;
  }

  void layout() override {
    // Footer controls are positioned in render() based on current dimensions
    // No pre-layout needed here
  }

  void update_input() {
    input_page->set_text(std::to_string(current_page + 1));
  }

  void next_page() {
    int ps = get_page_size();
    int total_pages = (rows.size() + ps - 1) / ps;
    if (current_page < total_pages - 1) current_page++;
    update_input();
  }

  void prev_page() {
    if (current_page > 0) current_page--;
    update_input();
  }

  bool on_event(const Event &event) override {
    // 1. Mouse Interaction
    if (event.is_mouse_event()) {
      // Focus Management on Click
      if (event.mouse_left()) {
        // Check Input first for focus
        if (input_page->on_event(event)) {
          input_page->set_focus(true);
          return true;
        } else {
          input_page->set_focus(false);
        }
      }

      // Dispatch to children (Handles Hover and Clicks)
      // We pass the event to everyone to ensure they update their state (like
      // hover)

      // Input (Hover/Interaction)
      if (input_page->on_event(event)) return true;

      // Scroll Wheel -> Move Selection and flip page if needed
      if (event.mouse_wheel()) {
        // Check Bounds
        if (event.x >= x && event.x < x + width && event.y >= y &&
            event.y < y + height) {
          int ps = get_page_size();
          int total_rows = (int)rows.size();

          if (event.mouse_wheel_up()) {  // Up
            if (selected_row_ > 0) {
              selected_row_--;
              if (selected_row_ < current_page * ps) prev_page();
              return true;
            }
          } else if (event.mouse_wheel_down()) {  // Down
            if (selected_row_ < total_rows - 1) {
              selected_row_++;
              if (selected_row_ >= (current_page + 1) * ps) next_page();
              return true;
            }
          }
        }
      }

      // Buttons - blur table when button clicked
      if (btn_first->on_event(event)) {
        set_focus(false);
        return true;
      }
      if (btn_prev->on_event(event)) {
        set_focus(false);
        return true;
      }
      if (btn_next->on_event(event)) {
        set_focus(false);
        return true;
      }
      if (btn_last->on_event(event)) {
        set_focus(false);
        return true;
      }

      // Consume background clicks to prevent fallthrough
      if (event.x >= x && event.x < x + width && event.y >= y &&
          event.y < y + height) {
        if (event.y > y && event.y < y + height - 2) {  // Valid row area
          int clicked_row_vis = event.y - (y + 1);      // -1 for header
          int ps = get_page_size();
          int hover_abs = current_page * ps + clicked_row_vis;
          if (event.mouse_move() || event.mouse_drag() ||
              event.mouse_motion()) {
            // Hover Update
            if (clicked_row_vis >= 0 && clicked_row_vis < ps &&
                hover_abs < (int)rows.size()) {
              hovered_row_ = hover_abs;
            } else {
              hovered_row_ = -1;
            }
          } else if (event.mouse_left() && clicked_row_vis >= 0 &&
                     clicked_row_vis < ps) {
            int abs_idx = current_page * ps + clicked_row_vis;
            if (abs_idx < (int)rows.size()) {
              selected_row_ = abs_idx;
              set_focus(true);  // Focus table to highlight header
              return true;      // Selection changed
            }
          }
        } else {
          hovered_row_ = -1;
        }

        if (event.mouse_move() || event.mouse_drag() || event.mouse_motion())
          return true;  // Consume motion

        if (event.mouse_left()) return true;  // Consume valid click area
      } else {
        if (event.mouse_move() || event.mouse_drag() || event.mouse_motion()) {
          if (hovered_row_ != -1) {
            hovered_row_ = -1;
            return true;
          }
        }
      }
    }

    // 2. Input Logic (if focused)
    if (event.is_key_event() && input_page->has_focus()) {
      if (input_page->on_event(event)) {
        std::string txt = input_page->get_value();
        try {
          int p = std::stoi(txt);
          int ps = get_page_size();
          int total_pages = (rows.size() + ps - 1) / ps;
          if (p >= 1 && p <= total_pages) current_page = p - 1;
        } catch (...) {
        }
        return true;
      }
    }

    // 3. Hotkeys navigation (only if input NOT focused)
    if (event.is_key_event() && !input_page->has_focus()) {
      if (event.key == 'n') {
        next_page();
        return true;
      }
      if (event.key == 'p') {
        prev_page();
        return true;
      }

      int ps = get_page_size();
      int total_rows = (int)rows.size();

      if (event.is_nav_up()) {  // Up
        if (selected_row_ > 0) {
          selected_row_--;
          // Adjust page if needed
          if (selected_row_ < current_page * ps) {
            prev_page();
          }
        }
        if (focused_) return true;  // Consume event when focused
      }
      if (event.is_nav_down()) {  // Down
        if (selected_row_ < total_rows - 1) {
          selected_row_++;
          // Adjust page if needed
          if (selected_row_ >= (current_page + 1) * ps) {
            next_page();
          }
        }
        if (focused_) return true;  // Consume event when focused
      }
      if (event.is_view_scroll_up()) {  // PageUp (1001 often PGUP in this lib)
        if (current_page > 0) {
          prev_page();
          selected_row_ = std::max(0, selected_row_ - ps);
        } else {
          selected_row_ = 0;
        }
        return true;
      }
      if (event.is_view_scroll_down()) {  // PageDown (1002 often PGDN)
        int total_pages = (total_rows + ps - 1) / ps;
        if (current_page < total_pages - 1) {
          next_page();
          selected_row_ = std::min(total_rows - 1, selected_row_ + ps);
        } else {
          selected_row_ = total_rows - 1;
        }
        return true;
      }
      if (event.is_nav_home()) {  // Home
        selected_row_ = 0;
        current_page = 0;
        update_input();
        return true;
      }
      if (event.is_nav_end()) {  // End
        selected_row_ = total_rows - 1;
        int total_pages = (total_rows + ps - 1) / ps;
        current_page = total_pages > 0 ? total_pages - 1 : 0;
        update_input();
        return true;
      }
    }
    return false;
  }

  void render(Buffer &buffer) override {
    if (columns.empty()) return;

    int ps = get_page_size();
    // Clamp current page if resize changed page count
    int total_pages = (rows.size() + ps - 1) / ps;
    if (total_pages < 1) total_pages = 1;
    if (current_page >= total_pages) current_page = total_pages - 1;

    // Resolve Defaults - use table headers from theme
    Color hbg = header_bg_color.resolve(Theme::current().table_header_bg);
    Color hfg = header_fg_color.resolve(Theme::current().table_header_fg);
    // Focus indicator: use TABLE HEADER FOCUS color when table or any child
    // control has focus
    bool any_child_focused = btn_first->has_focus() || btn_prev->has_focus() ||
                             input_page->has_focus() || btn_next->has_focus() ||
                             btn_last->has_focus();
    if (focused_ || any_child_focused) {
      hbg = Theme::current().table_header_bg_focus;
      hfg = Theme::current().table_header_fg_focus;
    }
    Color r_a = row_color_a.resolve(Theme::current().background);
    Color r_b = row_color_b.resolve(Theme::current().panel_bg);

    int current_x_offset = x;

    // 1. Calculate Column Widths
    int num_cols = columns.size();
    // Calculate Column Widths
    // Simple approach: Divide width equally.
    int col_width = width / num_cols;

    // 2. Render Header
    int draw_y = y;
    current_x_offset = x;

    for (int i = 0; i < num_cols; ++i) {
      // Last column takes remainder
      int current_cw = col_width;
      if (i == num_cols - 1) current_cw = width - (current_x_offset - x);

      StyledText styled_text = columns[i];
      auto text_chars =
          TextHelper::prepare_text_for_render(styled_text.plain_text());
      int text_dw = utf8_display_width(styled_text.plain_text());
      int padding = (current_cw - text_dw) / 2;
      if (padding < 0) padding = 0;

      // Fill with spaces first
      for (int k = 0; k < current_cw; ++k) {
        Cell c;
        c.fg_color = hfg;
        c.bg_color = hbg;
        c.content = " ";
        buffer.set(current_x_offset + k, draw_y, c);
      }

      int cell_x = 0;
      for (size_t j = 0; j < text_chars.size(); ++j) {
        if (padding + cell_x >= current_cw) break;
        const auto &ci = text_chars[j];
        Cell c;
        c.fg_color = hfg;
        c.bg_color = hbg;
        c.content = ci.content;

        // Apply styling
        if (!styled_text.empty()) {
          if (const TextSpan *span = styled_text.get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(current_x_offset + padding + cell_x, draw_y, c);
        if (ci.display_width == 2 && padding + cell_x + 1 < current_cw) {
          Cell skip;
          skip.content = "";
          skip.bg_color = hbg;
          buffer.set(current_x_offset + padding + cell_x + 1, draw_y, skip);
        }
        cell_x += ci.display_width;
      }

      current_x_offset += current_cw;
    }
    draw_y++;

    // 3. Render Rows
    int start_idx = current_page * ps;

    for (int r_vis = 0; r_vis < ps; ++r_vis) {
      int r = start_idx + r_vis;
      if (draw_y >= y + height - 2)
        break;  // Reserve 2 lines for footer (separator + controls)

      bool is_empty = (r >= (int)rows.size());
      Color c_row = (r_vis % 2 == 0) ? r_a : r_b;

      // Selection Highlight
      Color sel_bg = selected_bg_color.resolve(Theme::current().selection);
      Color sel_fg = selected_fg_color.resolve(Theme::current().background);

      Color cell_bg = c_row;
      Color cell_fg = Theme::current().foreground;
      if (r == selected_row_) {
        cell_bg = sel_bg;
        cell_fg = sel_fg;
      } else if (r == hovered_row_) {
        cell_bg = Theme::current().hover;
      }

      current_x_offset = x;
      for (int i = 0; i < num_cols; ++i) {
        int current_cw = col_width;
        if (i == num_cols - 1) current_cw = width - (current_x_offset - x);

        StyledText styled_cell = "";
        if (!is_empty && i < (int)rows[r].size()) styled_cell = rows[r][i];

        auto text_chars =
            TextHelper::prepare_text_for_render(styled_cell.plain_text());

        // Fill with spaces first
        for (int k = 0; k < current_cw; ++k) {
          Cell c;
          c.bg_color = cell_bg;
          c.fg_color = cell_fg;
          c.content = " ";
          buffer.set(current_x_offset + k, draw_y, c);
        }

        int cell_x = 0;
        for (size_t j = 0; j < text_chars.size(); ++j) {
          if (1 + cell_x >= current_cw) break;
          const auto &ci = text_chars[j];
          Cell c;
          c.bg_color = cell_bg;
          c.fg_color = cell_fg;
          c.content = ci.content;

          // Apply styling
          if (!styled_cell.empty()) {
            if (const TextSpan *span = styled_cell.get_span_at_char((int)j)) {
              if (!span->color.is_default) c.fg_color = span->color;
              c.bold = span->bold;
              c.italic = span->italic;
              c.underline = span->underline;
            }
          }

          buffer.set(current_x_offset + 1 + cell_x, draw_y, c);
          if (ci.display_width == 2 && 1 + cell_x + 1 < current_cw) {
            Cell skip;
            skip.content = "";
            skip.bg_color = cell_bg;
            buffer.set(current_x_offset + 1 + cell_x + 1, draw_y, skip);
          }
          cell_x += ci.display_width;
        }

        current_x_offset += current_cw;
      }
      draw_y++;
    }

    // 4. Render Footer Separator
    if (draw_y < y + height - 1) {
      current_x_offset = x;
      for (int i = 0; i < width; ++i) {
        Cell c;
        c.content = " ";
        c.bg_color = Theme::current().panel_bg;
        buffer.set(x + i, draw_y, c);
      }
      draw_y++;
    }

    // 5. Render Footer Controls
    int footer_y = y + height - 1;

    // Update Label text
    label_static->set_text(" of " + std::to_string(total_pages));

    int cx = x + (width - 25) / 2;
    if (cx < x) cx = x;

    auto layout_widget = [&](std::shared_ptr<Widget> w) {
      w->x = cx;
      w->y = footer_y;
      if (w == input_page)
        w->width = 4;  // fixed
      else if (auto b = std::dynamic_pointer_cast<Button>(w))
        w->width = b->get_label().length() + 2;
      else if (auto l = std::dynamic_pointer_cast<Label>(w))
        w->width = l->get_text().length();

      w->height = 1;
      w->render(buffer);
      cx += w->width + 1;  // Spacing
    };

    layout_widget(btn_first);
    layout_widget(btn_prev);

    if (input_page->get_value().empty() && !input_page->has_focus())
      update_input();

    layout_widget(input_page);
    layout_widget(label_static);
    layout_widget(btn_next);
    layout_widget(btn_last);
  }
};

/// @brief Efficient table for large datasets with scrolling
class TableScrollable : public Widget {
 public:
  std::vector<StyledText> columns;            ///< Column headers
  std::vector<std::vector<StyledText>> rows;  ///< Data rows
  std::vector<int> col_widths;  ///< Optional explicit column widths

  int selected_index = 0;  ///< Index of the currently selected row
  int scroll_offset = 0;   ///< Current vertical scroll position

  int hovered_row = -1;

  // Helper to add a row of strings
  void add_row(const std::vector<std::string> &row) {
    std::vector<StyledText> styled_row;
    styled_row.reserve(row.size());
    for (const auto &s : row) styled_row.emplace_back(s);
    rows.push_back(std::move(styled_row));
  }

  // Style options
  Color header_color = Color();       // Default: panel_bg/primary
  Color selected_bg_color = Color();  // Default: selection
  Color selected_fg_color = Color();  // Default: foreground
  Color row_fg_color = Color();       // Default: foreground

  std::function<void(int)> on_change;  ///< Callback when selection changes
  std::function<void(int)> on_submit;  ///< Callback on double-click or Enter

  // Scrollbar Configuration
  Color scrollbar_track_color = Color();
  Color scrollbar_thumb_color = Color();
  std::string scrollbar_track_char = "\u2591";  // Light Shade
  std::string scrollbar_thumb_char = "█";       // Full Block

  bool is_dragging_scrollbar = false;  // State for drag

  TableScrollable() { focusable = true; }

  void scroll_to_selection() {
    if (selected_index < scroll_offset) {
      scroll_offset = selected_index;
    } else if (selected_index >= scroll_offset + height - 1) {  // -1 for header
      scroll_offset = selected_index - (height - 2);
    }
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event()) {
      // SCROLLBAR HANDLING FIRST - before any other mouse processing
      int sb_x = x + width - 1;
      int visible_rows = height - 1;
      bool scrollbar_visible = ((int)rows.size() > visible_rows);

      // Check if in scrollbar zone (rightmost 3 columns)
      bool in_scrollbar_zone = (event.x >= sb_x - 2 && event.x <= sb_x &&
                                event.y > y && event.y < y + height);

      if (event.mouse_release()) {
        is_dragging_scrollbar = false;
      }

      // Handle scrollbar interactions FIRST
      if (scrollbar_visible && (is_dragging_scrollbar || in_scrollbar_zone)) {
        if (in_scrollbar_zone && event.mouse_left()) {
          is_dragging_scrollbar = true;
          set_focus(true);
        }

        if (is_dragging_scrollbar &&
            (event.mouse_left() || event.mouse_drag())) {
          int sb_y_start = y + 1;
          int sb_h = height - 1;

          int clamped_y =
              std::max(sb_y_start, std::min(event.y, y + height - 1));
          int click_y = clamped_y - sb_y_start;

          float ratio = (sb_h > 1) ? (float)click_y / (float)(sb_h - 1) : 0.0f;
          if (ratio < 0) ratio = 0;
          if (ratio > 1) ratio = 1;

          int target_row = (int)(ratio * ((int)rows.size() - 1));
          if (target_row < 0) target_row = 0;
          if (target_row >= (int)rows.size()) target_row = (int)rows.size() - 1;

          selected_index = target_row;
          scroll_to_selection();
          if (on_change) on_change(selected_index);
          return true;
        }

        // Even if not dragging yet, return true to prevent row selection
        if (in_scrollbar_zone) {
          return true;
        }
      }

      // Scroll Wheel -> Move Selection (only if not in scrollbar zone)
      if (event.x >= x && event.x < x + width && event.y >= y &&
          event.y < y + height) {
        if (event.mouse_wheel()) {
          if (event.mouse_wheel_up()) {  // Wheel Up
            if (selected_index > 0) {
              selected_index--;
              scroll_to_selection();
              if (on_change) on_change(selected_index);
              return true;
            }
          } else if (event.mouse_wheel_down()) {  // Wheel Down
            if (selected_index < (int)rows.size() - 1) {
              selected_index++;
              scroll_to_selection();
              if (on_change) on_change(selected_index);
              return true;
            }
          }
        }
      }

      // Row click handling - only for areas outside scrollbar zone
      if (event.mouse_left() || event.mouse_move() || event.mouse_motion()) {
        int row_click_max_x = x + width - 3;  // Exclude scrollbar zone
        if (event.y > y && event.y < y + height && event.x >= x &&
            event.x < row_click_max_x) {
          int click_row_offset = event.y - (y + 1);
          int clicked_index = scroll_offset + click_row_offset;

          if (event.mouse_move() || event.mouse_motion()) {
            // Hover
            if (clicked_index >= 0 && clicked_index < (int)rows.size()) {
              hovered_row = clicked_index;
            } else {
              hovered_row = -1;
            }
            return true;
          }

          if (event.mouse_left()) {
            if (clicked_index >= 0 && clicked_index < (int)rows.size()) {
              selected_index = clicked_index;
              if (on_change) on_change(selected_index);
              if (event.click_count >= 2 && on_submit)
                on_submit(selected_index);
              return true;
            }
          }
        } else {
          // Mouse moved out of row area
          if (event.mouse_move() || event.mouse_motion()) {
            if (hovered_row != -1) {
              hovered_row = -1;
              return true;
            }
          }
        }
      }
    }
    if (event.is_key_event()) {
      bool changed = false;
      if (event.is_nav_up()) {  // Up
        if (selected_index > 0) {
          selected_index--;
          changed = true;
        }
      } else if (event.is_nav_down()) {  // Down
        if (selected_index < (int)rows.size() - 1) {
          selected_index++;
          changed = true;
        }
      } else if (event.is_view_scroll_up()) {  // PageUp
        selected_index = std::max(0, selected_index - (height - 2));
        changed = true;
      } else if (event.is_view_scroll_down()) {  // PageDown
        selected_index =
            std::min((int)rows.size() - 1, selected_index + (height - 2));
        changed = true;
      } else if (event.is_nav_home()) {  // Home
        selected_index = 0;
        changed = true;
      } else if (event.is_nav_end()) {  // End
        selected_index = (int)rows.size() - 1;
        changed = true;
      } else if (event.is_enter()) {
        if (on_submit) on_submit(selected_index);
        return true;
      }

      if (changed) {
        scroll_to_selection();
        if (on_change) on_change(selected_index);
        return true;
      }
    }
    return false;
  }

  void render(Buffer &buffer) override {
    if (columns.empty()) return;

    // Auto layout columns
    std::vector<int> current_widths = col_widths;
    if (current_widths.empty() && width > 0) {
      int w = width / columns.size();
      for (size_t i = 0; i < columns.size(); ++i) current_widths.push_back(w);
    }

    // Flex logic: expand last column to fill remaining width
    int total_w = 0;
    for (int w : current_widths) total_w += w;

    if (width > total_w && !current_widths.empty()) {
      current_widths.back() += (width - total_w);
    }

    int draw_y = y;

    // Header - use different color when focused
    int cx = x;
    Color h_col = header_color.resolve(Theme::current().table_header_bg);
    Color h_fg = Theme::current().table_header_fg;
    // Focus indicator: Use TABLE HEADER FOCUS color for distinct highlight
    if (focused_) {
      h_col = Theme::current().table_header_bg_focus;
      h_fg = Theme::current().table_header_fg_focus;
    }

    for (size_t i = 0; i < columns.size(); ++i) {
      int cw = (i < current_widths.size()) ? current_widths[i] : 10;
      StyledText styled_text = columns[i];
      auto text_chars =
          TextHelper::prepare_text_for_render(styled_text.plain_text());

      // Fill with spaces first
      for (int k = 0; k < cw; ++k) {
        Cell c;
        c.fg_color = h_fg;
        c.bg_color = h_col;
        c.content = " ";
        buffer.set(cx + k, draw_y, c);
      }

      int cell_x = 0;
      for (size_t j = 0; j < text_chars.size(); ++j) {
        if (cell_x >= cw) break;
        const auto &ci = text_chars[j];
        Cell c;
        c.fg_color = h_fg;
        c.bg_color = h_col;
        c.content = ci.content;

        // Apply styling
        if (!styled_text.empty()) {
          if (const TextSpan *span = styled_text.get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(cx + cell_x, draw_y, c);
        if (ci.display_width == 2 && cell_x + 1 < cw) {
          Cell skip;
          skip.content = "";
          skip.bg_color = h_col;
          buffer.set(cx + cell_x + 1, draw_y, skip);
        }
        cell_x += ci.display_width;
      }
      cx += cw;
    }
    draw_y++;

    // Rows
    int visible_rows = height - 1;
    int end_row = std::min((int)rows.size(), scroll_offset + visible_rows);

    Color sel_bg = selected_bg_color.resolve(Theme::current().selection);
    Color sel_fg = selected_fg_color.resolve(Theme::current().background);
    Color row_fg = row_fg_color.resolve(Theme::current().foreground);

    for (int r = scroll_offset; r < end_row; ++r) {
      if (draw_y >= y + height) break;

      bool is_sel = (r == selected_index);
      const auto &row_data = rows[r];

      // Alternating row colors
      Color row_bg = (r % 2 == 0) ? Theme::current().background
                                  : Theme::current().panel_bg;

      cx = x;
      for (size_t i = 0; i < columns.size(); ++i) {
        int cw = (i < current_widths.size()) ? current_widths[i] : 10;
        StyledText styled_cell =
            (i < row_data.size()) ? row_data[i] : StyledText("");

        Color cell_bg =
            is_sel ? sel_bg
                   : (r == hovered_row ? Theme::current().hover : row_bg);
        Color cell_fg = is_sel ? sel_fg : row_fg;

        // Fill with spaces first
        for (int k = 0; k < cw; ++k) {
          Cell c;
          c.bg_color = cell_bg;
          c.fg_color = cell_fg;
          c.content = " ";
          buffer.set(cx + k, draw_y, c);
        }

        auto text_chars =
            TextHelper::prepare_text_for_render(styled_cell.plain_text());
        int cell_x = 0;
        for (size_t j = 0; j < text_chars.size(); ++j) {
          if (cell_x >= cw) break;
          const auto &ci = text_chars[j];
          Cell c;
          c.content = ci.content;
          c.bg_color = cell_bg;
          c.fg_color = cell_fg;

          // Apply styling
          if (!styled_cell.empty()) {
            if (const TextSpan *span = styled_cell.get_span_at_char((int)j)) {
              if (!span->color.is_default) c.fg_color = span->color;
              c.bold = span->bold;
              c.italic = span->italic;
              c.underline = span->underline;
            }
          }

          buffer.set(cx + cell_x, draw_y, c);
          if (ci.display_width == 2 && cell_x + 1 < cw) {
            Cell skip;
            skip.content = "";
            skip.bg_color = cell_bg;
            buffer.set(cx + cell_x + 1, draw_y, skip);
          }
          cell_x += ci.display_width;
        }
        cx += cw;
      }
      draw_y++;
    }

    // Draw Scrollbar (Overlay)
    int visible_h = height - 1;
    if ((int)rows.size() > visible_h && visible_h > 0) {
      render_scrollbar(buffer, x + width - 1, y + 1, visible_h, scroll_offset,
                       (int)rows.size(), true, scrollbar_track_color,
                       scrollbar_thumb_color, scrollbar_track_char,
                       scrollbar_thumb_char);
    }
  }
};

/// @brief Main application class managing the event loop and terminal
class App {
 public:
  App() : current_buffer_(0, 0), previous_buffer_(0, 0) {}
  // Resize Debounce State
  int pending_resize_w = 0;
  int pending_resize_h = 0;
  std::chrono::steady_clock::time_point last_resize_req;
  const std::chrono::milliseconds resize_delay = std::chrono::milliseconds(25);

  struct KeyBinding {
    int key;
    bool ctrl;
    bool alt;
    bool shift;

    bool operator==(const KeyBinding &other) const {
      return key == other.key && ctrl == other.ctrl && alt == other.alt &&
             shift == other.shift;
    }
  };

  struct KeyBindingHash {
    std::size_t operator()(const KeyBinding &k) const noexcept {
      return std::hash<std::size_t>{}((static_cast<std::size_t>(k.key) << 3) |
                                      (static_cast<std::size_t>(k.ctrl) << 2) |
                                      (static_cast<std::size_t>(k.alt) << 1) |
                                      static_cast<std::size_t>(k.shift));
    }
  };

  struct RegisteredKey {
    std::function<void()> callback;
    bool consume;
  };

  std::vector<KeyBinding> exit_keys_;
  std::unordered_map<KeyBinding, RegisteredKey, KeyBindingHash> key_events_;

  /// @brief Register a key that will exit the application
  void register_exit_key(int key, bool ctrl = false, bool alt = false,
                         bool shift = false) {
    exit_keys_.push_back({key, ctrl, alt, shift});
  }

  /// @brief Register a key that will execute a callback function
  void register_key(int key, std::function<void()> callback, bool ctrl = false,
                    bool alt = false, bool shift = false, bool consume = true) {
    key_events_[{key, ctrl, alt, shift}] = {callback, consume};
  }

  /// @brief Unregister a registered key combination
  void unregister_key(int key, bool ctrl = false, bool alt = false,
                      bool shift = false) {
    key_events_.erase({key, ctrl, alt, shift});
  }

  // Dialog Stack
  std::vector<std::shared_ptr<Dialog>> dialog_stack;

  // Notification Overlay (for event dispatch)
  std::shared_ptr<Notification> active_notification_;

  /// @brief Register a notification widget for overlay event dispatch
  void set_notification(std::shared_ptr<Notification> notif) {
    active_notification_ = notif;
  }

  /// @brief Open a dialog at a specific screen position
  /// @param d The dialog to open
  /// @param x X coordinate (screen absolute)
  /// @param y Y coordinate (screen absolute)
  void open_dialog(std::shared_ptr<Dialog> d, int x, int y) {
    for (auto &existing : dialog_stack)
      if (existing == d) return;
    dialog_stack.push_back(d);
    d->show(x, y);
  }

  /// @brief Open a dialog, auto-centered on screen
  /// @param d The dialog to open
  void open_dialog(std::shared_ptr<Dialog> d) {
    for (auto &existing : dialog_stack)
      if (existing == d) return;
    dialog_stack.push_back(d);

    // Auto-center using static Terminal check
    auto size = Terminal::getSize();
    int dw = d->fixed_width > 0 ? d->fixed_width : 40;
    int dh = d->fixed_height > 0 ? d->fixed_height : 10;

    int cx = (size.first - dw) / 2;
    int cy = (size.second - dh) / 2;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;

    d->show(cx, cy);
  }

  /// @brief Close a specific dialog
  void close_dialog(std::shared_ptr<Dialog> dialog) {
    auto it = std::find(dialog_stack.begin(), dialog_stack.end(), dialog);
    if (it != dialog_stack.end()) {
      dialog->is_open = false;  // Hide
      dialog_stack.erase(it);
    }
  }

  /// @brief Add a recurring timer
  /// @param interval_ms Interval in milliseconds
  /// @param callback Function to call
  /// @return Token identifier for the timer
  TimerId add_timer(int interval_ms, std::function<void()> callback) {
    Timer t;
    t.id = next_timer_id_++;
    t.interval = std::chrono::milliseconds(interval_ms);
    t.callback = callback;
    t.last_fire = std::chrono::steady_clock::now();
    timers_.push_back(t);
    return {t.id};
  }

  /// @brief Remove a timer by ID
  void remove_timer(TimerId id) {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) {
      if (it->id == id.id) {
        timers_.erase(it);
        return;
      }
    }
  }

  /// @brief Wake the event loop and trigger a redraw on the next iteration.
  /// Thread-safe. Can be called from any thread.
  void update() {
#ifdef _WIN32
    if (g_wakeup_event) SetEvent(g_wakeup_event);
#else
    if (g_wakeup_pipe[1] != -1) {
      [[maybe_unused]] char c = 1;
      // Write is non-blocking; ignore failure (pipe full means wakeup is
      // already pending)
      (void)write(g_wakeup_pipe[1], &c, 1);
    }
#endif
  }

  /// @brief Schedule a callback to run on the main UI thread, then redraw.
  /// Thread-safe. Use this to safely modify widgets from background threads.
  /// @param callback Function to execute on the UI thread
  void post(std::function<void()> callback) {
    {
      std::lock_guard<std::mutex> lock(post_mutex_);
      posted_callbacks_.push_back(std::move(callback));
    }
    update();
  }

  static void update_screen_size(int w, int h = 0) {
    if (w < 80)
      g_screen_size = ScreenSize::Small;
    else if (w < 120)
      g_screen_size = ScreenSize::Medium;
    else
      g_screen_size = ScreenSize::Large;

    if (h > 0) {
      if (h < 25)
        g_screen_height_cat = ScreenHeight::Small;
      else if (h < 50)
        g_screen_height_cat = ScreenHeight::Medium;
      else
        g_screen_height_cat = ScreenHeight::Large;
    }
  }

  static void quit() {
    if (quit_app) quit_app();
  }

  void run(std::shared_ptr<Widget> root) {
    Terminal term;
    term.drainInputBuffer();  // Clear stale events from initialization
    bool running = true;
    quit_app = [&]() { running = false; };
    bool needs_render = true;

    auto size = term.getSize();
    update_screen_size(size.first, size.second);
    current_buffer_.resize(size.first, size.second);
    previous_buffer_.resize(size.first, size.second);

    // ... (rest of initialization) ...

    // Skipping ahead to main loop logic
    // ... (skipping) ...

    /* Need to apply logic inside the loop, using separate replacement for
     * reliability */

    // Initial focus
    if (root) {
      std::vector<std::shared_ptr<Widget>> focusables;
      collect_focusables(root, focusables);
      if (!focusables.empty()) {
        focused_widget_ = focusables[0];
        focused_widget_->set_focus(true);
      }
    }

    // Sync all new timers to start roughly now
    auto now_start = std::chrono::steady_clock::now();
    for (auto &t : timers_) t.last_fire = now_start;

    // Focus Stack for Dialogs
    std::vector<std::shared_ptr<Widget>> focus_stack;
    size_t last_dialog_count = 0;

    while (running) {
      // Check Dialog Stack Changes for Focus Management
      if (dialog_stack.size() != last_dialog_count) {
        if (dialog_stack.size() > last_dialog_count) {
          // Dialog Opened
          auto &d = dialog_stack.back();

          // Only steal focus if the dialog requests it
          if (d->steal_focus) {
            focus_stack.push_back(focused_widget_);  // Save current

            if (focused_widget_) focused_widget_->set_focus(false);
            focused_widget_ = nullptr;

            std::vector<std::shared_ptr<Widget>> fs;
            collect_focusables(d, fs);
            if (!fs.empty()) {
              focused_widget_ = fs[0];
              focused_widget_->set_focus(true);
            }
          }
        } else {
          // Dialog Closed - Restore Depth
          while (focus_stack.size() > dialog_stack.size()) {
            std::shared_ptr<Widget> restore = focus_stack.back();
            focus_stack.pop_back();

            // Only restore if we are now at the top
            if (focus_stack.size() == dialog_stack.size()) {
              if (focused_widget_) focused_widget_->set_focus(false);
              focused_widget_ = restore;
              if (focused_widget_) focused_widget_->set_focus(true);
            }
          }
        }
        last_dialog_count = dialog_stack.size();
        needs_render = true;
      }
      auto now = std::chrono::steady_clock::now();
      int min_wait_ms = -1;  // Default: wait indefinitely

      // 0. Drain and execute posted callbacks (from post())
      {
        std::vector<std::function<void()>> callbacks;
        {
          std::lock_guard<std::mutex> lock(post_mutex_);
          callbacks.swap(posted_callbacks_);
        }
        if (!callbacks.empty()) {
          for (auto &cb : callbacks) {
            if (cb) cb();
          }
          needs_render = true;
        }
      }

      // 1. Handle Additional Timers
      for (auto &t : timers_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - t.last_fire);
        if (elapsed >= t.interval) {
          if (t.callback) {
            t.callback();
            needs_render = true;
          }
          t.last_fire = now;
          elapsed = std::chrono::milliseconds(0);
        }

        int wait = (int)(t.interval - elapsed).count();
        if (wait < 0) wait = 0;
        if (min_wait_ms == -1 || wait < min_wait_ms) min_wait_ms = wait;
      }

      // 2. Check pending resize
      if (pending_resize_w > 0) {
        auto elapsed_resize =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_resize_req);
        if (elapsed_resize >= resize_delay) {
          current_buffer_.resize(pending_resize_w, pending_resize_h);
          previous_buffer_.resize(pending_resize_w, pending_resize_h);
          previous_buffer_.clear();  // Force full redraw
          term.clearScreen();        // CLear physical screen artifacts

          pending_resize_w = 0;
          needs_render = true;
        } else {
          // Need to wake up soon to finish resize
          int wait_r = (int)(resize_delay - elapsed_resize).count();
          if (wait_r < 0) wait_r = 0;
          if (wait_r < min_wait_ms) min_wait_ms = wait_r;
        }
      }

      if (needs_render) {
        if (root) {
          Cell bg_cell;
          bg_cell.bg_color = Theme::current().background;
          bg_cell.fg_color = Theme::current().foreground;
          current_buffer_.clear(bg_cell);

          root->width = current_buffer_.width();
          root->height = current_buffer_.height();

          if (auto cont = std::dynamic_pointer_cast<Container>(root)) {
            cont->layout();
          }

          root->render(current_buffer_);
        }

        // Render Dialogs (Overlay)
        for (auto &d : dialog_stack) {
          if (d->is_open) d->render(current_buffer_);
        }

        // Render Automatic Tooltip (Top-most)
        if (active_tooltip_) {
          active_tooltip_->render(current_buffer_);
        }

        // Handle force redraw
        term.render(current_buffer_, previous_buffer_);
        previous_buffer_ = current_buffer_;
        needs_render = false;
      }

      // Wait for event with varying timeout, then batch process up to 50 events
      int max_batch = 50;
      int time_left = min_wait_ms;

      while (max_batch-- > 0) {
        Event event = term.readEvent(time_left);

        if (event.type == EventType::None) {
          break;  // No more events (or timeout)
        }

        // Wakeup event: just trigger a redraw, don't dispatch
        if (event.type == EventType::Wakeup) {
          needs_render = true;
          break;
        }

        // Subsequent reads should be instant (check buffer)
        time_left = 0;

        if (event.type == EventType::Resize) {
          // True Debounce: Always reset timer on new event
          last_resize_req = now;
          pending_resize_w = event.x;
          pending_resize_h = event.y;
          update_screen_size(event.x, event.y);
        }

        // 1. Mandatory Global Exit (Ctrl+C)
        bool is_ctrl_c = (event.is_key_event()) && event.is_copy();
        if (is_ctrl_c) {
          // Check if focused widget has selection - if so, let it consume the
          // event for Copy
          bool handled_as_copy = false;

          // Check active tooltip first
          if (active_tooltip_ &&
              active_tooltip_->selection_state_.has_selection()) {
            handled_as_copy = true;
          }

          // Check notification widgets (auto-discover from tree)
          if (!handled_as_copy && root) {
            std::function<bool(std::shared_ptr<Widget>)> has_notif_selection =
                [&](std::shared_ptr<Widget> w) -> bool {
              if (!w) return false;
              if (auto n = std::dynamic_pointer_cast<Notification>(w)) {
                if (n->selection_state_.has_selection()) {
                  return true;
                }
              }
              if (auto c = std::dynamic_pointer_cast<Container>(w)) {
                for (auto &child : c->get_children()) {
                  if (has_notif_selection(child)) return true;
                }
              }
              return false;
            };
            if (has_notif_selection(root)) {
              handled_as_copy = true;
            }
          }

          // Check focused widget
          if (!handled_as_copy && focused_widget_) {
            // Try dynamic cast to known widgets with selection
            if (auto input =
                    std::dynamic_pointer_cast<Input>(focused_widget_)) {
              if (input->has_selection()) handled_as_copy = true;
            } else if (auto area = std::dynamic_pointer_cast<TextArea>(
                           focused_widget_)) {
              if (area->has_selection()) handled_as_copy = true;
            } else if (auto label =
                           std::dynamic_pointer_cast<Label>(focused_widget_)) {
              if (label->selectable && label->has_selection())
                handled_as_copy = true;
            } else if (auto staticw =
                           std::dynamic_pointer_cast<Static>(focused_widget_)) {
              if (staticw->selectable && staticw->has_selection())
                handled_as_copy = true;
            } else if (auto para = std::dynamic_pointer_cast<Paragraph>(
                           focused_widget_)) {
              if (para->selectable && para->has_selection())
                handled_as_copy = true;
            } else if (auto border =
                           std::dynamic_pointer_cast<Border>(focused_widget_)) {
              if (border->has_selection()) handled_as_copy = true;
            }
          }

          if (!handled_as_copy) {
            running = false;
            break;
          }
          // If handled_as_copy, we fall through to "Focused Widget Dispatch"
        }

        // 1.5 Contextual Copy (Tooltip / Notification / Hovered Widget)
        // Must be handled before Focused Widget or Exit Keys to prevent exit on
        // Ctrl+C and ensure overlay copy works
        if (event.is_key_event() && event.is_copy()) {
          bool handled = false;
          // 1. Active Tooltip (Topmost)
          if (active_tooltip_) {
            if (active_tooltip_->on_event(event)) {
              handled = true;
            }
          }
          // 2. Notification widgets (auto-discovered from root)
          if (!handled && root) {
            std::shared_ptr<Notification> found_notif = nullptr;
            std::function<void(std::shared_ptr<Widget>)> find_notif_copy =
                [&](std::shared_ptr<Widget> w) {
                  if (!w || found_notif) return;
                  if (auto n = std::dynamic_pointer_cast<Notification>(w)) {
                    // Check if this notification has active selection
                    if (n->selection_state_.has_selection()) {
                      found_notif = n;
                      return;
                    }
                  }
                  if (auto c = std::dynamic_pointer_cast<Container>(w)) {
                    for (auto &child : c->get_children()) {
                      find_notif_copy(child);
                    }
                  }
                };
            find_notif_copy(root);

            if (found_notif && found_notif->on_event(event)) {
              handled = true;
            }
          }
          // 3. Hovered Widget (e.g. other overlay-like widgets)
          // Only if it's not the focused widget (focused is handled in step 2)
          if (!handled && hovered_widget_ &&
              hovered_widget_ != focused_widget_) {
            // Most widgets check has_focus() for keys, but overlays don't have
            // focus so this allows them to handle copy if they implement it
            // without focus check.
            if (hovered_widget_->on_event(event)) {
              handled = true;
            }
          }

          if (handled) {
            needs_render = true;
            continue;
          }
        }

        // 1.7 Registered Key Events
        if (event.is_key_event()) {
          KeyBinding kv = {event.key, event.ctrl, event.alt, event.shift};
          auto it = key_events_.find(kv);
          if (it != key_events_.end()) {
            it->second.callback();
            if (it->second.consume) {
              needs_render = true;
              continue;  // Consumed, skip subsequent dispatch steps
            }
            needs_render = true;
          }
        }

        // 2. Focused Widget Dispatch (Keys only)
        bool consumed = false;
        if ((event.is_key_event() || event.type == EventType::Paste) &&
            focused_widget_) {
          consumed = focused_widget_->on_event(event);
          if (consumed) {
            needs_render = true;
            continue;  // Key was consumed by the focused widget
          }
        }

        // 3. Registered Exit Keys (if not consumed)
        if (event.is_key_event()) {
          for (const auto &binding : exit_keys_) {
            if (event.key == binding.key && event.ctrl == binding.ctrl &&
                event.alt == binding.alt && event.shift == binding.shift) {
              running = false;
              break;
            }
          }
          if (!running) break;
        }

        // 4. Tab Navigation (if not consumed)
        if (event.is_key_event() && event.is_tab()) {
          handle_tab(root, event.shift);
          needs_render = true;
          continue;
        }

        // 5. Root Dispatch (Spatial or unhandled keys)
        if (event.is_mouse_event() || event.is_key_event() ||
            event.type == EventType::Paste) {
          if (event.is_mouse_event()) {
            // 5a. Dispatch ALL mouse events to active_tooltip_ if inside bounds
            // This handles clicks, drags, and releases for tooltip text
            // selection
            if (active_tooltip_ &&
                active_tooltip_->contains(event.x, event.y)) {
              if (active_tooltip_->on_event(event)) {
                needs_render = true;
                continue;  // Consumed by tooltip
              }
            }

            // 5b. Dispatch mouse events to Notification widgets
            // Auto-find notification widget by checking if any widget returns
            // true for hit_test and is a Notification. We search the root tree
            // for Notification widgets.
            {
              std::shared_ptr<Notification> found_notif = nullptr;
              std::function<void(std::shared_ptr<Widget>)> find_notif =
                  [&](std::shared_ptr<Widget> w) {
                    if (!w || found_notif) return;
                    if (auto n = std::dynamic_pointer_cast<Notification>(w)) {
                      if (n->hit_test(event.x, event.y)) {
                        found_notif = n;
                        return;
                      }
                    }
                    if (auto c = std::dynamic_pointer_cast<Container>(w)) {
                      for (auto &child : c->get_children()) {
                        find_notif(child);
                      }
                    }
                  };
              find_notif(root);

              if (found_notif && found_notif->on_event(event)) {
                needs_render = true;
                continue;  // Consumed by notification
              }
            }

            // Global Click-to-Focus
            if (event.mouse_left()) {
              std::shared_ptr<Widget> clicked_widget = nullptr;
              bool modal_blocking = false;

              // 1. Check Dialogs (Top-most)
              if (!dialog_stack.empty()) {
                for (auto it = dialog_stack.rbegin(); it != dialog_stack.rend();
                     ++it) {
                  auto d = *it;
                  if (!d->is_open) continue;

                  clicked_widget = find_widget_at(d, event.x, event.y, true, 0,
                                                  0, current_buffer_.width(),
                                                  current_buffer_.height());
                  if (clicked_widget) break;

                  // If this dialog is modal, it blocks clicks to anything below
                  // it
                  if (d->modal) {
                    modal_blocking = true;
                    break;
                  }
                }
              }

              // 2. Check Root (if not blocked)
              if (!clicked_widget && !modal_blocking) {
                clicked_widget = find_widget_at(root, event.x, event.y, true, 0,
                                                0, current_buffer_.width(),
                                                current_buffer_.height());
              }

              if (clicked_widget && clicked_widget->focusable) {
                if (focused_widget_ && focused_widget_ != clicked_widget) {
                  focused_widget_->set_focus(false);
                }
                focused_widget_ = clicked_widget;
                focused_widget_->set_focus(true);
                needs_render = true;
              } else {
                // Only clear focus if clicked on TRUE empty space (null
                // widget), not just a non-focusable widget like MenuBar
                if (!modal_blocking && clicked_widget == nullptr) {
                  if (focused_widget_) {
                    focused_widget_->set_focus(false);
                    focused_widget_ = nullptr;
                    needs_render = true;
                  }
                }
              }
            }
          }

          // --- Automatic Tooltip Logic ---
          if (active_tooltip_ && event.is_key_event() && event.is_copy()) {
            // Allow tooltip to handle copy if it has selection
            if (active_tooltip_->on_event(event)) {
              // Tooltip consumed copy
            }
          }

          if (event.is_mouse_event() &&
              (event.mouse_move() || event.mouse_drag() ||
               event.mouse_motion())) {
            std::shared_ptr<Widget> target_widget = nullptr;

            // Check dialogs first
            bool found_in_dialog = false;
            if (!dialog_stack.empty()) {
              for (auto it = dialog_stack.rbegin(); it != dialog_stack.rend();
                   ++it) {
                auto d = *it;
                if (!d->is_open) continue;
                target_widget = find_widget_at(d, event.x, event.y, false, 0, 0,
                                               current_buffer_.width(),
                                               current_buffer_.height());
                if (target_widget) {
                  found_in_dialog = true;
                  break;
                }
                if (d->modal) break;  // blocked
              }
            }

            if (!found_in_dialog && root) {
              target_widget = find_widget_at(root, event.x, event.y, false, 0,
                                             0, current_buffer_.width(),
                                             current_buffer_.height());
            }

            // Update Hover State
            if (target_widget != hovered_widget_) {
              // Check if we moved INTO the active tooltip or if we are still
              // interacting with it
              bool inside_tooltip = false;
              if (active_tooltip_ &&
                  active_tooltip_->contains(event.x, event.y)) {
                inside_tooltip = true;
                active_tooltip_->on_event(
                    event);  // Delegate events (e.g. selection) to tooltip
              }

              if (!inside_tooltip) {
                // Leave old
                if (hovered_widget_) {
                  hovered_widget_->set_hovered(false);
                  // Hide previous tooltip if active
                  if (hovered_widget_->tooltip_) {
                    hovered_widget_->tooltip_->hide();
                    if (active_tooltip_ == hovered_widget_->tooltip_)
                      active_tooltip_ = nullptr;
                  }
                }

                hovered_widget_ = target_widget;

                // Enter new
                if (hovered_widget_) {
                  hovered_widget_->set_hovered(true);
                  // Show new tooltip
                  if (hovered_widget_->tooltip_) {
                    hovered_widget_->tooltip_->attach(hovered_widget_);
                    hovered_widget_->tooltip_->show();
                    active_tooltip_ = hovered_widget_->tooltip_;
                  }
                } else {
                  active_tooltip_ = nullptr;
                }
                needs_render = true;
              }
            } else {
              // Still on same widget, but check if we are also on the tooltip
              // (e.g. overlap or just movement) OR if the widget updated its
              // tooltip dynamically (e.g. charts)
              if (active_tooltip_) {
                if (active_tooltip_->contains(event.x, event.y)) {
                  active_tooltip_->on_event(event);
                }

                // Check if the hovered widget has changed its tooltip instance
                // or properties This allows charts to update the tooltip
                // text/position while the mouse moves within the SAME widget
                if (hovered_widget_ && hovered_widget_->tooltip_ &&
                    hovered_widget_->tooltip_ != active_tooltip_) {
                  // The widget has swapped its tooltip!
                  // Or maybe it just created a new one.
                  active_tooltip_ = hovered_widget_->tooltip_;
                  active_tooltip_->attach(hovered_widget_);
                  active_tooltip_->show();
                  needs_render = true;
                } else if (hovered_widget_ &&
                           active_tooltip_ == hovered_widget_->tooltip_) {
                  // Same instance, but maybe text changed? we assume text
                  // update happens in widget::on_event If use Manual position,
                  // we need to ensure it's redrawn if moved The app loop
                  // redraws if needs_render is true, which usually happens if
                  // widget::on_event returns true. Charts return true on mouse
                  // move if tooltip changes.
                } else if (hovered_widget_ && !hovered_widget_->tooltip_ &&
                           active_tooltip_) {
                  // Widget removed its tooltip dynamically (e.g. chart no
                  // longer hovering point) Ensure we only remove it if it was
                  // the one we were showing But active_tooltip_ doesn't store
                  // "owner", only "target". If target matches hovered_widget_,
                  // we can safely remove it.
                  if (active_tooltip_->target == hovered_widget_) {
                    active_tooltip_->hide();
                    active_tooltip_ = nullptr;
                    needs_render = true;
                  }
                }
              } else if (hovered_widget_ && hovered_widget_->tooltip_) {
                // Widget gained a tooltip while we were hovering it!
                active_tooltip_ = hovered_widget_->tooltip_;
                active_tooltip_->attach(hovered_widget_);
                active_tooltip_->show();
                needs_render = true;
              }
            }
          }

          // -----------------------------

          bool dialog_handled = false;
          // Dispatch to Dialogs (Top-most)
          if (!dialog_stack.empty()) {
            for (int i = (int)dialog_stack.size() - 1; i >= 0; --i) {
              if (i >= (int)dialog_stack.size()) continue;
              auto d = dialog_stack[i];
              if (d->is_open) {
                if (d->on_event(event)) {
                  dialog_handled = true;
                  needs_render = true;
                  break;
                }
                // If modal, block fallthrough to root
                if (d->modal) {
                  dialog_handled = true;  // Consumed by modal barrier
                  break;
                }
              }
            }
          }

          if (!dialog_handled && root) {
            if (root->on_event(event)) {
              needs_render = true;
            }
          }

          // Sync active_tooltip_ immediately if the hovered widget dynamically
          // updated, created, or cleared its tooltip during on_event
          if (hovered_widget_) {
            if (hovered_widget_->tooltip_) {
              if (active_tooltip_ != hovered_widget_->tooltip_) {
                active_tooltip_ = hovered_widget_->tooltip_;
                active_tooltip_->attach(hovered_widget_);
                active_tooltip_->show();
                needs_render = true;
              }
            } else {
              if (active_tooltip_ &&
                  active_tooltip_->target == hovered_widget_) {
                active_tooltip_->hide();
                active_tooltip_ = nullptr;
                needs_render = true;
              }
            }
          }
        } else {
          needs_render = true;
        }
      }  // End batch loop
    }
  }

 private:
  struct Timer {
    int id;
    std::chrono::milliseconds interval;
    std::chrono::steady_clock::time_point last_fire;
    std::function<void()> callback;
  };

  std::vector<Timer> timers_;
  int next_timer_id_ = 1;

  // Thread-safe callback queue for post()
  std::mutex post_mutex_;
  std::vector<std::function<void()>> posted_callbacks_;

  std::shared_ptr<Widget> focused_widget_;

  // Hover State
  std::shared_ptr<Widget> hovered_widget_;
  std::shared_ptr<Tooltip> active_tooltip_;

  Buffer current_buffer_;
  Buffer previous_buffer_;

  void collect_focusables(std::shared_ptr<Widget> widget,
                          std::vector<std::shared_ptr<Widget>> &out) {
    if (!widget) return;
    // VISIBILITY CHECK: Skip hidden widgets and those with zero dimensions
    if (!widget->visible) return;
    if (widget->width <= 0 || widget->height <= 0) return;

    if (widget->focusable && (widget->tab_stop || widget == focused_widget_))
      out.push_back(widget);

    if (auto cont = std::dynamic_pointer_cast<Container>(widget)) {
      for (auto &child : cont->get_children()) {  // access children
        collect_focusables(child, out);
      }
    }
  }

  /// @brief Find the widget at the given coordinates
  /**
   * @brief Recursive helper to find the deepest widget at given screen
   * coordinates. Accounts for Z-order and clipping regions.
   */
  std::shared_ptr<Widget> find_widget_at(std::shared_ptr<Widget> widget, int x,
                                         int y, bool only_focusable = false,
                                         int cx = -1, int cy = -1, int cw = -1,
                                         int ch = -1) {
    if (!widget || !widget->visible) return nullptr;

    // 1. Clipping region check (parent clipping)
    if (cx != -1 && (x < cx || x >= cx + cw || y < cy || y >= cy + ch))
      return nullptr;

    // 2. Widget itself must be within hover coordinates
    if (x < widget->x || x >= widget->x + widget->width || y < widget->y ||
        y >= widget->y + widget->height) {
      return nullptr;
    }

    // 3. Try children first (they are on top)
    if (auto cont = std::dynamic_pointer_cast<Container>(widget)) {
      // New clipping region for children is intersection of current clip and
      // this widget
      Rect widget_rect = {widget->x, widget->y, widget->width, widget->height};
      Rect current_clip = (cx == -1) ? widget_rect : Rect{cx, cy, cw, ch};
      Rect n = current_clip.intersect(widget_rect);

      if (n.width > 0 && n.height > 0) {
        auto children = cont->get_children();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
          auto found = find_widget_at(*it, x, y, only_focusable, n.x, n.y,
                                      n.width, n.height);
          if (found) return found;
        }
      }
    }

    // 4. Check widget itself
    if (widget->hit_test(x, y)) {
      if (!only_focusable || widget->focusable) return widget;
    }

    return nullptr;
  }

  void handle_tab(std::shared_ptr<Widget> root, bool reverse = false) {
    std::shared_ptr<Widget> search_root = root;
    if (!dialog_stack.empty()) {
      search_root = dialog_stack.back();
    }

    std::vector<std::shared_ptr<Widget>> focusables;
    collect_focusables(search_root, focusables);
    if (focusables.empty()) return;

    auto it = std::find(focusables.begin(), focusables.end(), focused_widget_);
    int index = 0;
    if (it != focusables.end()) {
      index = std::distance(focusables.begin(), it);
      if (reverse) {
        index = (index - 1 + focusables.size()) % focusables.size();
      } else {
        index = (index + 1) % focusables.size();
      }
    } else {
      if (reverse)
        index = focusables.size() - 1;
      else
        index = 0;
    }

    if (focused_widget_) focused_widget_->set_focus(false);
    focused_widget_ = focusables[index];
    focused_widget_->set_focus(true);
  }
};

inline bool MenuBar::on_event(const Event &event) {
  if (event.is_mouse_event()) {
    if (event.y == y && event.x >= x && event.x < x + width) {
      // Find which item
      int rel_x = event.x - (x + 1);
      int current_x = 0;
      int found = -1;
      for (size_t i = 0; i < items.size(); ++i) {
        int w = utf8_display_width(items[i].label) + 2;  // spaces
        if (rel_x >= current_x && rel_x < current_x + w) {
          found = i;
          break;
        }
        current_x += w;
      }

      if (event.mouse_move() || event.mouse_drag()) {
        selected_index = found;
        return true;
      }

      if (event.mouse_left()) {
        if (found != -1) {
          if (!items[found].sub_items.empty()) {
            auto menu = std::make_shared<MenuDialog>(app_);
            menu->items = items[found].sub_items;
            menu->normal_bg = bg_color;
            menu->set_bg_color(bg_color);  // Ensure Border matches
            menu->normal_fg = text_color;
            menu->highlight_bg = hover_bg;
            menu->highlight_fg = hover_fg;
            menu->selection_indicator = selection_indicator;
            // Recalculate start_x for the found item to position popup
            // correctly
            int start_x = 0;
            for (int k = 0; k < found; ++k)
              start_x += utf8_display_width(items[k].label) + 2;

            if (app_) app_->open_dialog(menu, x + 1 + start_x, y + 1);
          } else {
            if (items[found].action) items[found].action();
          }
          selected_index = -1;
        }
        return true;
      }
    } else {
      selected_index = -1;
    }
  }
  return false;
}

// Dropdown Implementation Details (Popup)
class DropdownDialog : public Dialog {
 public:
  std::vector<StyledText> options;
  int selected_index = -1;
  std::function<void(int)> on_select;
  std::function<void()> on_close;

  DropdownDialog(App *app) : Dialog(app, BorderStyle::Single) {
    shadow = false;
    modal = false;  // Allow click-through to close
    bg_color_ = Theme::current().panel_bg;
    // No title
  }

  bool on_event(const Event &event) override {
    if (!is_open) return false;

    if (event.is_mouse_event() && event.mouse_left()) {
      if (!contains(event.x, event.y)) {
        // Clicked outside -> Close
        if (on_close) on_close();
        if (app_)
          app_->close_dialog(
              std::dynamic_pointer_cast<Dialog>(shared_from_this()));
        // Consume event to prevent re-opening if header was clicked
        return true;
      }

      // Handle click inside (Selection)
      // Map y to index. y is Start Y of dialog (border).
      // List starts at y + 1 (border top).
      int rel_y = event.y - (y + 1);
      if (rel_y >= 0 && rel_y < (int)options.size()) {
        if (on_select) on_select(rel_y);
        if (on_close) on_close();
        if (app_)
          app_->close_dialog(
              std::dynamic_pointer_cast<Dialog>(shared_from_this()));
        return true;
      }
    }

    if (event.is_mouse_event()) {
      // Hover Selection
      if (event.x >= x && event.x < x + width && event.y >= y &&
          event.y < y + height) {
        int rel_y = event.y - (y + 1);
        if (rel_y >= 0 && rel_y < (int)options.size()) {
          if (event.mouse_move() || event.mouse_drag() ||
              event.mouse_motion()) {
            selected_index = rel_y;
            return true;
          }
        }
      }
    }
    if (event.is_key_event()) {
      if (event.is_escape()) {  // Esc
        if (on_close) on_close();
        if (app_)
          app_->close_dialog(
              std::dynamic_pointer_cast<Dialog>(shared_from_this()));
        return true;
      }
    }
    return Dialog::on_event(event);
  }

  void render(Buffer &buffer) override {
    if (!is_open) return;
    Dialog::render(buffer);

    Color list_bg = Theme::current().panel_bg;
    Color list_fg = Theme::current().foreground;
    Color sel_fg = Theme::current().input_fg;
    Color sel_bg = Theme::current().selection;

    for (size_t i = 0; i < options.size(); ++i) {
      int row_y = y + 1 + i;
      bool is_sel = ((int)i == selected_index);

      int avail_w = width - 2;
      Color row_bg = is_sel ? sel_bg : list_bg;
      Color row_fg = is_sel ? sel_fg : list_fg;

      // Fill background first
      for (int k = 0; k < avail_w; ++k) {
        Cell c;
        c.content = " ";
        c.bg_color = row_bg;
        c.fg_color = row_fg;
        buffer.set(x + 1 + k, row_y, c);
      }

      auto label_chars =
          TextHelper::prepare_text_for_render(options[i].plain_text());
      for (size_t j = 0; j < label_chars.size(); ++j) {
        if ((int)j >= avail_w) break;
        const auto &ci = label_chars[j];
        Cell c;
        c.content = ci.content;
        c.fg_color = row_fg;
        c.bg_color = row_bg;

        // Apply styling
        if (!options[i].empty()) {
          if (const TextSpan *span = options[i].get_span_at_char((int)j)) {
            if (!span->color.is_default) c.fg_color = span->color;
            c.bold = span->bold;
            c.italic = span->italic;
            c.underline = span->underline;
          }
        }

        buffer.set(x + 1 + j, row_y, c);
        if (ci.display_width == 2 && (int)j + 1 < avail_w) {
          Cell skip;
          skip.content = "";
          skip.bg_color = row_bg;
          buffer.set(x + 1 + j + 1, row_y, skip);
        }
      }
    }
  }
};

inline void Dropdown::toggle() {
  if (is_open) {
    close_popup();
  } else {
    is_open = true;
    auto dlg = std::make_shared<DropdownDialog>(app_);
    dlg->options = options;
    dlg->selected_index = selected_index;
    dlg->width = width;
    if (dlg->width < 10) dlg->width = 10;  // Min width
    dlg->fixed_width = dlg->width;

    dlg->height = options.size() + 2;  // +2 for borders
    dlg->fixed_height = dlg->height;

    dlg->on_select = [this](int idx) { make_selection(idx); };
    dlg->on_close = [this]() {
      is_open = false;
      popup_ref_.reset();
    };

    popup_ref_ = dlg;
    if (app_) app_->open_dialog(dlg, x, y + 1);
  }
}

inline void Dropdown::close_popup() {
  is_open = false;
  if (auto dlg = popup_ref_.lock()) {
    if (app_) app_->close_dialog(dlg);
  }
  popup_ref_.reset();
}

/// @brief A spinner widget for indicating activity or progress
/// NOTE: Defined after App class since it requires full App definition for
/// timer methods
class Spinner : public Widget {
 public:
  struct Style {
    std::vector<std::string> frames;
    std::chrono::milliseconds interval = std::chrono::milliseconds(80);
    std::string completed_frame = "✓";
    bool map_value_to_frame =
        false;  // If true, value 0-1 selects frame. If false, spins.
  };

  // Indeterminate Styles (spin continuously)
  /// @brief Braille dots rotating animation
  static Style StyleBrailleSpin() {
    return {{"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"},
            std::chrono::milliseconds(80),
            "✓",
            false};
  }
  /// @brief Classic ASCII line spinner (-\|/)
  static Style StyleLineSpin() {
    return {{"-", "\\", "|", "/"}, std::chrono::milliseconds(100), "✓", false};
  }
  /// @brief Pie segments spinning animation
  static Style StylePieSpin() {
    return {{"○", "◔", "◑", "◕"}, std::chrono::milliseconds(200), "✓", false};
  }
  /// @brief Vertical bar bouncing up and down
  static Style StyleBounceBar() {
    return {
        {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█", "▇", "▆", "▅", "▄", "▃", "▂"},
        std::chrono::milliseconds(80),
        "✓",
        false};
  }
  /// @brief Simple ASCII pulse animation (.oOo.) for maximum compatibility
  static Style StylePulseAscii() {
    return {
        {".", "o", "O", "o", "."}, std::chrono::milliseconds(150), "✓", false};
  }

  // Determinate Styles (map value 0-1 to frame for progress display)
  /// @brief Pie fill progress indicator (○ → ●)
  static Style StylePieProgress() {
    return {
        {"○", "◔", "◑", "◕", "●"}, std::chrono::milliseconds(100), "✓", true};
  }
  /// @brief Vertical bar fill progress indicator
  static Style StyleBarProgress() {
    return {{" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"},
            std::chrono::milliseconds(100),
            "✓",
            true};
  }

  // Config
  Style style = StyleBrailleSpin();
  Color color = Color();
  Color completed_color = Color::Green();

  // State
  float value = -1.0f;  // If < 0, indeterminate. If >= 0, determinate.

  // Animation State
  App *app_ = nullptr;
  TimerId animation_timer_id_ = {-1};
  bool pending_timer_check_ = false;

  Spinner(App *app = nullptr, const Style &s = StyleBrailleSpin()) : app_(app) {
    style = s;
    autosize();
    height = 1;
    fixed_height = 1;
    pending_timer_check_ = true;
  }

  ~Spinner() {
    if (app_ && animation_timer_id_.id != -1) {
      app_->remove_timer(animation_timer_id_);
    }
  }

  void update_animation_state() {
    if (!app_) return;

    bool needs_timer = (value < 0);  // Indeterminate needs timer
    bool has_timer = (animation_timer_id_.id != -1);

    if (needs_timer && !has_timer) {
      // Register timer - callback just forces redraw
      animation_timer_id_ = app_->add_timer(style.interval.count(), []() {});
    } else if (!needs_timer && has_timer) {
      // Unregister
      app_->remove_timer(animation_timer_id_);
      animation_timer_id_ = {-1};
    }
  }

  void autosize() {
    int max_w = 1;
    for (const auto &f : style.frames) {
      int w = utf8_display_width(f);
      if (w > max_w) max_w = w;
    }
    int comp_w = utf8_display_width(style.completed_frame);
    if (comp_w > max_w) max_w = comp_w;

    if (fixed_width < max_w) fixed_width = max_w;
  }

  void render(Buffer &buffer) override {
    if (pending_timer_check_) {
      update_animation_state();
      pending_timer_check_ = false;
    }

    Color fg = color.resolve(Theme::current().primary);
    std::string frame;

    // Determine Frame
    if (value >= 1.0f) {
      frame = style.completed_frame;
      if (!completed_color.is_default)
        fg = completed_color;
      else
        fg = Theme::current().success;

      // If just finished, ensure timer is killed
      if (animation_timer_id_.id != -1) update_animation_state();
    } else if (value >= 0.0f && style.map_value_to_frame) {
      // Determinate Map
      int idx = (int)(value * (style.frames.size() - 1));
      if (idx < 0) idx = 0;
      if (idx >= (int)style.frames.size()) idx = (int)style.frames.size() - 1;
      frame = style.frames[idx];

      // Ensure timer is killed
      if (animation_timer_id_.id != -1) update_animation_state();
    } else {
      // Indeterminate or Determinate-Spin
      auto now = std::chrono::steady_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
      int idx = (ms / style.interval.count()) % style.frames.size();
      frame = style.frames[idx];

      // Ensure timer is running
      if (animation_timer_id_.id == -1) update_animation_state();
    }

    int fw = utf8_display_width(frame);
    // Center horizontally
    int start_x = (width - fw) / 2;
    if (start_x < 0) start_x = 0;

    int start_y = height / 2;

    // Fill background
    for (int dy = 0; dy < height; ++dy) {
      for (int dx = 0; dx < width; ++dx) {
        bool covered_by_frame =
            (dy == start_y && dx >= start_x && dx < start_x + fw);
        if (!covered_by_frame) {
          Color effective_bg = bg_color;
          if (bg_color.is_default) {
            Cell existing = buffer.get(x + dx, y + dy);
            effective_bg = existing.bg_color;
          }
          Cell c;
          c.bg_color = effective_bg;
          c.fg_color =
              fg;  // Should probably be normal fg? kept same as frame for now.
          c.content = " ";
          buffer.set(x + dx, y + dy, c);
        }
      }
    }

    // Render frame on top
    Color effective_bg = bg_color;
    if (bg_color.is_default) {
      Cell existing = buffer.get(x + start_x, y + start_y);
      effective_bg = existing.bg_color;
    }
    render_utf8_text(buffer, frame, x + start_x, y + start_y, width, fg,
                     effective_bg);
  }
};
inline bool MenuDialog::on_event(const Event &event) {
  if (!is_open) return false;

  // Mouse Interaction
  if (event.is_mouse_event()) {
    if (event.x >= x && event.x < x + width && event.y >= y &&
        event.y < y + height) {
      int rel_y = event.y - (y + 1);
      if (rel_y >= 0 && rel_y < (int)items.size()) {
        // Hover
        if (mouse_hover_select && (event.mouse_move() || event.mouse_drag() ||
                                   event.mouse_motion())) {
          selected_index = rel_y;
        }

        // Click
        if (event.mouse_left()) {
          selected_index = rel_y;
          if (items[selected_index].action) items[selected_index].action();
          // Close properly
          if (app_)
            app_->close_dialog(
                std::dynamic_pointer_cast<Dialog>(shared_from_this()));
          return true;
        }
      }
      return !event.mouse_wheel();  // Consume event inside menu, except wheel
    } else {
      // Click outside -> Close
      if (event.mouse_left()) {
        if (app_)
          app_->close_dialog(
              std::dynamic_pointer_cast<Dialog>(shared_from_this()));
        return false;
      }
    }
  }

  // Key Interaction
  if (event.is_key_event()) {
    if (event.is_nav_prev()) {  // Up
      selected_index--;
      if (selected_index < 0) selected_index = items.size() - 1;
      return true;
    }
    if (event.is_nav_next()) {  // Down
      selected_index++;
      if (selected_index >= (int)items.size()) selected_index = 0;
      return true;
    }
    if (event.is_enter()) {  // Enter
      if (selected_index >= 0 && selected_index < (int)items.size()) {
        if (items[selected_index].action) items[selected_index].action();
        if (app_)
          app_->close_dialog(
              std::dynamic_pointer_cast<Dialog>(shared_from_this()));
      }
      return true;
    }
    if (event.is_escape()) {  // Esc
      if (app_)
        app_->close_dialog(
            std::dynamic_pointer_cast<Dialog>(shared_from_this()));
      return true;
    }
  }
  return false;
}

inline void Dialog::open() {
  if (app_)
    app_->open_dialog(std::dynamic_pointer_cast<Dialog>(shared_from_this()));
}

inline void Dialog::open(int screen_x, int screen_y) {
  if (app_)
    app_->open_dialog(std::dynamic_pointer_cast<Dialog>(shared_from_this()),
                      screen_x, screen_y);
  else
    show(screen_x, screen_y);
}

inline void Dialog::close() {
  if (app_)
    app_->close_dialog(std::dynamic_pointer_cast<Dialog>(shared_from_this()));
}

class SearchInput : public Container {
 public:
  StyledText placeholder = "Search...";
  std::vector<StyledText> suggestions;
  std::function<void(const std::string &)> on_search;
  std::function<void(const std::string &)> on_change;

  int suggestion_limit = 5;
  bool show_suggestions_ = false;
  int selected_suggestion_ = -1;

  App *app_ = nullptr;
  std::weak_ptr<DropdownDialog> popup_ref_;

  SearchInput(App *app = nullptr) : app_(app) {
    focusable = true;
    height = 1;
    fixed_height = 1;
    input_ = std::make_shared<Input>();
    input_->placeholder = placeholder;
    input_->focusable =
        false;  // Prevents focused_widget_ from jumping to the child
    input_->tab_stop = false;  // Ensure parent remains the tab stop
    children_.push_back(input_);
  }

  std::string get_value() const { return input_->get_value(); }
  void set_value(const std::string &v) {
    input_->set_value(v);
    last_input_val_ = v;
  }

  void on_focus() override {
    Widget::on_focus();
    input_->set_focus(true);  // Propagate focus to internal input
  }

  void on_blur() override {
    Widget::on_blur();
    input_->set_focus(false);
  }

  void layout() override {
    input_->placeholder = placeholder;  // Sync placeholder to internal input
    input_->x = x;
    input_->y = y;
    input_->width = width;
    input_->height = 1;
  }

  bool on_event(const Event &event) override {
    // Priority 1: Special Key Handling (Navigation & Triggers)
    // Handle BEFORE input to avoid consumption of special combinations
    if (event.is_key_event() && (focused_ || input_->has_focus())) {
      // Ctrl+Space to trigger suggestions manually
      // Also handle key==0 as fallback for terminals that don't properly report
      // Ctrl+Space
      if ((event.key == ' ' && event.ctrl) ||
          (event.key == 0 && !show_suggestions_)) {
        std::string val = input_->get_value();
        filter_suggestions(val, true);  // Force open
        return true;
      }

      // Down arrow: navigate suggestions if open, or open suggestions if closed
      if (event.is_nav_down()) {  // Down
        if (show_suggestions_ &&
            selected_suggestion_ < (int)filtered_suggestions_.size() - 1) {
          selected_suggestion_++;
          auto dlg = popup_ref_.lock();
          if (dlg) dlg->selected_index = selected_suggestion_;
          return true;
        } else if (!show_suggestions_) {
          // Open suggestions on Down arrow when closed
          std::string val = input_->get_value();
          filter_suggestions(val, true);
          return true;
        }
      }
      if (event.is_nav_up()) {  // Up
        if (selected_suggestion_ > 0) {
          selected_suggestion_--;
          auto dlg = popup_ref_.lock();
          if (dlg) dlg->selected_index = selected_suggestion_;
          return true;
        }
      }
      if (event.is_enter()) {  // Enter
        if (selected_suggestion_ >= 0 &&
            selected_suggestion_ < (int)filtered_suggestions_.size()) {
          input_->set_value(filtered_suggestions_[selected_suggestion_]);
          last_input_val_ = input_->get_value();
        }
        show_suggestions_ = false;
        close_popup();
        if (on_search) on_search(input_->get_value());
        return true;
      }
      if (event.is_escape()) {  // Escape
        show_suggestions_ = false;
        close_popup();
        return true;
      }
    }

    // Priority 2: Dispatch to Input (Typing)
    // Skip if key is 0 to avoid inserting nulls
    bool consumed = false;
    if (event.type != EventType::Key || event.key != 0) {
      consumed = input_->on_event(event);
    }

    // Priority 3: Change Detection
    std::string val = input_->get_value();
    if (val != last_input_val_) {
      last_input_val_ = val;
      filter_suggestions(val);
      show_suggestions_ = !val.empty() && !filtered_suggestions_.empty();
      if (on_change) on_change(val);
    }

    // Return true for any key event when focused to trigger render
    if (event.is_key_event() && (focused_ || input_->has_focus())) {
      return true;
    }

    return consumed;
  }

 private:
  std::shared_ptr<Input> input_;
  std::vector<StyledText> filtered_suggestions_;
  std::string last_input_val_;

  void filter_suggestions(const std::string &query, bool force = false) {
    filtered_suggestions_.clear();
    if (query.empty() && !force) {
      close_popup();
      return;
    }

    if (force && query.empty()) {
      // Show all suggestions, up to limit
      for (const auto &s : suggestions) {
        filtered_suggestions_.push_back(s);
        if ((int)filtered_suggestions_.size() >= suggestion_limit) break;
      }
    } else {
      std::string lower_query = query;
      for (char &c : lower_query) c = std::tolower(c);

      for (const auto &s : suggestions) {
        std::string label = s.plain_text();
        std::string lower_s = label;
        for (char &c : lower_s) c = std::tolower(c);

        if (lower_s.find(lower_query) != std::string::npos) {
          filtered_suggestions_.push_back(s);
          if ((int)filtered_suggestions_.size() >= suggestion_limit) break;
        }
      }
    }

    // Update popup if needed
    if (!filtered_suggestions_.empty()) {
      show_suggestions_ = true;
      auto dlg = popup_ref_.lock();
      if (!dlg) {
        // Open new popup
        if (app_) {
          dlg = std::make_shared<DropdownDialog>(app_);
          dlg->steal_focus = false;  // Prevent popup from stealing focus
          dlg->options = filtered_suggestions_;
          dlg->width = width;
          if (dlg->width < 10) dlg->width = 10;
          dlg->fixed_width = dlg->width;
          dlg->height = (int)filtered_suggestions_.size() + 2;
          dlg->fixed_height = dlg->height;

          dlg->on_select = [this](int idx) {
            if (idx >= 0 && idx < (int)filtered_suggestions_.size()) {
              input_->set_value(filtered_suggestions_[idx].plain_text());
              last_input_val_ =
                  input_->get_value();  // Update last val to suppress trigger
              // Note: Don't call close_popup() here - DropdownDialog handles
              // closing Just reset our state; dialog will be closed by
              // DropdownDialog::on_event
              show_suggestions_ = false;
              popup_ref_.reset();
              if (on_search) on_search(input_->get_value());
            }
          };

          popup_ref_ = dlg;
          app_->open_dialog(dlg, x, y + 1);
        }
      } else {
        // Update existing popup
        dlg->options = filtered_suggestions_;
        dlg->height = (int)filtered_suggestions_.size() + 2;
        dlg->fixed_height = dlg->height;
      }
    } else {
      show_suggestions_ = false;
      close_popup();
    }

    selected_suggestion_ = filtered_suggestions_.empty() ? -1 : 0;
    // Sync selection to popup
    auto dlg_sync = popup_ref_.lock();
    if (dlg_sync) dlg_sync->selected_index = selected_suggestion_;
  }

  void close_popup() {
    show_suggestions_ = false;
    auto dlg = popup_ref_.lock();
    if (dlg && app_) {
      app_->close_dialog(std::dynamic_pointer_cast<Dialog>(dlg));
      popup_ref_.reset();
    }
  }
};

// ========================================================================
// ColorPicker Widget
// ========================================================================

/// @brief Interactive color picker widget with HSV gradient and RGB/Hex display
/// Allows visual color selection via hue/saturation gradient area and value
/// slider
class ColorPicker : public Widget {
 public:
  // Current selected color in HSV (0-1 range)
  float hue = 0.0f;         ///< Hue component (0.0 - 1.0)
  float saturation = 1.0f;  ///< Saturation component (0.0 - 1.0)
  float value = 1.0f;       ///< Value/brightness component (0.0 - 1.0)

  // Configuration
  int gradient_width = 20;   ///< Width of the hue/saturation gradient area
  int gradient_height = 10;  ///< Height of the hue/saturation gradient area
  bool show_values = true;   ///< Show RGB and Hex values
  bool show_preview = true;  ///< Show color preview box

  // Styling
  std::string cursor_char = "◆";  ///< Character for gradient cursor
  Color border_color = Color();   ///< Border color (defaults to theme)

  // Callbacks
  std::function<void(Color)> on_change;  ///< Called when color changes
  std::function<void(Color)> on_select;  ///< Called when Enter is pressed

  ColorPicker() {
    focusable = true;
    fixed_height = 14;  // Gradient + slider + values
    fixed_width = 35;
  }

  /// @brief Get the currently selected color as RGB
  Color get_color() const { return Color::hsv_to_rgb(hue, saturation, value); }

  /// @brief Set color from RGB values
  void set_color(const Color &c) {
    Color::rgb_to_hsv(c, hue, saturation, value);
  }

  /// @brief Set color from hex string (e.g., "#FF5500" or "FF5500")
  void set_hex(const std::string &hex) {
    Color c = hex_to_color(hex);
    set_color(c);
  }

  /// @brief Get hex string representation of current color
  std::string get_hex() const {
    Color c = get_color();
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return std::string(buf);
  }

  void render(Buffer &buffer) override {
    Color bg = bg_color.resolve(Theme::current().background);
    Color brd = border_color.resolve(Theme::current().border);
    Color fg = fg_color.resolve(Theme::current().foreground);

    if (focused_) {
      brd = Theme::current().border_focus;
    }

    // Calculate layout
    int gw = std::min(gradient_width, width - 5);
    int gh = std::min(gradient_height, height - 4);
    if (gw < 5) gw = 5;
    if (gh < 3) gh = 3;

    int slider_x = x + gw + 2;
    int preview_x = slider_x + 3;

    // Draw hue/saturation gradient
    for (int dy = 0; dy < gh; ++dy) {
      for (int dx = 0; dx < gw; ++dx) {
        float h = (float)dx / (float)(gw - 1);
        float s = 1.0f - (float)dy / (float)(gh - 1);
        Color c = Color::hsv_to_rgb(h, s, value);

        Cell cell;
        cell.content = " ";
        cell.bg_color = c;
        buffer.set(x + dx, y + dy, cell);
      }
    }

    // Draw gradient cursor
    int cursor_x = (int)(hue * (gw - 1));
    int cursor_y = (int)((1.0f - saturation) * (gh - 1));
    {
      Cell cell;
      cell.content = cursor_char;
      cell.fg_color =
          Color::contrast_color(Color::hsv_to_rgb(hue, saturation, value));
      cell.bg_color = Color::hsv_to_rgb(hue, saturation, value);
      buffer.set(x + cursor_x, y + cursor_y, cell);
    }

    // Draw value/brightness slider (vertical)
    int slider_h = gh;
    for (int dy = 0; dy < slider_h; ++dy) {
      float v = 1.0f - (float)dy / (float)(slider_h - 1);
      Color c = Color::hsv_to_rgb(hue, saturation, v);

      Cell cell;
      cell.content = "▐";
      cell.fg_color = c;
      cell.bg_color = bg;
      buffer.set(slider_x, y + dy, cell);

      cell.content = "█";
      buffer.set(slider_x + 1, y + dy, cell);
    }

    // Draw value slider cursor
    int slider_cursor_y = (int)((1.0f - value) * (slider_h - 1));
    {
      Cell cell;
      cell.content = "◀";
      cell.fg_color = focused_ ? Theme::current().primary : fg;
      cell.bg_color = bg;
      buffer.set(slider_x + 2, y + slider_cursor_y, cell);
    }

    // Draw color preview
    if (show_preview) {
      Color current = get_color();
      int preview_w = 6;
      int preview_h = 3;
      for (int dy = 0; dy < preview_h; ++dy) {
        for (int dx = 0; dx < preview_w; ++dx) {
          Cell cell;
          cell.content = " ";
          cell.bg_color = current;
          buffer.set(preview_x + dx, y + dy, cell);
        }
      }
    }

    // Draw RGB and Hex values
    if (show_values) {
      Color current = get_color();
      int text_y = y + gh + 1;

      // RGB values
      char rgb_buf[32];
      std::snprintf(rgb_buf, sizeof(rgb_buf), "R:%3d G:%3d B:%3d", current.r,
                    current.g, current.b);
      std::string rgb_str = rgb_buf;
      for (size_t i = 0; i < rgb_str.size() && (int)(x + i) < x + width; ++i) {
        Cell cell;
        cell.content = std::string(1, rgb_str[i]);
        cell.fg_color = fg;
        cell.bg_color = bg;
        buffer.set(x + i, text_y, cell);
      }

      // Hex value
      std::string hex = get_hex();
      text_y++;
      for (size_t i = 0; i < hex.size() && (int)(x + i) < x + width; ++i) {
        Cell cell;
        cell.content = std::string(1, hex[i]);
        cell.fg_color = fg;
        cell.bg_color = bg;
        buffer.set(x + i, text_y, cell);
      }

      // HSV values
      text_y++;
      char hsv_buf[32];
      std::snprintf(hsv_buf, sizeof(hsv_buf), "H:%3d S:%3d V:%3d",
                    (int)(hue * 360), (int)(saturation * 100),
                    (int)(value * 100));
      std::string hsv_str = hsv_buf;
      for (size_t i = 0; i < hsv_str.size() && (int)(x + i) < x + width; ++i) {
        Cell cell;
        cell.content = std::string(1, hsv_str[i]);
        cell.fg_color = fg;
        cell.bg_color = bg;
        buffer.set(x + i, text_y, cell);
      }
    }
  }

  bool on_event(const Event &event) override {
    if (event.is_mouse_event() && contains(event.x, event.y) &&
        !event.mouse_wheel()) {
      if (event.mouse_left() || event.mouse_drag()) {
        set_focus(true);

        int gw = std::min(gradient_width, width - 5);
        int gh = std::min(gradient_height, height - 4);
        if (gw < 5) gw = 5;
        if (gh < 3) gh = 3;

        int slider_x = x + gw + 2;

        // Check if clicking on gradient
        if (event.x >= x && event.x < x + gw && event.y >= y &&
            event.y < y + gh) {
          float old_h = hue, old_s = saturation;
          hue = (float)(event.x - x) / (float)(gw - 1);
          saturation = 1.0f - (float)(event.y - y) / (float)(gh - 1);
          hue = std::max(0.0f, std::min(1.0f, hue));
          saturation = std::max(0.0f, std::min(1.0f, saturation));
          if (hue != old_h || saturation != old_s) {
            if (on_change) on_change(get_color());
          }
          return true;
        }

        // Check if clicking on value slider
        if (event.x >= slider_x && event.x <= slider_x + 2 && event.y >= y &&
            event.y < y + gh) {
          float old_v = value;
          value = 1.0f - (float)(event.y - y) / (float)(gh - 1);
          value = std::max(0.0f, std::min(1.0f, value));
          if (value != old_v) {
            if (on_change) on_change(get_color());
          }
          return true;
        }
      }
      return true;
    }

    if (event.is_key_event() && focused_) {
      float step = 0.05f;
      bool changed = false;

      // Arrow keys for hue/saturation in gradient mode
      // Arrow_Left (1068 / D), Arrow_Right (1067 / C), Arrow_Up (1065 / A),
      // Arrow_Down (1066 / B)
      if (event.is_nav_right()) {  // Right
        hue += step;
        if (hue > 1.0f) hue = 1.0f;
        changed = true;
      }
      if (event.is_nav_left()) {  // Left
        hue -= step;
        if (hue < 0.0f) hue = 0.0f;
        changed = true;
      }
      if (event.is_nav_up()) {  // Up
        saturation += step;
        if (saturation > 1.0f) saturation = 1.0f;
        changed = true;
      }
      if (event.is_nav_down()) {  // Down
        saturation -= step;
        if (saturation < 0.0f) saturation = 0.0f;
        changed = true;
      }

      // Page Up/Down for value
      if (event.key == '=' || event.key == '+') {
        value += step;
        if (value > 1.0f) value = 1.0f;
        changed = true;
      }
      if (event.key == '-' || event.key == '_') {
        value -= step;
        if (value < 0.0f) value = 0.0f;
        changed = true;
      }

      // Enter to select
      if (event.is_activate()) {
        if (on_select) on_select(get_color());
        return true;
      }

      if (changed) {
        if (on_change) on_change(get_color());
        return true;
      }
    }

    return false;
  }

 private:
  /// @brief Parse hex color string to Color
  static Color hex_to_color(const std::string &hex) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);

    if (h.size() == 6) {
      unsigned int val;
      std::stringstream ss;
      ss << std::hex << h;
      ss >> val;
      return Color{(uint8_t)((val >> 16) & 0xFF), (uint8_t)((val >> 8) & 0xFF),
                   (uint8_t)(val & 0xFF)};
    }
    return Color::White();
  }
};

}  // namespace cpptui

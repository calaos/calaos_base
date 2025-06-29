#!/bin/bash

set -e  # Stop on error

# Colors for display
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Go to project root directory
cd "$(dirname "$0")/.."

echo_info "Starting translation update..."

# Update source file list
echo_info "Updating POTFILES.in..."
find ./src -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.c" \) | sort > po/POTFILES.in
echo_info "$(wc -l < po/POTFILES.in) source files found"

# Clean and regenerate autotools files completely
echo_info "Cleaning old build files..."
make distclean 2>/dev/null || true
rm -f config.status config.log
rm -f po/Makefile po/Makefile.in

echo_info "Regenerating autotools files..."
./autogen.sh

echo_info "Configuring project..."
./configure

# Check if po/ files are present
if [ ! -f "po/Makefile" ]; then
    echo_error "po/Makefile does not exist. Configuration problem."
    exit 1
fi

# Update .po files
echo_info "Updating translation files..."
cd po

# Generate/update main .pot file
if ! make calaos.pot; then
    echo_error "Failed to generate .pot file"
    exit 1
fi

# Update all .po files
if ! make update-po; then
    echo_error "Failed to update .po files"
    exit 1
fi

# Clean all .po files (remove fuzzy and obsolete entries)
echo_info "Cleaning up .po files..."
for po_file in *.po; do
    if [ -f "$po_file" ]; then
        msgattrib --no-obsolete --no-fuzzy "$po_file" -o "$po_file.tmp"
        mv "$po_file.tmp" "$po_file"
        echo_info "Cleaned $po_file"
    fi
done

echo_info "Translation update completed successfully!"
echo_info ".po files updated in the po/ directory"

# Display a summary of available languages
echo_info "Available languages for translation:"
for lang in *.po; do
    if [ -f "$lang" ]; then
        lang_code=$(basename "$lang" .po)
        # Count translated and untranslated strings
        total=$(msgfmt --statistics "$lang" 2>&1 | grep -o '[0-9]\+ translated' | grep -o '[0-9]\+' || echo "0")
        untranslated=$(msgfmt --statistics "$lang" 2>&1 | grep -o '[0-9]\+ untranslated' | grep -o '[0-9]\+' || echo "0")
        fuzzy=$(msgfmt --statistics "$lang" 2>&1 | grep -o '[0-9]\+ fuzzy' | grep -o '[0-9]\+' || echo "0")

        echo "  - $lang_code: $total translated, $fuzzy fuzzy, $untranslated untranslated"
    fi
done

echo_info "Translation update script finished successfully!"
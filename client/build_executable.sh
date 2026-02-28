#!/bin/bash
# Build script for FTP Client executable

echo "Building FTP Client executable..."

# Check if PyInstaller is installed
if ! python -c "import PyInstaller" 2>/dev/null; then
    echo "PyInstaller not found. Installing..."
    pip install pyinstaller
fi

# Clean previous builds
echo "Cleaning previous builds..."
rm -rf build/ dist/ *.spec

# Build the executable
echo "Building executable with PyInstaller..."
pyinstaller --onefile \
    --name client \
    --add-data "src:src" \
    --hidden-import tkinter \
    --hidden-import tkinter.ttk \
    --hidden-import tkinter.scrolledtext \
    --hidden-import tkinter.filedialog \
    --hidden-import tkinter.messagebox \
    --hidden-import readline \
    src/main.py

echo ""
echo "Build complete!"
echo "Executable location: dist/client"
echo ""
echo "Usage examples:"
echo "  GUI mode:              ./dist/client --gui"
echo "  Interactive CLI:       ./dist/client"
echo "  Connect and CLI:       ./dist/client -ip localhost -port 21 -u user -P pass"
echo "  Batch mode:            ./dist/client localhost -c 'USER anonymous' -c 'PASS anonymous'"
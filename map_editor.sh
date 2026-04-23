g++ map_editor.cpp -o map_editor -O2 -lpng -lX11 $(pkg-config --cflags --libs freetype2)
time ./map_editor

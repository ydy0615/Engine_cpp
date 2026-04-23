g++ test.cpp -o test -O2 -lpng -lX11 $(pkg-config --cflags --libs freetype2)
time ./test

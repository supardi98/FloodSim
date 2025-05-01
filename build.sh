gcc main.c -o main $(gdal-config --cflags) $(gdal-config --libs) -lm
time ./main data/dtm.tif 0.2
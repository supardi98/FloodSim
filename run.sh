gcc main.c -o main $(gdal-config --cflags) $(gdal-config --libs) -lm
time ./main dtm.tif 1


gcc main.c -o main $(gdal-config --cflags) $(gdal-config --libs) -lm
time ./main data/data_2.tif data/lahan.tif 50

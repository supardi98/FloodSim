cd src
# gcc main.c smoothing.c gdalShortcut.c -o ../main $(gdal-config --cflags) $(gdal-config --libs) -lm -lopen
gcc main.c transformation.c smoothing.c gdalShortcut.c -o ../main $(gdal-config --cflags) $(gdal-config --libs) -lm
cd ../
cp asli.tif data/asli.tif
time ./main data/asli.tif data/lahan.tif result/result_9.tif 250 -7.5352051 112.6938265 -7.5355800 112.6941597 75 0
# time ./main data/tess5.tif data/lahan.tif 300

cd src
# gcc main.c smoothing.c gdalShortcut.c -o ../main $(gdal-config --cflags) $(gdal-config --libs) -lm -lopen
gcc main.c transformation.c smoothing.c gdalShortcut.c -o ../main $(gdal-config --cflags) $(gdal-config --libs) -lm
cd ../
mkdir -p data
mkdir -p result
cp asli.tif data/asli.tif
time ./main data/asli.tif data/lahan.tif result/result_9.tif 250 -7.520335763075681 112.70448624963102 -7.520133619040644 112.7045767302233 4000 0.5 100
# time ./main data/tess5.tif data/lahan.tif 300

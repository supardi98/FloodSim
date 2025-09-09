cd src
# gcc main.c smoothing.c gdalShortcut.c -o ../main $(gdal-config --cflags) $(gdal-config --libs) -lm -lopen
gcc main.c transformation.c smoothing.c gdalShortcut.c -o ../main $(gdal-config --cflags) $(gdal-config --libs) -lm
cd ../
mkdir -p result
time ./main data/dem.tif data/lahan.tif result/result_build_test.tif result/pump_log_build_test.csv 0,2.5,0,5.0 15,15,15,15 5,5,5,5 -7.5200680748355 112.70477092805535 -7.520508553989 112.70464135101226 4000 0.5 5
# time ./main data/tess5.tif data/lahan.tif 300

#!/bin/bash

# Mengecek jumlah argumen minimal (16 argumen tanpa pumpradius)
# Contoh: ./run.sh data/dem.tif data/lahan.tif result/result.tif result/pump_log.csv result/tiles 0,2.5,0,5.0 15,15,15,15 5,5,5,5 -7.5200680748355 112.70477092805535 -7.520508553989 112.70464135101226 4000 0.5 5
if [ "$#" -lt 14 ]; then
    echo "Usage: $0 <dem.tif> <landuse.tif> <output.tif> <output_pump_log.csv> <output_tiles> "
    echo "       <rain_mm1,mm2,...> <interval_min1,interval_min2,...> "
    echo "       <iter1,iter2,...> <pumpInLat,...> <pumpInLon,...> "
    echo "       <pumpOutLat,...> <pumpOutLon,...> <pumpCapacity_m3_per_hr,...> "
    echo "       <pumpThreshold_m,...> [<pumpRadius_m,...>]"
    exit 1
fi

# Positional arguments
DEM="$1"
LANDUSE="$2"
OUTPUT_TIF="$3"
OUTPUT_PUMP="$4"
OUTPUT_TILES="$5"
RAIN_MM="$6"
INTERVAL_MIN="$7"
ITER="$8"
PUMP_IN_LAT="$9"
PUMP_IN_LON="${10}"
PUMP_OUT_LAT="${11}"
PUMP_OUT_LON="${12}"
PUMP_CAPACITY="${13}"
PUMP_THRESHOLD="${14}"
PUMP_RADIUS="${15}"   # opsional, bisa kosong

echo "DEM: $DEM"
echo "LANDUSE: $LANDUSE"
echo "OUTPUT_TIF: $OUTPUT_TIF"
echo "OUTPUT_PUMP: $OUTPUT_PUMP"
echo "OUTPUT_TILES: $OUTPUT_TILES"
echo "RAIN_MM: $RAIN_MM"
echo "INTERVAL_MIN: $INTERVAL_MIN"
echo "ITER: $ITER"
echo "PUMP_IN_LAT: $PUMP_IN_LAT"
echo "PUMP_IN_LON: $PUMP_IN_LON"
echo "PUMP_OUT_LAT: $PUMP_OUT_LAT"
echo "PUMP_OUT_LON: $PUMP_OUT_LON"
echo "PUMP_CAPACITY: $PUMP_CAPACITY"
echo "PUMP_THRESHOLD: $PUMP_THRESHOLD"
echo "PUMP_RADIUS: $PUMP_RADIUS"

shift 15  # sisa argumen kalau ada tambahan nanti

PROGRAM_TO_RUN="./main"

echo "Mulai simulasi dengan curah hujan $RAIN_MM mm"

# Mempersiapkan argumen pumpradius hanya jika diisi
if [ -z "$PUMP_RADIUS" ]; then
    $PROGRAM_TO_RUN "$DEM" "$LANDUSE" "$OUTPUT_TIF" "$OUTPUT_PUMP" \
        "$RAIN_MM" "$INTERVAL_MIN" "$ITER" "$PUMP_IN_LAT" "$PUMP_IN_LON" \
        "$PUMP_OUT_LAT" "$PUMP_OUT_LON" "$PUMP_CAPACITY" "$PUMP_THRESHOLD"
else
    $PROGRAM_TO_RUN "$DEM" "$LANDUSE" "$OUTPUT_TIF" "$OUTPUT_PUMP" \
        "$RAIN_MM" "$INTERVAL_MIN" "$ITER" "$PUMP_IN_LAT" "$PUMP_IN_LON" \
        "$PUMP_OUT_LAT" "$PUMP_OUT_LON" "$PUMP_CAPACITY" "$PUMP_THRESHOLD" "$PUMP_RADIUS"
fi

# Cek exit status
if [ $? -ne 0 ]; then
    exit 1
fi

echo "Simulasi selesai"
echo "Mulai Tiling"

gdal_translate -of VRT -ot Byte -scale 0 3 "$OUTPUT_TIF" result/result.vrt
gdaldem color-relief result/result.vrt colormap/jet.clr result/output.tif -alpha

rm -rf "$OUTPUT_TILES"
gdal2tiles.py -z 12-17 --resampling=bilinear --xyz result/output.tif "$OUTPUT_TILES"

rm -rf result/output.tif
rm -rf result/result.vrt

echo "Selesai"

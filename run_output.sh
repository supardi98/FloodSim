#------------------------------
echo "Simulasi selesai"
echo "Mulai Tiling"

gdal_translate -of VRT -ot Byte -scale 0 3 "$OUTPUT_TIF" result/result.vrt
gdaldem color-relief result/result.vrt colormap/jet.clr result/output.tif -alpha

rm -rf "$OUTPUT_TILES"
gdal2tiles.py -z 12-17 --resampling=bilinear --xyz result/output.tif "$OUTPUT_TILES"

rm -rf result/output.tif
rm -rf result/result.vrt

echo "Selesai"

#include "gdal.h"
#include "ogr_srs_api.h"
#include "ogr_api.h"
#include "cpl_conv.h"
#include <stdio.h>
#include <string.h>

// Konversi lat/lon (EPSG:4326) → pixel coordinate (col,row)
int LatLonToPixel(GDALDatasetH hDataset, double lat, double lon, int *col, int *row)
{
    if (hDataset == NULL || row == NULL || col == NULL)
    {
        return -1;
    }

    // --- Ambil GeoTransform raster ---
    double geoTransform[6];
    if (GDALGetGeoTransform(hDataset, geoTransform) != CE_None)
    {
        fprintf(stderr, "Tidak bisa ambil GeoTransform.\n");
        return -1;
    }
    for (int i = 0; i < 6; i++)
    {
        printf("%f\n", geoTransform[i]);
    }

    // --- Ambil SRS raster ---
    const char *projRef = GDALGetProjectionRef(hDataset);
    printf(projRef);
    double x = lon; // perhatikan: lon = X, lat = Y
    double y = lat;

    if (projRef != NULL && strlen(projRef) > 0)
    {
        // Raster punya SRS → transformasi dari EPSG:4326
        OGRSpatialReferenceH hRasterSRS = OSRNewSpatialReference(NULL);
        if (OSRImportFromWkt(hRasterSRS, (char **)&projRef) == OGRERR_NONE)
        {
            OGRSpatialReferenceH hLatLonSRS = OSRNewSpatialReference(NULL);
            OSRImportFromEPSG(hLatLonSRS, 4326);

            OGRCoordinateTransformationH hTransform =
                OCTNewCoordinateTransformation(hLatLonSRS, hRasterSRS);
            printf("%f,%f\n", x, y);
            if (hTransform != NULL)
            {
                if (!OCTTransform(hTransform, 1, &y, &x, NULL))
                {
                    fprintf(stderr, "Transformasi koordinat gagal.\n");
                }
                OCTDestroyCoordinateTransformation(hTransform);
            }

            OSRDestroySpatialReference(hLatLonSRS);
        }
        else
        {
            fprintf(stderr, "Raster punya SRS tapi gagal dibaca, pakai koordinat apa adanya.\n");
        }

        OSRDestroySpatialReference(hRasterSRS);
    }
    else
    {
        fprintf(stderr, "Warning: Raster tidak punya sistem koordinat, asumsikan input sudah sesuai raster CRS.\n");
    }
    printf("%f,%f\n", x, y);

    // --- Hitung pixel/line dari GeoTransform ---
    double px = (y - geoTransform[0]) / geoTransform[1];
    double py = (x - geoTransform[3]) / geoTransform[5];

    *row = (int)floor(py + 0.5);
    *col = (int)floor(px + 0.5);
    return 0;
}

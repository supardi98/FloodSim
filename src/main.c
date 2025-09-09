// main.c (modified for time-series rainfall)
#include "cpl_conv.h"
#include "gdal.h"
#include "gdalShortcut.h"
#include "smoothing.h"
#include "transformation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  double inLat, inLon, outLat, outLon;
  int px, py;
  int ox, oy;
  float capacity_m3hr;
  float threshold;
  int radius_px;
  int state;
  int cooldown;
} Pump;

int parseFloatArray(const char *str, float **out, int *n) {
  if (!str)
    return -1;
  char *s = strdup(str);
  if (!s)
    return -1;
  int count = 0;
  for (char *p = s; *p; p++)
    if (*p == ',')
      count++;
  *n = count + 1;
  *out = (float *)malloc(sizeof(float) * (*n));
  if (!(*out)) {
    free(s);
    return -1;
  }
  int i = 0;
  char *token = strtok(s, ",");
  while (token && i < *n) {
    (*out)[i++] = atof(token);
    token = strtok(NULL, ",");
  }
  free(s);
  return 0;
}

int main(int argc, const char *argv[]) {
  float infil_capacity_mm_per_hr[4] = {0.0f, 10.0f, 5.0f, 30.0f};

  if (argc < 13) {
    fprintf(stderr,
            "Usage: %s <dem.tif> <landuse.tif> <output.tif> "
            "<rain_mm1,mm2,...> <interval_min1,interval_min2,...> "
            "<iter1,iter2,...> <pumpInLat,...> <pumpInLon,...> "
            "<pumpOutLat,...> <pumpOutLon,...> <pumpCapacity_m3_per_hr,...> "
            "<pumpThreshold_m,...> [<pumpRadius_m,...>]\n",
            argv[0]);
    return 1;
  }

  const char *demFile = argv[1];
  const char *lahanFile = argv[2];
  const char *output = argv[3];

  // parse rainfall time-series arrays
  float *rain_mm_array = NULL;
  float *rain_interval_min_array = NULL;
  float *rain_iter_f_array = NULL;
  int nRain1 = 0, nRain2 = 0, nRain3 = 0;

  if (parseFloatArray(argv[4], &rain_mm_array, &nRain1) != 0) {
    fprintf(stderr, "Failed parse rain_mm array\n");
    return 1;
  }
  if (parseFloatArray(argv[5], &rain_interval_min_array, &nRain2) != 0) {
    fprintf(stderr, "Failed parse rain_interval_min array\n");
    return 1;
  }
  if (parseFloatArray(argv[6], &rain_iter_f_array, &nRain3) != 0) {
    fprintf(stderr, "Failed parse rain_iter array\n");
    return 1;
  }

  if (!(nRain1 == nRain2 && nRain1 == nRain3)) {
    fprintf(stderr, "Rain arrays must have same length\n");
    return 1;
  }
  int nSteps = nRain1;

  // pump args start at argv[7]...
  float *inLat = NULL, *inLon = NULL, *outLat = NULL, *outLon = NULL;
  float *capacities = NULL, *thresholds = NULL, *radii = NULL;
  int nPumps1 = 0, nPumps2 = 0, nPumps3 = 0, nPumps4 = 0, nPumps5 = 0,
      nPumps6 = 0, nPumps7 = 0;
  int pumpArgBase = 7;
  if (argc <= pumpArgBase + 4) {
    fprintf(stderr, "Not enough pump args\n");
    return 1;
  }

  // safe indexes
  // argv[pumpArgBase + 0] = pumpInLat
  // +1 = pumpInLon, +2 = pumpOutLat, +3 = pumpOutLon, +4 = capacities, +5 =
  // thresholds, +6 optional radii
  if (parseFloatArray(argv[pumpArgBase + 0], &inLat, &nPumps1) != 0)
    return 1;
  if (parseFloatArray(argv[pumpArgBase + 1], &inLon, &nPumps2) != 0)
    return 1;
  if (parseFloatArray(argv[pumpArgBase + 2], &outLat, &nPumps3) != 0)
    return 1;
  if (parseFloatArray(argv[pumpArgBase + 3], &outLon, &nPumps4) != 0)
    return 1;
  if (parseFloatArray(argv[pumpArgBase + 4], &capacities, &nPumps5) != 0)
    return 1;
  if (parseFloatArray(argv[pumpArgBase + 5], &thresholds, &nPumps6) != 0)
    return 1;

  int nPumps = nPumps1;
  if (argc >= pumpArgBase + 7 && argv[pumpArgBase + 6]) {
    if (parseFloatArray(argv[pumpArgBase + 6], &radii, &nPumps7) != 0) {
      // fallback default
      nPumps7 = nPumps;
      radii = (float *)malloc(sizeof(float) * nPumps);
      for (int i = 0; i < nPumps; i++)
        radii[i] = 2.0f;
    }
  } else {
    nPumps7 = nPumps;
    radii = (float *)malloc(sizeof(float) * nPumps);
    for (int i = 0; i < nPumps; i++)
      radii[i] = 2.0f;
  }

  if (!(nPumps1 == nPumps2 && nPumps1 == nPumps3 && nPumps1 == nPumps4 &&
        nPumps1 == nPumps5 && nPumps1 == nPumps6 && nPumps1 == nPumps7)) {
    fprintf(stderr, "All pump arrays must have same length\n");
    return 1;
  }

  GDALAllRegister();

  Raster dem = OpenTiff((char *)demFile, 0, -32767);
  if (!dem.dataset) {
    fprintf(stderr, "Failed to open DEM: %s\n", demFile);
    return 1;
  }
  float *elevArray = (float *)dem.pixelArray;

  Raster lahanData = OpenTiff((char *)lahanFile, 1, -1);
  if (!lahanData.dataset) {
    fprintf(stderr, "Failed to open landuse: %s\n", lahanFile);
    GDALClose(dem.dataset);
    return 1;
  }
  int *lahan = (int *)lahanData.pixelArray;

  int nXSize = dem.nXSize;
  int nYSize = dem.nYSize;
  if (nXSize <= 0 || nYSize <= 0) {
    fprintf(stderr, "Invalid raster size\n");
    GDALClose(dem.dataset);
    GDALClose(lahanData.dataset);
    return 1;
  }

  int hasNoData = 0;
  double noDataValue = GDALGetRasterNoDataValue(dem.band, &hasNoData);

  // defaults (can be tuned)
  float gsd = 0.5f;
  float pixelArea = gsd * gsd;

  // allocate pumps
  Pump *pumps = (Pump *)calloc((size_t)nPumps, sizeof(Pump));
  if (!pumps) {
    fprintf(stderr, "Failed to alloc pumps\n");
    return 1;
  }
  for (int i = 0; i < nPumps; i++) {
    int px = -1, py = -1, ox = -1, oy = -1;
    LatLonToPixel(dem.dataset, inLat[i], inLon[i], &px, &py);
    LatLonToPixel(dem.dataset, outLat[i], outLon[i], &ox, &oy);
    // set all coords to -1 initially (safety)
    pumps[i].px = -1;
    pumps[i].py = -1;
    pumps[i].ox = -1;
    pumps[i].oy = -1;
    if (px < 0 || px >= nXSize || py < 0 || py >= nYSize || ox < 0 ||
        ox >= nXSize || oy < 0 || oy >= nYSize) {
      fprintf(stderr,
              "Pump %d outside DEM bounds, ignored (in:%f,%f,out:%f,%f -> "
              "px=%d,py=%d,ox=%d,oy=%d)\n",
              i, inLat[i], inLon[i], outLat[i], outLon[i], px, py, ox, oy);
      continue;
    }
    pumps[i].inLat = inLat[i];
    pumps[i].inLon = inLon[i];
    pumps[i].outLat = outLat[i];
    pumps[i].outLon = outLon[i];
    pumps[i].px = px;
    pumps[i].py = py;
    pumps[i].ox = ox;
    pumps[i].oy = oy;
    pumps[i].capacity_m3hr = capacities[i];
    pumps[i].threshold = thresholds[i];
    pumps[i].radius_px = (int)ceil(radii[i] / gsd);
    if (pumps[i].radius_px < 1)
      pumps[i].radius_px = 1;
    pumps[i].state = 0;
    pumps[i].cooldown = 0;
  }

  size_t npix = (size_t)nXSize * (size_t)nYSize;
  float *water = (float *)CPLCalloc(npix, sizeof(float));
  float *tmp = (float *)CPLCalloc(npix, sizeof(float));
  if (!water || !tmp) {
    fprintf(stderr, "Memory allocation failed\n");
    CPLFree(water);
    CPLFree(tmp);
    GDALClose(dem.dataset);
    GDALClose(lahanData.dataset);
    return 1;
  }

  FILE *pumpLog = fopen("pump_log.csv", "w");
  if (!pumpLog) {
    perror("Failed to open pump_log.csv");
    CPLFree(water);
    CPLFree(tmp);
    GDALClose(dem.dataset);
    GDALClose(lahanData.dataset);
    return 1;
  }
  fprintf(pumpLog, "step,subiter,pump_id,inLat,inLon,outLat,outLon,water_level_"
                   "m,threshold_on_m,threshold_off_m,pumped_m,active\n");
  fflush(pumpLog);

  int dx[4] = {-1, 1, 0, 0};
  int dy[4] = {0, 0, -1, 1};
  int nDirs = 4;

  const int pumpCooldownEpochs = 1;
  const float pumpHysteresisFrac = 0.1f;

  // MAIN loop over time-steps (time-series)
  for (int step = 0; step < nSteps; step++) {
    float rain_mm = rain_mm_array[step];
    float interval_min = rain_interval_min_array[step];
    int iter = (int)roundf(rain_iter_f_array[step]);
    if (iter < 1)
      iter = 1;

    float rain_m = rain_mm / 1000.0f;
    // distribute rain for this timestep: add to all valid pixels
    for (size_t i = 0; i < npix; i++) {
      if (hasNoData && elevArray[i] == (float)noDataValue)
        continue;
      if (isnan(elevArray[i]))
        continue;
      water[i] += rain_m;
    }

    // decay factor optionally (same as previous logic)
    float t = (float)step / (float)((nSteps > 1) ? (nSteps - 1) : 1);
    float decayFactor = fmaxf(0.1f, 1.0f - t * 0.9f);

    // per-timestep sub-iterations
    for (int it = 0; it < iter; it++) {
      memcpy(tmp, water, sizeof(float) * npix);

      // water flow + infiltration (4-directional)
      for (int y = 1; y < nYSize - 1; y++) {
        for (int x = 1; x < nXSize - 1; x++) {
          int idx = y * nXSize + x;
          if (hasNoData && elevArray[idx] == (float)noDataValue)
            continue;
          if (isnan(elevArray[idx]))
            continue;

          float z = elevArray[idx] + water[idx];
          float total = 0.0f;
          float pot[4] = {0, 0, 0, 0};

          for (int d = 0; d < nDirs; d++) {
            int nx = x + dx[d];
            int ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= nXSize || ny >= nYSize)
              continue;
            int nidx = ny * nXSize + nx;
            if (hasNoData && elevArray[nidx] == (float)noDataValue)
              continue;
            if (isnan(elevArray[nidx]))
              continue;
            float zn = elevArray[nidx] + water[nidx];
            float diff = z - zn;
            if (diff > 0.0f) {
              pot[d] = diff;
              total += diff;
            }
          }

          if (total > 0.0f) {
            for (int d = 0; d < nDirs; d++) {
              if (pot[d] <= 0.0f)
                continue;
              int nx = x + dx[d];
              int ny = y + dy[d];
              int nidx = ny * nXSize + nx;
              float flow = (pot[d] / total) * water[idx];
              tmp[idx] -= flow;
              tmp[nidx] += flow;
            }
          }

          // infiltration
          int kelas = lahan[idx];
          if (kelas < 0 || kelas > 3)
            kelas = 0;
          float infil_mmhr = (kelas >= 0 && kelas < 4)
                                 ? infil_capacity_mm_per_hr[kelas] * decayFactor
                                 : 0.0f;
          float infil_m = infil_mmhr / 1000.0f;
          tmp[idx] = fmaxf(0.0f, tmp[idx] - infil_m);
        }
      } // end flow

      memcpy(water, tmp, sizeof(float) * npix);

      // pumps loop
      // compute dt_hours for pump volume on this sub-iter: (interval_min / 60)
      // / iter
      float timestep_hours = interval_min / 60.0f;
      float dt_hours = timestep_hours / (float)iter;

      for (int pid = 0; pid < nPumps; pid++) {
        Pump *p = &pumps[pid];
        if (p->px < 0 || p->py < 0 || p->ox < 0 || p->oy < 0)
          continue; // invalid pump

        int pidx = p->py * nXSize + p->px;
        int oidx = p->oy * nXSize + p->ox;
        if (pidx < 0 || pidx >= (int)npix)
          continue;
        if (oidx < 0 || oidx >= (int)npix)
          continue;

        float thresh_on = p->threshold;
        float thresh_off = p->threshold * (1.0f - pumpHysteresisFrac);
        if (thresh_off < 0.0f)
          thresh_off = 0.0f;

        if (p->cooldown > 0)
          p->cooldown--;
        if (!p->state) {
          if (water[pidx] > thresh_on && p->cooldown == 0) {
            p->state = 1;
            p->cooldown = pumpCooldownEpochs * iter;
          }
        } else {
          if (water[pidx] < thresh_off && p->cooldown == 0) {
            p->state = 0;
            p->cooldown = pumpCooldownEpochs * iter;
          }
        }

        float pumped_this_iter = 0.0f;

        if (p->state) {
          int countPix = 0;
          // count valid pixels inside radius
          for (int dyR = -p->radius_px; dyR <= p->radius_px; dyR++) {
            for (int dxR = -p->radius_px; dxR <= p->radius_px; dxR++) {
              int nx = p->px + dxR;
              int ny = p->py + dyR;
              if (nx < 0 || ny < 0 || nx >= nXSize || ny >= nYSize)
                continue;
              if (dxR * dxR + dyR * dyR > p->radius_px * p->radius_px)
                continue;
              countPix++;
            }
          }

          if (countPix > 0) {
            float pumpVolIter = p->capacity_m3hr * dt_hours;
            float pumped_depth_m = pumpVolIter / (pixelArea * (float)countPix);

            for (int dyR = -p->radius_px; dyR <= p->radius_px; dyR++) {
              for (int dxR = -p->radius_px; dxR <= p->radius_px; dxR++) {
                int nx = p->px + dxR;
                int ny = p->py + dyR;
                if (nx < 0 || ny < 0 || nx >= nXSize || ny >= nYSize)
                  continue;
                if (dxR * dxR + dyR * dyR > p->radius_px * p->radius_px)
                  continue;
                int nidx = ny * nXSize + nx;
                if (nidx < 0 || nidx >= (int)npix)
                  continue;
                float remove = fminf(water[nidx], pumped_depth_m);
                water[nidx] -= remove;
                if (oidx >= 0 && oidx < (int)npix)
                  water[oidx] += remove;
                pumped_this_iter += remove;
              }
            }
          }
        } // end if state

        fprintf(pumpLog,
                "%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%.3f,%.6f,%d\n", step,
                it, pid, p->inLat, p->inLon, p->outLat, p->outLon, water[pidx],
                thresh_on, thresh_off, pumped_this_iter, p->state);
      } // end pump loop

      fflush(pumpLog);
    } // end iter
  } // end steps

  // smoothing & write
  float *res = (float *)CPLMalloc(sizeof(float) * npix);
  if (!res) {
    fprintf(stderr, "Failed alloc res\n");
  } else {
    Smoothing(nYSize, nXSize, dx, dy, nDirs, elevArray, water, res,
              noDataValue);
    WriteTiff(dem.dataset, res, nXSize, nYSize, (char *)output);
    CPLFree(res);
  }

  // cleanup
  CPLFree(tmp);
  CPLFree(water);
  CPLFree(lahanData.pixelArray);
  GDALClose(dem.dataset);
  GDALClose(lahanData.dataset);

  fclose(pumpLog);
  free(inLat);
  free(inLon);
  free(outLat);
  free(outLon);
  free(capacities);
  free(thresholds);
  free(radii);
  free(pumps);
  free(rain_mm_array);
  free(rain_interval_min_array);
  free(rain_iter_f_array);

  return 0;
}

# FloodSim
Program untuk melakukan simulasi banjir akibat hujan menggunakan DTM<br>
Sementara hanya bisa dijalankan di Enviroment Linux

## Install Dependencies
Untuk run program yang sudah ada
```
sudo apt install libgdal-dev gdal-bin jq bc
```
Jika ingin build program dari awal
```
sudo apt install build-essential
```

## Build Program
```
gcc main.c -o main $(gdal-config --cflags) $(gdal-config --libs) -lm
```

## Run Program
Untuk jalankan program simulasi-nya saja cukup 
```
./main <File DTM> <Curah Hujan>
```

Untuk jalankan program otomatisasi 
```
./run.sh
```
{Program masih dalam tahap pengembagan}<br>




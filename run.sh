#!/bin/bash

# Configuration
API_URL="https://api.open-meteo.com/v1/forecast?latitude=-6.7609312&longitude=108.4201503&current=relative_humidity_2m,temperature_2m,wind_speed_10m,wind_direction_10m,rain,,weather_code&timezone=Asia%2FBangkok"   # Replace with your API
COOLDOWN_SECONDS=$((60 * 60))   # 1 hour
INTERVAL_SECONDS=$((5 * 60))    # 5 minutes
PROGRAM_TO_RUN="./main"    # Replace with your program path

# Check if jq is installed
if ! command -v jq &> /dev/null; then
    echo "Error: 'jq' is required but not installed. Install it using 'sudo apt install jq'."
    exit 1
fi

# Main loop
while true; do
    echo "$(date): Checking value from API..."

    # Get JSON from API
    RESPONSE=$(curl -s "$API_URL")

    # Extract 'value' using jq
    VALUE=$(echo "$RESPONSE" | jq -r '.current.rain')

    # Check if VALUE is numeric
    if [[ "$VALUE" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        echo "Retrieved value: $VALUE"

        if (( $(echo "$VALUE >= 0" | bc -l) )); then
            echo "Mulai simulasi dengan curah hujan $VALUE mm"
            $PROGRAM_TO_RUN data/dtm.tif $VALUE
            echo "Simulasi selesai"
            echo "Mulai Tiling"
            gdal_translate -of VRT -ot Byte -scale "result/result.tif" result/result.vrt
            rm -rf result/tiles
            gdal2tiles.py -z 12-17 result/result.vrt result/tiles
            echo "Selesai"
            sleep "$COOLDOWN_SECONDS"
        else
            # echo "Value is below threshold. Will check again in $((INTERVAL_SECONDS / 60)) minutes."
            echo "Cek lagi"
            sleep "$INTERVAL_SECONDS"
        fi
    else
        echo "Invalid or missing value in API response: $VALUE"
        sleep "$INTERVAL_SECONDS"
    fi
done
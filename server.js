import express from "express";
import { execFile } from "child_process";
import path from "path";
import cors from "cors";

const app = express();

const corsOptions = {
    origin: function (origin, callback) {
        if (process.env.NODE_ENV != "production") {
            return callback(null, true);
        }
        const regex = /^https?:\/\/([a-zA-Z0-9-]+\.)*prodev\.media$/;

        if (!origin || regex.test(origin)) {
            callback(null, true);
        } else {
            callback(new Error("Not allowed by CORS"));
        }
    },
    credentials: true,
};
app.use(cors(corsOptions));
app.use(express.json());

// Public folders
app.use("/data", express.static(path.join(process.cwd(), "data"), {
    index: "index.html", // default file untuk folder
    extensions: ["html"], // bisa buka file .html langsung
}));
app.use("/result", express.static(path.join(process.cwd(), "result"), {
    index: "index.html",
    extensions: ["html"]
}))

app.post("/simulate", (req, res) => {
    const { output_tif, pump_log, tiles_dir, rain_timeseries, pumps } = req.body;

    // Validasi field wajib
    if (!pump_log || !tiles_dir) {
        return res.status(400).json({ status: "error", message: "output_tif, pump_log, and tiles_dir are required" });
    }

    // Validasi rain_timeseries
    if (!Array.isArray(rain_timeseries) || rain_timeseries.length === 0) {
        return res.status(400).json({ status: "error", message: "rain_timeseries must be a non-empty array" });
    }

    for (let i = 0; i < rain_timeseries.length; i++) {
        const r = rain_timeseries[i];
        if (typeof r.mm !== "number" || typeof r.interval !== "number" || typeof r.iter !== "number") {
            return res.status(400).json({
                status: "error",
                message: `rain_timeseries[${i}] must have numeric mm, interval, and iter`
            });
        }
    }

    // Validasi pumps
    if (!Array.isArray(pumps)) {
        return res.status(400).json({ status: "error", message: "pumps must be an array" });
    }

    for (let i = 0; i < pumps.length; i++) {
        const p = pumps[i];
        const requiredPumpFields = ["in_lat", "in_lon", "out_lat", "out_lon", "capacity", "threshold"];
        for (const field of requiredPumpFields) {
            if (p[field] === undefined || p[field] === null) {
                return res.status(400).json({
                    status: "error",
                    message: `pumps[${i}] missing required field: ${field}`
                });
            }
        }
    }

    // Konversi rain_timeseries ke format string lama
    const rain_mm = rain_timeseries.map(r => r.mm).join(",");
    const interval_min = rain_timeseries.map(r => r.interval).join(",");
    const iter = rain_timeseries.map(r => r.iter).join(",");

    // Safety pump set to 0 is no pump
    let pump_in_lat = 0;
    let pump_in_lon = 0;
    let pump_out_lat = 0;
    let pump_out_lon = 0;
    let pump_capacity = 0;
    let pump_threshold = 0;
    let pump_radius = undefined;
    // Konversi pumps ke format string lama
    if (pumps.length > 0) {
        pump_in_lat = pumps.map(p => p.in_lat).join(",");
        pump_in_lon = pumps.map(p => p.in_lon).join(",");
        pump_out_lat = pumps.map(p => p.out_lat).join(",");
        pump_out_lon = pumps.map(p => p.out_lon).join(",");
        pump_capacity = pumps.map(p => p.capacity).join(",");
        pump_threshold = pumps.map(p => p.threshold).join(",");
        pump_radius = pumps.map(p => p.radius).join(",");
    }

    const args = [
        "data/dem.tif",
        "data/lahan.tif",
        output_tif || "result/delete_after_this.tif",
        pump_log,
        tiles_dir,
        rain_mm,
        interval_min,
        iter,
        pump_in_lat,
        pump_in_lon,
        pump_out_lat,
        pump_out_lon,
        pump_capacity,
        pump_threshold
    ];

    if (pump_radius) {
        args.push(pump_radius);
    }

    execFile("./run.sh", args, { cwd: process.cwd() }, (error, stdout, stderr) => {
        // delete tif if not set
        if (!output_tif) {
            execFile("rm", ["result/delete_after_this.tif"], { cwd: process.cwd() });
        }

        // Kalau error exit code

        // Kalau ada 'Failed:' di stdout/stderr
        if (/Failed:/i.test(stderr)) {
            const failedMatches = stderr.match(/Failed:.*/gi);
            return res.status(400).json({
                status: "error",
                message: failedMatches[0],
                data: null,
            });
        }

        if (error) {
            return res.status(500).json({
                status: "error",
                message: error.message,
                data: null,
            });
        }

        // Success
        return res.json({
            status: "success",
            message: "Simulation completed successfully",
            data: {
                tiles: `${req.protocol}://${req.get("host")}/${tiles_dir}/{z}/{x}/{y}.png`,
                leaflet: `${req.protocol}://${req.get("host")}/${tiles_dir}/leaflet.html`,
                openlayers: `${req.protocol}://${req.get("host")}/${tiles_dir}/openlayers.html`,
                output_tif: `${req.protocol}://${req.get("host")}/${output_tif}`,
                output_pump: `${req.protocol}://${req.get("host")}/${pump_log}`,
            }
        });
    });

});

app.listen(4000, () => console.log("Server running on port 4000"));

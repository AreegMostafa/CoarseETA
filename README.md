## CoarseETA
**CoarseETA** is a novel approach designed to significantly boost the accuracy of open-source Estimated Time of Arrival (ETA) services. It achieves this by intelligently combining their outputs with widely available, coarse zone-to-zone OD matrix traffic data, reducing the need for hard-to-obtain, detailed trajectory information. As **CoarseETA** acts as a low overhead (~1ms) calibration layer on top of any open-source routing engine.

## Problem

- **Commercial ETA Services**: Highly accurate but proprietary and only available within their closed ecosystems.
- **Open-Source ETA Services**: Freely accessible but significantly inaccurate because they lack access to real-time and historical traffic data.
- **Existing Open-Source Research**: Often requires large, public repositories of full or Origin-Destination (OD) trajectory data, which are difficult to obtain and not available for most cities worldwide.

This creates a critical need for an open-source ETA solution that can achieve high accuracy without relying on sensitive or scarce data.

## CoarseETA Approach

CoarseETA presents a paradigm shift. Instead of demanding detailed trajectory data, it leverages a type of information that is becoming increasingly available for cities globally: **coarse zone-to-zone OD matrix**.

For any given trip query between a source and destination, CoarseETA works in two elegant steps:

1.  **Obtain Ranking**: It first uses an existing open-source routing engine (like OSRM, ORS, etc.) to calculate the trip's estimated duration. From this, it derives the **ranking percentile** of this trip compared to all possible trips in the same area.
2.  **Apply Aggregate Data**: It then combines this percentile with the **coarse zone-to-zone OD matrix traffic data** (e.g., average speeds per road segment or historical traffic patterns by time of day) to produce a final, highly accurate, real-time ETA.

This method effectively injects real-world traffic knowledge into any open-source engine, dramatically improving its accuracy.

## Key Features

- **High Accuracy**: Significantly boosts the precision of standard open-source routing engines.
- **Data-Efficient**: Works with coarse, aggregate data that is easier to obtain for many cities, reducing the dependency on full trajectory datasets.
- **Engine Agnostic**: Designed to complement and enhance *any* open-source routing engine (e.g., OSRM, ORS, Valhalla).
- **Open Source**: Fully open-source to foster community development and wider accessibility.

## Respository Structure

```
CoarseETA/
├── config/        # Configuration files
├── headers/       # C++ header files
├── sources/       # C++ source files
├── coarseETA      # Main executable
├── makefile       # Build instructions
└── README.md
```


## Usage 
./coarseETA [config_path]

## Prerequisites

The offline phase of CoarseETA needs to be computed to run to have the following ready:
- SpatialETA Tables Path to find the ranks in real time
- Coarse zone-to-zone OD matrix hash table (code can be adjusted to use the matrix as is as well)
- The zones' shapes/polygons used to aggregate the coarse zone-to-zone OD matrix as a csv file
- A running routing engine to receive the initial ETA requests

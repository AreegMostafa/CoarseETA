
#ifndef READ_ZONES_H
#define READ_ZONES_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <sstream>

struct Point {
    double lon, lat;
    bool inBBox(double min_lon, double max_lon, 
                double min_lat, double max_lat) const {
        return (lon >= min_lon && lon <= max_lon && lat >= min_lat && lat <= max_lat);
    }
};

struct BBox {
    double min_lon = 180.0, max_lon = -180.0;
    double min_lat = 90.0, max_lat = -90.0;
    void expand(const Point& p){
        if (p.lon < min_lon) min_lon = p.lon;
        if (p.lon > max_lon) max_lon = p.lon;
        if (p.lat < min_lat) min_lat = p.lat;
        if (p.lat > max_lat) max_lat = p.lat;
    }
    bool contains(const Point& p) const {
        return p.inBBox(min_lon, max_lon, min_lat, max_lat);
    }
    bool intersects(const BBox& other) const {
        return !(max_lon < other.min_lon || min_lon > other.max_lon ||
                max_lat < other.min_lat || min_lat > other.max_lat);
    }
};

struct Polygon {
    std::vector<Point> vertices;
    bool contains(const Point& p) const {
        // Ray casting algorithm for point-in-polygon check
        bool inside = false;
        size_t n = vertices.size();
        if (n < 3) return false;
        
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            if (((vertices[i].lat > p.lat) != (vertices[j].lat > p.lat)) &&
                (p.lon < (vertices[j].lon - vertices[i].lon) * 
                        (p.lat - vertices[i].lat) / 
                        (vertices[j].lat - vertices[i].lat) + 
                        vertices[i].lon)) {
                inside = !inside;
            }
        }
        return inside;
    }
};

struct Zone {
    std::string id;
    std::vector<Polygon> polygons;  // Can have multiple polygons (multipolygon)
    BBox bbox;    
    bool containsPoint(const Point& p) const {
        // Bounding box check
        if (!bbox.contains(p)) return false;
        
        // Check if point is in any of the the polygons in the zone
        for (const auto& poly : polygons) {
            if (poly.contains(p)) return true;
        }
        return false;
    }
};

class WKTParser {
public:
    static std::vector<Zone> parseCSV(const std::string& filename); // parse csv of zones in the schema of <zone_id, geometry> 
private:

    static std::string trimQuotes(const std::string& str);
    static std::string trim(const std::string& str);
    static size_t findMatchingParen(const std::string& str, size_t start_pos);

    static bool parseWKTGeometry(const std::string& wkt, Zone& zone);
    static bool parseMultiPolygon(const std::string& wkt, Zone& zone);
    static bool parsePolygon(const std::string& wkt, Zone& zone);
    static bool pointsEqual(const Point& p1, const Point& p2, double epsilon = 1e-9);
    static Polygon parseSinglePolygonString(const std::string& poly_str);

    static void computeBBox(Zone& zone);
};

class GridIndex {
    struct Cell {
        std::vector<int> zone_indices;  // zones that intersect this grid cell
    };
    
    std::vector<std::vector<Cell>> grid;
    double min_lon, max_lon, min_lat, max_lat;
    int grid_size_x, grid_size_y;
    double cell_width, cell_height;
    std::vector<Zone> zones;
    
public:
    GridIndex(const std::vector<Zone>& z, int cells_per_degree = 10);    
    std::string findZoneContainingPoint(double lon, double lat);
    
private:
    int getGridX(double lon) {
        return static_cast<int>((lon - min_lon) / cell_width);
    }
    
    int getGridY(double lat) {
        return static_cast<int>((lat - min_lat) / cell_height);
    }
};

#endif // READ_ZONES_H
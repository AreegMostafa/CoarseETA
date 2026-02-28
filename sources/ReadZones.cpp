#include "../headers/ReadZones.hpp"

std::vector<Zone> WKTParser::parseCSV(const std::string& filename) {
    std::vector<Zone> zones;
    std::ifstream file(filename);
    std::string line;
    
    // Skip header
    std::getline(file, line);
    
    int zone_count = 0; 
    while (std::getline(file, line)) {
        zone_count++;
        
        // Find the comma separating zone_id and geometry
        size_t comma_pos = line.find(',');
        if (comma_pos == std::string::npos) {
            std::cerr << "Warning: No comma in line " << zone_count << "\n";
            continue;
        }
        
        Zone zone;
        zone.id = line.substr(0, comma_pos);
        
        // Extract WKT
        std::string wkt = line.substr(comma_pos + 1);
        wkt = trimQuotes(wkt);

        // Parse WKT
        if (!parseWKTGeometry(wkt, zone)) {
            std::cerr << "Warning: Failed to parse WKT for zone " << zone.id << "\n";
            continue;
        }
        
        // Precompute bbox of the zone
        computeBBox(zone);        
        zones.push_back(zone);
        
        if (zone_count % 100 == 0) {
            std::cout << "Finished Loading " << zone_count << " zones...\n";
        }
    }    
    std::cout << "Successfully loaded " << zones.size() << " zones from CSV: " << filename << "\n";

    return zones;
}    


std::string WKTParser::trimQuotes(const std::string& str) {
    if (str.empty()) return str;   
    size_t start = 0;
    size_t end = str.length();
    // Trim leading quotes and spaces
    while (start < end && (str[start] == '"' || std::isspace(str[start]))) {
        start++;
    }
    // Trim trailing quotes and spaces
    while (end > start && (str[end-1] == '"' || std::isspace(str[end-1]))) {
        end--;
    }
    return str.substr(start, end - start);
}


bool WKTParser::parseWKTGeometry(const std::string& wkt, Zone& zone) {
    // Convert to uppercase for case-insensitive comparison
    std::string wkt_upper = wkt;
    std::transform(wkt_upper.begin(), wkt_upper.end(), 
                    wkt_upper.begin(), ::toupper);
    
    if (wkt_upper.find("MULTIPOLYGON") == 0) {
        return parseMultiPolygon(wkt, zone);
    } 
    else if (wkt_upper.find("POLYGON") == 0) {
        return parsePolygon(wkt, zone);
    }
    else {
        std::cerr << "Unsupported WKT type: " << wkt.substr(0, 50) << "...\n";
        return false;
    }
}

bool WKTParser::parseMultiPolygon(const std::string& wkt, Zone& zone) {
    // MULTIPOLYGON (((lon lat, lon lat, ...)), ((lon lat, ...)), ...)
    size_t start = wkt.find("(((");
    if (start == std::string::npos) {
        std::cerr << "Invalid MULTIPOLYGON format\n";
        return false;
    }
    
    size_t end = wkt.rfind(")))");
    if (end == std::string::npos) {
        end = wkt.length();
    } else {
        end += 3;  // To include the closing )))
    }
    
    std::string polygons_str = wkt.substr(start, end - start);
    
    // Parse each polygon
    size_t pos = 0;
    while (pos < polygons_str.length()) {
        // Find next polygon start
        size_t poly_start = polygons_str.find("((", pos);
        if (poly_start == std::string::npos) break;
        
        // Find polygon end
        size_t poly_end = findMatchingParen(polygons_str, poly_start + 1);
        if (poly_end == std::string::npos) break;
        
        std::string poly_str = polygons_str.substr(poly_start, poly_end - poly_start + 1);
        
        // Parse this polygon
        Polygon polygon = parseSinglePolygonString(poly_str);
        if (!polygon.vertices.empty()) {
            zone.polygons.push_back(polygon);
        }
        
        pos = poly_end + 1;
    }
    
    return !zone.polygons.empty();
}

bool WKTParser::parsePolygon(const std::string& wkt, Zone& zone) {
    // POLYGON ((lon lat, lon lat, ...))
    size_t start = wkt.find("((");
    if (start == std::string::npos) {
        std::cerr << "Invalid POLYGON format\n";
        return false;
    }
    
    size_t end = wkt.rfind("))");
    if (end == std::string::npos) {
        end = wkt.length();
    } else {
        end += 2;  // Include the closing ))
    }
    
    std::string poly_str = wkt.substr(start, end - start);
    
    Polygon polygon = parseSinglePolygonString(poly_str);
    if (!polygon.vertices.empty()) {
        zone.polygons.push_back(polygon);
        return true;
    }
    
    return false;
}


size_t WKTParser::findMatchingParen(const std::string& str, size_t start_pos) {
    int depth = 0;
    for (size_t i = start_pos; i < str.length(); i++) {
        if (str[i] == '(') depth++;
        else if (str[i] == ')') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}


Polygon WKTParser::parseSinglePolygonString(const std::string& poly_str) {
    Polygon polygon;
    
    // Find the coordinates part inside the parentheses
    size_t start = poly_str.find('(');
    size_t end = poly_str.rfind(')');
    
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        return polygon;
    }
    
    std::string coords_str = poly_str.substr(start + 1, end - start - 1);
    

    // Parse polygon and if ring ignore holes only find the first ring
    size_t first_ring_end = coords_str.find(')');
    if (first_ring_end != std::string::npos) {
        std::string first_ring = coords_str.substr(0, first_ring_end);
        
        // Remove any leading '('
        if (!first_ring.empty() && first_ring[0] == '(') {
            first_ring = first_ring.substr(1);
        }
        
        // Parse the points
        std::stringstream ss(first_ring);
        std::string point_str;
        
        while (std::getline(ss, point_str, ',')) {
            point_str = trim(point_str);
            if (point_str.empty()) continue;
            
            std::stringstream point_ss(point_str);
            Point p;
            
            if (point_ss >> p.lon >> p.lat) {
                polygon.vertices.push_back(p);
            }
        }
    }

    // Ensure polygon is closed (first point == last point)
    if (polygon.vertices.size() >= 3) {
        if (!pointsEqual(polygon.vertices.front(), polygon.vertices.back())) {
            polygon.vertices.push_back(polygon.vertices.front());
        }
    }
    
    return polygon;
}


std::string WKTParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}


bool WKTParser::pointsEqual(const Point& p1, const Point& p2, double epsilon) {
    return std::abs(p1.lon - p2.lon) < epsilon && 
            std::abs(p1.lat - p2.lat) < epsilon;
}




void WKTParser::computeBBox(Zone& zone) {
    zone.bbox = BBox();
    for (const auto& poly : zone.polygons) {
        for (const auto& p : poly.vertices) {
            zone.bbox.expand(p);
        }
    }
}


GridIndex::GridIndex(const std::vector<Zone>& z, int cells_per_degree) : zones(z) {
    // Calculate global bounds
    min_lon = 180; max_lon = -180; min_lat = 90; max_lat = -90;
    for (const auto& zone : zones) {
        min_lon = std::min(min_lon, zone.bbox.min_lon);
        max_lon = std::max(max_lon, zone.bbox.max_lon);
        min_lat = std::min(min_lat, zone.bbox.min_lat);
        max_lat = std::max(max_lat, zone.bbox.max_lat);
    }
    
    // Add small padding
    min_lon -= 0.1; max_lon += 0.1;
    min_lat -= 0.1; max_lat += 0.1;
    
    // Initialize grid
    grid_size_x = static_cast<int>((max_lon - min_lon) * cells_per_degree) + 1;
    grid_size_y = static_cast<int>((max_lat - min_lat) * cells_per_degree) + 1;
    cell_width = (max_lon - min_lon) / grid_size_x;
    cell_height = (max_lat - min_lat) / grid_size_y;
    
    grid.resize(grid_size_y, std::vector<Cell>(grid_size_x));
    
    // Index zones
    for (int i = 0; i < zones.size(); i++) {
        const auto& bbox = zones[i].bbox;
        
        int min_x = getGridX(bbox.min_lon);
        int max_x = getGridX(bbox.max_lon);
        int min_y = getGridY(bbox.min_lat);
        int max_y = getGridY(bbox.max_lat);
        
        // Add zone to all intersecting cells
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                grid[y][x].zone_indices.push_back(i);
            }
        }
    }
}
    
std::string GridIndex::findZoneContainingPoint(double lon, double lat) {
    int x = getGridX(lon);
    int y = getGridY(lat);
    
    if (x < 0 || x >= grid_size_x || y < 0 || y >= grid_size_y) {
        return {};  // Point outside grid
    }

    for (int idx : grid[y][x].zone_indices) {
        if (zones[idx].containsPoint(Point{lon, lat})) {
            return zones[idx].id;
        }
    }
    return {};
}

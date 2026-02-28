#include "../headers/CoarseETA.hpp"


CoarseETA::CoarseETA(const std::string& spatialETA_path,
                     const std::string& hashTable_file,
                     const std::string& zones_path_csv,
                     const std::string& routingengine_server,
                     const std::string& engine,
                     TimeZoningType time_zoning_type,
                     int record_size,
                     int eta_offset): 
      spatialETA_path(spatialETA_path),
      record_size(record_size),
      eta_offset(eta_offset),
      hashTable_file(hashTable_file),
      zones_path_csv(zones_path_csv),
      routingengine_server(routingengine_server),
      engine(engine),
      zones(WKTParser::parseCSV(zones_path_csv)),
      spatial_index(zones),
      time_zoning_type(time_zoning_type)
{
    // Initialize aggregate_ranks map
    aggregate_ranks["min_max"] = {0, 100};
    aggregate_ranks["min_med_max"] = {0, 50, 100};
    aggregate_ranks["percentiles"] = {0, 25, 50, 75, 100};
    setup_hash_table();
}

void CoarseETA::setup_hash_table() {
    std::ifstream f(hashTable_file, std::ios::binary);
    uint64_t num_entries; // How many entries to load
    f.read(reinterpret_cast<char*>(&num_entries), 8);
    std::cout << "Loading Hash table index with " << num_entries << " entries...\n";
    
    for (uint64_t i = 0; i < num_entries; ++i) {
        uint32_t key_len; // get key
        f.read(reinterpret_cast<char*>(&key_len), 4);
        std::string key(key_len, '\0');
        f.read(&key[0], key_len);
        
        double buffer[10]; // get values 2 (min_max) + 3 (min_med_max) + 5 (percentiles)
        f.read(reinterpret_cast<char*>(buffer), 80);
        
        // save the read values
        AggregateValues values; 
        values.min_max = {buffer[0], buffer[1]};
        values.min_med_max = {buffer[2], buffer[3], buffer[4]};
        values.percentiles = {buffer[5], buffer[6], buffer[7], buffer[8], buffer[9]};
        
        hash_table[key] = values;
    }    
    std::cout << "Loaded the" << hash_table.size() << " entries!\n";
}

// set the type of agrgegate we want to use for this run of coarseETA
void CoarseETA::setAggregateTypeField(const std::string& type) {
    if      (type == "percentiles") field = &AggregateValues::percentiles;
    else if (type == "min_med_max") field = &AggregateValues::min_med_max;
    else if (type == "min_max")     field = &AggregateValues::min_max;
    else throw std::invalid_argument("Unknown aggregate type: " + type +
                                     "\nShould be either \"percentiles\" or \"min_med_max\" or \"min_max\"\n" );

    aggregate_type = type;
}

// Process the ETA Request
double CoarseETA::ETARequest(ETAQuery query, Timing& timing) {
    try{
        auto total_time_start = std::chrono::high_resolution_clock::now(); // start the timer for the total time
        // STEP 1: Zoning and Aggregates
        // Spatial Zoning
        std::string start_zone = spatial_index.findZoneContainingPoint(query.start_long, query.start_lat); // find the spatial zone id corresponding to the starting point
        std::string end_zone = spatial_index.findZoneContainingPoint(query.end_long, query.end_lat); // find the spatial zone id corresponding to the ending point
        // Temporal Zoning
        TimeZone timeZone = timeZoning(query.start_datetime); // expand the timestamp into season, day of week, daytype, hour of day rounded to the nearest hour and hour range periods

        //Perpare the key for the hash table index to get the ground truth aggregates using the spatial and temporal zones based on the requested temporal zoning type
        std::string key = "";
        switch(time_zoning_type) {
            case TimeZoningType::DOW_HOD:
                key += start_zone + "," + end_zone + "," + std::to_string(timeZone.season) + "," + std::to_string(timeZone.day_of_week) + "," + std::to_string(timeZone.adjusted_hour);
                break;
            case TimeZoningType::DAYTYPE_HOD:
                key += start_zone + "," + end_zone + "," + std::to_string(timeZone.season) + "," + timeZone.daytype + "," + std::to_string(timeZone.adjusted_hour);
                break;
            case TimeZoningType::DOW_RANGE:
                key += start_zone + "," + end_zone + "," + std::to_string(timeZone.season) + "," + std::to_string(timeZone.day_of_week) + "," + std::to_string(timeZone.start_hour) + "," + std::to_string(timeZone.end_hour);
                break;
            case TimeZoningType::DAYTYPE_RANGE:
                key += start_zone + "," + end_zone + "," + std::to_string(timeZone.season) + "," + timeZone.daytype + "," + std::to_string(timeZone.start_hour) + "," + std::to_string(timeZone.end_hour);
                break;
        }
        // Get the ground truth aggregate values and percentiles
        const std::vector<double>& aggeregate_list_x = aggregate_ranks.at(aggregate_type); // percentiles/ranks
        const std::vector<double>& aggeregate_list_y = hash_table.at(key).*field; // ground truth values from the hash table

        // STEP 2: Ranking Percentile
        auto engine_time_start = std::chrono::high_resolution_clock::now(); // start the timer for the routing engine time
        double os_eta = OpenSourceRoutingEngine(query.start_long, query.start_lat, query.end_long, query.end_lat); // query the routing engine to get os_eta 
        auto engine_time_end = std::chrono::high_resolution_clock::now(); // end the timer for the routing engine time time

        SearchResult search_result = binarySearchETA(start_zone, end_zone, os_eta); // search the spatial ETA table corresponding to the start and end zones for os_eta rank

        // interpolate the rank if an exact match was not found
        double rank = search_result.record_eta1; 
        if (search_result.record_eta2 != -1) {
            rank = rank + (os_eta - search_result.eta1) / (search_result.eta2 - search_result.eta1);
        }
        // calculate the rank in percentage where rank here is 0-indexed
        double rank_percent = 0;
        if (search_result.total_records > 1)
            rank_percent = ((rank) / ((double)search_result.total_records-1)) * 100;


        // STEP 3: Output ETA
        // search the ground truth aggregate list for the rank percentage
        StatResult stat_result = FindStat(aggeregate_list_x, aggeregate_list_y, rank_percent); 

        // calculate the output eta as the value corresponding to the rank percentage
        // if exact match is not found interpolate the eta
        double final_eta = stat_result.eta1;
        if (stat_result.rank2 != -1) {
            final_eta = stat_result.eta1 + (stat_result.eta2 - stat_result.eta1) * ((rank_percent - stat_result.rank1) / (stat_result.rank2 - stat_result.rank1));
        }
        auto total_time_end = std::chrono::high_resolution_clock::now(); // end the timer for the total time

        // calculate routing engine time
        timing.routing_engine = std::chrono::duration<double, std::milli>(engine_time_end - engine_time_start).count();
        // calculate total time
        timing.total = std::chrono::duration<double, std::milli>(total_time_end - total_time_start).count();
        // get CoarseETA's overhead
        timing.coarseETA = timing.total - timing.routing_engine;

        // return result
        return final_eta;

    } catch (const std::exception& e) {
        return -1.0; // NULL Error occured 
    }
}


TimeZone CoarseETA::timeZoning(const std::string& timestamp_str) {
    TimeZone timeZone;
    
    // Parse the timestamp string the formate used for now is "%Y-%m-%d %H:%M:%S"
    struct tm tm = {};
    std::stringstream ss(timestamp_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (ss.fail()) {
        std::cerr << "Failed to parse timestamp: " << timestamp_str << std::endl;
        return timeZone;
    }

    
    // Extract Time Zone
    int month = tm.tm_mon + 1;  // Get the months in 1 to 12
    int day_of_week = tm.tm_wday;  // 0-6 (Sunday=0)
    int python_weekday = (day_of_week + 6) % 7;  // Convert to Monday=0, Sunday=6 as the dataset was prepared in python change if needed
    timeZone.day_of_week = python_weekday;
    timeZone.daytype = (python_weekday >= 5) ? "weekend" : "weekday"; // get daytype

    // Get hour and round to the nearest hour (add 1 if minute > 30)
    int hour = tm.tm_hour;
    int minute = tm.tm_min;
    int hour_adjustment = (minute > 30) ? 1 : 0;
    timeZone.adjusted_hour = (hour + hour_adjustment) % 24;
    
    // Determine hour range periods
    std::vector<std::pair<int, int>> ranges = {
        {0, 6},    // Range 1: 00-06 Early Morning
        {7, 10},   // Range 2: 07-10 Morning Peak
        {11, 13},  // Range 3: 11-13 Noon Off Peak
        {14, 16},  // Range 4: 14-16 Afternoon Peak
        {17, 19},  // Range 5: 17-19 Evening Off Peak
        {20, 23}   // Range 6: 20-23 Late Evening
    };
    
    for (const auto& range : ranges) {
        if (timeZone.adjusted_hour >= range.first && 
            timeZone.adjusted_hour <= range.second) {
            timeZone.start_hour = range.first;
            timeZone.end_hour = range.second;
            break;
        }
    }


    // Determine season (1-4)
    // Season 1: Winter (Dec, Jan, Feb) {12, 1, 2}
    // Season 2: Spring (Mar, Apr, May) {3, 4, 5}
    // Season 3: Summer (Jun, Jul, Aug) {6, 7, 8}
    // Season 4: Fall (Sep, Oct, Nov) {9, 10, 11}
    timeZone.season = ((month + 9) % 12) / 3 + 1;    
    
    return timeZone;
}

double CoarseETA::OpenSourceRoutingEngine(double start_long, double start_lat, 
                                            double end_long, double end_lat) {

    // safe conversion from double to string without rounding for the coordinates
    auto dbl2str = [](double v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.17g", v);
        return std::string(buf);
    }; 

    try {
        if (engine == "osrm") { // call OSRM and return its resulting ETA
            std::string path = "/route/v1/driving/"
                + dbl2str(start_long) + "," + dbl2str(start_lat) + ";"

                + dbl2str(end_long) + "," + dbl2str(end_lat)
                + "?overview=false";
            std::string resp = httpRequest(routingengine_server, 5000, "GET", path);
            return parseRoutingEngineAnswerJson(resp, {"routes", "0", "duration"});

        } else if (engine == "ors") { // call ORS and return its resulting ETA
            std::string body = "{\"coordinates\":[[" 
                + dbl2str(start_long) + "," + dbl2str(start_lat) + "],["
                + dbl2str(end_long) + "," + dbl2str(end_lat) + "]]}";
            std::string resp = httpRequest(routingengine_server, 8082, "POST",
                                           "/ors/v2/directions/driving-car", body);
            return parseRoutingEngineAnswerJson(resp, {"routes", "0", "summary", "duration"});

        } else if (engine == "val") { // call Valhalla and return its resulting ETA
            std::string body = "{\"locations\":["
                "{\"lat\":" + dbl2str(start_lat) + ",\"lon\":" + dbl2str(start_long) + "},"
                "{\"lat\":" + dbl2str(end_lat) + ",\"lon\":" + dbl2str(end_long) + "}],"
                "\"costing\":\"auto\"}";
            std::string resp = httpRequest(routingengine_server, 8002, "POST", "/route", body);
            // Check for error_code 442 -> return -1
            if (resp.find("\"error_code\"") != std::string::npos) {
                double ec = parseRoutingEngineAnswerJson(resp, {"error_code"});
                if ((int)ec == 442) return -1.0;
                throw std::runtime_error("Valhalla error: " + resp);
            }
            return parseRoutingEngineAnswerJson(resp, {"trip", "summary", "time"});

        } else {
            throw std::runtime_error("Unsupported engine: " + engine);
        }
    } catch (const std::exception&) {
        return -1.0; // if engine error occurs
    }
}

// Raw HTTP request over TCP for faster computations for testing
std::string CoarseETA::httpRequest(const std::string& host, int port,
                                const std::string& method,
                                const std::string& path,
                                const std::string& body) {
    // Resolve host
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) throw std::runtime_error("Cannot resolve host: " + host);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) throw std::runtime_error("Socket creation failed");

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        throw std::runtime_error("Connection failed to " + host);
    }

    // Build HTTP request
    std::string request;
    if (method == "POST") {
        request = "POST " + path + " HTTP/1.0\r\n"
                  "Host: " + host + "\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: " + std::to_string(body.size()) + "\r\n"
                  "Connection: close\r\n\r\n" + body;
    } else {
        request = "GET " + path + " HTTP/1.0\r\n"
                  "Host: " + host + "\r\n"
                  "Connection: close\r\n\r\n";
    }

    // Send Request
    if (send(sock, request.c_str(), request.size(), 0) < 0) {
        close(sock);
        throw std::runtime_error("Send failed");
    }

    // Receive response
    std::string response;
    char buf[4096];
    int n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0)
        response.append(buf, n);
    close(sock);

    // Strip HTTP headers
    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos)
        throw std::runtime_error("Malformed HTTP response");
    return response.substr(header_end + 4);
}


double CoarseETA::parseRoutingEngineAnswerJson(const std::string& json, const std::vector<std::string>& path) {
    size_t pos = 0;
    size_t end = json.size();

    for (const auto& key : path) { // go through the keys determined based on the engine used 
        // Skip whitespace
        while (pos < end && isspace(json[pos])) pos++;

        if (json[pos] == '{') {
            // Object: find "key":
            std::string search = "\"" + key + "\"";
            size_t found = json.find(search, pos);
            if (found == std::string::npos || found >= end)
                throw std::runtime_error("Key not found: " + key);
            pos = found + search.size();
            // Skip to ':'
            pos = json.find(':', pos) + 1;
            while (pos < end && isspace(json[pos])) pos++;

        } else if (json[pos] == '[') {
            // Array: advance to the Nth element
            int idx = std::stoi(key);
            pos++; // skip '['
            for (int i = 0; i < idx; i++) {
                int depth = 0;
                bool in_string = false;
                while (pos < end) {
                    char c = json[pos++];
                    if (c == '\\' && in_string) { pos++; continue; }
                    if (c == '"') { in_string = !in_string; continue; }
                    if (in_string) continue;
                    if (c == '{' || c == '[') depth++;
                    else if (c == '}' || c == ']') depth--;
                    else if (c == ',' && depth == 0) break;
                }
                while (pos < end && isspace(json[pos])) pos++;
            }
        } else {
            throw std::runtime_error("Expected object or array at pos " + std::to_string(pos));
        }
    }

    // pos now points at the target value — parse double
    while (pos < end && isspace(json[pos])) pos++;
    return std::stod(json.substr(pos));
}


SearchResult CoarseETA::binarySearchETA(const std::string& zone1,
                              const std::string& zone2,
                              double os_eta) {
    SearchResult result{};
 
    // Compose the filename of the spatial eta table bin file using the start and end zones
    std::string filename = spatialETA_path + "/" + zone1 + "_" + zone2 + ".bin";

    FILE* f = fopen(filename.c_str(), "rb");
    if (!f) throw std::runtime_error("Cannot open file: " + filename);

    // Get total records
    if (fseeko(f, 0, SEEK_END) != 0) { fclose(f); throw std::runtime_error("fseeko SEEK_END failed"); }
    off_t file_size = ftello(f);
    if (file_size < 0) { fclose(f); throw std::runtime_error("ftello failed"); }
    long long total = (long long)file_size / (long long)record_size;
    result.total_records = total;

    if (total == 0) {
        fclose(f);
        return result;
    }

    // Begin the binary search
    long long lo = 0, hi = total - 1;
    long long mid = 0;
    double mid_eta = 0.0;
 
    result.record_eta2 = -1;
    result.eta2 = -1.0;

    while (lo <= hi) {
        mid = lo + (hi - lo) / 2;
        mid_eta = readETA(f, mid);

        if (mid_eta == os_eta) {
            // Exact match
            fclose(f);
            result.record_eta1 = mid;
            result.eta1 = mid_eta;
            return result;
        } else if (mid_eta < os_eta) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    //If no exact match is found
    // After binary search: lo is the first record with ETA > os_eta
    //                      hi = lo - 1 is the last record with ETA < os_eta
    if (lo >= total){
        // os_eta is outside the range of the file (more than the max eta)
        // then snap it to the max eta as an exact match
        result.record_eta1 = total - 1;
        result.eta1 = readETA(f, total - 1);
        fclose(f);
        return result;
    } else if (hi < 0) {
        // os_eta is outside the range of the file (less than the min eta)
        // then snap it to the min eta as an exact match
        result.record_eta1 = 0;
        result.eta1 = readETA(f, 0);
        fclose(f);
        return result;
    }

    double eta1 = readETA(f, hi);   // ETA1 < os_eta
    double eta2 = readETA(f, lo);   // ETA2 > os_eta

    fclose(f);

    result.record_eta1 = hi;
    result.eta1        = eta1;
    result.record_eta2 = lo;
    result.eta2        = eta2;

    return result;
}


double CoarseETA::readETA(FILE* f, long long record_idx) { // read the eta at the record idx
    if (fseeko(f, (off_t)(record_idx * record_size + eta_offset), SEEK_SET) != 0)
        throw std::runtime_error("fseeko failed");
    double eta;
    if (fread(&eta, sizeof(double), 1, f) != 1)
        throw std::runtime_error("fread failed");
    return eta;
}


StatResult CoarseETA::FindStat(const std::vector<double>& x, // percentile ranks, e.g. {0, 25, 50, 75, 100}
                               const std::vector<double>& y, // corresponding ground truth aggregate list / ETA values
                               double rank_p) {

    // Binary search on percentile ranks x for rank_p
    // This is in case CoarseETA receives percentiles > 5 values or full distribution
    const double* lo = x.data();
    const double* hi = x.data() + x.size();

    const double* it = std::lower_bound(lo, hi, rank_p);
    size_t idx = it - lo;

    StatResult res{};

    // below all or above all is an error here as rank_p shouldn't be < 0 or > 100
    res.rank2 = -1;   res.eta2 = -1.0;   // null
    res.rank1 = -1; res.eta1 = -1.0;
    if (it != hi && *it <= rank_p + 1e-9) {
        // Exact match
        res.rank1 = *it;  res.eta1 = y[idx];
    } else { // exact match not found
        if (idx != 0) { 
            res.rank1 = x[idx - 1];
            res.eta1  = y[idx - 1];
        }
        if (idx < x.size()) {
            res.rank2 = x[idx];
            res.eta2  = y[idx];
        }
    }
    return res;
}







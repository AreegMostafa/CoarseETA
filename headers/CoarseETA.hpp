#ifndef COARSE_ETA_H
#define COARSE_ETA_H

#include "../headers/ReadZones.hpp"
#include <ctime>
#include <iomanip>
#include <stdexcept>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdio>
#include <chrono>

// Type of time zoning to use
enum TimeZoningType {
    DOW_HOD = 0,
    DAYTYPE_HOD = 1,
    DOW_RANGE = 2,
    DAYTYPE_RANGE = 3
};

// ETA Query <s, d, t>
struct ETAQuery {
    double start_long;
    double start_lat;
    double end_long;
    double end_lat;
    std::string start_datetime;
};

struct TimeZone {
    int season;           // 1 (Dec-Feb), 2 (Mar-May), 3 (Jun-Aug), 4 (Sep-Nov) for seasons
    int day_of_week;      // 0-6 (Mon=0, Tues=1, Wed=2, Thu=3, Fri=4, Sat=5, Sunday=6) 
    std::string daytype;  // "weekday" or "weekend"
    int adjusted_hour;    // 0-23 with rounding to the nearest hour
    int start_hour;       // start of hour range for time periods
    int end_hour;         // end of hour range for time periods
    
    // For debugging
    void print() const {
        std::cout << "Season: " << season << ", " 
                  << "Day of week: " << day_of_week << ", "
                  << "Daytype: " << daytype << ", "
                  << "Adjusted hour: " << adjusted_hour << ", "
                  << "Hour range: [" << start_hour << " - " << end_hour << "]\n";
    }
};

// SpatialETA Table search result
struct SearchResult {
    // (ETA1 < os_eta < ETA2)
    long long record_eta1;   // max ETA < os_eta
    double    eta1;
    long long record_eta2;   // min ETA > os_eta
    double    eta2;
    double  total_records; // total records in file
};

//Aggregate List search result
struct StatResult {
    // (rank1 < rank_p < rank2)
    int rank1;  double eta1;  // rank1 < rank_p, eta1
    int rank2;  double eta2;  // rank2 > rank_p, eta2
};

struct Timing {
    double routing_engine; // time taken by the routing engine
    double total; // total time of the query response
    double coarseETA; // overhead by coarseETA operations (total time - routing engine time)
};

//CoarseETA Online Phase for Answering ETA Queries
class CoarseETA {
private:
    //The groundtruth values corresponding to the statistical percentiles
    struct AggregateValues {
        std::vector<double> min_max;       // [0,100]
        std::vector<double> min_med_max;   // [0,50,100]
        std::vector<double> percentiles;   // [0,25,50,75,100]
    };
    
    // type of percentiles used for the ground truth dataset (min_max:[0,100] or min_med_max[0,50,100] or percentiles[0,25,50,75,100])
    using PercentileField = std::vector<double> AggregateValues::*;
    PercentileField field;
    std::string aggregate_type; // aggregate type to be used (min_max:[0,100] or min_med_max[0,50,100] or percentiles[0,25,50,75,100])
    std::string hashTable_file; // Path to the hash table of the coarse zone-to-zone od matrix
    std::map<std::string, AggregateValues> hash_table; // hash index of the coarse zone to zone od matrix
    TimeZoningType time_zoning_type; // type of time zoning to use
    std::map<std::string, std::vector<double>> aggregate_ranks; // the ranks of the aggregates 

    std::string spatialETA_path; // SpatialETA tables folder path
    int record_size; // single record size in the SpatialETA table
    int eta_offset; // offset of the eta bytes in a single record

    std::string routingengine_server; // routing engine server ip
    std::string engine; // routing enginge used name engine name

    
    std::string zones_path_csv; // Path to the spatial zones of the dataset
    GridIndex spatial_index;  // grid index on the zones 
    std::vector<Zone> zones; // zones shapes

    // reading the hash index bin file of the coarse zone-to-zone OD matrix prepared from the offline phase
    void setup_hash_table(); 

    // Zone the trip's start time
    TimeZone timeZoning(const std::string& timestamp_str); 

    // query the open source routing engine
    double OpenSourceRoutingEngine( double start_long,  // start point longitude
                                    double start_lat,   // start point latitude
                                    double end_long,    // end point longitude
                                    double end_lat);    // end point longitude
    //http request method to the routing engine
    std::string httpRequest(const std::string& host,    // server with routing engine ip
                            int port,                   // port number
                            const std::string& method,  // GET or POST
                            const std::string& path,    // path of the request
                            const std::string& body = ""); /// Body of the query if used

    //parse the json result from the open source routing engine
    double parseRoutingEngineAnswerJson(const std::string& json, // the open source routing engine json result 
                                        const std::vector<std::string>& path);  // path to the result we need (ETA) which differs per engine

    // binary search for OS_ETA in the spatial ETA table
    SearchResult binarySearchETA(const std::string& zone1,
                              const std::string& zone2,
                              double os_eta);

    // get ETA from the single record at position
    double readETA( FILE* f,                // file pointer 
                    long long record_idx);  // record index
    
    // get the aggregate values corresponding to the rank percentile of OS_ETA
    StatResult FindStat(const std::vector<double>& x, // percentile ranks, e.g. {0, 25, 50, 75, 100}
                        const std::vector<double>& y, // corresponding aggregate list / ETA values
                        double rank_p);               // OS_ETA rank in percentage



    public:
    // Constructor
    CoarseETA(const std::string& spatialETA_path, // Spatial ETA tables folder path
              const std::string& hashTable_file, // Hash index bin file path
              const std::string& zones_path_csv, // zone shapes path in csv
              const std::string& routingengine_server, // routing engine server's ip
              const std::string& engine, // engine name
              TimeZoningType time_zoning_type = TimeZoningType::DOW_HOD, // time zoning type with the default being day of week and hour of day
              int record_size = 8, // total single record size in the spatial eta table
              int eta_offset = 0); // eta offset in the single record
    // set the aggregate statistics type field 
    void setAggregateTypeField(const std::string& type);    
    
    // receive an ETA request and time the response time
    double ETARequest(ETAQuery query,    // ETA query of s, d, t
                        Timing& timing); // compute the response time 
};

#endif // COARSE_ETA_H
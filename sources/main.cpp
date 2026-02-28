#include "../headers/CoarseETA.hpp"
#include "../config/config.hpp"

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config.ini>\n";
        return 1;
    }

    Config cfg = Config::load(argv[1]);

    TimeZoningType time_zoning_type = static_cast<TimeZoningType>(cfg.time_zoning_type);
    CoarseETA coarseETA( cfg.spatial_eta_path,  // SpatialETATables_path
                          cfg.hashindex_file,  // hashTable_file
                          cfg.zones_csv_file,  // zones_csv_file
                          cfg.routingengine_server,  // routingengine_server
                          cfg.engine,  // engine
                          time_zoning_type); 

                          
    coarseETA.setAggregateTypeField(cfg.aggregate_type);  // aggregate_type

    ETAQuery query;
    query.start_long = -73.95267486572266;
    query.start_lat = 40.723175048828125;
    query.end_long = -73.92391967773438;
    query.end_lat = 40.76137924194336;
    query.start_datetime = "2016-01-01 00:19:39";
    
    Timing timing;
    double eta = coarseETA.ETARequest(query, timing);
 
    std::cout << "Output ETA: " << eta << "\n";
    std::cout << "Total response time: " << timing.total << "\n"
              << "Engine's response time: " << timing.routing_engine << "\n"
              << "CoarseETA overhead: " << timing.coarseETA << "\n";
    return 0;
}
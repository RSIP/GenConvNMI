#include <map>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>

#include <boost/program_options.hpp>
#include <boost/numeric/ublas/io.hpp>

#include "cluster_reader.hpp"
#include "bimap_cluster_populator.hpp"
#include "calculate_till_tolerance.hpp"

using std::string;
using std::cout;
using std::cerr;
using std::endl;
namespace po = boost::program_options;
using namespace gecmi;


int main( int argc, char* argv[])
{
    po::options_description desc(
        "Invoke as : \n"
        "    gecmi [options] <clusters1> <clusters2>\n"
        "\n"
        "Allowed options: \n"
        );

    po::positional_options_description p;
    p.add( "input", 2 );
    desc.add_options()
        ("help,h", "produce help message")
        ("input",
            po::value<std::vector<string> >()->composing(),
            "name of the input files" )
        ("sync,s", "synchronize the node base, for example to fairly evaluate against"
            " top K selected clusters that cover only part of the original nodes")
        ("fnmi,f", "evaluate also FNMI")
        ("risk,r",
            po::value<double>()->default_value(0.01),
            "probability of value being outside" )
        ("error,e",
            po::value<double>()->default_value(0.01),
            "admissible error" )
    ;
    po::variables_map vm;
    po::store( po::command_line_parser(argc, argv)
       .options(desc)
       .positional(p).run(), vm
    );
    po::notify(vm);
    if ( vm.count("help" ) )
    {
        cout << desc << endl;
        return 1;
    }
    std::vector< std::string > positionals;
    try {
        positionals = vm["input"].as<std::vector<std::string> >();
    } catch ( boost::bad_any_cast const& ) {
        cerr << "Please, provide two input files to proceed. Use `gecmi -h` for more info" << endl;
        throw;
    }
    if ( positionals.size() != 2 )
        throw std::invalid_argument("Please provide exactly two input files as input\n");

    std::ifstream in1(positionals[0].c_str() );
    if( !in1 )
        throw std::system_error(errno, std::system_category(), "Could not open the first file\n");

    std::ifstream in2(positionals[1].c_str() );
    if( !in2 )
        throw std::system_error(errno, std::system_category(), "Could not open the second file\n");

    // Read the clusters
    two_relations_ptr two_rel( new two_relations_t );
    bimap_cluster_populator  bcp1( two_rel->first );
    bimap_cluster_populator  bcp2( two_rel->second );
    // bimap_cluster_populator:  left: Nodes, right: Clusters

    read_clusters_without_remappings(
        in1,
        bcp1
    );

    read_clusters_without_remappings(
        in2,
        bcp2
    );

    // Synchronize the number of nodes in both collections if required
    if (vm.count("sync")) {
        const auto  b1lnum = bcp1.uniqlSize();
        const auto  b2lnum = bcp2.uniqlSize();
        if(b1lnum != b2lnum) {
            std::cerr << "WARNING, the number of nodes is different in the collections: "
                << b1lnum << " vs " << b2lnum << ". The nodes"
                " will be synchronized by removing non-matching ones from the largest collection\n";
            if(b1lnum < b2lnum)  // bcp1 is the base for the sync, the nodes are removed from bcp2
                bcp2.sync(bcp1);
            else bcp1.sync(bcp2);  // bcp2 is the base for the sync, the nodes are removed from bcp1

            // Throw the exception if the synchronization is failed
            if(bcp1.uniqlSize() != bcp2.uniqlSize())
                throw std::domain_error("Input collections have different node base and can't be synchronized gracefully\n");
        }
    }

    double risk = vm["risk" ].as< double>();
    double epvar  = vm["error"].as<double>();

    calculated_info_t cit = calculate_till_tolerance(
        two_rel, risk, epvar );

    cout << "NMI: " << cit.nmi;
    if (vm.count("fnmi")) {
        const auto b1rnum = bcp1.uniqrSize();
        const auto b2rnum = bcp2.uniqrSize();

        cout << ", FNMI: " << cit.nmi * exp(-double(abs(b1rnum - b2rnum))
            / std::max(b1rnum, b2rnum)) << " (cls1: " << b1rnum << ", cls2: " << b2rnum << ")\n";
    } else cout << endl;

    return 0;
}

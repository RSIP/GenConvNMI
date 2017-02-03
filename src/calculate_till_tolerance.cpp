#include <iostream>

#include <tbb/task_scheduler_init.h> // <-- For controlling number of working threads
#include <tbb/parallel_for.h>

#include "bimap_cluster_populator.hpp"
#include "confusion.hpp"
#include "parallel_worker.hpp"
#include "deep_complete_simulator.hpp"
#include "calculate_till_tolerance.hpp"


//constexpr size_t  EVCOUNT_THRESHOLD = 8192;  // Gives also good results on middle-size networks
// Grain: 1024 .. 2048;  1536 is ~ the best on both middle-size and large networks
// on the working laptop (4 cores Intel(R) Core(TM) i7-4600U CPU @ 2.10GHz-3.3 GHz)
// Checked in the range 128 .. 8192
// Note: it has not significant dependence on the taks complexity: the same value is
// optimal using vector instantiation and shuffling
constexpr size_t  EVCOUNT_GRAIN = 1536;

namespace gecmi {

calculated_info_t calculate_till_tolerance(
    two_relations_ref two_rel,
    double risk , // <-- Upper bound of probability of the true value being
                  //  -- farthest from estimated value than the epvar
    double epvar,
    bool fasteval  // Use approximate fast evaluation
    )
{
    assert(risk > 0 && risk < 1 && epvar > 0 && epvar < 1 && "risk and epvar should E (0, 1)");

    importance_matrix_t norm_conf;
    importance_vector_t norm_cols;
    importance_vector_t norm_rows;

    // left: Nodes, right: Clusters
    size_t rows = uniqSize(two_rel.first.right) + 1;
    size_t cols = uniqSize(two_rel.second.right) + 1;

    counter_matrix_t cm =
        boost::numeric::ublas::zero_matrix< importance_float_t >( rows, cols );

    importance_float_t nmi;
    importance_float_t max_var = 1.0e10;


    vertices_t  vertices;
    vertices.reserve( uniqSize( two_rel.first.left ) );
    auto& vmap = two_rel.first.left;  // First vmap
    // Fill the vertices
    for(const auto& ind = vmap.begin(); ind != vmap.end();) {
        vertices.push_back(ind->first);
        const_cast<typename std::remove_reference_t<decltype(vmap)>::iterator&>(ind)
            = vmap.equal_range(ind->first).second;
    }
#ifdef DEBUG
    const auto  vertDbgSize = uniqSize( two_rel.second.left );
    if(vertices.size() != vertDbgSize)
        throw std::domain_error("calculate_till_tolerance(), The vertices both clusterings should be the same: "
            + std::to_string(vertices.size()) + " != " + std::to_string(vertDbgSize) + "\n");
#endif  // DEBUG

    deep_complete_simulator dcs(two_rel, vertices);

//    // The expected number of communities should not increase the square root from the number of nodes
//    // Note: such definition should yield faster computation when the number of clusters is huge
//    // and their size is small (SNAP Amazon dataset)
//    const size_t  steps = (rows - 1) * (cols - 1);  // Process all cells of the table

    // Evaluate required accuracy:
    const double  acr = 2*risk/(risk + epvar)*epvar;
    // Evaluate required visiting ratio:  node_acr_min >= (1 / (node_degree=2) / 2) ^ visrt
    // Note: /2 because any link has 2 incident nodes and is evaluated from both sides
    // P_lowest(link) occurs in case the node has 2 links and is equal to: (1/node_degree / 2)^visrt
    // => visrt <= log_0.25(node_acr_min) = log_2(node_acr_min)/-2 < log2(acr) / -2
    // Note: log2(acr=0..1) < 0   => log2(acr) / -2 > 0
    // For the whole network the accuracy is much higher

    // The expected number of vertices to process (considering multiple walks through the same vertex)
    //size_t  steps = vertices.size() * 0.65 * log2(acr) / -2;  // Note: *0.65 because anyway visrt is selected too large
    float  avgdeg = fasteval ? 0.65f : 1;  // Average degree, let it be 0.65 for 10K and decreasing on larger nets
    if(fasteval) {
        const float  degrt = log2(vertices.size()) - log2(8192);
        if(degrt > 1 / avgdeg)  // ~ >= 13 K
            avgdeg = 1 / degrt;
    }
    size_t  steps = vertices.size() * avgdeg * log2(acr) / -2;  // Note: *0.65 because anyway visrt is selected too large
    if(steps < vertices.size() * 1.75f * avgdeg) {
        //assert(0 && "The number of steps is expected to be at least twice the number of vertices");
        steps = vertices.size() * 2 * avgdeg;
    }

    // Use this to adjust number of threads
    tbb::task_scheduler_init tsi;

    while( epvar < max_var )
    {

        tbb::spin_mutex wait_for_matrix;
        try {
            parallel_for(
                tbb::blocked_range< size_t >( 0, steps, EVCOUNT_GRAIN ),  // EVCOUNT_THRESHOLD
                direct_worker< counter_matrix_t* >( dcs, &cm, &wait_for_matrix )
            );
        } catch (tbb::tbb_exception const& e) {
            std::cout << "e" << std::endl;
            throw std::runtime_error("SystemIsSuspiciuslyFailingTooMuch ctt (maybe your partition is not solvable?)");
        }

        size_t total_events = total_events_from_unmi_cm( cm );
        normalize_events(
            cm,
            norm_conf,
            norm_cols,
            norm_rows
            );
        variances_at_prob(
            norm_conf, norm_cols, norm_rows,
            total_events,
            risk,
            max_var,
            nmi
            );
        //std::cout << total_events << " events simulated. Approx. error: " <<  max_var << std::endl;
    }

    return calculated_info_t{max_var, nmi};
}// calculate_till_tolerance

}  // gecmi

#include <fstream>
#include <iostream>
#include <iomanip>
#include <random>
#include <functional>
#include <mutex>
#include <ios>
#include <tuple>

#include <omp.h>

#include <satgirgs/Generator.h>


namespace satgirgs {

std::vector<double> generateWeights(int n, double ple, int weightSeed, bool parallel) {
    const auto threads = parallel ? std::max(1, std::min(omp_get_max_threads(), n / 10000)) : 1;
    auto result = std::vector<double>(n);

    #pragma omp parallel num_threads(threads)
    {
        const auto tid = omp_get_thread_num();
        auto gen = std::default_random_engine{weightSeed >= 0 ? (weightSeed+tid) : std::random_device()()};
        auto dist = std::uniform_real_distribution<>{};

        #pragma omp for schedule(static)
        for (int i = 0; i < n; ++i) {
            result[i] = std::pow((std::pow(0.5*n, -ple + 1) - 1) * dist(gen) + 1, 1 / (-ple + 1));
        }
    }

    return result;
}

std::vector<std::vector<double>> generatePositions(int n, int dimension, int positionSeed, bool parallel) {
    const auto threads = parallel ? std::max(1, std::min(omp_get_max_threads(), n / 10000)) : 1;
    auto result = std::vector<std::vector<double>>(n, std::vector<double>(dimension));

    #pragma omp parallel num_threads(threads)
    {
        const auto tid = omp_get_thread_num();
        auto gen = std::default_random_engine{positionSeed >= 0 ? (positionSeed+tid) : std::random_device()()};
        auto dist = std::uniform_real_distribution<>{};

        #pragma omp for schedule(static)
        for(int i=0; i<n; ++i)
            for (int d=0; d<dimension; ++d)
                result[i][d] = dist(gen);
    }

    return result;
}

std::vector<Node2D> convertToNodes(std::vector<std::vector<double>> positions, std::vector<double> weights, int indiceOffset){
    assert(positions.size() == weights.size());
    std::vector<Node2D> result;
    for(int i = 0; i < positions.size(); i++){
        result.push_back(Node2D(positions[i], weights[i], indiceOffset + i));
    }
    return result;
}

// sort and then collect runs of equal edges
std::vector<std::tuple<int,int,int>> deduplicateEdges(std::vector<std::pair<int, int>> &edges){
    std::vector<std::tuple<int,int,int>> new_edges;
    sort(edges.begin(), edges.end());

    std::pair<int,int> last = edges[0];
    int run_length = 0;
    for(auto edge : edges){
        if(edge == last){
            run_length++;
        } else {
            int u, v;
            std::tie(u, v) = last; 
            new_edges.emplace_back(u, v, run_length);
            run_length = 1;
        }
        last = edge;
    }
    return new_edges;
}

// we could make this more efficient using a k-d-tree (refer to https://rosettacode.org/wiki/K-d_tree#C.2B.2B for this)
std::vector<std::pair<int, int>> generateEdges(const std::vector<Node2D> &c_nodes,
        const std::vector<Node2D> &nc_nodes, bool debugMode) {

    using edge_vector = std::vector<std::pair<int, int>>;
    edge_vector result;

    std::vector<std::pair<
            edge_vector,
            uint64_t[31] /* avoid false sharing */
    > > local_edges(omp_get_max_threads());

    constexpr auto block_size = size_t{1} << 20;

    std::mutex m;
    auto flush = [&] (const edge_vector& local) {
        std::lock_guard<std::mutex> lock(m);
        result.insert(result.end(), local.cbegin(), local.cend());
    };

    auto addEdge = [&](int u, int v, int tid) {
        auto& local = local_edges[tid].first;
        if(u > v) std::swap(u, v);
        local.emplace_back(u,v);
        if (local.size() == block_size) {
            flush(local);
            local.clear();
        }
    };

    const auto num_threads = omp_get_max_threads();

    #pragma omp parallel for schedule(static), num_threads(num_threads)
    for(int clauseIndex = 0; clauseIndex < c_nodes.size(); clauseIndex++){
        auto cp = c_nodes[clauseIndex];
        const auto threadId = omp_get_thread_num();

        auto nearest = std::min_element(nc_nodes.begin(), nc_nodes.end(), [&](const Node2D& a, const Node2D& b) {return a.weightedDistance(cp) < b.weightedDistance(cp);});
        auto secondNearest = std::min_element(nc_nodes.begin(), nc_nodes.end(), [&](const Node2D& a, const Node2D& b) {
            if(a == *nearest) return false;
            if(b == *nearest) return true;
            return a.weightedDistance(cp) < b.weightedDistance(cp);
        }); // ignore first minimum

        if(debugMode){
            // add non-clause - clause edges
            // offset clauseIndex by number of non-clause nodes to distinguish from non-clause indices
            addEdge(nearest->index, nc_nodes.size() + clauseIndex, threadId);
            addEdge(secondNearest->index, nc_nodes.size() + clauseIndex, threadId); 
        } else {
            // add non-clause - non-clause edges
            addEdge(nearest->index, secondNearest->index, threadId);
        }
    }

    for(const auto& v : local_edges)
        flush(v.first);

    return result;
}


void saveDot(const std::vector<Node2D>& c_nodes, const std::vector<Node2D>& nc_nodes,
             const std::vector<std::tuple<int, int, int>> &graph, const std::string &file, bool debugMode) {

    std::ofstream f{file};
    if(!f.is_open())
        throw std::runtime_error{"Error: failed to open file \"" + file + '\"'};
    f << "graph girg {\n\toverlap=scale;\n\n";
    f << std::fixed;
    for (int i = 0; i < nc_nodes.size(); ++i) {
        f << '\t' << nc_nodes[i].index << " [label=\""
          << std::setprecision(2) << nc_nodes[i].weight << std::setprecision(6)
          << "\", pos=\"";
        for (auto d = 0u; d < nc_nodes[i].coord.size(); ++d)
            f << (d == 0 ? "" : ",") << nc_nodes[i].coord[d];
        f << "!\"];\n";
    }
    f << std::fixed;
    if(debugMode){
        for (int i = 0; i < c_nodes.size(); ++i) {
            f << '\t' << c_nodes[i].index << " [color=\"red\",style=\"filled\", label=\""
            << std::setprecision(2) << c_nodes[i].weight << std::setprecision(6)
            << "\", pos=\"";
            for (auto d = 0u; d < c_nodes[i].coord.size(); ++d)
                f << (d == 0 ? "" : ",") << c_nodes[i].coord[d];
            f << "!\"];\n";
        }
    }
    f << '\n';
    int u, v, w;
    for (const auto &[u, v, w] : graph)
        f << '\t' << u << "\t-- " << v << "[label=\"" << w << "\"];\n";
    f << "}\n";
}

} // namespace satgirgs

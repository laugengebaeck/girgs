
#include <hypergirgs/RadiusLayer.h>

#include <cassert>

#include <hypergirgs/AngleHelper.h>


namespace hypergirgs {


RadiusLayer::RadiusLayer(double r_min, double r_max, unsigned int targetLevel, const std::vector<int> &nodes,
                         const std::vector<double> &angles, const std::vector<Point> &points)
: m_r_min(r_min)
, m_r_max(r_max)
, m_target_level(targetLevel)
{
    // allocate stuff
    const auto cellsInLevel = AngleHelper::numCellsInLevel(targetLevel);
    m_prefix_sums.resize(cellsInLevel+1, 0);

    // count num of points in each cell
    for(auto node : nodes) {
        const auto cell = AngleHelper::cellForPoint(angles[node], targetLevel);
        assert(cell < cellsInLevel);
        ++m_prefix_sums[cell];
    }

    // compute exclusive prefix sums
    // prefix_sums[i] is the number of all points in cells j<i of the same level
    {
        unsigned sum = 0;
        for(auto& val : m_prefix_sums)  {
            const auto tmp = val;
            val = sum;
            sum += tmp;
        }
    }

    m_points.resize(m_prefix_sums.back());

    // fill point lookup
    auto num_inserted = std::vector<int>(cellsInLevel, 0); // keeps track of bucket size for counting sort
    for(auto node : nodes){
        auto targetCell = AngleHelper::cellForPoint(angles[node], targetLevel);
        m_points[m_prefix_sums[targetCell] + num_inserted[targetCell]] = points[node];
        ++num_inserted[targetCell];
    }
}

int RadiusLayer::pointsInCell(unsigned int cell, unsigned int level) const {
    assert(level <= m_target_level);
    assert(AngleHelper::firstCellOfLevel(level) <= cell && cell < AngleHelper::firstCellOfLevel(level+1)); // cell is from correct level

    // we want the begin-th and end-th cell in level targetLevel to be the first and last descendant of cell in this level
    // we could apply the firstChild function to find the first descendant but this is in O(1)
    auto descendants = AngleHelper::numCellsInLevel(m_target_level - level);
    auto localIndexCell = cell - AngleHelper::firstCellOfLevel(level);
    auto localIndexDescendant = localIndexCell * descendants; // each cell before the parent splits in 2^D cells in the next layer that are all before our descendant
    auto begin = localIndexDescendant;
    auto end = begin + descendants - 1;

    assert(begin + AngleHelper::firstCellOfLevel(level) < AngleHelper::firstCellOfLevel(m_target_level+1));
    assert(end + AngleHelper::firstCellOfLevel(level) < AngleHelper::firstCellOfLevel(m_target_level+1));

    return m_prefix_sums[end] - m_prefix_sums[begin] + (m_prefix_sums[end + 1] - m_prefix_sums[end]);
}

const Point& RadiusLayer::kthPoint(unsigned int cell, unsigned int level, int k) const {
    assert(level <= m_target_level);
    assert(AngleHelper::firstCellOfLevel(level) <= cell && cell < AngleHelper::firstCellOfLevel(level+1)); // cell is from fromLevel

    // same as in "pointsInCell"
    auto descendants = AngleHelper::numCellsInLevel(m_target_level - level);
    auto localIndexCell = cell - AngleHelper::firstCellOfLevel(level);
    auto localIndexDescendant = localIndexCell * descendants;
    auto begin = localIndexDescendant;

    return m_points[m_prefix_sums[begin] + k];
}

const Point* RadiusLayer::firstPointPointer(unsigned int cell, unsigned int level) const {
    assert(level <= m_target_level);
    assert(AngleHelper::firstCellOfLevel(level) <= cell && cell < AngleHelper::firstCellOfLevel(level + 1)); // cell is from fromLevel

    // same as in "pointsInCell"
    auto descendants = AngleHelper::numCellsInLevel(m_target_level - level);
    auto localIndexCell = cell - AngleHelper::firstCellOfLevel(level);
    auto localIndexDescendant = localIndexCell * descendants;
    auto begin = localIndexDescendant;

    return m_points.data() + m_prefix_sums[begin];
}


} // namespace hypergirgs

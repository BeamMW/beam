#include "filter.h"

#include <algorithm>
#include <numeric>

using namespace std;

namespace beam::wallet
{
Filter::Filter(size_t size)
    : _samples(size, 0.0)
    , _index{0}
    , _is_poor{true}
{
}

void Filter::addSample(double value)
{
    _samples[_index] = value;
    _index = (_index + 1) % _samples.size();
    if (_is_poor)
    {
        _is_poor = _index + 1 < _samples.size();
    }
}

double Filter::getAverage() const
{
    double sum = accumulate(_samples.begin(), _samples.end(), 0.0);
    return sum / (_is_poor ? _index : _samples.size());
}

double Filter::getMedian() const
{
    vector<double> temp(_samples.begin(), _samples.end());
    size_t medianPos = (_is_poor ? _index : temp.size()) / 2;
    nth_element(temp.begin(),
                temp.begin() + medianPos,
                _is_poor ? temp.begin() + _index : temp.end());
    return temp[medianPos];
}
}  // namespace beamui

#include "filters/BodyFilter.h"
#include <algorithm>

void BodyFilter::apply(PointCloud& cloud, const Frame&) {
    cloud.erase(
        std::remove_if(cloud.begin(), cloud.end(), [this](const Point& p) {
            return p.x < minX || p.x > maxX
                || p.y < minY || p.y > maxY
                || p.z < minZ || p.z > maxZ;
        }),
        cloud.end()
    );
}

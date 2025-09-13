#include <clay/clay.h>
#include <sstream>
#include <cmath>

namespace clay {

// ClayParams
bool ClayParams::is_valid() const {
    if (k <= 0 || m <= 0 || w <= 0) {
        return false;
    }

    if (d < k || d > k + m - 1) {
        return false;
    }

    if ((w & (w - 1)) != 0) {
        return false;
    }
    
    return true;
}

std::string ClayParams::to_string() const {
    std::ostringstream oss;
    oss << "ClayParams{k=" << k << ", m=" << m << ", d=" << d << ", w=" << w << "}";
    return oss.str();
}

} 

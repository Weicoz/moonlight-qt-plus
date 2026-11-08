#pragma once

#include <QString>

namespace NetworkInterfaceUtils {
inline bool isMacVirtualEthernet(const QString& name)
{
    if (!name.startsWith(QStringLiteral("feth")) || name.size() == 4) {
        return false;
    }
    for (int i = 4; i < name.size(); ++i) {
        if (name[i] < QLatin1Char('0') || name[i] > QLatin1Char('9')) {
            return false;
        }
    }
    return true;
}
} // namespace NetworkInterfaceUtils

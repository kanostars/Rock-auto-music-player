#pragma once
#include <QColor>
#include <algorithm>
#include <array>
#include <cmath>

namespace rock {
// Track indices stay stable when filtering, soloing, or editing a song.
inline QColor trackColor(int track) {
    static constexpr std::array<const char*,12> palette{
        "#189e91", "#8763bd", "#c07835", "#328ca8", "#bf688a", "#63964b",
        "#637fc0", "#a58435", "#b36265", "#497fb0", "#9a68a3", "#598b72"
    };
    track=std::max(0,track);
    if(track<static_cast<int>(palette.size()))return QColor(palette[track]);
    return QColor::fromHsvF(std::fmod(.48+track*.38196601125,1.0),.58,.70+(track%3)*.06);
}
}

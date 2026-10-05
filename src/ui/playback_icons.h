#pragma once
#include <QIcon>
namespace rock {
enum class PlaylistIcon {Up,Down,Remove,Loop,Single,Shuffle,Once,Previous,Next,Play,Pause,Headphones,Keyboard,List,Restore,Power,Volume,Opacity};
QIcon playlistIcon(PlaylistIcon kind,bool light=false);
}

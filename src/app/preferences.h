#pragma once
#include <QObject>
#include <QKeySequence>
#include <array>
class QKeyEvent;

namespace rock {
enum class ShortcutAction {Preview,Undo,Redo,RedoAlternate,SelectAll,DeleteNotes,PitchUp,PitchDown,CancelEdit,Fullscreen,PerformancePause,PerformanceStop,PerformancePrevious,PerformanceNext,MiniMode,RangeLeftToPlayhead,RangeRightToPlayhead,Count};
constexpr size_t shortcutCount=static_cast<size_t>(ShortcutAction::Count);
using ShortcutBindings=std::array<QKeySequence,shortcutCount>;
struct ShortcutDefinition {ShortcutAction action;const char* id;const char* name;const char* defaultKey;bool global;};
const std::array<ShortcutDefinition,shortcutCount>& shortcutDefinitions();
ShortcutBindings defaultShortcuts();
QString validateShortcuts(const ShortcutBindings& bindings);
bool matchesShortcut(ShortcutAction action,const QKeyEvent* event);

class Preferences:public QObject {
    Q_OBJECT
public:
    static Preferences& instance();
    bool darkTheme() const{return dark_;}
    void setDarkTheme(bool dark);
    const ShortcutBindings& shortcuts() const{return bindings_;}
    QKeySequence shortcut(ShortcutAction action) const{return bindings_[static_cast<size_t>(action)];}
    QString shortcutText(ShortcutAction action) const;
    bool saveShortcuts(const ShortcutBindings& bindings,QString& error);
signals:
    void themeChanged();
    void shortcutsChanged();
private:
    explicit Preferences(QObject* parent);
    bool dark_{};
    ShortcutBindings bindings_;
};
}

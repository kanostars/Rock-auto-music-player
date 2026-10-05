#pragma once
#include "app/preferences.h"
#include <QAbstractNativeEventFilter>
namespace rock {
struct NativeHotkey {unsigned modifiers{},key{};bool valid{};};
NativeHotkey nativeHotkey(const QKeySequence& sequence);
bool checkGlobalShortcuts(const ShortcutBindings& bindings,QString& error);

class GlobalShortcut:public QObject,public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit GlobalShortcut(int id,QObject* parent=nullptr);
    ~GlobalShortcut() override;
    bool enable(const QKeySequence& key,QString& error);
    void disable();
    bool enabled() const{return enabled_;}
    QKeySequence key() const{return key_;}
    bool nativeEventFilter(const QByteArray&,void*,qintptr*) override;
signals:
    void activated();
private:
    int id_;QKeySequence key_;bool enabled_{};
};
}

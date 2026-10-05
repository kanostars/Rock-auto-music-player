#include "preferences.h"
#include "platform/global_shortcut.h"
#include <QKeyEvent>
#include <QSettings>

namespace rock {
    const std::array<ShortcutDefinition, shortcutCount> &shortcutDefinitions() {
        static const std::array<ShortcutDefinition, shortcutCount> definitions{
            {
                {ShortcutAction::Preview, "preview", "试听播放 / 暂停", "Space", false},
                {ShortcutAction::Undo, "undo", "撤销", "Ctrl+Z", false},
                {ShortcutAction::Redo, "redo", "重做", "Ctrl+Y", false},
                {ShortcutAction::RedoAlternate, "redoAlternate", "重做（备用）", "Ctrl+Shift+Z", false},
                {ShortcutAction::SelectAll, "selectAll", "全选音符", "Ctrl+A", false},
                {ShortcutAction::DeleteNotes, "deleteNotes", "删除选中音符", "Del", false},
                {ShortcutAction::PitchUp, "pitchUp", "升高一个音级", "Up", false},
                {ShortcutAction::PitchDown, "pitchDown", "降低一个音级", "Down", false},
                {ShortcutAction::CancelEdit, "cancelEdit", "取消拖动 / 添加 / 选择", "Esc", false},
                {ShortcutAction::Fullscreen, "fullscreen", "独立轨道全屏 / 还原", "F11", false},
                {ShortcutAction::PerformancePause, "performancePause", "演奏暂停 / 继续", "Ctrl+W", true},
                {ShortcutAction::PerformanceStop, "performanceStop", "终止演奏", "Ctrl+R", true},
                {ShortcutAction::PerformancePrevious, "performancePrevious", "上一首", "Ctrl+Q", true},
                {ShortcutAction::PerformanceNext, "performanceNext", "下一首", "Ctrl+E", true},
                {ShortcutAction::MiniMode, "miniMode", "主窗口 / 小窗切换", "F12", true},
                {ShortcutAction::RangeLeftToPlayhead, "rangeLeftToPlayhead", "左边界移至播放标", "Alt+[", false},
                {ShortcutAction::RangeRightToPlayhead, "rangeRightToPlayhead", "右边界移至播放标", "Alt+]", false}
            }
        };
        return definitions;
    }

    ShortcutBindings defaultShortcuts() {
        ShortcutBindings bindings;
        for (const auto &d: shortcutDefinitions())
            bindings[static_cast<size_t>(d.action)] = QKeySequence(
                QString::fromUtf8(d.defaultKey), QKeySequence::PortableText);
        return bindings;
    }

    QString validateShortcuts(const ShortcutBindings &bindings) {
        for (size_t i = 0; i < bindings.size(); ++i) {
            const auto &key = bindings[i];
            const auto &d = shortcutDefinitions()[i];
            if (key.isEmpty()) {
                if (d.action == ShortcutAction::PerformancePause || d.action == ShortcutAction::PerformanceStop)
                    return
                            QString("%1不能为空。").arg(QString::fromUtf8(d.name));
                continue;
            }
            if (key.count() != 1 || key[0].key() == Qt::Key_unknown || key[0].key() == Qt::Key_Control || key[0].key()
                == Qt::Key_Shift || key[0].key() == Qt::Key_Alt || key[0].key() == Qt::Key_Meta)
                return
                        QString("%1需要一个有效的按键组合。").arg(QString::fromUtf8(d.name));
            if (d.global && !nativeHotkey(key).valid)return "全局快捷键可用 F1～F24，或 Ctrl / Alt 加字母、数字，可同时使用 Shift。";
            for (size_t j = 0; j < i; ++j)
                if (!bindings[j].isEmpty() && key == bindings[j])
                    return
                            QString("%1与%2使用了相同的快捷键 %3。").arg(QString::fromUtf8(d.name),
                                                              QString::fromUtf8(shortcutDefinitions()[j].name),
                                                              key.toString(QKeySequence::NativeText));
        }
        return {};
    }

    Preferences::Preferences(QObject *parent) : QObject(parent), bindings_(defaultShortcuts()) {
        QSettings settings;
        dark_ = settings.value("appearance/dark", false).toBool();
        auto saved = bindings_;
        for (const auto &d: shortcutDefinitions()) {
            const auto path = "shortcuts/" + QString::fromUtf8(d.id);
            if (settings.contains(path))
                saved[static_cast<size_t>(d.action)] = QKeySequence(
                    settings.value(path).toString(), QKeySequence::PortableText);
        }
        const auto original = saved;
        if (settings.value("shortcuts/controlDefaultsVersion", 0).toInt() < 2) {
            for (const auto action: {ShortcutAction::PerformancePause, ShortcutAction::PerformanceStop}) {
                const auto index = static_cast<size_t>(action);
                const QKeySequence oldDefault(action == ShortcutAction::PerformancePause ? "Ctrl+Alt+Q" : "Ctrl+Alt+E");
                if (saved[index] == oldDefault)saved[index] = bindings_[index];
            }
        }
        // Preserve custom bindings that already use a newly introduced default.
        for (const auto action: {
                 ShortcutAction::PerformancePrevious, ShortcutAction::PerformanceNext, ShortcutAction::MiniMode,
                 ShortcutAction::RangeLeftToPlayhead, ShortcutAction::RangeRightToPlayhead
             }) {
            const auto index = static_cast<size_t>(action);
            const auto path = "shortcuts/" + QString::fromUtf8(shortcutDefinitions()[index].id);
            if (!settings.contains(path))
                for (size_t other = 0; other < saved.size(); ++other)
                    if (
                        other != index && saved[index] == saved[other]) {
                        saved[index] = {};
                        break;
                    }
        }
        if (!validateShortcuts(saved).isEmpty())
            for (const auto action: {
                     ShortcutAction::PerformancePause,
                     ShortcutAction::PerformanceStop
                 })
                saved[static_cast<size_t>(action)] = original[static_cast<size_t>
                    (action)];
        if (validateShortcuts(saved).isEmpty()) {
            bindings_ = saved;
            for (const auto action: {ShortcutAction::PerformancePause, ShortcutAction::PerformanceStop}) {
                const auto index = static_cast<size_t>(action);
                if (saved[index] != original[index])
                    settings.setValue(
                        "shortcuts/" + QString::fromUtf8(shortcutDefinitions()[index].id),
                        saved[index].toString(QKeySequence::PortableText));
            }
        }
        settings.setValue("shortcuts/controlDefaultsVersion", 2);
    }

    Preferences &Preferences::instance() {
        static auto *preferences = new Preferences(QCoreApplication::instance());
        return *preferences;
    }

    void Preferences::setDarkTheme(bool dark) {
        if (dark_ == dark)return;
        dark_ = dark;
        QSettings().setValue("appearance/dark", dark);
        emit themeChanged();
    }

    QString Preferences::shortcutText(ShortcutAction action) const {
        const auto key = shortcut(action);
        return key.isEmpty() ? QString("未设置") : key.toString(QKeySequence::NativeText);
    }

    bool Preferences::saveShortcuts(const ShortcutBindings &bindings, QString &error) {
        error = validateShortcuts(bindings);
        if (!error.isEmpty())return false;
        bool globalChanged = false;
        for (const auto &definition: shortcutDefinitions())
            if (
                definition.global && bindings[static_cast<size_t>(definition.action)] != bindings_[static_cast<size_t>(
                    definition.action)])
                globalChanged = true;
        if (globalChanged && !checkGlobalShortcuts(bindings, error))return false;
        if (bindings_ == bindings)return true;
        QSettings settings;
        for (const auto &d: shortcutDefinitions())
            settings.setValue("shortcuts/" + QString::fromUtf8(d.id),
                              bindings[static_cast<size_t>(d.action)].toString(
                                  QKeySequence::PortableText));
        bindings_ = bindings;
        emit shortcutsChanged();
        return true;
    }

    bool matchesShortcut(ShortcutAction action, const QKeyEvent *event) {
        const auto sequence = Preferences::instance().shortcut(action);
        return !sequence.isEmpty() && sequence == QKeySequence(event->keyCombination());
    }
}

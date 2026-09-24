#include "playback/performance_engine.h"
#include "playback/performance_controller.h"
#include "platform/key_output.h"
#include <QtTest>
#include <QApplication>
#include <QWidget>
#include <QKeyEvent>
#include <QLineEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QTextStream>
#include <QVBoxLayout>
#include <set>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace rock;
class FakeOutput:public KeyOutput {
public:
    TargetStatus target{TargetStatus::Ready};bool modifiers{},failDown{},opened{},activated{},activationSucceeds{true};
    std::vector<KeyBatch> events;
    bool prepare(const OutputTarget&,QString&) override {opened=true;return true;}
    bool activate() override {activated=true;return activationSucceeds;}
    TargetStatus targetStatus() override{return target;}
    bool modifiersHeld() override{return modifiers;}
    bool send(KeyBatch b) override{events.push_back(b);return !(failDown&&b.down);}
    void close() override{opened=false;}
};
class PerformanceTests:public QObject {
    Q_OBJECT
private slots:
    void configurableCountdown(){
        for(int seconds:{1,3,5,60,3600})for(bool activate:{false,true}){
            auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
            QVERIFY(e.start({{{0,1}},1,0,.03,.02,seconds},{},activate,10));
            QCOMPARE(e.snapshot().countdown,double(seconds));QCOMPARE(o->activated,activate);
            e.tick(10+seconds-.001);QVERIFY(o->events.empty());
            e.tick(10+seconds);QCOMPARE(o->events.size(),size_t(1));QCOMPARE(o->events.back().down,uint16_t(1));
        }
        for(int seconds:{0,-1}){
            auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
            QVERIFY(!e.start({{{0,1}},1,0,.03,.02,seconds},{},true,0));
            QVERIFY(!o->opened);QVERIFY(!o->activated);QVERIFY(o->events.empty());
        }
    }
    void customCountdownPauseAndActivationFailure(){
        auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
        o->activationSucceeds=false;o->target=TargetStatus::NotForeground;
        QVERIFY(e.start({{{0,1}},1,0,.03,.02,3},{},true,0));
        QVERIFY(e.snapshot().message.contains("前台焦点"));
        e.togglePause(1);e.tick(50);QVERIFY(o->events.empty());
        e.togglePause(50);QCOMPARE(e.snapshot().state,PerformanceState::Paused);
        o->target=TargetStatus::Ready;e.togglePause(100);
        e.tick(101.999);QVERIFY(o->events.empty());e.tick(102);QCOMPARE(o->events.size(),size_t(1));
    }
    void countdownAndActivation(){
        for(bool activate:{false,true}){
            auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine engine(std::move(output));
            QVERIFY(engine.start({{{2,3},{2.2,4}},2.4,2,.03,.02},{},activate,100));QCOMPARE(o->activated,activate);
            engine.tick(104.999);QVERIFY(o->events.empty());QCOMPARE(engine.snapshot().position,2.0);
            engine.tick(105);QCOMPARE(o->events.size(),size_t(1));QCOMPARE(o->events[0].down,uint16_t(3));
            engine.tick(105.031);QCOMPARE(o->events.back().up,uint16_t(3));
            engine.tick(105.2);QCOMPARE(o->events.back().down,uint16_t(4));
            engine.tick(105.24);engine.tick(105.41);QCOMPARE(engine.snapshot().state,PerformanceState::Finished);QVERIFY(!o->opened);
        }
    }
    void pauseResumeAndStop(){
        auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
        e.start({{{0,1},{1,2}},2,0,.3,.02},{},false,0);e.tick(5);e.tick(5.1);e.togglePause(5.1);
        QCOMPARE(o->events.back().up,uint16_t(1));const auto position=e.snapshot().position;const auto count=o->events.size();
        e.tick(50);QCOMPARE(o->events.size(),count);QCOMPARE(e.snapshot().position,position);
        o->modifiers=true;e.togglePause(50);e.tick(51);QCOMPARE(e.snapshot().position,position);
        o->modifiers=false;e.tick(51.9);QCOMPARE(o->events.back().down,uint16_t(2));e.stop();QCOMPARE(o->events.back().up,uint16_t(2));
        QCOMPARE(e.snapshot().state,PerformanceState::Stopped);QVERIFY(!o->opened);
    }
    void pauseDuringCountdown(){
        auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
        e.start({{{0,1}},1,0,.03,.02},{},false,0);e.togglePause(2);e.tick(100);QVERIFY(o->events.empty());
        e.togglePause(100);e.tick(102.999);QVERIFY(o->events.empty());e.tick(103);QCOMPARE(o->events.size(),size_t(1));
    }
    void focusLossAndFailureRelease(){
        auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
        e.start({{{0,1},{1,2}},2,0,.3,.02},{},false,0);o->target=TargetStatus::NotForeground;e.tick(5);
        QCOMPARE(e.snapshot().state,PerformanceState::Paused);QVERIFY(o->events.empty());
        o->target=TargetStatus::Ready;e.togglePause(6);e.tick(6);o->target=TargetStatus::NotForeground;e.tick(6.1);
        QCOMPARE(e.snapshot().state,PerformanceState::Paused);QCOMPARE(o->events.back().up,uint16_t(1));
        e.stop();o->target=TargetStatus::Ready;o->failDown=true;e.start({{{0,3}},1,0,.03,.02},{},false,10);e.tick(15);
        QCOMPARE(e.snapshot().state,PerformanceState::Failed);QCOMPARE(o->events.back().up,uint16_t(3));QVERIFY(!o->opened);
    }
    void lateDispatchDoesNotBurst(){
        auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
        e.start({{{0,1},{.2,2},{.3,4}},1,0,.03,.02},{},false,0);e.tick(5);e.tick(5.5);
        QCOMPARE(e.snapshot().state,PerformanceState::Paused);QCOMPARE(e.snapshot().position,.2);QCOMPARE(o->events.size(),size_t(2));
        e.togglePause(6);e.tick(6);QCOMPARE(o->events.back().down,uint16_t(2));
    }
    void delayedReleaseWaitsWithoutPausing(){
        Conversion c;c.duration=46;c.notes={{0,6,Mapping::Exact,45.66,.03,false,0,60},
            {1,6,Mapping::Exact,45.71,.03,false,100,160},{2,8,Mapping::Exact,45.71,.03,false,100,160}};
        auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
        QVERIFY(e.start(makePerformancePlan(c,45.66,30,20),{},false,0));
        e.tick(5);QCOMPARE(o->events.back().down,uint16_t(64));
        e.tick(5.047);QCOMPARE(o->events.back().up,uint16_t(64)); // 17 ms late release.
        e.tick(5.05);QCOMPARE(e.snapshot().state,PerformanceState::Playing);QCOMPARE(o->events.size(),size_t(2));
        e.tick(5.066);QCOMPARE(o->events.size(),size_t(2)); // Still need 1 ms of the configured gap.
        e.tick(5.067);QCOMPARE(e.snapshot().state,PerformanceState::Playing);
        QCOMPARE(o->events.size(),size_t(3));QCOMPARE(o->events.back().down,uint16_t(320)); // Keep the chord together.
        e.stop();QCOMPARE(o->events.back().up,uint16_t(320));
    }
    void repeatTimingUnderJitter_data(){
        QTest::addColumn<double>("period");
        QTest::newRow("1-ms")<<.001;QTest::newRow("7-ms")<<.007;QTest::newRow("15.625-ms")<<.015625;
    }
    void repeatTimingUnderJitter(){
        QFETCH(double,period);
        Conversion c;c.duration=20;
        std::vector<uint16_t> expected;
        for(int i=0;i<400;++i){c.notes.push_back({i,6,Mapping::Exact,i*.05,.02,false,i*100,i*100+40});
            if(i%5==0)c.notes.push_back({400+i,8,Mapping::Exact,i*.05,.02,false,i*100,i*100+40});
            expected.push_back(i%5==0?320:64);}
        auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
        QVERIFY(e.start(makePerformancePlan(c,0,30,20),{},false,0));
        std::array<double,9> down{},up;up.fill(-1e100);uint16_t held=0;size_t read=0;std::vector<uint16_t> played;
        for(int tick=0;tick<60000&&e.snapshot().active();++tick){
            const double now=5+tick*period;e.tick(now);
            QVERIFY2(e.snapshot().state!=PerformanceState::Paused,qPrintable(e.snapshot().message));
            for(;read<o->events.size();++read){const auto event=o->events[read];
                for(int key=0;key<9;++key){const uint16_t bit=uint16_t(1<<key);
                    if(event.up&bit){QVERIFY(held&bit);QVERIFY(now-down[key]+1e-9>=.03);held&=~bit;up[key]=now;}
                    if(event.down&bit){QVERIFY(!(held&bit));QVERIFY(now-up[key]+1e-9>=.02);held|=bit;down[key]=now;}}
                if(event.down)played.push_back(event.down);
            }
        }
        QCOMPARE(e.snapshot().state,PerformanceState::Finished);QCOMPARE(played,expected);QCOMPARE(held,uint16_t(0));
    }
    void pauseOrStopWhileWaitingForRepeat(){
        for(bool stop:{false,true}){
            auto output=std::make_unique<FakeOutput>();auto* o=output.get();PerformanceEngine e(std::move(output));
            QVERIFY(e.start({{{0,64},{.05,64}},.2,0,.03,.02},{},false,0));
            e.tick(5);e.tick(5.06);QCOMPARE(e.snapshot().state,PerformanceState::Playing);QCOMPARE(o->events.size(),size_t(2));
            if(stop){e.stop();e.tick(6);QCOMPARE(e.snapshot().state,PerformanceState::Stopped);QCOMPARE(o->events.size(),size_t(2));}
            else{e.togglePause(5.061);e.tick(6);QCOMPARE(o->events.size(),size_t(2));
                e.togglePause(6);e.tick(6);QCOMPARE(o->events.size(),size_t(3));QCOMPARE(o->events.back().down,uint16_t(64));}
        }
    }
    void preflightMatchesDisplayedConflicts(){
        for(int spacing:{98,100,102}){ // 49 / 50 / 51 ms with 30 ms hold + 20 ms gap.
            Song song;song.ppq=1000;song.endTick=1000;song.tempos={{0,500000,0}};
            song.tracks={{"test",0,0,3}};song.notes={{0,60,100,0,80},{0,60,100,0,80},{0,60,100,spacing,spacing+80}};
            auto settings=defaultSettings(song);settings.autoTranspose=false;
            auto c=convert(song,settings);auto r=retimePerformance(song,c,settings);
            QCOMPARE(c.conflicts,spacing<100?1:0);QCOMPARE(r.conflicts,c.conflicts);
            if(spacing<100){QVERIFY(r.notes.back().conflict);QVERIFY_EXCEPTION_THROWN(makePerformancePlan(r,0,30,20),std::invalid_argument);}
            else QCOMPARE(makePerformancePlan(r,0,30,20).strikes.size(),size_t(2));
        }
    }
    void plansTempoConflictsAndOffset(){
        Song song;song.ppq=100;song.endTick=200;song.tempos={{0,500000,0},{100,1000000,.5}};
        Conversion c;c.duration=1.5;c.notes={{0,0,Mapping::Edited,0,.1,false,0,20},{1,1,Mapping::Exact,.5,.2,false,100,120},{2,1,Mapping::Exact,.5,.2,false,100,120},{3,-1,Mapping::Deleted,1,.1,false,150,160}};
        Settings s;s.fixedTempo=true;s.bpm=60;auto r=retimePerformance(song,c,s);
        QCOMPARE(r.duration,2.0);QCOMPARE(r.notes[1].start,1.0);QCOMPARE(r.notes[0].mapping,Mapping::Edited);
        auto p=makePerformancePlan(r,.5,30,20);QCOMPARE(p.strikes.size(),size_t(1));QCOMPARE(p.strikes[0].time,1.0);QCOMPARE(p.strikes[0].keys,uint16_t(2));
        QVERIFY_EXCEPTION_THROWN(makePerformancePlan(r,2,30,20),std::invalid_argument);
        r.notes.push_back({4,1,Mapping::Exact,1.02,.1,false,102,112});
        QVERIFY_EXCEPTION_THROWN(makePerformancePlan(r,0,30,20),std::invalid_argument);
        r.notes.back().start=1.06;QCOMPARE(makePerformancePlan(r,0,30,20).strikes.size(),size_t(3));
    }
    void nativeWindowActivation(){
        if(!qEnvironmentVariableIsSet("ROCK_NATIVE_ACTIVATION_TEST"))QSKIP("Opt-in desktop test activates only its own helper window; no keys are sent.");
#ifdef Q_OS_WIN
        QProcess receiver;
        auto cleanup=qScopeGuard([&]{if(receiver.state()!=QProcess::NotRunning){receiver.terminate();if(!receiver.waitForFinished(2000)){receiver.kill();receiver.waitForFinished(2000);}}});
        receiver.start(QCoreApplication::applicationFilePath(),{"-platform","windows","--activation-receiver"});
        QVERIFY(receiver.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(receiver.canReadLine(),5000);
        const auto identity=receiver.readLine().trimmed();
        bool ok=false;const auto id=identity.toULongLong(&ok);QVERIFY2(ok,identity.constData());
        const auto target=reinterpret_cast<HWND>(quintptr(id));const auto pid=quint32(receiver.processId());
        QWidget source;source.setWindowTitle("自动演奏 · 焦点切换测试");source.resize(320,100);source.show();
        for(WPARAM command:{WPARAM(SC_RESTORE),WPARAM(SC_MAXIMIZE),WPARAM(SC_MINIMIZE)}){
            DWORD_PTR result=0;
            QVERIFY(SendMessageTimeoutW(target,WM_SYSCOMMAND,command,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&result));
            // The helper grants the test process permission to take the foreground
            // back between cases; activation of the target itself uses production code.
            QVERIFY(SendMessageTimeoutW(target,WM_APP+42,GetCurrentProcessId(),0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&result));
            QCoreApplication::processEvents();source.raise();source.activateWindow();SetForegroundWindow(reinterpret_cast<HWND>(source.winId()));
            QTRY_COMPARE(GetForegroundWindow(),reinterpret_cast<HWND>(source.winId()));
            QVERIFY(activateOutputWindow(id,pid));QCOMPARE(GetForegroundWindow(),target);QVERIFY(!IsIconic(target));
            if(command==SC_MAXIMIZE)QVERIFY(IsZoomed(target));
            GUITHREADINFO info{};info.cbSize=sizeof(info);
            QVERIFY(GetGUIThreadInfo(GetWindowThreadProcessId(target,nullptr),&info));
            QVERIFY(info.hwndFocus==target||IsChild(target,info.hwndFocus));
            QTest::qWait(100);QCOMPARE(GetForegroundWindow(),target);
        }
        QVERIFY(!activateOutputWindow(id,GetCurrentProcessId()));
        QVERIFY(PostMessageW(target,WM_CLOSE,0,0));QVERIFY(receiver.waitForFinished(3000));
        QVERIFY(!activateOutputWindow(id,pid));
#endif
    }
    void nativeControlledReceiver(){
        if(!qEnvironmentVariableIsSet("ROCK_NATIVE_OUTPUT_TEST"))QSKIP("Opt-in desktop test sends keys only to its own foreground receiver.");
#ifdef Q_OS_WIN
        const auto keyboards=discoverOutputKeyboards();QVERIFY2(keyboards.error.isEmpty(),qPrintable(keyboards.error));QVERIFY(!keyboards.choices.empty());
        struct Receiver:QWidget {int downs{},ups{};std::set<int> keys;void keyPressEvent(QKeyEvent* e)override{if(!e->isAutoRepeat()){++downs;keys.insert(e->key());}}void keyReleaseEvent(QKeyEvent* e)override{if(!e->isAutoRepeat())++ups;}} receiver;
        receiver.setWindowTitle("九键手碟 · 自动演奏隔离测试");receiver.resize(500,180);receiver.show();receiver.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&receiver));
        PerformanceController controller;QString error;
        QVERIFY2(controller.start({{{0,511}},.3,0,.04,.02},{keyboards.choices.front().id.toString(),quint64(receiver.winId()),GetCurrentProcessId()},true,error),qPrintable(error));
        QTest::qWait(100);QCOMPARE(receiver.downs,0);
        // Exercise the same native messages Windows delivers for the registered global shortcuts.
        QVERIFY(PostThreadMessageW(GetCurrentThreadId(),WM_HOTKEY,0x5241,0));
        QTRY_COMPARE(controller.snapshot().state,PerformanceState::Paused);QTest::qWait(100);QCOMPARE(receiver.downs,0);
        QVERIFY(PostThreadMessageW(GetCurrentThreadId(),WM_HOTKEY,0x5241,0));
        QTRY_COMPARE(controller.snapshot().state,PerformanceState::Countdown);
        QTRY_COMPARE_WITH_TIMEOUT(controller.snapshot().state,PerformanceState::Finished,7000);
        QTRY_COMPARE(receiver.downs,9);QTRY_COMPARE(receiver.ups,9);QCOMPARE(receiver.keys.size(),size_t(9));
        QVERIFY2(controller.start({{{0,1}},1,0,.03,.02},{keyboards.choices.front().id.toString(),quint64(receiver.winId()),GetCurrentProcessId()},false,error),qPrintable(error));
        QVERIFY(PostThreadMessageW(GetCurrentThreadId(),WM_HOTKEY,0x5242,0));QTRY_COMPARE(controller.snapshot().state,PerformanceState::Stopped);
        QCOMPARE(receiver.downs,9);QVERIFY(discoverOutputKeyboards().error.isEmpty());
#endif
    }
};
int main(int argc,char** argv){
    QApplication app(argc,argv);
    if(app.arguments().contains("--activation-receiver")){
        struct Receiver:QWidget {
#ifdef Q_OS_WIN
            bool nativeEvent(const QByteArray& type,void* message,qintptr* result) override {
                const auto* msg=static_cast<MSG*>(message);
                if(msg->message==WM_APP+42){AllowSetForegroundWindow(DWORD(msg->wParam));if(result)*result=1;return true;}
                return QWidget::nativeEvent(type,message,result);
            }
#endif
        } window;
        window.setWindowTitle("自动演奏 · 隔离焦点接收窗口");window.resize(360,120);
        auto* layout=new QVBoxLayout(&window);auto* input=new QLineEdit;layout->addWidget(input);
        window.show();input->setFocus();
        QTextStream stream(stdout);stream<<qulonglong(window.winId())<<Qt::endl;
        return app.exec();
    }
    PerformanceTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "performance_tests.moc"

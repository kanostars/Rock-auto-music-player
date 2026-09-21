#include "ui/main_window.h"
#include "ui/piano_roll.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSignalSpy>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>
#include <MidiFile.h>
#include <sstream>

class UiTests : public QObject {
    Q_OBJECT
    static void drag(QWidget* widget,const QPoint& from,const QPoint& to,bool cancel=false,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        QTest::mousePress(widget,Qt::LeftButton,modifiers,from);
        QMouseEvent move(QEvent::MouseMove,QPointF(to),QPointF(widget->mapToGlobal(to)),Qt::NoButton,Qt::LeftButton,modifiers);
        QApplication::sendEvent(widget,&move);
        if(cancel)QTest::keyClick(widget,Qt::Key_Escape);
        QTest::mouseRelease(widget,Qt::LeftButton,modifiers,to);
    }
private slots:
    void initTestCase() {
        QVERIFY(QDir().mkpath(qEnvironmentVariable("ROCK_SCREENSHOTS")));
    }
    void reportPagination() {
        QTemporaryDir fixtures;QVERIFY(fixtures.isValid());
        for(int count:{0,200,201,1001}) {
            smf::MidiFile midi;midi.setTPQ(480);midi.addTempo(0,0,120);
            for(int i=0;i<count;++i) {
                midi.addNoteOn(0,i*240,0,rock::pitches[i%9],100);
                midi.addNoteOff(0,i*240+120,0,rock::pitches[i%9]);
            }
            std::ostringstream stream;QVERIFY(midi.write(stream));auto bytes=stream.str();
            QFile file(fixtures.path()+"/page-test.mid");QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes.data(),static_cast<qint64>(bytes.size())),static_cast<qint64>(bytes.size()));file.close();
            rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();
            w.importFiles({file.fileName()});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
            QTest::mouseClick(w.findChild<QPushButton*>("reportButton"),Qt::LeftButton);
            auto* report=w.findChild<QDialog*>("conversionReport");QVERIFY(report);
            auto* table=report->findChild<QTableWidget*>("conversionTable");auto* page=report->findChild<QSpinBox*>("reportPage");
            auto* previous=report->findChild<QPushButton*>("reportPreviousPage");auto* next=report->findChild<QPushButton*>("reportNextPage");
            auto* first=report->findChild<QPushButton*>("reportFirstPage");auto* last=report->findChild<QPushButton*>("reportLastPage");
            auto* range=report->findChild<QLabel*>("reportRecordRange");
            QVERIFY(table&&page&&previous&&next&&first&&last&&range);
            QCOMPARE(table->rowCount(),std::min(200,count));QCOMPARE(page->value(),1);
            QCOMPARE(page->maximum(),std::max(1,(count+199)/200));QVERIFY(!previous->isEnabled());QVERIFY(!first->isEnabled());
            QCOMPARE(page->isEnabled(),count>200);QCOMPARE(next->isEnabled(),count>200);
            std::set<int> sources;
            for(int current=1;current<=page->maximum();++current) {
                QCOMPARE(page->value(),current);QCOMPARE(table->rowCount(),std::min(200,count-(current-1)*200));
                for(int row=0;row<table->rowCount();++row) {
                    int expected=(current-1)*200+row;
                    QCOMPARE(table->item(row,0)->data(Qt::UserRole).toInt(),expected);
                    QCOMPARE(table->verticalHeaderItem(row)->text(),QString::number(expected+1));
                    QCOMPARE(table->item(row,0)->text(),QString::number(expected*.25,'f',3));
                    QVERIFY(sources.insert(expected).second);
                }
                if(current<page->maximum())QTest::mouseClick(next,Qt::LeftButton);
            }
            QCOMPARE(sources.size(),size_t(count));QVERIFY(!next->isEnabled());QVERIFY(!last->isEnabled());
            if(!count){QCOMPARE(range->text(),QString("共 0 条记录"));continue;}
            QVERIFY(range->text().contains(QString::number(count)));
            if(count>200) {
                QTest::mouseClick(previous,Qt::LeftButton);QCOMPARE(page->value(),page->maximum()-1);
                QTest::mouseClick(first,Qt::LeftButton);QCOMPARE(table->item(0,0)->data(Qt::UserRole).toInt(),0);
                page->setFocus();page->selectAll();QTest::keyClicks(page,"2");QTest::keyClick(page,Qt::Key_Return);
                QCOMPARE(page->value(),2);QCOMPARE(table->item(0,0)->data(Qt::UserRole).toInt(),200);
                QTest::mouseClick(last,Qt::LeftButton);QCOMPARE(page->value(),page->maximum());
            }
            if(count==1001) {
                QTest::qWait(25);QVERIFY(report->grab().save(qEnvironmentVariable("ROCK_SCREENSHOTS")+"/14-report-last-page.png"));
            }
            int source=table->item(table->rowCount()-1,0)->data(Qt::UserRole).toInt();
            QVERIFY(QMetaObject::invokeMethod(table,"cellDoubleClicked",Qt::DirectConnection,Q_ARG(int,table->rowCount()-1),Q_ARG(int,0)));
            QCOMPARE(w.findChild<rock::PianoRoll*>("pianoRoll")->selectedSource(),source);
            QCOMPARE(source,count-1);
        }
    }
    void addNotesWithUndoAndPlayback() {
        rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();QTest::qWait(80);
        auto* add=w.findChild<QPushButton*>("addNoteButton");QVERIFY(add);QVERIFY(!add->isEnabled());
        w.importFiles({qEnvironmentVariable("ROCK_SAMPLES")+"/nine-keys.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        auto* roll=w.findChild<rock::PianoRoll*>("pianoRoll");roll->setZoom(80);
        auto* undo=w.findChild<QPushButton*>("undoButton");auto* redo=w.findChild<QPushButton*>("redoButton");
        auto* play=w.findChild<QPushButton*>("playButton");auto* deleteMode=w.findChild<QPushButton*>("deleteMode");
        auto point=[&](double seconds,int target) {
            int rh=std::max(14,(roll->viewport()->height()-44)/10);
            return QPoint(qRound(92+seconds*roll->zoom()-roll->horizontalScrollBar()->value()),44+(8-target)*rh+rh/2);
        };
        const int shift=w.currentResult()->octaveShift;
        QTest::mouseClick(add,Qt::LeftButton);QVERIFY(add->isChecked());QVERIFY(roll->addMode());
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(4.5,8));
        QCOMPARE(w.currentResult()->notes.size(),size_t(10));QCOMPARE(roll->selectedSource(),9);
        const auto created=w.currentResult()->notes[9];QCOMPARE(created.target,8);
        QVERIFY(std::abs(created.start-4.5)<.002);QVERIFY(std::abs(created.duration-.5)<.002);
        QCOMPARE(w.currentResult()->octaveShift,shift);QCOMPARE(w.currentResult()->edited,1);
        QVERIFY(w.findChild<QLabel*>("noteDetails")->text().contains("手工新增"));
        // Existing notes remain editable; clicking one does not create a duplicate.
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(4.7,8));QCOMPARE(w.currentResult()->notes.size(),size_t(10));
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(4.5,-1));
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(80,100));
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(400,20));QCOMPARE(w.currentResult()->notes.size(),size_t(10));
        QTest::keyClick(roll->viewport(),Qt::Key_Escape);QVERIFY(!add->isChecked());QVERIFY(!roll->addMode());
        QTest::mouseClick(undo,Qt::LeftButton);QCOMPARE(w.currentResult()->edited,0);QCOMPARE(w.currentResult()->deleted,0);
        QCOMPARE(w.currentResult()->excluded,0);QCOMPARE(w.currentResult()->notes[9].target,-1);
        QVERIFY(std::abs(w.currentResult()->duration-4.25)<.002);
        QTest::mouseClick(redo,Qt::LeftButton);QCOMPARE(w.currentResult()->notes[9].target,8);
        // A creation followed by edit and deletion has three independently undoable steps.
        auto from=point(4.7,8);int rh=std::max(14,(roll->viewport()->height()-44)/10);
        drag(roll->viewport(),from,from+QPoint(20,rh));QCOMPARE(w.currentResult()->notes[9].target,7);
        QTest::keyClick(roll->viewport(),Qt::Key_Delete);QCOMPARE(w.currentResult()->deleted,1);
        QTest::mouseClick(undo,Qt::LeftButton);QCOMPARE(w.currentResult()->notes[9].target,7);
        QTest::mouseClick(undo,Qt::LeftButton);QCOMPARE(w.currentResult()->notes[9].target,8);
        QTest::mouseClick(undo,Qt::LeftButton);QCOMPARE(w.currentResult()->notes[9].target,-1);
        QTest::mouseClick(add,Qt::LeftButton);QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(4.5,0));
        QCOMPARE(w.currentResult()->notes.size(),size_t(11));QCOMPARE(w.currentResult()->notes[10].target,0);QVERIFY(!redo->isEnabled());
        QTest::mouseClick(deleteMode,Qt::LeftButton);QVERIFY(!add->isChecked());QVERIFY(!roll->addMode());
        QTest::mouseClick(add,Qt::LeftButton);QVERIFY(!deleteMode->isChecked());
        QTest::mouseClick(play,Qt::LeftButton);QVERIFY(play->text().contains("暂停试听"));QVERIFY(!add->isEnabled());QVERIFY(!roll->addMode());
        QTest::mouseClick(play,Qt::LeftButton);QVERIFY(add->isEnabled());
        auto* speed=w.findChild<QDoubleSpinBox*>("speedSpin");speed->setValue(2);
        QTest::mouseClick(w.findChild<QPushButton*>("primary"),Qt::LeftButton);
        QVERIFY(std::abs(w.currentResult()->notes[10].start-2.25)<.002);QVERIFY(std::abs(w.currentResult()->notes[10].duration-.25)<.002);
        QCOMPARE(w.currentResult()->notes[9].target,-1);QCOMPARE(w.currentResult()->octaveShift,shift);
        roll->setZoom(80);QTest::mouseClick(add,Qt::LeftButton);
        QTest::qWait(25);QVERIFY(w.grab().save(qEnvironmentVariable("ROCK_SCREENSHOTS")+"/12-add-notes.png"));
        w.resize(1120,740);QTest::qWait(25);QVERIFY(w.grab().save(qEnvironmentVariable("ROCK_SCREENSHOTS")+"/13-add-notes-compact.png"));
        // Added notes survive switching files, without becoming imported/original notes.
        w.importFiles({qEnvironmentVariable("ROCK_SAMPLES")+"/empty.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        QVERIFY(!roll->addMode());w.findChild<QListWidget*>("songList")->setCurrentRow(0);
        QCOMPARE(w.currentResult()->notes[10].target,0);QCOMPARE(w.currentResult()->notes[9].target,-1);
    }
    void addToEmptyAndFilteredTracks() {
        rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();QTest::qWait(80);
        w.importFiles({qEnvironmentVariable("ROCK_SAMPLES")+"/empty.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        auto* roll=w.findChild<rock::PianoRoll*>("pianoRoll");roll->setZoom(160);
        auto* add=w.findChild<QPushButton*>("addNoteButton");auto* tree=w.findChild<QTreeWidget*>("trackTree");
        auto point=[&](double seconds,int target) {
            int rh=std::max(14,(roll->viewport()->height()-44)/10);
            return QPoint(qRound(92+seconds*roll->zoom()-roll->horizontalScrollBar()->value()),44+(8-target)*rh+rh/2);
        };
        QTest::mouseClick(add,Qt::LeftButton);QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(.25,0));
        QCOMPARE(w.currentResult()->notes.size(),size_t(1));QCOMPARE(w.currentResult()->notes[0].target,0);QCOMPARE(tree->topLevelItemCount(),1);
        QVERIFY(tree->topLevelItem(0)->text(0).contains("手工音轨"));QVERIFY(w.currentResult()->duration>=.75);
        QTest::mouseClick(w.findChild<QPushButton*>("undoButton"),Qt::LeftButton);QCOMPARE(w.currentResult()->edited,0);
        QTest::mouseClick(w.findChild<QPushButton*>("redoButton"),Qt::LeftButton);QCOMPARE(w.currentResult()->edited,1);
        w.importFiles({qEnvironmentVariable("ROCK_SAMPLES")+"/studio-demo.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        roll->setZoom(80);auto* filter=w.findChild<QComboBox*>("trackFilter");filter->setCurrentIndex(2);
        int track=filter->currentData().toInt();tree->topLevelItem(track)->setCheckState(0,Qt::Unchecked);
        auto count=w.currentResult()->notes.size();QTest::mouseClick(add,Qt::LeftButton);
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(.25,8));QCOMPARE(w.currentResult()->notes.size(),count);
        tree->topLevelItem(track)->setCheckState(0,Qt::Checked);
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(.25,8));QCOMPARE(w.currentResult()->notes.size(),count+1);
        QCOMPARE(w.currentResult()->notes.back().target,8);QVERIFY(roll->selectedSources().contains(static_cast<int>(count)));
        tree->topLevelItem(track)->setCheckState(0,Qt::Unchecked);QCOMPARE(w.currentResult()->notes.back().mapping,rock::Mapping::Excluded);
        tree->topLevelItem(track)->setCheckState(0,Qt::Checked);QCOMPARE(w.currentResult()->notes.back().target,8);
    }
    void boxSelectionAndBatchEditing() {
        rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();QTest::qWait(80);
        w.importFiles({qEnvironmentVariable("ROCK_SAMPLES")+"/nine-keys.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        auto* roll=w.findChild<rock::PianoRoll*>("pianoRoll");roll->setZoom(80);
        auto* undo=w.findChild<QPushButton*>("undoButton");auto* redo=w.findChild<QPushButton*>("redoButton");
        auto* remove=w.findChild<QPushButton*>("deleteNoteButton");
        const int rh=std::max(14,(roll->viewport()->height()-44)/10);
        auto point=[&](int id,int edge=0) {
            const auto& n=w.currentResult()->notes[id];double x=92+n.start*roll->zoom()-roll->horizontalScrollBar()->value();
            x+=edge<0?2:edge>0?n.duration*roll->zoom()-2:n.duration*roll->zoom()/2;
            return QPoint(qRound(x),44+(8-n.target)*rh+rh/2);
        };
        const QPoint boxStart(93,44+9*rh+rh/2),boxEnd(198,44+6*rh+1);
        QSignalSpy seek(roll,&rock::PianoRoll::seekRequested);
        drag(roll->viewport(),boxStart,boxEnd);
        QVERIFY(roll->selectedSources()==std::set<int>({0,1,2}));QCOMPARE(seek.count(),0);
        QCOMPARE(w.currentResult()->edited,0);QVERIFY(!undo->isEnabled());QVERIFY(remove->text().contains("3"));
        QVERIFY(w.findChild<QLabel*>("noteDetails")->text().contains("已选中 3"));
        QTest::keyClick(roll->viewport(),Qt::Key_Down);QVERIFY(!undo->isEnabled()); // already includes B
        auto from=point(1);drag(roll->viewport(),from,from-QPoint(0,20*rh));
        for(int id=0;id<3;++id)QCOMPARE(w.currentResult()->notes[id].target,id+6);
        QCOMPARE(w.currentResult()->edited,3);QVERIFY(roll->selectedSources().size()==3);
        QTest::keyClick(roll->viewport(),Qt::Key_Up); // U is a group boundary, not individual clamping
        QTest::mouseClick(undo,Qt::LeftButton);QCOMPARE(w.currentResult()->edited,0);QVERIFY(!undo->isEnabled());
        QTest::mouseClick(redo,Qt::LeftButton);QCOMPARE(w.currentResult()->edited,3);
        QTest::mouseClick(undo,Qt::LeftButton);
        from=point(1,1);drag(roll->viewport(),from,from+QPoint(20,0));
        for(int id=0;id<3;++id)QVERIFY(std::abs(w.currentResult()->notes[id].duration-.5)<.002);
        QTest::mouseClick(undo,Qt::LeftButton);QCOMPARE(w.currentResult()->edited,0);
        from=point(1,-1);drag(roll->viewport(),from,from+QPoint(300,0));
        for(int id=0;id<3;++id) {
            const auto& n=w.currentResult()->notes[id];QVERIFY(n.duration>0&&n.duration<.02);
            QVERIFY(std::abs(n.start+n.duration-(id*.5+.25))<.002);
        }
        QTest::mouseClick(undo,Qt::LeftButton);
        from=point(1);drag(roll->viewport(),from,from+QPoint(40,0));
        for(int id=0;id<3;++id)QVERIFY(std::abs(w.currentResult()->notes[id].start-(id*.5+.5))<.002);
        from=point(1);drag(roll->viewport(),from,from-QPoint(400,0));
        for(int id=0;id<3;++id)QVERIFY(std::abs(w.currentResult()->notes[id].start-id*.5)<.002);
        // Box cancellation restores the previous group; a simple blank click still seeks.
        drag(roll->viewport(),boxStart,QPoint(400,45),true);QVERIFY(roll->selectedSources()==std::set<int>({0,1,2}));
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::ControlModifier,point(2));
        QVERIFY(roll->selectedSources()==std::set<int>({0,1}));
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::ShiftModifier,point(2));
        QVERIFY(roll->selectedSources()==std::set<int>({0,1,2}));
        const QPoint extraStart(210,44+5*rh+1),extraEnd(240,44+6*rh-1);
        drag(roll->viewport(),extraStart,extraEnd,false,Qt::ShiftModifier);
        QVERIFY(roll->selectedSources()==std::set<int>({0,1,2,3}));
        QTest::keyClick(roll->viewport(),Qt::Key_Delete);QCOMPARE(w.currentResult()->deleted,4);
        QVERIFY(roll->selectedSources().empty());
        QTest::mouseClick(undo,Qt::LeftButton);QCOMPARE(w.currentResult()->deleted,0);QCOMPARE(roll->selectedSources().size(),size_t(4));
        QTest::mouseClick(redo,Qt::LeftButton);QCOMPARE(w.currentResult()->deleted,4);
        QTest::mouseClick(undo,Qt::LeftButton);
        // Unequal lengths shrink by one common delta, stopping at the shortest note.
        roll->selectSource(0,false);from=point(0,1);drag(roll->viewport(),from,from+QPoint(20,0));
        roll->selectSources({0,1,2});from=point(1,1);drag(roll->viewport(),from,from-QPoint(300,0));
        QVERIFY(std::abs(w.currentResult()->notes[0].duration-.26)<.002);
        for(int id:{1,2})QVERIFY(w.currentResult()->notes[id].duration>0&&w.currentResult()->notes[id].duration<.02);
        QTest::mouseClick(undo,Qt::LeftButton);
        QTest::keyClick(roll->viewport(),Qt::Key_Up);
        for(int id=0;id<3;++id)QCOMPARE(w.currentResult()->notes[id].target,id+1);
        QTest::qWait(25);QVERIFY(w.grab().save(qEnvironmentVariable("ROCK_SCREENSHOTS")+"/11-batch-selection.png"));
        QTest::mouseClick(w.findChild<QPushButton*>("primary"),Qt::LeftButton);
        QVERIFY(roll->selectedSources()==std::set<int>({0,1,2})); // conversion retains group edits and selection
        for(int id=0;id<3;++id)QCOMPARE(w.currentResult()->notes[id].target,id+1);
        QTest::keyClick(roll->viewport(),Qt::Key_A,Qt::ControlModifier);QCOMPARE(roll->selectedSources().size(),size_t(9));
        QTest::keyClick(roll->viewport(),Qt::Key_Escape);QVERIFY(roll->selectedSources().empty());
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,boxStart);QCOMPARE(seek.count(),1);
    }
    void batchSelectionRespectsFilterAndPlaybackLock() {
        auto song=std::make_shared<rock::Song>();song->ppq=480;song->tempos={{0,500000,0}};
        song->tracks={{"one",0,0,2},{"two",1,1,2}};
        song->notes={{0,45,100,480,960},{1,52,100,480,960},{0,54,100,960,1440},{1,57,100,960,1440}};song->endTick=1440;
        auto settings=rock::defaultSettings(*song);settings.autoOctave=false;settings.nearest=false;
        auto result=std::make_shared<rock::Conversion>(rock::convert(*song,settings));
        rock::PianoRoll roll;roll.resize(700,450);roll.show();QTest::qWait(25);roll.setMusic(song,result);roll.setZoom(160);roll.setTrackFilter(0);
        int deletes=0,edits=0;std::vector<rock::MappedNote> batch;
        connect(&roll,&rock::PianoRoll::deleteRequested,&roll,[&]{++deletes;});
        connect(&roll,&rock::PianoRoll::notesEdited,&roll,[&](const auto& notes){++edits;batch=notes;});
        const int rh=std::max(14,(roll.viewport()->height()-44)/10);
        // A reverse-direction rectangle spans both mapped and skipped notes, but only the visible track.
        drag(roll.viewport(),QPoint(400,44+10*rh-1),QPoint(94,45));
        QVERIFY(roll.selectedSources()==std::set<int>({0,2}));
        QTest::keyClick(roll.viewport(),Qt::Key_Up);QCOMPARE(edits,1);QCOMPARE(batch.size(),size_t(1));QCOMPARE(batch[0].source,0);QCOMPARE(batch[0].target,1);
        QTest::keyClick(roll.viewport(),Qt::Key_Delete);QCOMPARE(deletes,1); // skipped selection can also be deleted
        roll.setEditingEnabled(false);
        QTest::keyClick(roll.viewport(),Qt::Key_Up);QTest::keyClick(roll.viewport(),Qt::Key_Delete);
        drag(roll.viewport(),QPoint(192,44+8*rh+rh/2),QPoint(240,44+6*rh+rh/2));
        QCOMPARE(edits,1);QCOMPARE(deletes,1);QVERIFY(!roll.isEditing());
        roll.setEditingEnabled(true);roll.setTrackFilter(1);QVERIFY(roll.selectedSources().empty());
        QTest::keyClick(roll.viewport(),Qt::Key_A,Qt::ControlModifier);QVERIFY(roll.selectedSources()==std::set<int>({1,3}));
        // Selection uses timeline coordinates even when horizontally scrolled.
        roll.setTrackFilter(-1);roll.setZoom(1200);roll.horizontalScrollBar()->setValue(500);
        drag(roll.viewport(),QPoint(93,44+10*rh-1),QPoint(500,45));
        QVERIFY(roll.selectedSources()==std::set<int>({0,1}));
    }
    void playbackLocksEditorAndSpaceResumes() {
        rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();QTest::qWait(80);
        w.importFiles({qEnvironmentVariable("ROCK_SAMPLES")+"/nine-keys.mid"});
        QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        auto* roll=w.findChild<rock::PianoRoll*>("pianoRoll");auto* play=w.findChild<QPushButton*>("playButton");
        auto* remove=w.findChild<QPushButton*>("deleteNoteButton");auto* deleteMode=w.findChild<QPushButton*>("deleteMode");
        auto* undo=w.findChild<QPushButton*>("undoButton");auto* redo=w.findChild<QPushButton*>("redoButton");
        roll->setZoom(160);
        auto point=[&](int source) {
            const auto& note=w.currentResult()->notes[source];
            int rh=std::max(14,(roll->viewport()->height()-44)/10);
            return QPoint(qRound(92+(note.start+note.duration/2)*roll->zoom()-roll->horizontalScrollBar()->value()),44+(8-note.target)*rh+rh/2);
        };
        QTest::mouseClick(play,Qt::LeftButton);QTest::qWait(40);
        QVERIFY(play->text().contains("暂停试听"));QVERIFY(!roll->editingEnabled());
        QVERIFY(!remove->isEnabled());QVERIFY(!deleteMode->isEnabled());QVERIFY(!undo->isEnabled());QVERIFY(!redo->isEnabled());
        auto before=w.currentResult()->notes[0];int beforeDeleted=w.currentResult()->deleted;
        QPoint from=point(0),to=from+QPoint(80,-30);drag(roll->viewport(),from,to);
        QCOMPARE(w.currentResult()->notes[0].start,before.start);QCOMPARE(w.currentResult()->notes[0].duration,before.duration);
        QCOMPARE(w.currentResult()->notes[0].target,before.target);QCOMPARE(w.currentResult()->edited,0);
        QTest::keyClick(roll->viewport(),Qt::Key_Delete);QCOMPARE(w.currentResult()->deleted,beforeDeleted);
        QVERIFY(play->text().contains("暂停试听"));QVERIFY(!remove->isEnabled());
        // Clicking an empty timeline position seeks and pauses, leaving focus in the roll.
        int rh=std::max(14,(roll->viewport()->height()-44)/10);
        QPoint empty(qRound(92+.35*roll->zoom()),44+rh/2);
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,empty);
        QVERIFY(play->text().contains("手盘试听"));QVERIFY(roll->editingEnabled());
        QVERIFY(roll->hasFocus()||roll->viewport()->hasFocus());
        QString sought=w.findChild<QLabel*>("previewClock")->text();QVERIFY(!sought.startsWith("00:00.00"));
        QTest::keyClick(roll->viewport(),Qt::Key_Space);QTest::qWait(70);
        QVERIFY(play->text().contains("暂停试听"));QVERIFY(!roll->editingEnabled());
        QVERIFY(w.findChild<QLabel*>("previewClock")->text()!=sought);
        QTest::keyClick(roll->viewport(),Qt::Key_Space);QVERIFY(play->text().contains("手盘试听"));QVERIFY(roll->editingEnabled());
    }
    void parameterValidationAndConflictGate() {
        rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();
        w.importFiles({qEnvironmentVariable("ROCK_FIXTURES")+"/pirates.mid"});
        QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        auto* bpm=w.findChild<QDoubleSpinBox*>("bpmSpin");auto* speed=w.findChild<QDoubleSpinBox*>("speedSpin");
        auto* hold=w.findChild<QSpinBox*>("holdSpin");auto* gap=w.findChild<QSpinBox*>("gapSpin");
        auto* apply=w.findChild<QPushButton*>("primary");auto* play=w.findChild<QPushButton*>("playButton");
        auto* clock=w.findChild<QLabel*>("previewClock");
        w.findChild<QComboBox*>("tempoMode")->setCurrentIndex(1);
        // Actual typing must retain the entire rejected input, rather than Qt silently dropping a digit.
        const std::array<std::pair<QAbstractSpinBox*,QString>,8> invalid{{{bpm,"401"},{bpm,"19"},{speed,"3.01"},{speed,"0.24"},{hold,"1001"},{hold,"0"},{gap,"1001"},{gap,"-1"}}};
        for(auto [spin,text]:invalid) {
            auto* edit=spin->findChild<QLineEdit*>();spin->setFocus();spin->selectAll();QTest::keyClicks(edit,text);
            QVERIFY(edit->text().contains(text));QTest::keyClick(spin,Qt::Key_Return);
            auto* warning=w.findChild<QMessageBox*>("validationWarning");QVERIFY(warning);QVERIFY(warning->isVisible());
            QVERIFY(warning->text().contains("有效范围"));QVERIFY(warning->text().contains(text));
            warning->accept();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }
        QCOMPARE(bpm->value(),120.0);QCOMPARE(speed->value(),1.0);QCOMPARE(hold->value(),30);QCOMPARE(gap->value(),20);
        // Pasted values and an Apply click follow the same validation path.
        auto* edit=speed->findChild<QLineEdit*>();speed->setFocus();edit->setText("99 ×");
        QTest::mouseClick(apply,Qt::LeftButton);
        auto* warning=w.findChild<QMessageBox*>("validationWarning");QVERIFY(warning);QVERIFY(warning->text().contains("0.25～3"));
        QCOMPARE(speed->value(),1.0);warning->accept();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        // Inclusive bounds are accepted through typed input.
        const std::array<std::pair<QAbstractSpinBox*,QString>,8> bounds{{{bpm,"20"},{bpm,"400"},{speed,"0.25"},{speed,"3"},{hold,"1"},{hold,"1000"},{gap,"0"},{gap,"1000"}}};
        for(auto [spin,text]:bounds) {
            spin->setFocus();spin->selectAll();QTest::keyClicks(spin->findChild<QLineEdit*>(),text);QTest::keyClick(spin,Qt::Key_Return);
            QVERIFY(!w.findChild<QMessageBox*>("validationWarning"));
        }
        QCOMPARE(bpm->value(),400.0);QCOMPARE(speed->value(),3.0);QCOMPARE(hold->value(),1000);QCOMPARE(gap->value(),1000);
        w.findChild<QComboBox*>("tempoMode")->setCurrentIndex(0);speed->setValue(1);gap->setValue(20);
        // Draft settings may not bypass conflict validation by playing the old conversion.
        QTest::mouseClick(play,Qt::LeftButton);warning=w.findChild<QMessageBox*>("validationWarning");
        QVERIFY(warning);QVERIFY(warning->text().contains("先点击"));warning->accept();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QTest::mouseClick(apply,Qt::LeftButton);QVERIFY(w.currentResult()->conflicts>0);
        QString before=clock->text();QTest::mouseClick(play,Qt::LeftButton);
        warning=w.findChild<QMessageBox*>("validationWarning");QVERIFY(warning);QVERIFY(warning->text().contains("同键过密冲突"));
        QTest::qWait(100);QCOMPARE(clock->text(),before);QVERIFY(play->text().contains("手盘试听"));
        QVERIFY(warning->grab().save(qEnvironmentVariable("ROCK_SCREENSHOTS")+"/10-conflict-warning.png"));
        warning->accept();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        hold->setValue(30);QTest::mouseClick(apply,Qt::LeftButton);QCOMPARE(w.currentResult()->conflicts,0);
        QTest::mouseClick(play,Qt::LeftButton);QTest::qWait(100);QVERIFY(play->text().contains("暂停试听"));
        QVERIFY(clock->text()!=before);QTest::mouseClick(w.findChild<QPushButton*>("stopButton"),Qt::LeftButton);
    }
    void octaveAdaptation() {
        rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();
        w.importFiles({qEnvironmentVariable("ROCK_FIXTURES")+"/pirates.mid"});
        QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        QVERIFY(w.currentResult());QCOMPARE(w.currentResult()->octaveShift,-12);
        QCOMPARE(w.currentResult()->exact,241);QCOMPARE(w.currentResult()->notes[0].target,4);
        auto* info=w.findChild<QLabel*>("octaveInfo");QVERIFY(info->text().contains("-12"));
        auto* mode=w.findChild<QComboBox*>("octaveMode");auto* apply=w.findChild<QPushButton*>("primary");
        mode->setCurrentIndex(1);QCOMPARE(w.currentResult()->octaveShift,-12);
        QTest::mouseClick(apply,Qt::LeftButton);QCOMPARE(w.currentResult()->octaveShift,0);
        for(const auto& note:w.currentResult()->notes)QCOMPARE(note.target,8);
        mode->setCurrentIndex(0);w.findChild<QComboBox*>("strategyCombo")->setCurrentIndex(1);
        QTest::mouseClick(apply,Qt::LeftButton);QCOMPARE(w.currentResult()->exact,241);
        QCOMPARE(w.currentResult()->skipped,0);QCOMPARE(w.currentResult()->octaveShift,-12);
        auto* roll=w.findChild<rock::PianoRoll*>("pianoRoll");roll->setZoom(30);
        QTest::qWait(50);
        auto screenshots=qEnvironmentVariable("ROCK_SCREENSHOTS");QDir().mkpath(screenshots);
        QVERIFY(w.grab().save(screenshots+"/07-pirates-octave.png"));
        w.resize(1120,740);QTest::qWait(50);
        QVERIFY(w.grab().save(screenshots+"/08-pirates-compact.png"));
        QTest::mouseClick(w.findChild<QPushButton*>("reportButton"),Qt::LeftButton);
        auto* report=w.findChild<QDialog*>("conversionReport");QVERIFY(report);QVERIFY(report->isVisible());
        auto* table=report->findChild<QTableWidget*>("conversionTable");QVERIFY(table);
        QCOMPARE(table->rowCount(),200);QCOMPARE(table->item(0,1)->text(),QString("A4"));
        QCOMPARE(table->item(0,2)->text(),QString("A3"));QCOMPARE(table->item(0,3)->text(),QString("J"));
        QVERIFY(table->item(0,5)->text().contains("八度适配"));
        for(int row=1;row<table->rowCount();++row)
            QVERIFY(table->item(row,0)->text().toDouble()>=table->item(row-1,0)->text().toDouble());
        QTest::mouseClick(report->findChild<QPushButton*>("reportNextPage"),Qt::LeftButton);QCOMPARE(table->rowCount(),41);
        QTest::mouseClick(report->findChild<QPushButton*>("reportPreviousPage"),Qt::LeftButton);QCOMPARE(table->rowCount(),200);
        QTest::qWait(50);QVERIFY(report->grab().save(screenshots+"/09-conversion-report.png"));
        int source=table->item(0,0)->data(Qt::UserRole).toInt();
        QVERIFY(QMetaObject::invokeMethod(table,"cellDoubleClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,0)));
        QCOMPARE(roll->selectedSource(),source);QVERIFY(w.findChild<QLabel*>("noteDetails")->text().contains("MIDI 69"));
    }
    void importConvertAndDisplay() {
        rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();QTest::qWait(150);
        const QString samples=qEnvironmentVariable("ROCK_SAMPLES");
        const QString screenshots=qEnvironmentVariable("ROCK_SCREENSHOTS");
        QDir().mkpath(screenshots);
        QVERIFY(w.grab().save(screenshots+"/01-empty.png"));
        auto* play=w.findChild<QPushButton*>("playButton");QVERIFY(!play->isEnabled());
        QTemporaryDir unicode;QVERIFY(unicode.isValid());
        QString file=unicode.path()+"/九键测试曲.mid";QVERIFY(QFile::copy(samples+"/studio-demo.mid",file));
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile(file)});
        QDragEnterEvent enter(QPoint(500,300),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&w,&enter);QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(500,300),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&w,&drop);
        QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),15000);
        QVERIFY(w.currentResult());QCOMPARE(w.currentResult()->approximate,4);
        QCOMPARE(w.currentResult()->excluded,1);QCOMPARE(w.currentResult()->exact,92);
        auto* tracks=w.findChild<QTreeWidget*>("trackTree");QCOMPARE(tracks->topLevelItemCount(),3);
        QVERIFY(play->isEnabled());QTest::qWait(150);
        QVERIFY(w.grab().save(screenshots+"/02-imported.png"));
        w.resize(1120,740);QTest::qWait(50);
        auto* compactRoll=w.findChild<rock::PianoRoll*>("pianoRoll");
        QVERIFY(compactRoll->geometry().bottom()<compactRoll->parentWidget()->height()-35);
        QVERIFY(w.grab().save(screenshots+"/04-compact.png"));
        w.resize(1460,900);QTest::qWait(50);

        auto* strategy=w.findChild<QComboBox*>("strategyCombo");strategy->setCurrentIndex(1);
        QCOMPARE(w.currentResult()->approximate,4); // drafts don't change applied results
        QTest::mouseClick(w.findChild<QPushButton*>("primary"),Qt::LeftButton);
        QCOMPARE(w.currentResult()->skipped,4);QCOMPARE(w.currentResult()->approximate,0);
        QVERIFY(w.grab().save(screenshots+"/03-skip-mode.png"));
        strategy->setCurrentIndex(0);QTest::mouseClick(w.findChild<QPushButton*>("primary"),Qt::LeftButton);
        auto* speed=w.findChild<QDoubleSpinBox*>("speedSpin");double original=w.currentResult()->duration;
        speed->setValue(2);QTest::mouseClick(w.findChild<QPushButton*>("primary"),Qt::LeftButton);
        QVERIFY(std::abs(w.currentResult()->duration-original/2)<1e-8);
        speed->setValue(1);QTest::mouseClick(w.findChild<QPushButton*>("primary"),Qt::LeftButton);
        QTest::mouseClick(w.findChild<QPushButton*>("noTracks"),Qt::LeftButton);
        QCOMPARE(w.currentResult()->excluded,97);QVERIFY(!play->isEnabled());
        QTest::mouseClick(w.findChild<QPushButton*>("allTracks"),Qt::LeftButton);
        QCOMPARE(w.currentResult()->excluded,0);QVERIFY(play->isEnabled());
        tracks->topLevelItem(2)->setCheckState(0,Qt::Unchecked);
        tracks->topLevelItem(0)->setCheckState(1,Qt::Checked);QCOMPARE(w.currentResult()->exact+w.currentResult()->approximate,64);
        tracks->topLevelItem(0)->setCheckState(1,Qt::Unchecked);

        auto* roll=w.findChild<rock::PianoRoll*>("pianoRoll");
        auto* filter=w.findChild<QComboBox*>("trackFilter");filter->setCurrentIndex(1);
        QCOMPARE(w.currentResult()->exact,92); // display filter does not mute tracks
        int rowHeight=std::max(14,(roll->viewport()->height()-44)/10);
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(96,44+2*rowHeight+rowHeight/2));
        QVERIFY(w.findChild<QLabel*>("noteDetails")->text().contains("MIDI 60"));filter->setCurrentIndex(0);
        QTest::mouseClick(play,Qt::LeftButton);QTest::qWait(120);QVERIFY(play->text().contains("暂停"));
        auto* volume=w.findChild<QSlider*>("volumeSlider");QVERIFY(volume);volume->setValue(0);QCOMPARE(volume->value(),0);volume->setValue(60);
        QTest::mouseClick(play,Qt::LeftButton);QVERIFY(play->text().contains("手盘试听"));
        QString pausedClock=w.findChild<QLabel*>("previewClock")->text();QTest::qWait(50);
        QCOMPARE(w.findChild<QLabel*>("previewClock")->text(),pausedClock);
        QTest::mouseClick(play,Qt::LeftButton);QTest::qWait(50);QVERIFY(play->text().contains("暂停试听"));
        QTest::mouseClick(w.findChild<QPushButton*>("stopButton"),Qt::LeftButton);QVERIFY(play->text().contains("手盘试听"));
        QVERIFY(w.findChild<QLabel*>("previewClock")->text().startsWith("00:00.00"));

        auto* library=w.findChild<QListWidget*>("songList");QCOMPARE(library->count(),1);
        w.importFiles({samples+"/invalid.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        QCOMPARE(library->count(),1);QVERIFY(w.findChild<QLabel*>("statusText")->text().contains("导入失败"));
        w.importFiles({samples+"/nine-keys.mid",samples+"/empty.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        QCOMPARE(library->count(),3);QVERIFY(w.currentResult()->notes.empty());QVERIFY(!play->isEnabled());
        library->setCurrentRow(1);QCOMPARE(w.currentResult()->exact,9);QVERIFY(play->isEnabled());
        library->setCurrentRow(0);QCOMPARE(w.currentResult()->exact,92);
        QTest::mouseClick(w.findChild<QPushButton*>("reportButton"),Qt::LeftButton);
        auto* dialog=w.findChild<QDialog*>();QVERIFY(dialog);QVERIFY(dialog->isVisible());dialog->close();
    }
    void editNotes() {
        rock::MainWindow w(nullptr,rock::AudioBackend::NullTest);w.show();QTest::qWait(100);
        auto samples=qEnvironmentVariable("ROCK_SAMPLES");
        w.importFiles({samples+"/nine-keys.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        auto* roll=w.findChild<rock::PianoRoll*>("pianoRoll");roll->setZoom(160);
        auto* undo=w.findChild<QPushButton*>("undoButton");auto* redo=w.findChild<QPushButton*>("redoButton");
        auto* remove=w.findChild<QPushButton*>("deleteNoteButton");auto* deleteMode=w.findChild<QPushButton*>("deleteMode");
        auto* apply=w.findChild<QPushButton*>("primary");auto* play=w.findChild<QPushButton*>("playButton");
        auto point=[&](int source,int edge=0) {
            const auto& note=w.currentResult()->notes[source];
            const int rh=std::max(14,(roll->viewport()->height()-44)/10);
            double x=92+note.start*roll->zoom()-roll->horizontalScrollBar()->value();
            double width=std::max(4.0,note.duration*roll->zoom());
            x+=edge<0?2:edge>0?width-2:width/2;
            return QPoint(qRound(x),44+(8-note.target)*rh+rh/2);
        };
        QVERIFY(!undo->isEnabled());QVERIFY(!remove->isEnabled());
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(0));
        QVERIFY(remove->isEnabled());QVERIFY(!undo->isEnabled()); // selection alone is not an edit
        QTest::mouseClick(play,Qt::LeftButton);
        QVERIFY(!roll->editingEnabled());QTest::mouseClick(play,Qt::LeftButton);QVERIFY(roll->editingEnabled());
        int rh=std::max(14,(roll->viewport()->height()-44)/10);
        auto from=point(0);drag(roll->viewport(),from,from+QPoint(64,-2*rh));
        QCOMPARE(w.currentResult()->edited,1);QCOMPARE(w.currentResult()->notes[0].target,2);
        QVERIFY(std::abs(w.currentResult()->notes[0].start-.4)<.002);
        QVERIFY(std::abs(w.currentResult()->notes[0].duration-.25)<.002);
        QVERIFY(play->text().contains("手盘试听"));QVERIFY(undo->isEnabled());
        auto end=w.currentResult()->notes[0].start+w.currentResult()->notes[0].duration;
        from=point(0,1);drag(roll->viewport(),from,from+QPoint(40,0));
        QVERIFY(std::abs(w.currentResult()->notes[0].duration-.5)<.002);
        end=w.currentResult()->notes[0].start+w.currentResult()->notes[0].duration;
        from=point(0,-1);drag(roll->viewport(),from,from+QPoint(16,0));
        QVERIFY(std::abs(w.currentResult()->notes[0].start-.5)<.002);
        QVERIFY(std::abs(w.currentResult()->notes[0].start+w.currentResult()->notes[0].duration-end)<.002);
        auto before=w.currentResult()->notes[0];
        from=point(0);drag(roll->viewport(),from,from+QPoint(32,-rh),true);
        QCOMPARE(w.currentResult()->notes[0].start,before.start);QCOMPARE(w.currentResult()->notes[0].target,before.target);

        QTest::mouseClick(apply,Qt::LeftButton);QCOMPARE(w.currentResult()->notes[0].start,before.start);
        auto* speed=w.findChild<QDoubleSpinBox*>("speedSpin");speed->setValue(2);QTest::mouseClick(apply,Qt::LeftButton);
        QVERIFY(std::abs(w.currentResult()->notes[0].start-before.start/2)<1e-8);
        speed->setValue(1);QTest::mouseClick(apply,Qt::LeftButton);
        auto* track=w.findChild<QTreeWidget*>("trackTree")->topLevelItem(0);
        track->setCheckState(0,Qt::Unchecked);QCOMPARE(w.currentResult()->excluded,9);
        track->setCheckState(0,Qt::Checked);QCOMPARE(w.currentResult()->notes[0].start,before.start);
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(0));
        QTest::keyClick(roll->viewport(),Qt::Key_Delete);QCOMPARE(w.currentResult()->deleted,1);QVERIFY(!remove->isEnabled());
        QTest::mouseClick(apply,Qt::LeftButton);QCOMPARE(w.currentResult()->deleted,1);
        QTest::mouseClick(undo,Qt::LeftButton);QCOMPARE(w.currentResult()->deleted,0);QCOMPARE(w.currentResult()->notes[0].start,before.start);
        QTest::mouseClick(redo,Qt::LeftButton);QCOMPARE(w.currentResult()->deleted,1);
        QTest::mouseClick(undo,Qt::LeftButton);

        QTest::mouseClick(deleteMode,Qt::LeftButton);QVERIFY(deleteMode->isChecked());
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(1));QCOMPARE(w.currentResult()->deleted,1);
        QTest::mouseClick(deleteMode,Qt::LeftButton);QTest::mouseClick(undo,Qt::LeftButton);
        QVERIFY(redo->isEnabled());
        roll->setFocus();QTest::keyClick(roll,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(w.currentResult()->deleted,1);
        QTest::keyClick(roll,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(w.currentResult()->deleted,0);
        // New edit after undo discards the redo branch; left edge clamps at time zero.
        QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(0));
        from=point(0,-1);drag(roll->viewport(),from,from-QPoint(160,0));
        QCOMPARE(w.currentResult()->notes[0].start,0.0);QVERIFY(!redo->isEnabled());
        before=w.currentResult()->notes[0];
        from=point(0,1);drag(roll->viewport(),from,from-QPoint(180,0));
        QVERIFY(w.currentResult()->notes[0].duration>0);QVERIFY(w.currentResult()->notes[0].duration<.02);
        QTest::mouseClick(undo,Qt::LeftButton);
        // Per-song history and edits survive changing the selected file.
        w.importFiles({samples+"/studio-demo.mid"});QTRY_VERIFY_WITH_TIMEOUT(!w.isImporting(),10000);
        QCOMPARE(w.currentResult()->edited,0);QVERIFY(!undo->isEnabled());
        w.findChild<QListWidget*>("songList")->setCurrentRow(0);QCOMPARE(w.currentResult()->edited,1);QVERIFY(undo->isEnabled());
        QCOMPARE(w.currentResult()->notes[0].start,before.start);QCOMPARE(w.currentResult()->notes[0].duration,before.duration);
        roll->setZoom(160);roll->selectSource(0);QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,point(0));
        QVERIFY(w.grab().save(qEnvironmentVariable("ROCK_SCREENSHOTS")+"/05-note-editor.png"));
        w.resize(1120,740);QTest::qWait(50);
        QVERIFY(roll->geometry().bottom()<roll->parentWidget()->height()-35);
        QVERIFY(w.grab().save(qEnvironmentVariable("ROCK_SCREENSHOTS")+"/06-editor-compact.png"));
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);QApplication::setStyle("Fusion");app.setFont(QFont("Microsoft YaHei UI",10));
    if(QApplication::platformName()=="offscreen") {
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/seguisym.ttf");
    }
    UiTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "ui_tests.moc"

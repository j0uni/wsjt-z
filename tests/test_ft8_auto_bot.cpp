#include <algorithm>

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "widgets/FT8AutoBot.hpp"

namespace
{
QString decodeLine(QString const& message, int freq = 1234, int snr = -5, QString const& modeMark = QStringLiteral("#"))
{
  return QStringLiteral("230000 %1  0.2 %2 %3  %4")
    .arg(QStringLiteral("%1").arg(snr, 3, 10, QLatin1Char(' ')),
         QStringLiteral("%1").arg(freq, 4, 10, QLatin1Char(' ')),
         modeMark,
         message);
}

class FakeHost final
  : public FT8AutoBotHost
{
public:
  QString modeValue {QStringLiteral("FT8")};
  QString bandValue {QStringLiteral("20m")};
  QString myCallValue {QStringLiteral("OH1ZZZ")};
  QString myGridValue {QStringLiteral("KP20")};
  QString dxCallValue;
  QString logBookPathValue;
  int qsoProgressValue {0};
  bool transmittingValue {false};
  bool autoEnabledValue {false};
  bool txFirstValue {false};
  int trPeriodValue {15};
  bool tailenderCandidateValue {false};
  QStringList filteredCalls;
  QHash<QString, QString> countriesByCall;
  QSet<QString> workedCountries;
  QHash<bool, QVector<int> > busyBinsBySlot;

  QString armedCall;
  QString armedGrid;
  int armedRxFreq {0};
  int armedTxFreq {0};
  int armedReportDb {0};
  bool armedTxFirst {false};
  int setTxFreqCalls {0};
  int startQsoCalls {0};
  int enableAutoTxCalls {0};
  int stopTxCalls {0};
  int clearDxCalls {0};
  int startCqCalls {0};
  int setAutoSequenceCalls {0};
  QStringList logEntries;

  QString mode() const override { return modeValue; }
  QString band() const override { return bandValue; }
  QString myCall() const override { return myCallValue; }
  QString myGrid() const override { return myGridValue; }
  QString dxCall() const override { return dxCallValue; }
  QString logBookPath() const override { return logBookPathValue; }
  int qsoProgress() const override { return qsoProgressValue; }
  bool transmitting() const override { return transmittingValue; }
  bool autoEnabled() const override { return autoEnabledValue; }
  bool txFirst() const override { return txFirstValue; }
  int trPeriodSeconds() const override { return trPeriodValue; }

  QString countryForCall(QString const& call) const override
  {
    return countriesByCall.value(call.trimmed().toUpper());
  }

  bool countryWorked(QString const& country, QString const&, QString const&) const override
  {
    return workedCountries.contains(country);
  }

  bool callWorkedGlobally(QString const& call) const override
  {
    Q_UNUSED(call);
    return false;
  }

  bool callsignFiltered(DecodedText const& decoded) const override
  {
    QString call;
    QString grid;
    decoded.deCallAndGrid(call, grid);
    return filteredCalls.contains(call.trimmed().toUpper());
  }

  bool tailenderCandidate(DecodedText const& decoded) const override
  {
    Q_UNUSED(decoded);
    return tailenderCandidateValue;
  }

  QVector<int> busyTxBins(int, int, int, bool txFirstSlot) const override
  {
    return busyBinsBySlot.value(txFirstSlot);
  }

  void botSetDx(QString const& call, QString const& grid, int rxFreq, int txFreq,
                int reportDb, bool txFirst) override
  {
    armedCall = call;
    armedGrid = grid;
    armedRxFreq = rxFreq;
    armedTxFreq = txFreq;
    armedReportDb = reportDb;
    armedTxFirst = txFirst;
    dxCallValue = call;
  }

  void botStartQso() override { ++startQsoCalls; }
  void botEnableAutoTx(bool) override { ++enableAutoTxCalls; }
  void botClearDx() override { ++clearDxCalls; dxCallValue.clear(); }
  void botStopTx() override { ++stopTxCalls; }
  void botStartCQ() override { ++startCqCalls; }
  void botSetAutoSequence(bool) override { ++setAutoSequenceCalls; }
  void botSetTxFreq(int txFreq) override
  {
    ++setTxFreqCalls;
    armedTxFreq = txFreq;
  }
  void botLog(QString const& category, QString const& detail) override
  {
    logEntries.append(category + QLatin1Char(':') + detail);
  }
};
}

class TestFT8AutoBot
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void score_new_call_and_new_dx_add_correctly()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QVERIFY2(bot.state() == FT8AutoBotState::InQSO,
             qPrintable(QStringLiteral("state=%1 armedCall=%2 armedTxFirst=%3 armedTxFreq=%4 logs=%5")
                            .arg(QString::number(static_cast<int>(bot.state())),
                                 host.armedCall,
                                 host.armedTxFirst ? QStringLiteral("true") : QStringLiteral("false"),
                                 QString::number(host.armedTxFreq),
                                 host.logEntries.join(QStringLiteral(" | ")))));
    QCOMPARE(host.armedCall, QStringLiteral("VK6XYZ"));
    QVERIFY(host.armedTxFreq >= 1000);
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("country=Australia")) && entry.contains(QStringLiteral("new_dx=500"));
    }));
  }

  void below_threshold_enters_idle_at_999_with_default_1000()
  {
    QTemporaryDir dir;
    FT8AutoBotMemory memory {QDir {dir.path()}};
    QVERIFY(memory.addWorked(QStringLiteral("JA1ABC")));

    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("JA1ABC"), QStringLiteral("Japan"));
    host.workedCountries.insert(QStringLiteral("Japan"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);

    DecodedText decoded {decodeLine(QStringLiteral("CQ JA1ABC PM95"), 1999)};
    bot.onDecode(decoded);
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
  }

  void cq_enters_idle_after_exactly_10_empty_periods()
  {
    QTemporaryDir dir;
    FakeHost host;
    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);

    for (int i = 0; i < 9; ++i) {
      bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
      QVERIFY(bot.state() != FT8AutoBotState::Idle);
    }

    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
    QCOMPARE(bot.snapshot().cqWithoutCallerCount, 10);
  }

  void worked_file_blocks_call_globally()
  {
    QTemporaryDir dir;
    FT8AutoBotMemory memory {QDir {dir.path()}};
    QVERIFY(memory.addWorked(QStringLiteral("OH1ABC/P")));
    QVERIFY(memory.isWorked(QStringLiteral("OH1ABC")));
  }

  void cooldown_expires_and_reenables_candidate()
  {
    QTemporaryDir dir;
    FT8AutoBotMemory memory {QDir {dir.path()}};
    auto const nowUtc = QDateTime::currentDateTimeUtc();
    QVERIFY(memory.addCooldown(QStringLiteral("K1ABC"), QStringLiteral("stuck"), nowUtc, 30));
    QVERIFY(memory.isOnCooldown(QStringLiteral("K1ABC"), nowUtc.addSecs(10)));
    QCOMPARE(memory.purgeExpiredCooldowns(nowUtc.addSecs(31 * 60)), 1);
    QVERIFY(!memory.isOnCooldown(QStringLiteral("K1ABC"), nowUtc.addSecs(31 * 60)));
  }

  void adopting_qso_suppresses_stuck_until_grace_clears()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.dxCallValue = QStringLiteral("K1ABC");
    host.qsoProgressValue = 1;

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    QCOMPARE(bot.state(), FT8AutoBotState::AdoptingQSO);

    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QVERIFY(bot.state() == FT8AutoBotState::AdoptingQSO || bot.state() == FT8AutoBotState::InQSO);
    QCOMPARE(host.stopTxCalls, 0);
  }

  void operator_handoff_does_not_create_cooldown()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.dxCallValue = QStringLiteral("K1ABC");
    host.qsoProgressValue = 1;

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.setEnabled(false);

    QVERIFY(!bot.memory().isOnCooldown(QStringLiteral("K1ABC")));
  }

  void watchdog_abandons_active_qso_and_sets_cooldown()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QVERIFY2(bot.state() == FT8AutoBotState::InQSO,
             qPrintable(QStringLiteral("state=%1 armedCall=%2 armedTxFirst=%3 armedTxFreq=%4 logs=%5")
                            .arg(QString::number(static_cast<int>(bot.state())),
                                 host.armedCall,
                                 host.armedTxFirst ? QStringLiteral("true") : QStringLiteral("false"),
                                 QString::number(host.armedTxFreq),
                                 host.logEntries.join(QStringLiteral(" | ")))));
    bot.onWatchdogTriggered();

    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
    QVERIFY(bot.memory().isOnCooldown(QStringLiteral("VK6XYZ")));
  }

  void cq_stuck_limit_uses_bot_owned_tx_counter()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("DL1ASI"), QStringLiteral("Germany"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    auto settings = bot.settings();
    settings.stuckCycleLimit = 3;
    bot.setSettings(settings);
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);

    auto const now = QDateTime::currentDateTimeUtc();
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("OH1ZZZ DL1ASI -19"), 1709)});
    bot.onPeriodBoundary(now);

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(host.armedCall, QStringLiteral("DL1ASI"));

    bot.onTransmitStarted(QStringLiteral("DL1ASI OH1ZZZ -19"), now.addSecs(15));
    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(bot.counters().activeQsoCycles, 1);

    bot.onTransmitStarted(QStringLiteral("DL1ASI OH1ZZZ -19"), now.addSecs(30));
    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(bot.counters().activeQsoCycles, 2);

    bot.onTransmitStarted(QStringLiteral("DL1ASI OH1ZZZ -19"), now.addSecs(45));
    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
    QVERIFY(bot.memory().isOnCooldown(QStringLiteral("DL1ASI")));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("ABANDON:call=DL1ASI cycles=3 reason=stuck"));
    }));
  }

  void cq_stuck_limit_ignores_unrelated_tx_text()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("DL1ASI"), QStringLiteral("Germany"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    auto settings = bot.settings();
    settings.stuckCycleLimit = 2;
    bot.setSettings(settings);
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);

    auto const now = QDateTime::currentDateTimeUtc();
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("OH1ZZZ DL1ASI -19"), 1709)});
    bot.onPeriodBoundary(now);

    bot.onTransmitStarted(QStringLiteral("CQ OH1ZZZ KP20"), now.addSecs(15));
    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(bot.counters().activeQsoCycles, 0);

    bot.onTransmitStarted(QStringLiteral("DL1ASI OH1ZZZ -19"), now.addSecs(30));
    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(bot.counters().activeQsoCycles, 1);
  }

  void decode_with_my_callsign_is_logged_to_bot_console()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.myCallValue = QStringLiteral("OH1ZZZ");

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);

    auto const line = decodeLine(QStringLiteral("K2ABC OH1ZZZ -10"), 1500);
    bot.onDecode(DecodedText {line});

    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [&line] (QString const& entry) {
      return entry == QStringLiteral("RX_MYCALL:msg=%1").arg(line);
    }));
  }

  void cq_reply_to_me_is_candidate_unrelated_cq_is_not()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("K1ABC"), QStringLiteral("United States"));
    host.countriesByCall.insert(QStringLiteral("K2ABC"), QStringLiteral("United States"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ K1ABC FN31"), 1400)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QVERIFY(host.armedCall.isEmpty());

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("OH1ZZZ K2ABC -10"), 1500)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QVERIFY2(host.armedCall == QStringLiteral("K2ABC"),
             qPrintable(QStringLiteral("armedCall=%1 armedTxFirst=%2 armedTxFreq=%3 logs=%4")
                            .arg(host.armedCall,
                                 host.armedTxFirst ? QStringLiteral("true") : QStringLiteral("false"),
                                 QString::number(host.armedTxFreq),
                                 host.logEntries.join(QStringLiteral(" | ")))));
  }

  void cq_tailender_candidate_is_accepted_via_host_rule()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.tailenderCandidateValue = true;
    host.countriesByCall.insert(QStringLiteral("K3ABC"), QStringLiteral("United States"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ K3ABC OF77"), 1600)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(host.armedCall, QStringLiteral("K3ABC"));
  }

  void sp_reply_to_me_is_candidate_without_cq()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("DL1ASI"), QStringLiteral("Germany"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("OH1ZZZ DL1ASI -19"), 1709)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(host.armedCall, QStringLiteral("DL1ASI"));
  }

  void sp_reply_to_me_gets_moderate_preference_over_cq()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("DL1ASI"), QStringLiteral("Germany"));
    host.countriesByCall.insert(QStringLiteral("HB9HVG"), QStringLiteral("Switzerland"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("OH1ZZZ DL1ASI -19"), 1709)});
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ HB9HVG JN36"), 2163)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(host.armedCall, QStringLiteral("DL1ASI"));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("SCORE:candidate call=DL1ASI"))
          && entry.contains(QStringLiteral("reply_to_me=125"));
    }));
  }

  void idle_tx_plan_uses_even_odd_specific_busy_bins()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.txFirstValue = false;
    host.busyBinsBySlot.insert(false, QVector<int> {2000, 2050, 2100});
    host.busyBinsBySlot.insert(true, QVector<int> {1000, 1050, 1100});

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    auto snapshot = bot.snapshot();
    QVERIFY(snapshot.plannedTxFreq >= 1000);
    QVERIFY(!host.busyBinsBySlot.value(false).contains(snapshot.plannedTxFreq));
  }

  void arming_rechecks_planned_tx_freq_before_transmit()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));
    host.busyBinsBySlot.insert(false, QVector<int> {2000});

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);
    for (int i = 0; i < 10; ++i) {
      bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    }
    QCOMPARE(bot.state(), FT8AutoBotState::Idle);

    auto firstPlan = bot.snapshot().plannedTxFreq;
    host.busyBinsBySlot.insert(false, QVector<int> {firstPlan});
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("OH1ZZZ VK6XYZ -10"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QVERIFY(host.armedTxFreq != firstPlan || firstPlan == 0);
  }

  void arming_fails_safely_when_all_tx_bins_are_busy()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    QVector<int> allBusy;
    for (int freq = 1000; freq <= 3000; freq += 50) {
      allBusy.append(freq);
    }
    host.busyBinsBySlot.insert(false, allBusy);

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
    QCOMPARE(host.startQsoCalls, 0);
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("ARM:failed=tx_freq_unavailable"));
    }));
  }

  void backfill_worked_from_adif_blocks_existing_calls()
  {
    QTemporaryDir dir;
    QFile adif {dir.filePath(QStringLiteral("wsjtx_log.adi"))};
    QVERIFY(adif.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream {&adif};
    stream << "<call:6>VK6XYZ <eor>\n";
    adif.close();

    FakeHost host;
    host.logBookPathValue = adif.fileName();
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QVERIFY(host.armedCall.isEmpty());
    QVERIFY(bot.memory().isWorked(QStringLiteral("VK6XYZ")));
  }

  void idle_wake_in_cq_mode_arms_high_scoring_reply()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("K2ABC"), QStringLiteral("United States"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);
    for (int i = 0; i < 10; ++i) {
      bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    }
    QCOMPARE(bot.state(), FT8AutoBotState::Idle);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("OH1ZZZ K2ABC -10"), 1500)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QVERIFY2(host.armedCall == QStringLiteral("K2ABC"),
             qPrintable(host.logEntries.join(QStringLiteral(" | "))));
  }

  void idle_wake_in_sp_mode_arms_high_scoring_cq()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QCOMPARE(bot.state(), FT8AutoBotState::Idle);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"), 1500)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(host.armedCall, QStringLiteral("VK6XYZ"));
  }

  void sp_skips_directed_cq_with_text_between_cq_and_callsign()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("OH1ABC"), QStringLiteral("Finland"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ USA OH1ABC KP20"), 1500)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QVERIFY(host.armedCall.isEmpty());
    QVERIFY(bot.state() != FT8AutoBotState::InQSO);
  }

  void stale_cq_reply_is_dropped_before_scoring()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("K2ABC"), QStringLiteral("United States"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("OH1ZZZ K2ABC -10"), 1500)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc().addSecs(61));

    QVERIFY(host.armedCall.isEmpty());
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("reason=stale"));
    }));
  }

  void raw_decode_without_callsign_is_logged_for_debugging()
  {
    QTemporaryDir dir;
    FakeHost host;

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("RR73"), 1500)});

    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("RX:no_callsign msg="));
    }));
  }

  void sp_enters_study_after_configured_cycles()
  {
    QTemporaryDir dir;
    FakeHost host;

    FT8AutoBot bot {&host, QDir {dir.path()}};
    auto settings = bot.settings();
    settings.studyAfterCycles = 2;
    bot.setSettings(settings);
    bot.setEnabled(true);

    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QCOMPARE(bot.state(), FT8AutoBotState::Idle);

    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QCOMPARE(bot.state(), FT8AutoBotState::Study);
    QCOMPARE(bot.snapshot().studyCyclesRemaining, 2);

    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QCOMPARE(bot.state(), FT8AutoBotState::Study);
    QCOMPARE(bot.snapshot().studyCyclesRemaining, 1);

    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QCOMPARE(bot.state(), FT8AutoBotState::Hunting);
  }

  void sp_enters_study_before_next_pick_when_threshold_matures_during_qso()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));
    host.countriesByCall.insert(QStringLiteral("K2ABC"), QStringLiteral("United States"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    auto settings = bot.settings();
    settings.studyAfterCycles = 3;
    bot.setSettings(settings);
    bot.setEnabled(true);

    auto const now = QDateTime::currentDateTimeUtc();
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(now);

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(host.armedCall, QStringLiteral("VK6XYZ"));
    QCOMPARE(host.startQsoCalls, 1);

    bot.onPeriodBoundary(now.addSecs(15));
    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);

    bot.onQsoProgress(4);
    bot.onQsoProgress(0);
    QCOMPARE(bot.state(), FT8AutoBotState::Hunting);

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ K2ABC FN31"))});
    bot.onPeriodBoundary(now.addSecs(30));

    QCOMPARE(bot.state(), FT8AutoBotState::Study);
    QCOMPARE(bot.snapshot().studyCyclesRemaining, 2);
    QCOMPARE(host.startQsoCalls, 1);
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("STUDY:enter"));
    }));
  }

  void study_exit_rechecks_tx_plan_for_target_reply_slot()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.txFirstValue = false;
    host.countriesByCall.insert(QStringLiteral("K2ABC"), QStringLiteral("United States"));
    host.busyBinsBySlot.insert(false, QVector<int> {});
    host.busyBinsBySlot.insert(true, QVector<int> {1700});

    FT8AutoBot bot {&host, QDir {dir.path()}};
    auto settings = bot.settings();
    settings.studyAfterCycles = 2;
    bot.setSettings(settings);
    bot.setEnabled(true);

    auto const now = QDateTime::currentDateTimeUtc();
    bot.onPeriodBoundary(now);
    QCOMPARE(bot.state(), FT8AutoBotState::Idle);

    bot.onPeriodBoundary(now.addSecs(15));
    QCOMPARE(bot.state(), FT8AutoBotState::Study);
    auto const studyPlan = bot.snapshot().plannedTxFreq;
    QCOMPARE(bot.snapshot().plannedTxFirst, false);

    settings.studyAfterCycles = 99;
    bot.setSettings(settings);

    if (bot.state() == FT8AutoBotState::Study) bot.onPeriodBoundary(now.addSecs(30));
    if (bot.state() == FT8AutoBotState::Study) bot.onPeriodBoundary(now.addSecs(45));
    if (bot.state() == FT8AutoBotState::Study) bot.onPeriodBoundary(now.addSecs(60));
    QVERIFY(bot.state() != FT8AutoBotState::Study);

    auto oddSlotDecode = decodeLine(QStringLiteral("CQ K2ABC FN31"), 1500);
    oddSlotDecode.replace(0, 6, QStringLiteral("230015"));
    bot.onDecode(DecodedText {oddSlotDecode});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(host.armedCall, QStringLiteral("K2ABC"));
    QVERIFY(host.armedTxFirst);
    QVERIFY(!host.busyBinsBySlot.value(true).contains(host.armedTxFreq));
    QVERIFY(host.armedTxFreq != studyPlan || studyPlan == 0);
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("TX_PLAN:slot_switch"));
    }));
  }

  void detailed_logs_include_tx_plan_and_arm()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("TX_PLAN:"));
    }));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("ARM:selected"));
    }));
  }

  void duplicate_decodes_same_cycle_are_deduped()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"), 1400)});
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"), 1400)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.counters().candidatesThisCycle, 0);
    QCOMPARE(host.startQsoCalls, 1);
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("reason=duplicate_cycle"));
    }));
  }

  void suspend_then_resume_on_next_supported_period()
  {
    QTemporaryDir dir;
    FakeHost host;

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    QCOMPARE(bot.state(), FT8AutoBotState::Hunting);

    bot.suspend(QStringLiteral("mode change"));
    QCOMPARE(bot.state(), FT8AutoBotState::Paused);
    QCOMPARE(bot.snapshot().pauseReason, QStringLiteral("mode change"));

    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    QCOMPARE(bot.state(), FT8AutoBotState::Hunting);
    QVERIFY(bot.snapshot().pauseReason.isEmpty());
  }

  void mode_change_failure_adds_cooldown_for_active_qso()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    bot.failForModeChange();

    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
    QVERIFY(bot.memory().isOnCooldown(QStringLiteral("VK6XYZ")));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("reason=mode_change"));
    }));
  }

  void active_target_working_other_station_triggers_qrm_cooldown()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.myCallValue = QStringLiteral("OH1ZZZ");
    host.countriesByCall.insert(QStringLiteral("TC0HZR"), QStringLiteral("Turkey"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ TC0HZR KM38"), 1730)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(host.armedCall, QStringLiteral("TC0HZR"));

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("PD1HBL TC0HZR -13"), 1730)});

    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
    QVERIFY(bot.memory().isOnCooldown(QStringLiteral("TC0HZR")));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("QRM:call=TC0HZR"));
    }));
  }

  void lost_active_state_is_readopted_before_qrm_detection()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.myCallValue = QStringLiteral("OH3CUF");

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setMode(FT8AutoBotMode::CQ);
    bot.setEnabled(true);

    host.dxCallValue = QStringLiteral("EA3BBA");
    host.qsoProgressValue = 2;

    bot.onDecode(DecodedText {decodeLine(QStringLiteral("DH1KJ EA3BBA R+01"), 626)});

    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
    QVERIFY(bot.memory().isOnCooldown(QStringLiteral("EA3BBA")));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("STATE:enter AdoptingQSO call=EA3BBA"));
    }));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("QRM:call=EA3BBA"));
    }));
  }

  void late_stage_progress_reset_releases_qso_without_stuck_timeout()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("G3AKA"), QStringLiteral("England"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ G3AKA IO91"), 1500)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);

    bot.onQsoProgress(1);
    bot.onQsoProgress(4);
    bot.onQsoProgress(0);

    QCOMPARE(bot.state(), FT8AutoBotState::Hunting);
    QVERIFY(bot.memory().isWorked(QStringLiteral("G3AKA")));
    QVERIFY(!bot.memory().isOnCooldown(QStringLiteral("G3AKA")));
    QCOMPARE(bot.counters().qsosCompleted, 1);
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("progress_reset"));
    }));
  }

  void late_stage_progress_reset_blocks_same_station_on_next_cycle()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("TY5AD"), QStringLiteral("Benin"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ TY5AD JJ16"), 263)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::InQSO);
    QCOMPARE(host.armedCall, QStringLiteral("TY5AD"));

    bot.onQsoProgress(1);
    bot.onQsoProgress(3);
    bot.onQsoProgress(5);
    bot.onQsoProgress(0);

    QCOMPARE(bot.state(), FT8AutoBotState::Hunting);
    QVERIFY(bot.memory().isWorked(QStringLiteral("TY5AD")));

    host.dxCallValue.clear();
    auto const armsBefore = host.startQsoCalls;
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ TY5AD JJ16"), 263)});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(host.startQsoCalls, armsBefore);
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("SCORE:skipped=TY5AD reason=worked_file"));
    }));
  }

  void external_logged_qso_updates_worked_memory()
  {
    QTemporaryDir dir;
    FakeHost host;

    FT8AutoBot bot {&host, QDir {dir.path()}};
    QVERIFY(!bot.memory().isWorked(QStringLiteral("K1ABC")));

    bot.syncLoggedQso(QStringLiteral("K1ABC"), QStringLiteral("20m"), QStringLiteral("FT8"), false);

    QVERIFY(bot.memory().isWorked(QStringLiteral("K1ABC")));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("QSO_OK:call=K1ABC"));
    }));
  }

  void bot_owned_qso_increments_new_dxcc_counter_when_candidate_was_new_dx()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    bot.onQsoLogged(QStringLiteral("VK6XYZ"), QStringLiteral("20m"), QStringLiteral("FT8"));

    QCOMPARE(bot.counters().newDxcc, 1);
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.contains(QStringLiteral("QSO_OK:call=VK6XYZ country=Australia new_dx=yes"));
    }));
  }

  void duplicate_logged_qso_after_progress_reset_does_not_double_count()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    bot.onQsoProgress(1);
    bot.onQsoProgress(4);
    bot.onQsoProgress(0);
    QCOMPARE(bot.counters().qsosCompleted, 1);

    bot.onQsoLogged(QStringLiteral("VK6XYZ"), QStringLiteral("20m"), QStringLiteral("FT8"));

    QCOMPARE(bot.counters().qsosCompleted, 1);
    QCOMPARE(bot.counters().newDxcc, 1);
  }

  void invalid_settings_are_sanitized_before_use()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    FT8AutoBotSettings settings;
    settings.minScore = -5;
    settings.idleListenSeconds = 0;
    settings.cqIdleAfter = 0;
    settings.cooldownMinutes = 0;
    settings.stuckCycleLimit = 0;
    settings.idleTxPlanMinHz = -100;
    settings.idleTxPlanMaxHz = 50;
    settings.idleTxPlanStepHz = 0;
    settings.adoptionGracePeriods = 0;
    bot.setSettings(settings);

    auto const normalized = bot.settings();
    QCOMPARE(normalized.minScore, 0);
    QCOMPARE(normalized.idleListenSeconds, 15);
    QCOMPARE(normalized.cqIdleAfter, 1);
    QCOMPARE(normalized.cooldownMinutes, 1);
    QCOMPARE(normalized.stuckCycleLimit, 1);
    QCOMPARE(normalized.idleTxPlanMinHz, 200);
    QCOMPARE(normalized.idleTxPlanMaxHz, 200);
    QCOMPARE(normalized.idleTxPlanStepHz, 10);
    QCOMPARE(normalized.adoptionGracePeriods, 1);

    host.busyBinsBySlot.insert(false, QVector<int> {200});
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());

    QCOMPARE(bot.state(), FT8AutoBotState::Idle);
  }

  void abandon_logs_abandon_and_cooldown_categories()
  {
    QTemporaryDir dir;
    FakeHost host;
    host.countriesByCall.insert(QStringLiteral("VK6XYZ"), QStringLiteral("Australia"));

    FT8AutoBot bot {&host, QDir {dir.path()}};
    bot.setEnabled(true);
    bot.onDecode(DecodedText {decodeLine(QStringLiteral("CQ VK6XYZ OF87"))});
    bot.onPeriodBoundary(QDateTime::currentDateTimeUtc());
    bot.onWatchdogTriggered();

    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("ABANDON:"));
    }));
    QVERIFY(std::any_of(host.logEntries.begin(), host.logEntries.end(), [] (QString const& entry) {
      return entry.startsWith(QStringLiteral("COOLDOWN:call=VK6XYZ"));
    }));
  }
};

QTEST_MAIN(TestFT8AutoBot)

#include "test_ft8_auto_bot.moc"

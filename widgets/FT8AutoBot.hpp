#ifndef FT8AUTOBOT_HPP
#define FT8AUTOBOT_HPP

#include <QDateTime>
#include <QDir>
#include <QSet>
#include <QVector>

#include "Decoder/decodedtext.h"
#include "widgets/FT8AutoBotMemory.hpp"

enum class FT8AutoBotMode {
  SearchAndPounce,
  CQ
};

enum class FT8AutoBotState {
  Disabled,
  Paused,
  Hunting,
  Study,
  CallingCQ,
  AdoptingQSO,
  Arming,
  InQSO,
  Abandoning,
  Idle
};

struct FT8AutoBotSettings
{
  int minScore {1000};
  int idleListenSeconds {120};
  int cqIdleAfter {10};
  int cooldownMinutes {30};
  int stuckCycleLimit {3};
  int studyAfterCycles {12};
  bool acceptRr73AsCq {false};
  bool reuseMainWindowFilters {true};
  bool wakeDuringIdleInCQMode {true};
  int idleTxPlanMinHz {1000};
  int idleTxPlanMaxHz {2400};
  int idleTxPlanStepHz {50};
  int adoptionGracePeriods {2};
  bool logToFile {true};
};

struct FT8AutoBotSnapshot
{
  bool enabled {false};
  FT8AutoBotMode mode {FT8AutoBotMode::SearchAndPounce};
  FT8AutoBotState state {FT8AutoBotState::Disabled};
  QString targetCall;
  QString targetGrid;
  QString targetCountry;
  int targetDistanceKm {0};
  int plannedTxFreq {0};
  bool plannedTxFirst {false};
  int cqWithoutCallerCount {0};
  int studyCyclesRemaining {0};
  QDateTime idleUntilUtc;
  int idleRemainingSeconds {0};
  QString pauseReason;
};

struct FT8AutoBotCounters
{
  int qsosCompleted {0};
  int newDxcc {0};
  int attempts {0};
  int abandonedStuck {0};
  int abandonedQrm {0};
  int candidatesThisCycle {0};
  int blockedWorked {0};
  int blockedCooldown {0};
  int idleEntries {0};
  int activeQsoCycles {0};
};

struct FT8AutoBotLastDecision
{
  QString action;
  QString reason;
  QString scoreBreakdown;
};

class FT8AutoBotHost
{
public:
  virtual ~FT8AutoBotHost() = default;

  virtual QString mode() const = 0;
  virtual QString band() const = 0;
  virtual QString myCall() const = 0;
  virtual QString myGrid() const = 0;
  virtual QString dxCall() const = 0;
  virtual QString logBookPath() const = 0;
  virtual int qsoProgress() const = 0;
  virtual bool transmitting() const = 0;
  virtual bool autoEnabled() const = 0;
  virtual bool txFirst() const = 0;
  virtual int trPeriodSeconds() const = 0;

  virtual QString countryForCall(QString const& call) const = 0;
  virtual bool countryWorked(QString const& country, QString const& mode, QString const& band) const = 0;
  virtual bool callWorkedGlobally(QString const& call) const = 0;
  virtual bool callsignFiltered(DecodedText const& decoded) const = 0;
  virtual bool tailenderCandidate(DecodedText const& decoded) const = 0;
  virtual QVector<int> busyTxBins(int hzMin, int hzMax, int stepHz, bool txFirstSlot) const = 0;
  virtual void botLog(QString const& category, QString const& detail) = 0;

  virtual void botSetDx(QString const& call, QString const& grid, int rxFreq, int txFreq,
                        int reportDb, bool txFirst) = 0;
  virtual void botStartQso() = 0;
  virtual void botEnableAutoTx(bool on) = 0;
  virtual void botClearDx() = 0;
  virtual void botStopTx() = 0;
  virtual void botStartCQ() = 0;
  virtual void botSetAutoSequence(bool on) = 0;
  virtual void botSetTxFreq(int txFreq) = 0;
};

class FT8AutoBot
{
public:
  explicit FT8AutoBot(FT8AutoBotHost * host, QDir const& dataDir = QDir {});

  void setSettings(FT8AutoBotSettings const& settings);
  FT8AutoBotSettings const& settings() const;

  void setEnabled(bool enabled);
  bool enabled() const;

  void setMode(FT8AutoBotMode mode);
  FT8AutoBotMode mode() const;
  FT8AutoBotState state() const;
  FT8AutoBotSnapshot snapshot() const;

  void onDecode(DecodedText const& decoded);
  void onPeriodBoundary(QDateTime const& nowUtc = QDateTime::currentDateTimeUtc());
  void onQsoProgress(int qsoProgress);
  void onQsoLogged(QString const& call, QString const& band, QString const& mode);
  void syncLoggedQso(QString const& call, QString const& band, QString const& mode,
                     bool botOwned = false);
  void onQrmDetected(QString const& call);
  void onWatchdogTriggered();
  void failForModeChange();
  void refreshWorkedFromLogBook();
  void suspend(QString const& reason);

  FT8AutoBotMemory & memory();
  FT8AutoBotMemory const& memory() const;
  FT8AutoBotCounters const& counters() const;
  FT8AutoBotLastDecision const& lastDecision() const;

private:
  struct Candidate
  {
    QString call;
    QString grid;
    QString country;
    QDateTime decodedAtUtc;
    int rxFreq {0};
    int reportDb {0};
    bool txFirst {false};
    bool isCqLike {false};
    bool isReplyToMe {false};
    bool isNewDx {false};
    int seenCycleIndex {0};
    int scoreNewCall {0};
    int scoreNewDx {0};
    int scoreDistance {0};
    int totalScore {0};
  };

  struct ActiveQso
  {
    QString call;
    QString country;
    bool wasNewDx {false};
    int lastProgress {0};
    int maxProgressSeen {0};
    int sameStateCycles {0};
    bool inAdoptionGrace {false};
    int adoptionPeriodsRemaining {0};
  };

  bool isCurrentModeSupported() const;
  bool isReplyToMe(DecodedText const& decoded) const;
  bool isCqLike(DecodedText const& decoded, QString const& candidateCall) const;
  bool activeQsoShowsOtherPartner(DecodedText const& decoded) const;
  bool decodeTxFirst(DecodedText const& decoded) const;
  int distanceScore(QString const& grid) const;
  Candidate buildCandidate(DecodedText const& decoded);
  void evaluateCandidates(QDateTime const& nowUtc);
  int candidateAgeSeconds(Candidate const& candidate, QDateTime const& nowUtc) const;
  int candidateAgeScore(Candidate const& candidate, QDateTime const& nowUtc) const;
  bool candidateIsFresh(Candidate const& candidate, QDateTime const& nowUtc) const;
  void enterStudy();
  void enterIdle(QDateTime const& nowUtc, QString const& reason = QString {});
  void resumeFromIdle();
  void planIdleTxFrequency();
  int pickBestTxFreq(bool txFirstSlot) const;
  bool ensurePlannedTxFreq(bool txFirstSlot);
  void armCandidate(Candidate const& candidate);
  void startActiveQso(QString const& call);
  void adoptExistingQso();
  void abandonActiveQso(QString const& reason, QDateTime const& nowUtc);
  void clearCycleCandidates();
  void backfillWorkedFromLogBook();
  void resumeFromPause();
  void log(QString const& category, QString const& detail) const;
  QString scoreDetail(Candidate const& candidate, QDateTime const& nowUtc) const;
  void setLastDecision(QString const& action, QString const& reason,
                       QString const& scoreBreakdown = QString {});
  static QString slotText(bool txFirst);
  static int qsoProgressCallingValue();

  FT8AutoBotHost * host_ {nullptr};
  FT8AutoBotSettings settings_;
  FT8AutoBotMemory memory_;
  FT8AutoBotMode mode_ {FT8AutoBotMode::SearchAndPounce};
  FT8AutoBotState state_ {FT8AutoBotState::Disabled};
  QVector<Candidate> cycleCandidates_;
  FT8AutoBotSnapshot snapshot_;
  FT8AutoBotCounters counters_;
  FT8AutoBotLastDecision lastDecision_;
  ActiveQso activeQso_;
  bool hasActiveQso_ {false};
  int cycleIndex_ {0};
  int spCyclesSinceStudy_ {0};
  QSet<QString> sessionDxccWorked_;
};

#endif

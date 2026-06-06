#include "widgets/FT8AutoBot.hpp"

#include <limits>

#include <QRegularExpression>
#include <QtMath>

#include "Radio.hpp"
#include "astro.h"
#include "logbook/AD1CCty.hpp"
#include "wsjtx_config.h"

namespace
{
extern "C"
{
  void azdist_(char* MyGrid, char* HisGrid, double* utch, int* nAz, int* nEl,
               int* nDmiles, int* nDkm, int* nHotAz, int* nHotABetter,
               fortran_charlen_t, fortran_charlen_t);
}

QString normalizedBase(QString const& value)
{
  return Radio::base_callsign(value.trimmed().toUpper());
}

bool containsDirectedCall(QString const& text, QString const& myCall)
{
  auto const base = normalizedBase(myCall);
  return text.contains(QStringLiteral(" %1 ").arg(myCall))
      || (!base.isEmpty() && text.contains(QStringLiteral(" %1 ").arg(base)))
      || text.contains(QStringLiteral(" <%1> ").arg(myCall));
}

bool looksLikeGrid(QString const& word)
{
  static QRegularExpression const gridPattern {
    QStringLiteral("^[A-R]{2}[0-9]{2}([A-X]{2})?$"),
    QRegularExpression::CaseInsensitiveOption
  };
  return gridPattern.match(word.trimmed()).hasMatch();
}

bool looksLikeCallWord(QString const& word)
{
  auto const upper = word.trimmed().toUpper();
  if (upper.isEmpty()) return false;
  if (upper == QStringLiteral("CQ")
      || upper == QStringLiteral("73")
      || upper == QStringLiteral("RR73")
      || upper.startsWith(QStringLiteral("R-"))) {
    return false;
  }
  if (looksLikeGrid(upper)) return false;
  return upper.contains(QRegularExpression {QStringLiteral("[0-9]")});
}

FT8AutoBotSettings normalizedSettings(FT8AutoBotSettings settings)
{
  settings.minScore = qBound(0, settings.minScore, 5000);
  settings.idleListenSeconds = qBound(15, settings.idleListenSeconds, 600);
  settings.cqIdleAfter = qBound(1, settings.cqIdleAfter, 50);
  settings.cooldownMinutes = qBound(1, settings.cooldownMinutes, 24 * 60);
  settings.stuckCycleLimit = qBound(1, settings.stuckCycleLimit, 10);
  settings.idleTxPlanMinHz = qBound(200, settings.idleTxPlanMinHz, 4000);
  settings.idleTxPlanMaxHz = qBound(settings.idleTxPlanMinHz, settings.idleTxPlanMaxHz, 4000);
  settings.idleTxPlanStepHz = qBound(10, settings.idleTxPlanStepHz, 200);
  settings.adoptionGracePeriods = qBound(1, settings.adoptionGracePeriods, 10);
  return settings;
}
}

FT8AutoBot::FT8AutoBot(FT8AutoBotHost * host, QDir const& dataDir)
  : host_ {host}
  , memory_ {dataDir}
{
  memory_.load();
  backfillWorkedFromLogBook();
  snapshot_.mode = mode_;
  snapshot_.state = state_;
}

void FT8AutoBot::setSettings(FT8AutoBotSettings const& settings)
{
  settings_ = normalizedSettings(settings);
}

FT8AutoBotSettings const& FT8AutoBot::settings() const
{
  return settings_;
}

void FT8AutoBot::setEnabled(bool enabled)
{
  if (enabled == snapshot_.enabled) return;

  snapshot_.enabled = enabled;
  if (!enabled) {
    if (state_ == FT8AutoBotState::InQSO || state_ == FT8AutoBotState::AdoptingQSO) {
      log(QStringLiteral("STATE"), QStringLiteral("Operator handoff; leaving active QSO untouched"));
    }
    hasActiveQso_ = false;
    activeQso_ = ActiveQso {};
    state_ = FT8AutoBotState::Disabled;
    clearCycleCandidates();
    snapshot_.state = state_;
    snapshot_.pauseReason.clear();
    log(QStringLiteral("CONFIG"), QStringLiteral("Bot disabled"));
    return;
  }

  backfillWorkedFromLogBook();
  host_->botSetAutoSequence(true);
  if (!host_->dxCall().trimmed().isEmpty() && host_->qsoProgress() != qsoProgressCallingValue()) {
    adoptExistingQso();
  } else {
    state_ = mode_ == FT8AutoBotMode::SearchAndPounce
      ? FT8AutoBotState::Hunting
      : FT8AutoBotState::CallingCQ;
  }
  snapshot_.state = state_;
  snapshot_.pauseReason.clear();
  log(QStringLiteral("CONFIG"), QStringLiteral("Bot enabled mode=%1")
      .arg(mode_ == FT8AutoBotMode::CQ ? QStringLiteral("CQ") : QStringLiteral("SP")));
}

bool FT8AutoBot::enabled() const
{
  return snapshot_.enabled;
}

void FT8AutoBot::setMode(FT8AutoBotMode mode)
{
  mode_ = mode;
  snapshot_.mode = mode_;
  if (!enabled()) return;
  if (state_ == FT8AutoBotState::Disabled) return;
  if (state_ == FT8AutoBotState::Paused) return;
  if (state_ == FT8AutoBotState::Idle) return;
  if (hasActiveQso_) return;
  state_ = mode_ == FT8AutoBotMode::SearchAndPounce
    ? FT8AutoBotState::Hunting
    : FT8AutoBotState::CallingCQ;
  snapshot_.state = state_;
}

FT8AutoBotMode FT8AutoBot::mode() const
{
  return mode_;
}

FT8AutoBotState FT8AutoBot::state() const
{
  return state_;
}

FT8AutoBotSnapshot FT8AutoBot::snapshot() const
{
  return snapshot_;
}

void FT8AutoBot::onDecode(DecodedText const& decoded)
{
  if (!enabled() || !isCurrentModeSupported()) return;
  if (state_ == FT8AutoBotState::Paused) return;
  if (state_ == FT8AutoBotState::Disabled || state_ == FT8AutoBotState::InQSO) return;
  if (state_ == FT8AutoBotState::Abandoning) return;

  if (settings_.reuseMainWindowFilters && host_->callsignFiltered(decoded)) {
    log(QStringLiteral("FILTER"), QStringLiteral("skipped=host_filter msg=%1")
        .arg(decoded.string().trimmed()));
    return;
  }

  auto const candidate = buildCandidate(decoded);
  if (candidate.call.isEmpty()) return;

  if (mode_ == FT8AutoBotMode::SearchAndPounce) {
    if (!candidate.isCqLike) {
      log(QStringLiteral("FILTER"), QStringLiteral("skipped=%1 reason=not_cq_like").arg(candidate.call));
      return;
    }
  } else {
    if (!candidate.isReplyToMe && !host_->tailenderCandidate(decoded)) {
      log(QStringLiteral("FILTER"), QStringLiteral("skipped=%1 reason=not_reply_to_me_or_tailender").arg(candidate.call));
      return;
    }
  }

  for (auto const& existing : cycleCandidates_) {
    if (normalizedBase(existing.call) == normalizedBase(candidate.call)
        && existing.rxFreq == candidate.rxFreq) {
      log(QStringLiteral("FILTER"), QStringLiteral("skipped=%1 reason=duplicate_cycle rx=%2")
          .arg(candidate.call, QString::number(candidate.rxFreq)));
      return;
    }
  }

  cycleCandidates_.append(candidate);
  counters_.candidatesThisCycle = cycleCandidates_.size();
  log(QStringLiteral("SCORE"), QStringLiteral("candidate %1").arg(scoreDetail(candidate)));

  if (state_ == FT8AutoBotState::AdoptingQSO && hasActiveQso_ && candidate.call == activeQso_.call) {
    activeQso_.inAdoptionGrace = false;
    if (activeQso_.adoptionPeriodsRemaining <= 0) {
      state_ = FT8AutoBotState::InQSO;
      snapshot_.state = state_;
    }
  }
}

void FT8AutoBot::onPeriodBoundary(QDateTime const& nowUtc)
{
  if (!enabled() || !isCurrentModeSupported()) return;

  if (state_ == FT8AutoBotState::Paused) {
    resumeFromPause();
    clearCycleCandidates();
    return;
  }

  if (snapshot_.idleUntilUtc.isValid()) {
    snapshot_.idleRemainingSeconds = qMax(0, static_cast<int>(nowUtc.secsTo(snapshot_.idleUntilUtc)));
  } else {
    snapshot_.idleRemainingSeconds = 0;
  }

  ++cycleIndex_;
  auto const purged = memory_.purgeExpiredCooldowns(nowUtc);
  if (purged > 0) {
    log(QStringLiteral("COOLDOWN"), QStringLiteral("purged=%1").arg(QString::number(purged)));
  }

  if (state_ == FT8AutoBotState::AdoptingQSO && hasActiveQso_) {
    if (activeQso_.adoptionPeriodsRemaining > 0) {
      --activeQso_.adoptionPeriodsRemaining;
    }
    if (activeQso_.adoptionPeriodsRemaining <= 0) {
      activeQso_.inAdoptionGrace = false;
      state_ = FT8AutoBotState::InQSO;
      snapshot_.state = state_;
    }
  }

  if (state_ == FT8AutoBotState::InQSO && hasActiveQso_ && !activeQso_.inAdoptionGrace) {
    ++activeQso_.sameStateCycles;
    counters_.activeQsoCycles = activeQso_.sameStateCycles;
    log(QStringLiteral("STATE"), QStringLiteral("qso call=%1 progress=%2 same_cycles=%3")
        .arg(activeQso_.call,
             QString::number(activeQso_.lastProgress),
             QString::number(activeQso_.sameStateCycles)));
    if (activeQso_.sameStateCycles > settings_.stuckCycleLimit) {
      abandonActiveQso(QStringLiteral("stuck"), nowUtc);
    }
  }

  if (state_ == FT8AutoBotState::Idle) {
    planIdleTxFrequency();
    if (settings_.wakeDuringIdleInCQMode
        && !host_->transmitting()
        && !cycleCandidates_.isEmpty()) {
      log(QStringLiteral("IDLE"), QStringLiteral("wake_check candidates=%1")
          .arg(QString::number(cycleCandidates_.size())));
      evaluateCandidates(nowUtc);
      if (state_ != FT8AutoBotState::Idle) {
        clearCycleCandidates();
        return;
      }
    }
    if (snapshot_.idleUntilUtc.isValid() && nowUtc >= snapshot_.idleUntilUtc) {
      resumeFromIdle();
    }
    clearCycleCandidates();
    return;
  }

  if (state_ == FT8AutoBotState::Hunting
      || state_ == FT8AutoBotState::CallingCQ) {
    if (!host_->transmitting() && host_->qsoProgress() == qsoProgressCallingValue()) {
      if (mode_ == FT8AutoBotMode::CQ && cycleCandidates_.isEmpty()) {
        ++snapshot_.cqWithoutCallerCount;
        host_->botStartCQ();
        log(QStringLiteral("CQ"), QStringLiteral("count=%1 no acceptable caller")
            .arg(QString::number(snapshot_.cqWithoutCallerCount)));
        if (snapshot_.cqWithoutCallerCount >= settings_.cqIdleAfter) {
          enterIdle(nowUtc, QStringLiteral("cq_exhausted"));
        }
      } else {
        evaluateCandidates(nowUtc);
      }
    }
  }

  clearCycleCandidates();
}

void FT8AutoBot::onQsoProgress(int qsoProgress)
{
  if (!hasActiveQso_) return;
  if (qsoProgress == activeQso_.lastProgress) return;
  log(QStringLiteral("STATE"), QStringLiteral("progress call=%1 from=%2 to=%3")
      .arg(activeQso_.call,
           QString::number(activeQso_.lastProgress),
           QString::number(qsoProgress)));
  activeQso_.lastProgress = qsoProgress;
  activeQso_.sameStateCycles = 0;
  if (state_ == FT8AutoBotState::AdoptingQSO) {
    activeQso_.inAdoptionGrace = false;
    state_ = FT8AutoBotState::InQSO;
    snapshot_.state = state_;
  }
}

void FT8AutoBot::onQsoLogged(QString const& call, QString const&, QString const&)
{
  syncLoggedQso(call, QString {}, QString {}, true);
  host_->botEnableAutoTx(false);
  host_->botClearDx();
  hasActiveQso_ = false;
  activeQso_ = ActiveQso {};
  snapshot_.cqWithoutCallerCount = 0;
  snapshot_.targetCall.clear();
  snapshot_.targetGrid.clear();
  snapshot_.targetCountry.clear();
  snapshot_.targetDistanceKm = 0;
  counters_.activeQsoCycles = 0;
  state_ = mode_ == FT8AutoBotMode::SearchAndPounce
    ? FT8AutoBotState::Hunting
    : FT8AutoBotState::CallingCQ;
  snapshot_.state = state_;
}

void FT8AutoBot::syncLoggedQso(QString const& call, QString const&, QString const&, bool botOwned)
{
  auto const country = host_->countryForCall(call);
  memory_.addWorked(call);
  memory_.removeCooldown(call);

  if (botOwned) {
    ++counters_.qsosCompleted;
    if (!country.isEmpty() && !sessionDxccWorked_.contains(country)) {
      sessionDxccWorked_.insert(country);
      ++counters_.newDxcc;
    }
    setLastDecision(QStringLiteral("QSO complete"),
                    QStringLiteral("Logged %1").arg(normalizedBase(call)),
                    QStringLiteral("worked_count=%1").arg(QString::number(memory_.workedCount())));
  }

  log(QStringLiteral("QSO_OK"), QStringLiteral("call=%1 worked_count=%2 source=%3")
      .arg(normalizedBase(call),
           QString::number(memory_.workedCount()),
           botOwned ? QStringLiteral("bot") : QStringLiteral("external")));
}

void FT8AutoBot::onQrmDetected(QString const& call)
{
  if (!enabled()) return;
  Q_UNUSED(call);
  abandonActiveQso(QStringLiteral("qrm"), QDateTime::currentDateTimeUtc());
}

void FT8AutoBot::onWatchdogTriggered()
{
  if (!enabled()) return;
  if (!hasActiveQso_) {
    log(QStringLiteral("QSO_FAIL"), QStringLiteral("reason=watchdog no_active_qso"));
    setLastDecision(QStringLiteral("Watchdog triggered"),
                    QStringLiteral("Transmit watchdog fired without an active bot QSO"));
    enterIdle(QDateTime::currentDateTimeUtc(), QStringLiteral("watchdog"));
    return;
  }
  abandonActiveQso(QStringLiteral("watchdog"), QDateTime::currentDateTimeUtc());
}

void FT8AutoBot::failForModeChange()
{
  if (!enabled()) return;
  if (!hasActiveQso_) {
    log(QStringLiteral("QSO_FAIL"), QStringLiteral("reason=mode_change no_active_qso"));
    setLastDecision(QStringLiteral("Mode change"),
                    QStringLiteral("Unsupported mode change with no active bot QSO"));
    return;
  }
  abandonActiveQso(QStringLiteral("mode_change"), QDateTime::currentDateTimeUtc());
}

void FT8AutoBot::refreshWorkedFromLogBook()
{
  backfillWorkedFromLogBook();
}

void FT8AutoBot::suspend(QString const& reason)
{
  if (!enabled()) return;
  if (state_ == FT8AutoBotState::Disabled || state_ == FT8AutoBotState::Paused) return;

  state_ = FT8AutoBotState::Paused;
  snapshot_.state = state_;
  snapshot_.pauseReason = reason;
  snapshot_.idleRemainingSeconds = 0;
  host_->botEnableAutoTx(false);
  clearCycleCandidates();
  log(QStringLiteral("STATE"), QStringLiteral("paused reason=%1").arg(reason));
  setLastDecision(QStringLiteral("Paused"), reason);
}

FT8AutoBotMemory const& FT8AutoBot::memory() const
{
  return memory_;
}

FT8AutoBotMemory & FT8AutoBot::memory()
{
  return memory_;
}

FT8AutoBotCounters const& FT8AutoBot::counters() const
{
  return counters_;
}

FT8AutoBotLastDecision const& FT8AutoBot::lastDecision() const
{
  return lastDecision_;
}

bool FT8AutoBot::isCurrentModeSupported() const
{
  auto const currentMode = host_->mode().trimmed().toUpper();
  return currentMode == QStringLiteral("FT8") || currentMode == QStringLiteral("FT4");
}

bool FT8AutoBot::isReplyToMe(DecodedText const& decoded) const
{
  return containsDirectedCall(decoded.string(), host_->myCall());
}

bool FT8AutoBot::isCqLike(DecodedText const& decoded) const
{
  auto const message = decoded.string().trimmed().toUpper();
  if (settings_.acceptRr73AsCq
      && (message.contains(QStringLiteral(" RR73"))
          || message.endsWith(QStringLiteral("RR73"))
          || message.contains(QStringLiteral(" 73"))
          || message.endsWith(QStringLiteral("73")))) {
    return true;
  }

  auto const words = decoded.messageWords();
  if (words.size() < 3) return false;

  QString call;
  QString grid;
  decoded.deCallAndGrid(call, grid);
  return message.contains(QStringLiteral("CQ "));
}

bool FT8AutoBot::decodeTxFirst(DecodedText const& decoded) const
{
  auto const trPeriod = host_->trPeriodSeconds();
  if (trPeriod <= 0) return host_->txFirst();
  auto const nmod = static_cast<int>(fmod(static_cast<double>(decoded.timeInSeconds()), 2.0 * trPeriod));
  return nmod != 0;
}

int FT8AutoBot::distanceScore(QString const& grid) const
{
  if (grid.trimmed().size() < 4 || host_->myGrid().trimmed().size() < 4) return 0;

  qint64 nsec = (QDateTime::currentMSecsSinceEpoch() / 1000) % 86400;
  double utch = nsec / 3600.0;
  int nAz = 0;
  int nEl = 0;
  int nDmiles = 0;
  int nDkm = 0;
  int nHotAz = 0;
  int nHotABetter = 0;

  auto const myGrid = (host_->myGrid() + QStringLiteral("      ")).left(6).toLatin1();
  auto const dxGrid = (grid + QStringLiteral("      ")).left(6).toLatin1();
  azdist_(const_cast<char *>(myGrid.constData()),
          const_cast<char *>(dxGrid.constData()),
          &utch, &nAz, &nEl, &nDmiles, &nDkm, &nHotAz, &nHotABetter, 6, 6);
  return qMin(nDkm, 20000) / 50;
}

FT8AutoBot::Candidate FT8AutoBot::buildCandidate(DecodedText const& decoded)
{
  Candidate candidate;
  decoded.deCallAndGrid(candidate.call, candidate.grid);
  candidate.call = candidate.call.trimmed().toUpper();
  candidate.grid = candidate.grid.trimmed().toUpper();
  if (candidate.call.isEmpty()) {
    auto const words = decoded.messageWords();
    for (int i = 0; i < words.size(); ++i) {
      auto const word = words.at(i).trimmed().toUpper();
      if (!looksLikeCallWord(word)) continue;
      candidate.call = word;
      if (candidate.grid.isEmpty() && i + 1 < words.size()) {
        auto const maybeGrid = words.at(i + 1).trimmed().toUpper();
        if (looksLikeGrid(maybeGrid)) candidate.grid = maybeGrid;
      }
      break;
    }
  }
  if (candidate.call.isEmpty()) return candidate;

  if (normalizedBase(candidate.call) == normalizedBase(host_->dxCall())) return Candidate {};
  if (memory_.isWorked(candidate.call) || host_->callWorkedGlobally(candidate.call)) {
    ++counters_.blockedWorked;
    if (!memory_.isWorked(candidate.call)) {
      memory_.addWorked(candidate.call);
      log(QStringLiteral("WORKED"), QStringLiteral("backfilled=%1 source=logbook").arg(normalizedBase(candidate.call)));
    }
    log(QStringLiteral("SCORE"), QStringLiteral("skipped=%1 reason=worked_file").arg(candidate.call));
    return Candidate {};
  }
  if (memory_.isOnCooldown(candidate.call)) {
    ++counters_.blockedCooldown;
    auto const expires = memory_.cooldownExpiryForCall(candidate.call);
    log(QStringLiteral("SCORE"), QStringLiteral("skipped=%1 reason=cooldown expires=%2")
        .arg(candidate.call, expires.toUTC().toString(Qt::ISODate)));
    return Candidate {};
  }

  candidate.country = host_->countryForCall(candidate.call);
  candidate.rxFreq = decoded.frequencyOffset();
  candidate.reportDb = decoded.report().toInt();
  candidate.txFirst = decodeTxFirst(decoded);
  candidate.isCqLike = isCqLike(decoded);
  candidate.isReplyToMe = isReplyToMe(decoded);

  candidate.scoreNewCall = 1000;
  if (!candidate.country.isEmpty()
      && !host_->countryWorked(candidate.country, host_->mode(), host_->band())) {
    candidate.scoreNewDx = 500;
  }
  candidate.scoreDistance = distanceScore(candidate.grid);
  candidate.totalScore = candidate.scoreNewCall + candidate.scoreNewDx + candidate.scoreDistance;
  return candidate;
}

void FT8AutoBot::evaluateCandidates(QDateTime const& nowUtc)
{
  Candidate best;
  bool haveBest = false;

  for (auto const& candidate : cycleCandidates_) {
    if (!haveBest
        || candidate.totalScore > best.totalScore
        || (candidate.totalScore == best.totalScore && candidate.reportDb > best.reportDb)
        || (candidate.totalScore == best.totalScore && candidate.reportDb == best.reportDb
            && candidate.rxFreq < best.rxFreq)) {
      best = candidate;
      haveBest = true;
    }
  }

  if (!haveBest || best.totalScore < settings_.minScore) {
    if (haveBest) {
      log(QStringLiteral("IDLE"), QStringLiteral("reason=below_threshold best_total=%1 min_score=%2 duration_sec=%3 %4")
          .arg(QString::number(best.totalScore),
               QString::number(settings_.minScore),
               QString::number(settings_.idleListenSeconds),
               scoreDetail(best)));
      setLastDecision(QStringLiteral("Entered Idle"),
                      QStringLiteral("Best score below threshold"),
                      scoreDetail(best));
    } else {
      log(QStringLiteral("IDLE"), QStringLiteral("reason=no_candidates duration_sec=%1")
          .arg(QString::number(settings_.idleListenSeconds)));
      setLastDecision(QStringLiteral("Entered Idle"),
                      QStringLiteral("No eligible candidates this cycle"));
    }
    enterIdle(nowUtc, haveBest ? QStringLiteral("below_threshold") : QStringLiteral("no_candidates"));
    return;
  }

  snapshot_.cqWithoutCallerCount = 0;
  log(QStringLiteral("ARM"), QStringLiteral("selected %1").arg(scoreDetail(best)));
  setLastDecision(QStringLiteral("Selected target"),
                  QStringLiteral("Best eligible candidate"),
                  scoreDetail(best));
  armCandidate(best);
}

void FT8AutoBot::enterIdle(QDateTime const& nowUtc, QString const& reason)
{
  state_ = FT8AutoBotState::Idle;
  snapshot_.state = state_;
  snapshot_.idleUntilUtc = nowUtc.addSecs(settings_.idleListenSeconds);
  snapshot_.idleRemainingSeconds = settings_.idleListenSeconds;
  host_->botEnableAutoTx(false);
  planIdleTxFrequency();
  ++counters_.idleEntries;
  QString extra;
  if (reason == QStringLiteral("cq_exhausted")) {
    extra = QStringLiteral(" cq_count=%1").arg(QString::number(snapshot_.cqWithoutCallerCount));
  }
  log(QStringLiteral("IDLE"), QStringLiteral("enter until=%1 reason=%2 duration_sec=%3%4")
      .arg(snapshot_.idleUntilUtc.toUTC().toString(Qt::ISODate),
           reason.isEmpty() ? QStringLiteral("unspecified") : reason,
           QString::number(settings_.idleListenSeconds),
           extra));
}

void FT8AutoBot::resumeFromIdle()
{
  snapshot_.idleUntilUtc = QDateTime {};
  snapshot_.idleRemainingSeconds = 0;
  snapshot_.cqWithoutCallerCount = 0;
  state_ = mode_ == FT8AutoBotMode::SearchAndPounce
    ? FT8AutoBotState::Hunting
    : FT8AutoBotState::CallingCQ;
  snapshot_.state = state_;
  log(QStringLiteral("IDLE"), QStringLiteral("exit state=%1")
      .arg(state_ == FT8AutoBotState::CallingCQ ? QStringLiteral("CallingCQ") : QStringLiteral("Hunting")));
}

void FT8AutoBot::planIdleTxFrequency()
{
  auto const txFirstSlot = host_->txFirst();
  snapshot_.plannedTxFirst = txFirstSlot;
  snapshot_.plannedTxFreq = pickBestTxFreq(txFirstSlot);
  if (snapshot_.plannedTxFreq > 0) {
    log(QStringLiteral("TX_PLAN"), QStringLiteral("freq=%1 source=idle_waterfall slot=%2")
        .arg(QString::number(snapshot_.plannedTxFreq), slotText(txFirstSlot)));
  }
}

int FT8AutoBot::pickBestTxFreq(bool txFirstSlot) const
{
  auto busy = host_->busyTxBins(settings_.idleTxPlanMinHz,
                                settings_.idleTxPlanMaxHz,
                                settings_.idleTxPlanStepHz,
                                txFirstSlot);
  QSet<int> busySet;
  for (auto const bin : busy) busySet.insert(bin);

  int bestFreq = 0;
  int bestScore = std::numeric_limits<int>::min();
  int const center = (settings_.idleTxPlanMinHz + settings_.idleTxPlanMaxHz) / 2;

  for (int freq = settings_.idleTxPlanMinHz; freq <= settings_.idleTxPlanMaxHz; freq += settings_.idleTxPlanStepHz) {
    if (busySet.contains(freq)) continue;

    int nearestBusyDistance = settings_.idleTxPlanMaxHz - settings_.idleTxPlanMinHz;
    for (auto const busyFreq : busy) {
      nearestBusyDistance = qMin(nearestBusyDistance, qAbs(freq - busyFreq));
    }
    int score = nearestBusyDistance * 10 - qAbs(freq - center);
    if (bestFreq == 0 || score > bestScore) {
      bestFreq = freq;
      bestScore = score;
    }
  }

  return bestFreq;
}

bool FT8AutoBot::ensurePlannedTxFreq()
{
  auto const txFirstSlot = host_->txFirst();
  auto const busy = host_->busyTxBins(settings_.idleTxPlanMinHz,
                                      settings_.idleTxPlanMaxHz,
                                      settings_.idleTxPlanStepHz,
                                      txFirstSlot);
  log(QStringLiteral("ARM"), QStringLiteral("tx_check planned=%1 slot=%2 busy_count=%3 range=%4-%5 step=%6")
      .arg(QString::number(snapshot_.plannedTxFreq),
           slotText(txFirstSlot),
           QString::number(busy.size()),
           QString::number(settings_.idleTxPlanMinHz),
           QString::number(settings_.idleTxPlanMaxHz),
           QString::number(settings_.idleTxPlanStepHz)));
  if (!busy.contains(snapshot_.plannedTxFreq) && snapshot_.plannedTxFreq != 0) {
    log(QStringLiteral("ARM"), QStringLiteral("tx_clear freq=%1 slot=%2")
        .arg(QString::number(snapshot_.plannedTxFreq), slotText(txFirstSlot)));
    return true;
  }

  auto const previous = snapshot_.plannedTxFreq;
  snapshot_.plannedTxFreq = pickBestTxFreq(txFirstSlot);
  snapshot_.plannedTxFirst = txFirstSlot;
  log(QStringLiteral("TX_PLAN"), QStringLiteral("recheck previous=%1 new=%2 slot=%3")
      .arg(QString::number(previous),
           QString::number(snapshot_.plannedTxFreq),
           slotText(txFirstSlot)));
  if (snapshot_.plannedTxFreq == 0) {
    log(QStringLiteral("ARM"), QStringLiteral("tx_blocked previous=%1 slot=%2")
        .arg(QString::number(previous), slotText(txFirstSlot)));
  } else {
    log(QStringLiteral("ARM"), QStringLiteral("tx_repick previous=%1 new=%2 slot=%3")
        .arg(QString::number(previous),
             QString::number(snapshot_.plannedTxFreq),
             slotText(txFirstSlot)));
  }
  return snapshot_.plannedTxFreq != 0;
}

void FT8AutoBot::armCandidate(Candidate const& candidate)
{
  state_ = FT8AutoBotState::Arming;
  snapshot_.state = state_;
  ++counters_.attempts;

  if (!ensurePlannedTxFreq()) {
    log(QStringLiteral("ARM"), QStringLiteral("failed=tx_freq_unavailable call=%1").arg(candidate.call));
    setLastDecision(QStringLiteral("Arm failed"),
                    QStringLiteral("No clear TX frequency available"),
                    scoreDetail(candidate));
    enterIdle(QDateTime::currentDateTimeUtc(), QStringLiteral("arm_failed"));
    return;
  }

  host_->botSetTxFreq(snapshot_.plannedTxFreq);
  host_->botSetDx(candidate.call, candidate.grid, candidate.rxFreq, snapshot_.plannedTxFreq,
                  candidate.reportDb, candidate.txFirst);
  log(QStringLiteral("ARM"), QStringLiteral("commit call=%1 rx=%2 tx=%3 reply_slot=%4 plan_slot=%5 report=%6")
      .arg(candidate.call,
           QString::number(candidate.rxFreq),
           QString::number(snapshot_.plannedTxFreq),
           slotText(candidate.txFirst),
           slotText(snapshot_.plannedTxFirst),
           QString::number(candidate.reportDb)));
  snapshot_.targetCall = candidate.call;
  snapshot_.targetGrid = candidate.grid;
  snapshot_.targetCountry = candidate.country;
  snapshot_.targetDistanceKm = candidate.scoreDistance * 50;
  host_->botStartQso();
  host_->botEnableAutoTx(true);
  startActiveQso(candidate.call);
}

void FT8AutoBot::startActiveQso(QString const& call)
{
  hasActiveQso_ = true;
  activeQso_ = ActiveQso {};
  activeQso_.call = normalizedBase(call);
  activeQso_.lastProgress = host_->qsoProgress();
  state_ = FT8AutoBotState::InQSO;
  snapshot_.state = state_;
  counters_.activeQsoCycles = 0;
  log(QStringLiteral("STATE"), QStringLiteral("InQSO Hunting->InQSO call=%1 band=%2 mode=%3")
      .arg(activeQso_.call, host_->band(), host_->mode()));
}

void FT8AutoBot::adoptExistingQso()
{
  hasActiveQso_ = true;
  activeQso_ = ActiveQso {};
  activeQso_.call = normalizedBase(host_->dxCall());
  activeQso_.lastProgress = host_->qsoProgress();
  activeQso_.inAdoptionGrace = true;
  activeQso_.adoptionPeriodsRemaining = qMax(1, settings_.adoptionGracePeriods);
  snapshot_.targetCall = host_->dxCall().trimmed();
  snapshot_.targetGrid.clear();
  snapshot_.targetCountry = host_->countryForCall(snapshot_.targetCall);
  snapshot_.targetDistanceKm = 0;
  state_ = FT8AutoBotState::AdoptingQSO;
  snapshot_.state = state_;
  log(QStringLiteral("STATE"), QStringLiteral("enter AdoptingQSO call=%1 grace_periods=%2")
      .arg(snapshot_.targetCall, QString::number(activeQso_.adoptionPeriodsRemaining)));
}

void FT8AutoBot::abandonActiveQso(QString const& reason, QDateTime const& nowUtc)
{
  if (!hasActiveQso_) return;

  state_ = FT8AutoBotState::Abandoning;
  snapshot_.state = state_;
  memory_.addCooldown(activeQso_.call, reason, nowUtc, settings_.cooldownMinutes);
  if (reason == QStringLiteral("stuck")) {
    ++counters_.abandonedStuck;
  } else if (reason == QStringLiteral("qrm")) {
    ++counters_.abandonedQrm;
  }
  log(QStringLiteral("ABANDON"), QStringLiteral("call=%1 cycles=%2 reason=%3")
      .arg(activeQso_.call, QString::number(activeQso_.sameStateCycles), reason));
  log(QStringLiteral("COOLDOWN"), QStringLiteral("call=%1 expires=%2 reason=%3")
      .arg(activeQso_.call,
           nowUtc.addSecs(settings_.cooldownMinutes * 60).toUTC().toString(Qt::ISODate),
           reason));
  log(QStringLiteral("QSO_FAIL"), QStringLiteral("call=%1 reason=%2 cooldown_until=%3")
      .arg(activeQso_.call,
           reason,
           nowUtc.addSecs(settings_.cooldownMinutes * 60).toUTC().toString(Qt::ISODate)));
  setLastDecision(QStringLiteral("Abandoned QSO"),
                  QStringLiteral("reason=%1").arg(reason),
                  QStringLiteral("cycles=%1").arg(QString::number(activeQso_.sameStateCycles)));
  if (host_->transmitting()) {
    host_->botStopTx();
  }
  host_->botEnableAutoTx(false);
  host_->botClearDx();
  hasActiveQso_ = false;
  activeQso_ = ActiveQso {};
  snapshot_.targetCall.clear();
  snapshot_.targetGrid.clear();
  snapshot_.targetCountry.clear();
  snapshot_.targetDistanceKm = 0;
  counters_.activeQsoCycles = 0;
  enterIdle(nowUtc, QStringLiteral("abandon_%1").arg(reason));
}

void FT8AutoBot::clearCycleCandidates()
{
  cycleCandidates_.clear();
  counters_.candidatesThisCycle = 0;
}

void FT8AutoBot::backfillWorkedFromLogBook()
{
  auto const added = memory_.backfillWorkedFromAdif(host_->logBookPath());
  if (added > 0) {
    log(QStringLiteral("WORKED"), QStringLiteral("backfill_added=%1 source=adif")
        .arg(QString::number(added)));
  }
}

void FT8AutoBot::resumeFromPause()
{
  if (state_ != FT8AutoBotState::Paused) return;

  snapshot_.pauseReason.clear();
  snapshot_.idleUntilUtc = QDateTime {};
  snapshot_.idleRemainingSeconds = 0;
  if (!host_->dxCall().trimmed().isEmpty() && host_->qsoProgress() != qsoProgressCallingValue()) {
    adoptExistingQso();
  } else {
    hasActiveQso_ = false;
    activeQso_ = ActiveQso {};
    state_ = mode_ == FT8AutoBotMode::SearchAndPounce
      ? FT8AutoBotState::Hunting
      : FT8AutoBotState::CallingCQ;
    snapshot_.state = state_;
    log(QStringLiteral("STATE"), QStringLiteral("resumed state=%1")
        .arg(state_ == FT8AutoBotState::CallingCQ ? QStringLiteral("CallingCQ")
                                                  : QStringLiteral("Hunting")));
  }
  setLastDecision(QStringLiteral("Resumed"),
                  QStringLiteral("Context change cleared; bot resumed"));
}

void FT8AutoBot::log(QString const& category, QString const& detail) const
{
  if (!host_ || detail.isEmpty()) return;
  host_->botLog(category, detail);
}

QString FT8AutoBot::scoreDetail(Candidate const& candidate) const
{
  return QStringLiteral("call=%1 score=%2 new_call=%3 new_dx=%4 distance=%5 dist_km=%6 min=%7 rx=%8 report=%9")
    .arg(candidate.call,
         QString::number(candidate.totalScore),
         QString::number(candidate.scoreNewCall),
         QString::number(candidate.scoreNewDx),
         QString::number(candidate.scoreDistance),
         QString::number(candidate.scoreDistance * 50),
         QString::number(settings_.minScore),
         QString::number(candidate.rxFreq),
         QString::number(candidate.reportDb));
}

void FT8AutoBot::setLastDecision(QString const& action, QString const& reason,
                                 QString const& scoreBreakdown)
{
  lastDecision_.action = action;
  lastDecision_.reason = reason;
  lastDecision_.scoreBreakdown = scoreBreakdown;
}

QString FT8AutoBot::slotText(bool txFirst)
{
  return txFirst ? QStringLiteral("odd|1st") : QStringLiteral("even|2nd");
}

int FT8AutoBot::qsoProgressCallingValue()
{
  return 0;
}

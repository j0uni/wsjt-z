#include "FT8AutoBotWindow.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
QLineEdit * makeReadOnlyLineEdit(QWidget * parent)
{
  auto edit = new QLineEdit {parent};
  edit->setReadOnly(true);
  edit->setClearButtonEnabled(false);
  edit->setMinimumWidth(150);
  edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  return edit;
}

QString modeText(FT8AutoBotMode mode)
{
  switch (mode) {
  case FT8AutoBotMode::SearchAndPounce: return QObject::tr("Search && Pounce");
  case FT8AutoBotMode::CQ: return QObject::tr("CQ");
  }
  return QObject::tr("Unknown");
}

QString stateText(FT8AutoBotState state)
{
  switch (state) {
  case FT8AutoBotState::Disabled: return QObject::tr("Disabled");
  case FT8AutoBotState::Paused: return QObject::tr("Paused");
  case FT8AutoBotState::Hunting: return QObject::tr("Hunting");
  case FT8AutoBotState::Study: return QObject::tr("Study");
  case FT8AutoBotState::CallingCQ: return QObject::tr("Calling CQ");
  case FT8AutoBotState::AdoptingQSO: return QObject::tr("Adopting QSO");
  case FT8AutoBotState::Arming: return QObject::tr("Arming");
  case FT8AutoBotState::InQSO: return QObject::tr("In QSO");
  case FT8AutoBotState::Abandoning: return QObject::tr("Abandoning");
  case FT8AutoBotState::Idle: return QObject::tr("Idle");
  }
  return QObject::tr("Unknown");
}
}

FT8AutoBotWindow::FT8AutoBotWindow(QWidget * parent)
  : QWidget {parent}
  , enabled_ {new QCheckBox {tr("Enable FT8 Bot"), this}}
  , mode_ {new QComboBox {this}}
  , reuseFilters_ {new QCheckBox {tr("Reuse Main Filters"), this}}
  , minScore_ {new QSpinBox {this}}
  , cqIdleAfter_ {new QSpinBox {this}}
  , idleListenSeconds_ {new QSpinBox {this}}
  , cooldownMinutes_ {makeReadOnlyLineEdit(this)}
  , stuckCycleLimit_ {new QSpinBox {this}}
  , studyAfterCycles_ {new QSpinBox {this}}
  , acceptRr73AsCq_ {new QCheckBox {tr("Accept RR73 as CQ"), this}}
  , wakeDuringIdle_ {new QCheckBox {tr("Wake During Idle (CQ)"), this}}
  , idleTxPlanMin_ {new QSpinBox {this}}
  , idleTxPlanMax_ {new QSpinBox {this}}
  , idleTxPlanStep_ {new QSpinBox {this}}
  , adoptionGracePeriods_ {new QSpinBox {this}}
  , logToFile_ {new QCheckBox {tr("Write Log File"), this}}
  , state_ {makeReadOnlyLineEdit(this)}
  , qsoProgress_ {makeReadOnlyLineEdit(this)}
  , target_ {makeReadOnlyLineEdit(this)}
  , plannedTx_ {makeReadOnlyLineEdit(this)}
  , idleUntil_ {makeReadOnlyLineEdit(this)}
  , cqCount_ {makeReadOnlyLineEdit(this)}
  , activeCycles_ {makeReadOnlyLineEdit(this)}
  , autoTx_ {makeReadOnlyLineEdit(this)}
  , autoSeq_ {makeReadOnlyLineEdit(this)}
  , worked_ {makeReadOnlyLineEdit(this)}
  , cooldown_ {makeReadOnlyLineEdit(this)}
  , qsosCompleted_ {makeReadOnlyLineEdit(this)}
  , newDxcc_ {makeReadOnlyLineEdit(this)}
  , attempts_ {makeReadOnlyLineEdit(this)}
  , abandonedStuck_ {makeReadOnlyLineEdit(this)}
  , abandonedQrm_ {makeReadOnlyLineEdit(this)}
  , candidatesThisCycle_ {makeReadOnlyLineEdit(this)}
  , blockedWorked_ {makeReadOnlyLineEdit(this)}
  , blockedCooldown_ {makeReadOnlyLineEdit(this)}
  , idleEntries_ {makeReadOnlyLineEdit(this)}
  , lastAction_ {makeReadOnlyLineEdit(this)}
  , lastReason_ {makeReadOnlyLineEdit(this)}
  , lastScore_ {makeReadOnlyLineEdit(this)}
  , decisionLog_ {new QPlainTextEdit {this}}
  , clearLog_ {new QPushButton {tr("Clear View"), this}}
  , copyLog_ {new QPushButton {tr("Copy Log"), this}}
  , openLog_ {new QPushButton {tr("Open Log File"), this}}
  , clearWorked_ {new QPushButton {tr("Clear Worked"), this}}
  , clearCooldown_ {new QPushButton {tr("Clear Cooldown"), this}}
{
  setWindowTitle(tr("FT8 Auto Bot"));
  resize(1280, 720);
  setMinimumSize(1180, 680);

  mode_->addItem(modeText(FT8AutoBotMode::SearchAndPounce), static_cast<int>(FT8AutoBotMode::SearchAndPounce));
  mode_->addItem(modeText(FT8AutoBotMode::CQ), static_cast<int>(FT8AutoBotMode::CQ));
  minScore_->setRange(0, 5000);
  cqIdleAfter_->setRange(1, 50);
  idleListenSeconds_->setRange(15, 600);
  stuckCycleLimit_->setRange(1, 10);
  studyAfterCycles_->setRange(1, 100);
  idleTxPlanMin_->setRange(200, 4000);
  idleTxPlanMax_->setRange(500, 4000);
  idleTxPlanStep_->setRange(10, 200);
  adoptionGracePeriods_->setRange(1, 10);

  auto widenEditor = [] (QWidget * widget, int minWidth = 150) {
    widget->setMinimumWidth(minWidth);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  };
  widenEditor(mode_, 210);
  widenEditor(minScore_);
  widenEditor(cqIdleAfter_);
  widenEditor(idleListenSeconds_);
  widenEditor(cooldownMinutes_, 170);
  widenEditor(stuckCycleLimit_);
  widenEditor(studyAfterCycles_);
  widenEditor(idleTxPlanMin_);
  widenEditor(idleTxPlanMax_);
  widenEditor(idleTxPlanStep_);
  widenEditor(adoptionGracePeriods_);
  widenEditor(state_, 180);
  widenEditor(qsoProgress_, 180);
  widenEditor(target_, 220);
  widenEditor(plannedTx_, 180);
  widenEditor(idleUntil_, 180);
  widenEditor(cqCount_);
  widenEditor(activeCycles_);
  widenEditor(autoTx_);
  widenEditor(autoSeq_);
  widenEditor(worked_);
  widenEditor(cooldown_, 180);
  widenEditor(qsosCompleted_);
  widenEditor(newDxcc_);
  widenEditor(attempts_);
  widenEditor(abandonedStuck_);
  widenEditor(abandonedQrm_);
  widenEditor(candidatesThisCycle_);
  widenEditor(blockedWorked_);
  widenEditor(blockedCooldown_);
  widenEditor(idleEntries_);
  widenEditor(lastAction_, 180);
  widenEditor(lastReason_, 220);
  widenEditor(lastScore_, 220);

  auto controls = new QGroupBox {tr("Controls"), this};
  auto controlsLayout = new QFormLayout {controls};
  controlsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  controlsLayout->setHorizontalSpacing(12);
  controlsLayout->addRow(QString {}, enabled_);
  controlsLayout->addRow(tr("Mode"), mode_);
  controlsLayout->addRow(QString {}, reuseFilters_);
  controlsLayout->addRow(tr("Min Score"), minScore_);
  controlsLayout->addRow(tr("CQ Limit Before Idle"), cqIdleAfter_);
  controlsLayout->addRow(tr("Idle Listen Seconds"), idleListenSeconds_);
  controlsLayout->addRow(tr("Cooldown Minutes"), cooldownMinutes_);
  controlsLayout->addRow(tr("Stuck Cycle Limit"), stuckCycleLimit_);
  controlsLayout->addRow(tr("Study After Cycles"), studyAfterCycles_);
  controlsLayout->addRow(QString {}, acceptRr73AsCq_);
  controlsLayout->addRow(QString {}, wakeDuringIdle_);
  controlsLayout->addRow(tr("Idle TX Min Hz"), idleTxPlanMin_);
  controlsLayout->addRow(tr("Idle TX Max Hz"), idleTxPlanMax_);
  controlsLayout->addRow(tr("Idle TX Step Hz"), idleTxPlanStep_);
  controlsLayout->addRow(tr("Adoption Grace Periods"), adoptionGracePeriods_);
  controlsLayout->addRow(QString {}, logToFile_);

  auto status = new QGroupBox {tr("Status"), this};
  auto statusLayout = new QFormLayout {status};
  statusLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  statusLayout->setHorizontalSpacing(12);
  statusLayout->addRow(tr("State"), state_);
  statusLayout->addRow(tr("WSJT-X QSO Progress"), qsoProgress_);
  statusLayout->addRow(tr("Target"), target_);
  statusLayout->addRow(tr("Planned TX"), plannedTx_);
  statusLayout->addRow(tr("Idle Until"), idleUntil_);
  statusLayout->addRow(tr("CQ Count"), cqCount_);
  statusLayout->addRow(tr("Active QSO Cycles"), activeCycles_);
  statusLayout->addRow(tr("Enable TX"), autoTx_);
  statusLayout->addRow(tr("Auto Seq"), autoSeq_);
  statusLayout->addRow(tr("Worked"), worked_);
  statusLayout->addRow(tr("Cooldown"), cooldown_);

  auto counters = new QGroupBox {tr("Session Counters"), this};
  auto countersLayout = new QFormLayout {counters};
  countersLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  countersLayout->setHorizontalSpacing(12);
  countersLayout->addRow(tr("QSOs Completed"), qsosCompleted_);
  countersLayout->addRow(tr("New DXCC"), newDxcc_);
  countersLayout->addRow(tr("Attempts"), attempts_);
  countersLayout->addRow(tr("Abandoned (Stuck)"), abandonedStuck_);
  countersLayout->addRow(tr("Abandoned (QRM)"), abandonedQrm_);
  countersLayout->addRow(tr("Candidates This Cycle"), candidatesThisCycle_);
  countersLayout->addRow(tr("Blocked (Worked)"), blockedWorked_);
  countersLayout->addRow(tr("Blocked (Cooldown)"), blockedCooldown_);
  countersLayout->addRow(tr("Idle Entries"), idleEntries_);

  auto lastDecision = new QGroupBox {tr("Last Decision"), this};
  auto lastDecisionLayout = new QFormLayout {lastDecision};
  lastDecisionLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  lastDecisionLayout->setHorizontalSpacing(12);
  lastDecisionLayout->addRow(tr("Action"), lastAction_);
  lastDecisionLayout->addRow(tr("Reason"), lastReason_);
  lastDecisionLayout->addRow(tr("Score Breakdown"), lastScore_);

  decisionLog_->setReadOnly(true);
  decisionLog_->setLineWrapMode(QPlainTextEdit::NoWrap);
  decisionLog_->setMaximumBlockCount(1000);

  auto actionsLayout = new QHBoxLayout {};
  actionsLayout->addWidget(clearLog_);
  actionsLayout->addWidget(copyLog_);
  actionsLayout->addWidget(openLog_);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(clearWorked_);
  actionsLayout->addWidget(clearCooldown_);

  auto leftColumn = new QVBoxLayout {};
  leftColumn->addWidget(controls);
  leftColumn->addStretch(1);

  auto middleColumn = new QVBoxLayout {};
  middleColumn->addWidget(status);
  middleColumn->addWidget(lastDecision);
  middleColumn->addStretch(1);

  auto rightColumn = new QVBoxLayout {};
  rightColumn->addWidget(counters);
  rightColumn->addStretch(1);

  auto topLayout = new QHBoxLayout {};
  topLayout->setSpacing(16);
  topLayout->addLayout(leftColumn, 1);
  topLayout->addLayout(middleColumn, 1);
  topLayout->addLayout(rightColumn, 1);
  topLayout->setStretch(0, 5);
  topLayout->setStretch(1, 4);
  topLayout->setStretch(2, 3);

  auto mainLayout = new QVBoxLayout {this};
  mainLayout->addLayout(topLayout);
  mainLayout->addLayout(actionsLayout);
  mainLayout->addWidget(decisionLog_, 1);

  connect(enabled_, &QCheckBox::toggled, this, &FT8AutoBotWindow::enableRequested);
  connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] (int index) {
    auto const value = static_cast<FT8AutoBotMode>(mode_->itemData(index).toInt());
    Q_EMIT modeRequested(value);
  });
  connect(reuseFilters_, &QCheckBox::toggled, this, &FT8AutoBotWindow::reuseFiltersRequested);
  connect(minScore_, QOverload<int>::of(&QSpinBox::valueChanged), this, &FT8AutoBotWindow::minScoreRequested);
  connect(cqIdleAfter_, QOverload<int>::of(&QSpinBox::valueChanged), this, &FT8AutoBotWindow::cqIdleAfterRequested);
  connect(idleListenSeconds_, QOverload<int>::of(&QSpinBox::valueChanged), this, &FT8AutoBotWindow::idleListenSecondsRequested);
  connect(stuckCycleLimit_, QOverload<int>::of(&QSpinBox::valueChanged), this, &FT8AutoBotWindow::stuckCycleLimitRequested);
  connect(acceptRr73AsCq_, &QCheckBox::toggled, this, &FT8AutoBotWindow::acceptRr73AsCqRequested);
  connect(wakeDuringIdle_, &QCheckBox::toggled, this, &FT8AutoBotWindow::wakeDuringIdleRequested);
  connect(idleTxPlanMin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &FT8AutoBotWindow::idleTxPlanMinRequested);
  connect(idleTxPlanMax_, QOverload<int>::of(&QSpinBox::valueChanged), this, &FT8AutoBotWindow::idleTxPlanMaxRequested);
  connect(idleTxPlanStep_, QOverload<int>::of(&QSpinBox::valueChanged), this, &FT8AutoBotWindow::idleTxPlanStepRequested);
  connect(adoptionGracePeriods_, QOverload<int>::of(&QSpinBox::valueChanged), this, &FT8AutoBotWindow::adoptionGracePeriodsRequested);
  connect(logToFile_, &QCheckBox::toggled, this, &FT8AutoBotWindow::logToFileRequested);
  connect(clearLog_, &QPushButton::clicked, this, &FT8AutoBotWindow::clearLogRequested);
  connect(copyLog_, &QPushButton::clicked, this, &FT8AutoBotWindow::copyLogRequested);
  connect(openLog_, &QPushButton::clicked, this, &FT8AutoBotWindow::openLogRequested);
  connect(clearWorked_, &QPushButton::clicked, this, &FT8AutoBotWindow::clearWorkedRequested);
  connect(clearCooldown_, &QPushButton::clicked, this, &FT8AutoBotWindow::clearCooldownRequested);
}

void FT8AutoBotWindow::setSnapshot(FT8AutoBotSnapshot const& snapshot)
{
  QSignalBlocker enabledBlocker {enabled_};
  QSignalBlocker modeBlocker {mode_};

  enabled_->setChecked(snapshot.enabled);
  mode_->setCurrentIndex(snapshot.mode == FT8AutoBotMode::SearchAndPounce ? 0 : 1);
  setReadOnlyText(state_, stateText(snapshot.state));
  if (snapshot.state == FT8AutoBotState::Paused && !snapshot.pauseReason.isEmpty()) {
    setReadOnlyText(state_, QStringLiteral("%1 (%2)").arg(stateText(snapshot.state), snapshot.pauseReason));
  } else if (snapshot.state == FT8AutoBotState::Study && snapshot.studyCyclesRemaining > 0) {
    setReadOnlyText(state_, QStringLiteral("%1 (%2 left)").arg(stateText(snapshot.state),
                                                               QString::number(snapshot.studyCyclesRemaining)));
  }
  QStringList targetParts;
  if (!snapshot.targetCall.isEmpty()) targetParts << snapshot.targetCall;
  if (!snapshot.targetGrid.isEmpty()) targetParts << snapshot.targetGrid;
  if (!snapshot.targetCountry.isEmpty()) targetParts << snapshot.targetCountry;
  if (snapshot.targetDistanceKm > 0) targetParts << QStringLiteral("%1 km").arg(QString::number(snapshot.targetDistanceKm));
  setReadOnlyText(target_, targetParts.join(QStringLiteral(" ")));
  setReadOnlyText(plannedTx_, snapshot.plannedTxFreq > 0
                  ? QStringLiteral("%1 Hz (%2)")
                    .arg(QString::number(snapshot.plannedTxFreq),
                         snapshot.plannedTxFirst ? tr("odd / 1st") : tr("even / 2nd"))
                  : QString {});
  setReadOnlyText(idleUntil_, snapshot.idleUntilUtc.isValid()
                  ? QStringLiteral("%1 (%2 s)")
                    .arg(snapshot.idleUntilUtc.toLocalTime().toString(QStringLiteral("hh:mm:ss")),
                         QString::number(snapshot.idleRemainingSeconds))
                  : QString {});
  setReadOnlyText(cqCount_, QString::number(snapshot.cqWithoutCallerCount));
}

void FT8AutoBotWindow::setSettings(FT8AutoBotSettings const& settings)
{
  QSignalBlocker reuseBlocker {reuseFilters_};
  QSignalBlocker minScoreBlocker {minScore_};
  QSignalBlocker cqIdleBlocker {cqIdleAfter_};
  QSignalBlocker idleListenBlocker {idleListenSeconds_};
  QSignalBlocker stuckBlocker {stuckCycleLimit_};
  QSignalBlocker studyBlocker {studyAfterCycles_};
  QSignalBlocker rr73Blocker {acceptRr73AsCq_};
  QSignalBlocker wakeBlocker {wakeDuringIdle_};
  QSignalBlocker txMinBlocker {idleTxPlanMin_};
  QSignalBlocker txMaxBlocker {idleTxPlanMax_};
  QSignalBlocker txStepBlocker {idleTxPlanStep_};
  QSignalBlocker graceBlocker {adoptionGracePeriods_};
  QSignalBlocker logBlocker {logToFile_};

  reuseFilters_->setChecked(settings.reuseMainWindowFilters);
  minScore_->setValue(settings.minScore);
  cqIdleAfter_->setValue(settings.cqIdleAfter);
  idleListenSeconds_->setValue(settings.idleListenSeconds);
  setReadOnlyText(cooldownMinutes_, QString::number(settings.cooldownMinutes));
  stuckCycleLimit_->setValue(settings.stuckCycleLimit);
  studyAfterCycles_->setValue(settings.studyAfterCycles);
  acceptRr73AsCq_->setChecked(settings.acceptRr73AsCq);
  wakeDuringIdle_->setChecked(settings.wakeDuringIdleInCQMode);
  idleTxPlanMin_->setValue(settings.idleTxPlanMinHz);
  idleTxPlanMax_->setValue(settings.idleTxPlanMaxHz);
  idleTxPlanStep_->setValue(settings.idleTxPlanStepHz);
  adoptionGracePeriods_->setValue(settings.adoptionGracePeriods);
  logToFile_->setChecked(settings.logToFile);
}

void FT8AutoBotWindow::setMemoryInfo(int workedCount, int cooldownCount, QString const& nextCooldownText)
{
  setReadOnlyText(worked_, QString::number(workedCount));
  setReadOnlyText(cooldown_, nextCooldownText.isEmpty()
                  ? QString::number(cooldownCount)
                  : QStringLiteral("%1 (next %2)").arg(QString::number(cooldownCount), nextCooldownText));
}

void FT8AutoBotWindow::setRuntimeInfo(QString const& qsoProgressText, QString const& activeQsoCyclesText,
                                      QString const& autoTxText, QString const& autoSeqText)
{
  setReadOnlyText(qsoProgress_, qsoProgressText);
  setReadOnlyText(activeCycles_, activeQsoCyclesText);
  setReadOnlyText(autoTx_, autoTxText);
  setReadOnlyText(autoSeq_, autoSeqText);
}

void FT8AutoBotWindow::setCounters(FT8AutoBotCounters const& counters)
{
  setReadOnlyText(qsosCompleted_, QString::number(counters.qsosCompleted));
  setReadOnlyText(newDxcc_, QString::number(counters.newDxcc));
  setReadOnlyText(attempts_, QString::number(counters.attempts));
  setReadOnlyText(abandonedStuck_, QString::number(counters.abandonedStuck));
  setReadOnlyText(abandonedQrm_, QString::number(counters.abandonedQrm));
  setReadOnlyText(candidatesThisCycle_, QString::number(counters.candidatesThisCycle));
  setReadOnlyText(blockedWorked_, QString::number(counters.blockedWorked));
  setReadOnlyText(blockedCooldown_, QString::number(counters.blockedCooldown));
  setReadOnlyText(idleEntries_, QString::number(counters.idleEntries));
}

void FT8AutoBotWindow::setLastDecision(FT8AutoBotLastDecision const& decision)
{
  setReadOnlyText(lastAction_, decision.action);
  setReadOnlyText(lastReason_, decision.reason);
  setReadOnlyText(lastScore_, decision.scoreBreakdown);
}

void FT8AutoBotWindow::setDecisionLog(QStringList const& entries)
{
  decisionLog_->setPlainText(entries.join('\n'));
}

void FT8AutoBotWindow::appendDecisionLog(QString const& entry)
{
  decisionLog_->appendPlainText(entry);
}

QString FT8AutoBotWindow::selectedDecisionLogText() const
{
  auto const cursor = decisionLog_->textCursor();
  if (cursor.hasSelection()) {
    return cursor.selectedText();
  }
  return decisionLog_->toPlainText();
}

void FT8AutoBotWindow::hideEvent(QHideEvent * event)
{
  QWidget::hideEvent(event);
  Q_EMIT windowVisibleChanged(false);
}

void FT8AutoBotWindow::showEvent(QShowEvent * event)
{
  QWidget::showEvent(event);
  Q_EMIT windowVisibleChanged(true);
}

void FT8AutoBotWindow::setReadOnlyText(QLineEdit * edit, QString const& text)
{
  edit->setText(text);
  edit->setCursorPosition(0);
}

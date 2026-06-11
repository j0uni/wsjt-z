#ifndef FT8AUTOBOTWINDOW_HPP
#define FT8AUTOBOTWINDOW_HPP

#include <QObject>
#include <QWidget>

#include "widgets/FT8AutoBot.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class FT8AutoBotWindow
  : public QWidget
{
  Q_OBJECT

public:
  explicit FT8AutoBotWindow(QWidget * parent = nullptr);

  void setSnapshot(FT8AutoBotSnapshot const& snapshot);
  void setSettings(FT8AutoBotSettings const& settings);
  void setMemoryInfo(int workedCount, int cooldownCount, QString const& nextCooldownText);
  void setRuntimeInfo(QString const& qsoProgressText, QString const& activeQsoCyclesText,
                      QString const& autoTxText, QString const& autoSeqText);
  void setCounters(FT8AutoBotCounters const& counters);
  void setLastDecision(FT8AutoBotLastDecision const& decision);
  void setDecisionLog(QStringList const& entries);
  void appendDecisionLog(QString const& entry);
  QString selectedDecisionLogText() const;
  QSpinBox * studyAfterCyclesControl() const { return studyAfterCycles_; }

Q_SIGNALS:
  void enableRequested(bool enabled);
  void modeRequested(FT8AutoBotMode mode);
  void reuseFiltersRequested(bool enabled);
  void minScoreRequested(int value);
  void cqIdleAfterRequested(int value);
  void idleListenSecondsRequested(int value);
  void stuckCycleLimitRequested(int value);
  void acceptRr73AsCqRequested(bool enabled);
  void wakeDuringIdleRequested(bool enabled);
  void idleTxPlanMinRequested(int value);
  void idleTxPlanMaxRequested(int value);
  void idleTxPlanStepRequested(int value);
  void adoptionGracePeriodsRequested(int value);
  void logToFileRequested(bool enabled);
  void clearLogRequested();
  void copyLogRequested();
  void openLogRequested();
  void clearWorkedRequested();
  void clearCooldownRequested();
  void windowVisibleChanged(bool visible);

protected:
  void hideEvent(QHideEvent *) override;
  void showEvent(QShowEvent *) override;

private:
  void setReadOnlyText(QLineEdit * edit, QString const& text);

  QCheckBox * enabled_;
  QComboBox * mode_;
  QCheckBox * reuseFilters_;
  QSpinBox * minScore_;
  QSpinBox * cqIdleAfter_;
  QSpinBox * idleListenSeconds_;
  QLineEdit * cooldownMinutes_;
  QSpinBox * stuckCycleLimit_;
  QSpinBox * studyAfterCycles_;
  QCheckBox * acceptRr73AsCq_;
  QCheckBox * wakeDuringIdle_;
  QSpinBox * idleTxPlanMin_;
  QSpinBox * idleTxPlanMax_;
  QSpinBox * idleTxPlanStep_;
  QSpinBox * adoptionGracePeriods_;
  QCheckBox * logToFile_;
  QLineEdit * state_;
  QLineEdit * qsoProgress_;
  QLineEdit * target_;
  QLineEdit * plannedTx_;
  QLineEdit * idleUntil_;
  QLineEdit * cqCount_;
  QLineEdit * activeCycles_;
  QLineEdit * autoTx_;
  QLineEdit * autoSeq_;
  QLineEdit * worked_;
  QLineEdit * cooldown_;
  QLineEdit * qsosCompleted_;
  QLineEdit * newDxcc_;
  QLineEdit * attempts_;
  QLineEdit * abandonedStuck_;
  QLineEdit * abandonedQrm_;
  QLineEdit * candidatesThisCycle_;
  QLineEdit * blockedWorked_;
  QLineEdit * blockedCooldown_;
  QLineEdit * idleEntries_;
  QLineEdit * lastAction_;
  QLineEdit * lastReason_;
  QLineEdit * lastScore_;
  QPlainTextEdit * decisionLog_;
  QPushButton * clearLog_;
  QPushButton * copyLog_;
  QPushButton * openLog_;
  QPushButton * clearWorked_;
  QPushButton * clearCooldown_;
};

#endif

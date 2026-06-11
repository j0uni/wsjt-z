#ifndef FT8AUTOBOTMEMORY_HPP
#define FT8AUTOBOTMEMORY_HPP

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QStringList>
#include <QSet>
#include <QString>

struct FT8AutoBotCooldownEntry
{
  QString call;
  QDateTime expiresUtc;
  QString reason;
};

class FT8AutoBotMemory
{
public:
  explicit FT8AutoBotMemory(QDir const& dataDir = QDir {});

  void setDataDir(QDir const& dataDir);
  bool load();

  QString workedFilePath() const;
  QString cooldownFilePath() const;

  bool isWorked(QString const& call) const;
  bool isOnCooldown(QString const& call, QDateTime const& nowUtc = QDateTime::currentDateTimeUtc()) const;
  bool mayCall(QString const& call, QDateTime const& nowUtc = QDateTime::currentDateTimeUtc()) const;

  bool addWorked(QString const& call);
  bool addCooldown(QString const& call, QString const& reason,
                   QDateTime const& nowUtc = QDateTime::currentDateTimeUtc(),
                   int cooldownMinutes = 30);
  bool removeCooldown(QString const& call);
  bool clearWorked();
  bool clearCooldowns();
  int backfillWorkedFromAdif(QString const& adifPath);
  int purgeExpiredCooldowns(QDateTime const& nowUtc = QDateTime::currentDateTimeUtc());

  int workedCount() const;
  int cooldownCount(QDateTime const& nowUtc = QDateTime::currentDateTimeUtc()) const;
  QDateTime nextCooldownExpiry(QDateTime const& nowUtc = QDateTime::currentDateTimeUtc()) const;
  QDateTime cooldownExpiryForCall(QString const& call,
                                  QDateTime const& nowUtc = QDateTime::currentDateTimeUtc()) const;
  QSet<QString> const& workedCalls() const;

  static QString normalizeCall(QString const& call);

private:
  bool appendWorkedLine(QString const& normalizedCall) const;
  bool appendCooldownLine(FT8AutoBotCooldownEntry const& entry) const;
  bool rewriteWorkedFile() const;
  bool rewriteCooldownFile() const;

  QDir dataDir_;
  QSet<QString> worked_;
  QHash<QString, FT8AutoBotCooldownEntry> cooldown_;
};

#endif

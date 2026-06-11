#include "widgets/FT8AutoBotMemory.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "Radio.hpp"

namespace
{
QString isoFormat(QDateTime const& dt)
{
  return dt.toUTC().toString(Qt::ISODate);
}
}

FT8AutoBotMemory::FT8AutoBotMemory(QDir const& dataDir)
  : dataDir_ {dataDir}
{
}

void FT8AutoBotMemory::setDataDir(QDir const& dataDir)
{
  dataDir_ = dataDir;
}

bool FT8AutoBotMemory::load()
{
  worked_.clear();
  cooldown_.clear();

  QFile workedFile {workedFilePath()};
  if (workedFile.exists() && workedFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream stream {&workedFile};
    while (!stream.atEnd()) {
      auto line = stream.readLine().trimmed();
      if (line.isEmpty() || line.startsWith('#')) continue;
      worked_.insert(normalizeCall(line));
    }
  }

  QFile cooldownFile {cooldownFilePath()};
  if (cooldownFile.exists() && cooldownFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream stream {&cooldownFile};
    auto const nowUtc = QDateTime::currentDateTimeUtc();
    while (!stream.atEnd()) {
      auto line = stream.readLine().trimmed();
      if (line.isEmpty() || line.startsWith('#')) continue;

      auto const parts = line.split('\t');
      if (parts.size() < 2) continue;

      FT8AutoBotCooldownEntry entry;
      entry.call = normalizeCall(parts.at(0));
      entry.expiresUtc = QDateTime::fromString(parts.at(1), Qt::ISODate);
      entry.reason = parts.size() > 2 ? parts.at(2) : QString {};
      if (!entry.expiresUtc.isValid() || entry.expiresUtc <= nowUtc) continue;
      cooldown_.insert(entry.call, entry);
    }
  }

  return true;
}

QString FT8AutoBotMemory::workedFilePath() const
{
  return dataDir_.absoluteFilePath(QStringLiteral("ft8_autobot_worked.txt"));
}

QString FT8AutoBotMemory::cooldownFilePath() const
{
  return dataDir_.absoluteFilePath(QStringLiteral("ft8_autobot_cooldown.txt"));
}

bool FT8AutoBotMemory::isWorked(QString const& call) const
{
  return worked_.contains(normalizeCall(call));
}

bool FT8AutoBotMemory::isOnCooldown(QString const& call, QDateTime const& nowUtc) const
{
  auto const key = normalizeCall(call);
  auto const it = cooldown_.find(key);
  return it != cooldown_.end() && it->expiresUtc.isValid() && nowUtc < it->expiresUtc;
}

bool FT8AutoBotMemory::mayCall(QString const& call, QDateTime const& nowUtc) const
{
  return !isWorked(call) && !isOnCooldown(call, nowUtc);
}

bool FT8AutoBotMemory::addWorked(QString const& call)
{
  auto const normalized = normalizeCall(call);
  if (normalized.isEmpty() || worked_.contains(normalized)) return false;
  worked_.insert(normalized);
  return appendWorkedLine(normalized);
}

bool FT8AutoBotMemory::addCooldown(QString const& call, QString const& reason,
                                   QDateTime const& nowUtc, int cooldownMinutes)
{
  FT8AutoBotCooldownEntry entry;
  entry.call = normalizeCall(call);
  entry.expiresUtc = nowUtc.toUTC().addSecs(cooldownMinutes * 60);
  entry.reason = reason;
  if (entry.call.isEmpty()) return false;

  cooldown_.insert(entry.call, entry);
  return rewriteCooldownFile();
}

bool FT8AutoBotMemory::removeCooldown(QString const& call)
{
  auto const removed = cooldown_.remove(normalizeCall(call));
  if (!removed) return false;
  return rewriteCooldownFile();
}

bool FT8AutoBotMemory::clearWorked()
{
  if (worked_.isEmpty()) {
    return rewriteWorkedFile();
  }

  worked_.clear();
  return rewriteWorkedFile();
}

bool FT8AutoBotMemory::clearCooldowns()
{
  if (cooldown_.isEmpty()) {
    return rewriteCooldownFile();
  }

  cooldown_.clear();
  return rewriteCooldownFile();
}

int FT8AutoBotMemory::backfillWorkedFromAdif(QString const& adifPath)
{
  QFile adifFile {adifPath};
  if (adifPath.trimmed().isEmpty() || !adifFile.exists()
      || !adifFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return 0;
  }

  auto const content = QString::fromUtf8(adifFile.readAll());
  static QRegularExpression const callField {
    QStringLiteral("<call:\\d+>([^<\\s]+)"),
    QRegularExpression::CaseInsensitiveOption
  };

  int added = 0;
  auto it = callField.globalMatch(content);
  while (it.hasNext()) {
    auto const match = it.next();
    auto const normalized = normalizeCall(match.captured(1));
    if (normalized.isEmpty() || worked_.contains(normalized)) continue;
    worked_.insert(normalized);
    ++added;
  }

  if (added) {
    rewriteWorkedFile();
  }
  return added;
}

int FT8AutoBotMemory::purgeExpiredCooldowns(QDateTime const& nowUtc)
{
  int removed = 0;
  for (auto it = cooldown_.begin(); it != cooldown_.end();) {
    if (!it->expiresUtc.isValid() || it->expiresUtc <= nowUtc) {
      it = cooldown_.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }

  if (removed) {
    rewriteCooldownFile();
  }
  return removed;
}

int FT8AutoBotMemory::workedCount() const
{
  return worked_.size();
}

int FT8AutoBotMemory::cooldownCount(QDateTime const& nowUtc) const
{
  int count = 0;
  for (auto const& entry : cooldown_) {
    if (entry.expiresUtc.isValid() && nowUtc < entry.expiresUtc) {
      ++count;
    }
  }
  return count;
}

QDateTime FT8AutoBotMemory::nextCooldownExpiry(QDateTime const& nowUtc) const
{
  QDateTime nearest;
  for (auto const& entry : cooldown_) {
    if (!entry.expiresUtc.isValid() || entry.expiresUtc <= nowUtc) continue;
    if (!nearest.isValid() || entry.expiresUtc < nearest) {
      nearest = entry.expiresUtc;
    }
  }
  return nearest;
}

QDateTime FT8AutoBotMemory::cooldownExpiryForCall(QString const& call, QDateTime const& nowUtc) const
{
  auto const it = cooldown_.find(normalizeCall(call));
  if (it == cooldown_.end() || !it->expiresUtc.isValid() || it->expiresUtc <= nowUtc) {
    return QDateTime {};
  }
  return it->expiresUtc;
}

QSet<QString> const& FT8AutoBotMemory::workedCalls() const
{
  return worked_;
}

QString FT8AutoBotMemory::normalizeCall(QString const& call)
{
  return Radio::base_callsign(call.trimmed().toUpper());
}

bool FT8AutoBotMemory::appendWorkedLine(QString const& normalizedCall) const
{
  QFile file {workedFilePath()};
  if (!file.open(QIODevice::Append | QIODevice::Text)) return false;

  QTextStream stream {&file};
  stream << normalizedCall << '\n';
  return file.error() == QFile::NoError;
}

bool FT8AutoBotMemory::appendCooldownLine(FT8AutoBotCooldownEntry const& entry) const
{
  QFile file {cooldownFilePath()};
  if (!file.open(QIODevice::Append | QIODevice::Text)) return false;

  QTextStream stream {&file};
  stream << entry.call << '\t' << isoFormat(entry.expiresUtc) << '\t' << entry.reason << '\n';
  return file.error() == QFile::NoError;
}

bool FT8AutoBotMemory::rewriteWorkedFile() const
{
  QFile file {workedFilePath()};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;

  QTextStream stream {&file};
  stream << "# FT8 Auto Bot — worked stations (do not call again)\n";
  for (auto const& worked : worked_) {
    stream << worked << '\n';
  }
  return file.error() == QFile::NoError;
}

bool FT8AutoBotMemory::rewriteCooldownFile() const
{
  QFile file {cooldownFilePath()};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;

  QTextStream stream {&file};
  stream << "# call\tutc_expires_iso\treason\n";
  for (auto const& entry : cooldown_) {
    if (!entry.expiresUtc.isValid()) continue;
    stream << entry.call << '\t' << isoFormat(entry.expiresUtc) << '\t' << entry.reason << '\n';
  }
  return file.error() == QFile::NoError;
}

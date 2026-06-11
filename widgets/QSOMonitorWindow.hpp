#ifndef QSO_MONITOR_WINDOW_HPP
#define QSO_MONITOR_WINDOW_HPP

#include <QWidget>

class QLineEdit;
class QPlainTextEdit;

class QSOMonitorWindow
  : public QWidget
{
  Q_OBJECT

public:
  explicit QSOMonitorWindow (QWidget * parent = nullptr);

  void set_station_info (QString const& qso_state
                         , QString const& call
                         , QString const& grid
                         , QString const& distance
                         , QString const& bearing
                         , QString const& country
                         , QString const& continent
                         , QString const& cq_zone
                         , QString const& itu_zone
                         , QString const& state);
  void set_auto_info (QString const& auto_cq
                      , QString const& auto_call
                      , QString const& priority_call
                      , QString const& last_action
                      , QString const& last_reason);
  void set_decision_log (QStringList const& entries);
  void append_decision_log (QString const& entry);

Q_SIGNALS:
  void clear_log_requested ();
  void window_visible_changed (bool visible);

protected:
  void hideEvent (QHideEvent *) override;
  void showEvent (QShowEvent *) override;

private:
  void set_read_only_text (QLineEdit *, QString const&);

  QLineEdit * qso_state_;
  QLineEdit * call_;
  QLineEdit * grid_;
  QLineEdit * distance_;
  QLineEdit * bearing_;
  QLineEdit * country_;
  QLineEdit * continent_;
  QLineEdit * cq_zone_;
  QLineEdit * itu_zone_;
  QLineEdit * state_;

  QLineEdit * auto_cq_;
  QLineEdit * auto_call_;
  QLineEdit * priority_call_;
  QLineEdit * last_action_;
  QLineEdit * last_reason_;

  QPlainTextEdit * decision_log_;
};

#endif

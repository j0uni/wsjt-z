#include "QSOMonitorWindow.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTextCursor>
#include <QVBoxLayout>

namespace
{
  QLineEdit * make_read_only_line_edit (QWidget * parent)
  {
    auto edit = new QLineEdit {parent};
    edit->setReadOnly (true);
    edit->setClearButtonEnabled (false);
    edit->setCursorPosition (0);
    return edit;
  }
}

QSOMonitorWindow::QSOMonitorWindow (QWidget * parent)
  : QWidget {parent}
  , qso_state_ {make_read_only_line_edit (this)}
  , call_ {make_read_only_line_edit (this)}
  , grid_ {make_read_only_line_edit (this)}
  , distance_ {make_read_only_line_edit (this)}
  , bearing_ {make_read_only_line_edit (this)}
  , country_ {make_read_only_line_edit (this)}
  , continent_ {make_read_only_line_edit (this)}
  , cq_zone_ {make_read_only_line_edit (this)}
  , itu_zone_ {make_read_only_line_edit (this)}
  , state_ {make_read_only_line_edit (this)}
  , auto_cq_ {make_read_only_line_edit (this)}
  , auto_call_ {make_read_only_line_edit (this)}
  , priority_call_ {make_read_only_line_edit (this)}
  , last_action_ {make_read_only_line_edit (this)}
  , last_reason_ {make_read_only_line_edit (this)}
  , decision_log_ {new QPlainTextEdit {this}}
{
  setWindowTitle (tr ("QSO Monitor"));
  resize (760, 620);

  auto station_group = new QGroupBox {tr ("Current Station"), this};
  auto station_form = new QFormLayout {station_group};
  station_form->addRow (tr ("QSO State"), qso_state_);
  station_form->addRow (tr ("Callsign"), call_);
  station_form->addRow (tr ("Grid"), grid_);
  station_form->addRow (tr ("Distance"), distance_);
  station_form->addRow (tr ("Bearing"), bearing_);
  station_form->addRow (tr ("Country"), country_);
  station_form->addRow (tr ("Continent"), continent_);
  station_form->addRow (tr ("CQ Zone"), cq_zone_);
  station_form->addRow (tr ("ITU Zone"), itu_zone_);
  station_form->addRow (tr ("US State"), state_);

  auto automation_group = new QGroupBox {tr ("Automation"), this};
  auto automation_form = new QFormLayout {automation_group};
  automation_form->addRow (tr ("Auto CQ"), auto_cq_);
  automation_form->addRow (tr ("Auto Call"), auto_call_);
  automation_form->addRow (tr ("Priority Call"), priority_call_);
  automation_form->addRow (tr ("Last Action"), last_action_);
  automation_form->addRow (tr ("Why"), last_reason_);

  auto top_layout = new QHBoxLayout;
  top_layout->addWidget (station_group, 1);
  top_layout->addWidget (automation_group, 1);

  auto log_group = new QGroupBox {tr ("Decision Log"), this};
  auto log_layout = new QVBoxLayout {log_group};
  decision_log_->setReadOnly (true);
  decision_log_->setLineWrapMode (QPlainTextEdit::NoWrap);
  decision_log_->setMaximumBlockCount (500);
  auto clear_button = new QPushButton {tr ("Clear Log"), log_group};
  clear_button->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Fixed);
  connect (clear_button, &QPushButton::clicked, this, &QSOMonitorWindow::clear_log_requested);
  log_layout->addWidget (decision_log_, 1);
  log_layout->addWidget (clear_button, 0, Qt::AlignRight);

  auto main_layout = new QVBoxLayout {this};
  main_layout->addLayout (top_layout);
  main_layout->addWidget (log_group, 1);
}

void QSOMonitorWindow::set_station_info (QString const& qso_state
                                         , QString const& call
                                         , QString const& grid
                                         , QString const& distance
                                         , QString const& bearing
                                         , QString const& country
                                         , QString const& continent
                                         , QString const& cq_zone
                                         , QString const& itu_zone
                                         , QString const& state)
{
  set_read_only_text (qso_state_, qso_state);
  set_read_only_text (call_, call);
  set_read_only_text (grid_, grid);
  set_read_only_text (distance_, distance);
  set_read_only_text (bearing_, bearing);
  set_read_only_text (country_, country);
  set_read_only_text (continent_, continent);
  set_read_only_text (cq_zone_, cq_zone);
  set_read_only_text (itu_zone_, itu_zone);
  set_read_only_text (state_, state);
}

void QSOMonitorWindow::set_auto_info (QString const& auto_cq
                                      , QString const& auto_call
                                      , QString const& priority_call
                                      , QString const& last_action
                                      , QString const& last_reason)
{
  set_read_only_text (auto_cq_, auto_cq);
  set_read_only_text (auto_call_, auto_call);
  set_read_only_text (priority_call_, priority_call);
  set_read_only_text (last_action_, last_action);
  set_read_only_text (last_reason_, last_reason);
}

void QSOMonitorWindow::set_decision_log (QStringList const& entries)
{
  decision_log_->setPlainText (entries.join ('\n'));
  auto cursor = decision_log_->textCursor ();
  cursor.movePosition (QTextCursor::End);
  decision_log_->setTextCursor (cursor);
}

void QSOMonitorWindow::append_decision_log (QString const& entry)
{
  decision_log_->appendPlainText (entry);
}

void QSOMonitorWindow::hideEvent (QHideEvent * event)
{
  QWidget::hideEvent (event);
  Q_EMIT window_visible_changed (false);
}

void QSOMonitorWindow::showEvent (QShowEvent * event)
{
  QWidget::showEvent (event);
  Q_EMIT window_visible_changed (true);
}

void QSOMonitorWindow::set_read_only_text (QLineEdit * edit, QString const& text)
{
  edit->setText (text);
  edit->setCursorPosition (0);
}

#ifndef CLINTWINDOW_H
#define CLINTWINDOW_H

#include <QtWidgets>
#include <QMainWindow>

#include "clintprogram.h"
#include "vizprojection.h"

class QWidget;
class QTextEdit;

class ClintWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit ClintWindow(QWidget *parent = 0);

signals:

public slots:
  void fileOpen();
  void fileClose();
  void fileSaveSvg();
  void openFileByName(QString fileName);

  void scopTransformed();

private:
  QAction *m_actionFileOpen;
  QAction *m_actionFileClose;
  QAction *m_actionFileSaveSvg;
  QAction *m_actionFileQuit;
  QMenuBar *m_menuBar;

  bool m_fileOpen = false;

  ClintProgram *m_program = nullptr;
  VizProjection *m_projection = nullptr;
  QTextEdit *codeEditor = nullptr;
  QTextEdit *scriptEditor = nullptr;

  bool m_showOriginalCode = false;

  void setupActions();
  void setupMenus();

  void resetCentralWidget(QWidget *interface = nullptr);
};

#endif // CLINTWINDOW_H

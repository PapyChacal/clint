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
  ~ClintWindow();

  ClintScop *regenerateScop(ClintScop *vscop, int parameterValue);
  ClintScop *regenerateScopOsl(ClintScop *vscop, osl_scop_p scop, int parameterValue, bool swapMapper);
  void createProjections(ClintScop *vscop);
signals:

public slots:
  void fileOpen();
  void fileClose();
  void fileSaveSvg();
  void openFileByName(QString fileName);

  void editUndo();
  void editRedo();
  void editVizProperties();

  void viewFreezeToggled(bool value);
  void viewProjectionMatrixToggled(bool value);

  void scopTransformed();

  void updateCodeEditor();
  void reparseCode();
  void reparseScript();

  void changeParameter(int value);

private:
  QAction *m_actionFileOpen;
  QAction *m_actionFileClose;
  QAction *m_actionFileSaveSvg;
  QAction *m_actionFileQuit;

  QAction *m_actionEditUndo;
  QAction *m_actionEditRedo;
  QAction *m_actionEditVizProperties;

  QAction *m_actionViewFreeze;
  QAction *m_actionViewProjectionMatrix;

  QMenuBar *m_menuBar;

  bool m_fileOpen = false;

  ClintProgram *m_program = nullptr;
  VizProjection *m_projection = nullptr;
  std::vector<VizProjection *> m_allProjections;
  QWidget *m_projectionMatrixWidget = nullptr;
  QWidget *m_graphicalInterface = nullptr;
  QTextEdit *m_codeEditor = nullptr;
  QTextEdit *m_scriptEditor = nullptr;
  QPushButton *m_reparseCodeButton = nullptr,
              *m_reparseScriptButton = nullptr;

  bool m_showOriginalCode = false;
  int m_parameterValue = 6;

  void setupActions();
  void setupMenus();

  void resetCentralWidget(QWidget *interface = nullptr, bool deleteGraphicalInterface = true);
  void resetProjectionMatrix(ClintScop *vscop);
};

#endif // CLINTWINDOW_H

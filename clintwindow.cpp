#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <QtSvg>

#include "vizprojection.h"
#include "clintwindow.h"
#include "oslutils.h"
#include "propertiesdialog.h"

void ClintWindow::resetCentralWidget(QWidget *interface, bool deleteGraphicalInterface) {
  if (centralWidget() != nullptr) {
    QWidget *oldWidget = centralWidget();
    if (!deleteGraphicalInterface && m_graphicalInterface) {
      m_graphicalInterface->setParent(nullptr);
    }
    m_scriptEditor->setParent(nullptr);
    m_codeEditor->setParent(nullptr);
    m_reparseScriptButton->setParent(nullptr);
    m_reparseCodeButton->setParent(nullptr);
    delete oldWidget;
  }

  if (m_graphicalInterface == m_projectionMatrixWidget && deleteGraphicalInterface)
    m_projectionMatrixWidget = nullptr;
  m_graphicalInterface = interface;

  if (interface == nullptr) {
    setCentralWidget(nullptr);
    return;
  }

  QGridLayout *topLayout = new QGridLayout;
  topLayout->addWidget(interface, 0, 0, 2, 1 /*,Qt::AlignCenter | Qt::AlignVCenter*/);
  topLayout->addWidget(m_reparseScriptButton, 0, 1, 1, 1);
  topLayout->addWidget(m_reparseCodeButton, 1, 1, 1, 1);
  topLayout->addWidget(m_scriptEditor, 0, 2, 1, 1 /*,Qt::AlignCenter | Qt::AlignTop*/);
  topLayout->addWidget(m_codeEditor, 1, 2, 1, 1  /*,Qt::AlignCenter| Qt::AlignBottom*/);
  topLayout->setHorizontalSpacing(0);

  topLayout->setColumnStretch(0, 3);
  topLayout->setColumnStretch(2, 1);
  topLayout->setRowStretch(0, 1);
  topLayout->setRowStretch(1, 3);

  QWidget *topWidget = new QWidget;
  topWidget->setLayout(topLayout);
  setCentralWidget(topWidget);
}

ClintWindow::ClintWindow(QWidget *parent) :
  QMainWindow(parent) {

  QString filename;
  QStringList args = qApp->arguments();
  for (int i = 0; i < args.size(); i++) {
    if (!args[i].startsWith("--") && !args[i].endsWith("clint")) {
      if (filename.length() != 0) {
        QMessageBox::warning(this, "Multiple files to open", "Extra file to open is ignored", QMessageBox::Ok, QMessageBox::Ok);
        continue;
      }
      filename = args[i];
    }
  }

  QFont monospacefont("PT Mono");
  m_scriptEditor = new QTextEdit;
  m_codeEditor = new QTextEdit;
  m_codeEditor->setFont(monospacefont);
  m_scriptEditor->setFont(monospacefont);

  m_reparseCodeButton = new QPushButton("<");
  m_reparseScriptButton = new QPushButton("<");
  connect(m_reparseCodeButton, &QPushButton::clicked, this, &ClintWindow::reparseCode);
  connect(m_reparseScriptButton, &QPushButton::clicked, this, &ClintWindow::reparseScript);

  setWindowTitle("Clint: Chunky Loop INTerface");
  setupActions();
  setupMenus();
  if (filename.length() != 0) {
    openFileByName(filename);
  }
}

ClintWindow::~ClintWindow() {
  if (m_projection)
    delete m_projection;
}

void ClintWindow::setupActions() {
  m_actionFileOpen = new QAction(QIcon::fromTheme("document-open"), "Open...", this);
  m_actionFileClose = new QAction("Close", this);
  m_actionFileSaveSvg = new QAction("Save as SVG", this);
  m_actionFileQuit = new QAction(QIcon::fromTheme("application-exit"), "Quit", this);

  m_actionFileOpen->setShortcut(QKeySequence::Open);
  m_actionFileClose->setShortcut(QKeySequence::Close);
  m_actionFileQuit->setShortcut(QKeySequence::Quit);

  m_actionFileClose->setEnabled(false);

  m_actionEditUndo = new QAction(QIcon::fromTheme("edit-undo"), "Undo", this);
  m_actionEditRedo = new QAction(QIcon::fromTheme("edit-redo"), "Redo", this);
  m_actionEditVizProperties = new QAction("Visualization properties", this);

  m_actionEditUndo->setShortcut(QKeySequence::Undo);
  m_actionEditRedo->setShortcut(QKeySequence::Redo);

  m_actionEditUndo->setEnabled(false);
  m_actionEditRedo->setEnabled(false);

  m_actionViewFreeze = new QAction("Keep original code", this);
  m_actionViewProjectionMatrix = new QAction("Projection matrix", this);
  m_actionViewProjectionMatrix->setCheckable(true);
  m_actionViewProjectionMatrix->setChecked(true);
  m_actionViewProjectionMatrix->setEnabled(false);
  m_actionViewFreeze->setCheckable(true);

  connect(m_actionFileOpen, &QAction::triggered, this, &ClintWindow::fileOpen);
  connect(m_actionFileClose, &QAction::triggered, this, &ClintWindow::fileClose);
  connect(m_actionFileSaveSvg, &QAction::triggered, this, &ClintWindow::fileSaveSvg);
  connect(m_actionFileQuit, &QAction::triggered, qApp, &QApplication::quit);

  connect(m_actionEditUndo, &QAction::triggered, this, &ClintWindow::editUndo);
  connect(m_actionEditRedo, &QAction::triggered, this, &ClintWindow::editRedo);
  connect(m_actionEditVizProperties, &QAction::triggered, this, &ClintWindow::editVizProperties);

  connect(m_actionViewFreeze, &QAction::toggled, this, &ClintWindow::viewFreezeToggled);
  connect(m_actionViewProjectionMatrix, &QAction::toggled, this, &ClintWindow::viewProjectionMatrixToggled);
}

void ClintWindow::setupMenus() {
  m_menuBar = new QMenuBar(this);
  QMenu *fileMenu = new QMenu("File");
  fileMenu->addAction(m_actionFileOpen);
  fileMenu->addAction(m_actionFileSaveSvg);
  fileMenu->addAction(m_actionFileClose);
  fileMenu->addSeparator();
  fileMenu->addAction(m_actionFileQuit);

  QMenu *editMenu = new QMenu("Edit");
  editMenu->addAction(m_actionEditUndo);
  editMenu->addAction(m_actionEditRedo);
  editMenu->addSeparator();
  editMenu->addAction(m_actionEditVizProperties);

  QMenu *viewMenu = new QMenu("View");
  viewMenu->addAction(m_actionViewFreeze);
  viewMenu->addAction(m_actionViewProjectionMatrix);

  m_menuBar->addAction(fileMenu->menuAction());
  m_menuBar->addAction(editMenu->menuAction());
  m_menuBar->addAction(viewMenu->menuAction());
  m_menuBar->setNativeMenuBar(false);  // Override MacOS behavior since it does not display the menu

  setMenuBar(m_menuBar);
}

void ClintWindow::fileOpen() {
  QString selectedFilter;
  QString fileName = QFileDialog::getOpenFileName(this, "Open file", QString(), "OpenScop files (*.scop);;C/C++ sources (*.c *.cpp *.cxx)", &selectedFilter);
  if (fileName.isNull())
    return;

  if (m_fileOpen) {
    fileClose();
  }

  openFileByName(fileName);
}

void ClintWindow::fileClose() {
  if (!m_fileOpen)
    return;

  if (m_program) {
    ClintScop *vscop = (*m_program)[0];
    if (vscop)
      disconnect(vscop, &ClintScop::transformExecuted, this, &ClintWindow::scopTransformed);
  }

  setWindowTitle("Clint: Chunky Loop INTerface");

  resetCentralWidget(nullptr);
  m_program->setParent(nullptr);
  delete m_program;
  m_program = nullptr;

  for (VizProjection *vp : m_allProjections) {
    vp->setParent(nullptr);
    delete vp;
  }
  m_allProjections.clear();

  m_fileOpen = false;
  m_actionFileClose->setEnabled(false);
  m_actionViewProjectionMatrix->setEnabled(false);
}

void ClintWindow::fileSaveSvg() {
  if (!m_fileOpen)
    return;

  QString fileName = QFileDialog::getSaveFileName(this, "Save SVG image", QString(), "Scalable Vector Graphics (*.svg)");
  if (fileName.isNull())
    return;

  QSvgGenerator *generator = new QSvgGenerator;
  generator->setFileName(fileName);
  generator->setSize(m_allProjections[0]->projectionSize());
  QPainter *painter = new QPainter(generator);
  m_allProjections[0]->paintProjection(painter);

  delete painter;
  delete generator;
}

void ClintWindow::changeParameter(int value) {
  if (value > 0) {
    m_parameterValue = value;
    regenerateScop((*m_program)[0], value);
  }
}

void ClintWindow::resetProjectionMatrix(ClintScop *vscop) {
  if (m_projectionMatrixWidget) {
    m_projectionMatrixWidget->setParent(nullptr);
    delete m_projectionMatrixWidget;
  }

  m_projectionMatrixWidget = new QWidget(this);
  QGridLayout *projectionsLayout = new QGridLayout;
  int counter = 0;
  for (int i = 0, e = vscop->dimensionality(); i < e - 1; i++) {
    for (int j = i + 1; j < e; j++) {
      projectionsLayout->addWidget(m_allProjections[counter++]->widget(), j-1, i);
    }
  }
  projectionsLayout->setContentsMargins(0, 0, 0, 0);
  m_projectionMatrixWidget->setLayout(projectionsLayout);
}

void ClintWindow::createProjections(ClintScop *vscop) {
  for (VizProjection *vp : m_allProjections) {
    vp->setParent(nullptr);
    delete vp;
  }
  m_allProjections.clear();

  for (int i = 0, e = vscop->dimensionality(); i < e - 1; i++) {
    for (int j = i + 1; j < e; j++) {
      VizProjection *vp = new VizProjection(i, j, this);
      vp->projectScop(vscop);
      m_allProjections.push_back(vp);
    }
  }

  resetProjectionMatrix(vscop);
  resetCentralWidget(m_projectionMatrixWidget);
}

void ClintWindow::openFileByName(QString fileName) {
  char *cFileName = strdup(QFile::encodeName(fileName).constData());
  QString fileNameNoPath = QFileInfo(fileName).fileName();
  FILE *file = fopen(cFileName, "r");
  free(cFileName);
  if (!file) {
    QMessageBox::critical(this, QString(), QString("Could not open %1 for reading").arg(fileNameNoPath), QMessageBox::Ok, QMessageBox::Ok);
    return;
  }

  osl_scop_p scop = nullptr;
  char *originalCode = nullptr;
  if (fileName.endsWith(".scop")) {
    scop = osl_scop_read(file);
  } else if (fileName.endsWith(".c") ||
             fileName.endsWith(".cpp") ||
             fileName.endsWith(".cxx")) {
    scop = oslFromCCode(file);
    originalCode = fileContents(file);
  } else {
    CLINT_UNREACHABLE;
  }
  fclose(file);
  if (!scop) {
    QMessageBox::warning(this, QString(), "No SCoP in the given file", QMessageBox::Ok, QMessageBox::Ok);
    return;
  }

  setWindowTitle(QString("%1 - Clint").arg(fileNameNoPath));

  m_program = new ClintProgram(scop, originalCode, this);
  ClintScop *vscop = (*m_program)[0];
  connect(vscop, &ClintScop::transformExecuted, this, &ClintWindow::scopTransformed);

  createProjections(vscop);

  m_codeEditor->setHtml(vscop->originalHtml());
  m_scriptEditor->setHtml(QString());

  m_fileOpen = true;
  m_actionFileClose->setEnabled(true);
  m_actionViewProjectionMatrix->setChecked(true);
  m_actionViewProjectionMatrix->setEnabled(true);

  if (originalCode)
    free(originalCode);
}

ClintScop *ClintWindow::regenerateScopOsl(ClintScop *vscop, osl_scop_p scop, int parameterValue = -1, bool swapMapper = true) {
  if (parameterValue == -1)
    parameterValue = m_parameterValue;

  ClintScop *newscop = new ClintScop(scop, parameterValue, nullptr, m_program);
  createProjections(newscop);
  for (TransformationGroup g : vscop->transformationSequence().groups) {
    newscop->transform(g);
  }
  if (swapMapper) {
    newscop->resetRedoSequence(vscop->redoSequence());
    newscop->swapBetaMapper(vscop);
    newscop->setScopSilent(vscop->scopPart());
  } else {
    newscop->updateCode();
  }
  (*m_program)[0] = newscop;
  disconnect(vscop, &ClintScop::transformExecuted, this, &ClintWindow::scopTransformed);
  connect(newscop, &ClintScop::transformExecuted, this, &ClintWindow::scopTransformed);

  updateCodeEditor();
  if (swapMapper)
    m_scriptEditor->setText(newscop->currentScript());

  return newscop;
}

ClintScop *ClintWindow::regenerateScop(ClintScop *vscop, int parameterValue = -1) {
  if (parameterValue == -1)
    parameterValue = m_parameterValue;

  osl_scop_p scop = vscop->appliedScop();
  return regenerateScopOsl(vscop, scop, parameterValue);
}

void ClintWindow::editUndo() {

  if (!m_program)
    return;
  ClintScop *vscop = (*m_program)[0];
  if (!vscop)
    return;
  CLINT_ASSERT(vscop->hasUndo(), "No undo possible, but the button is enabled");
  vscop->undoTransformation();

  ClintScop *newscop = regenerateScop(vscop);
  delete vscop;

  m_actionEditUndo->setEnabled(newscop->hasUndo());
  m_actionEditRedo->setEnabled(true);
}

void ClintWindow::editRedo() {

  if (!m_program)
    return;
  ClintScop *vscop = (*m_program)[0];
  if (!vscop)
    return;
  CLINT_ASSERT(vscop->hasRedo(), "No redo possible, but the button is enabled");
  vscop->redoTransformation();

  ClintScop *newscop = regenerateScop(vscop);
  delete vscop;

  m_actionEditRedo->setEnabled(newscop->hasRedo());
  m_actionEditUndo->setEnabled(true);
}

void ClintWindow::editVizProperties() {
  if (m_allProjections.size() == 0)
    return;
  PropertiesDialog *dialog = new PropertiesDialog(m_allProjections[0]->vizProperties());
//  connect(dialog, &QDialog::rejected, dialog, &QDialog::deleteLater);
  connect(dialog, &PropertiesDialog::parameterChange, this, &ClintWindow::changeParameter);
  dialog->show();
}

void ClintWindow::viewFreezeToggled(bool value) {
  m_showOriginalCode = value;
  scopTransformed();
}

void ClintWindow::viewProjectionMatrixToggled(bool value) {
  if (!m_actionViewProjectionMatrix->isEnabled())
    return;
  if (value) {
    resetCentralWidget(m_projectionMatrixWidget);
  } else {
    m_projection = new VizProjection(0, 1, this);
    m_projection->projectScop((* m_program)[0]);
    resetCentralWidget(m_projection->widget(), false);
  }
}

void ClintWindow::updateCodeEditor() {
  if (!m_program)
    return;
  ClintScop *vscop = (*m_program)[0];
  if (!vscop)
    return;

  if (!m_showOriginalCode) {
//    codeEditor->setText(QString(vscop->generatedCode()));
    m_codeEditor->setHtml(QString(vscop->generatedHtml()));
  } else {
//    codeEditor->setText(QString(vscop->originalCode()));
    m_codeEditor->setHtml(QString(vscop->originalHtml()));
  }
}

void ClintWindow::reparseCode() {
  if (!m_program)
    return;
  ClintScop *vscop = (*m_program)[0];
  if (!vscop)
    return;

  QString plainText = m_codeEditor->toPlainText();
  char *code = strdup(plainText.toStdString().c_str());
  osl_scop_p scop = parseCode(code);
  if (scop == nullptr) {
    QMessageBox::critical(this, "Could not parse code", "Could not extract polyhedral representation from the code",
                          QMessageBox::Ok, QMessageBox::Ok);
  } else {
    QString plainText = m_scriptEditor->toPlainText();
    char *script = strdup(plainText.toStdString().c_str());

    int result = parseClay(scop, script);
    if (result != 0) {
      QMessageBox::critical(this, "Could not apply transformation", "The transformation sequence is not applicable to the modified code",
                            QMessageBox::Ok, QMessageBox::Ok);
    } else {
      regenerateScopOsl(vscop, scop, -1, false);
    }
    free(script);
  }
  free(code);
}

void ClintWindow::reparseScript() {
  if (!m_program)
    return;
  ClintScop *vscop = (*m_program)[0];
  if (!vscop)
    return;

  QString plainText = m_scriptEditor->toPlainText();
  char *script = strdup(plainText.toStdString().c_str());
  osl_scop_p scop = osl_scop_clone(vscop->scopPart());

  int result = parseClay(scop, script);
  if (result != 0) {
    QMessageBox::critical(this, "Could not apply transformation", "Could not apply specified transformation sequence to the code",
                          QMessageBox::Ok, QMessageBox::Ok);
  } else {
    regenerateScopOsl(vscop, scop, -1, false);
  }

  free(script);
}

void ClintWindow::scopTransformed() {
  if (!m_program)
    return;
  ClintScop *vscop = (*m_program)[0];
  if (!vscop)
    return;

  updateCodeEditor();
  m_scriptEditor->setText(QString(vscop->currentScript()));

  if (vscop->hasRedo())
    vscop->clearRedo();

  m_actionEditUndo->setEnabled(vscop->hasUndo());
  m_actionEditRedo->setEnabled(false);

  for (VizProjection *vp : m_allProjections) {
    vp->updateProjection();
  }
}

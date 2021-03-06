#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <QtSvg>

#include "vizprojection.h"
#include "clintwindow.h"
#include "oslutils.h"
#include "propertiesdialog.h"
#include "clayparser.h"

#include <exception>

void ClintWindow::resetCentralWidget(QWidget *interface) {
  if (centralWidget() != nullptr) {
    QWidget *oldWidget = centralWidget();
    m_graphicalInterface->setParent(nullptr);
    m_scriptEditor->setParent(nullptr);
    m_codeEditor->setParent(nullptr);
    m_reparseScriptButton->setParent(nullptr);
    m_reparseCodeButton->setParent(nullptr);
    delete oldWidget;
  }
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
  resetCentralWidget(nullptr);
  if (m_projection) {
    if (m_projection->widget() && m_projection->widget()->parent() == nullptr)
      delete m_projection->widget();
    if (m_projection->parent() == nullptr)
      delete m_projection;
  }
  if (m_projectionOverview && m_projectionOverview->parent() == nullptr)
    delete m_projectionOverview;
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
  m_fileBasename = QString();

  resetCentralWidget(nullptr);
  deleteProjectionOverview();
  deleteProjection();
  m_program->setParent(nullptr);
  m_program->deleteLater();
  m_program = nullptr;

  setWindowTitle("Clint - Chunky Loop INTeraction");


  m_fileOpen = false;
  m_actionFileClose->setEnabled(false);
  m_actionViewProjectionMatrix->setEnabled(false);
}

void ClintWindow::fileSaveSvg() {
  if (!m_fileOpen || m_graphicalInterface == nullptr)
    return;

  QString fileName = QFileDialog::getSaveFileName(this, "Save SVG image", QString(), "Scalable Vector Graphics (*.svg)");
  if (fileName.isNull())
    return;

  QSvgGenerator *generator = new QSvgGenerator;
  generator->setFileName(fileName);

  if (m_projection && m_graphicalInterface == m_projection->widget()) {
    generator->setSize(m_projection->projectionSize());
    QPainter *painter = new QPainter(generator);
    m_projection->paintProjection(painter);
    delete painter;
  } else if (m_graphicalInterface == m_projectionOverview) {
    m_projectionOverview->fillSvg(generator);
  }

  delete generator;
}

void ClintWindow::changeParameter(int value) {
  if (value > 0) {
    m_parameterValue = value;
    regenerateScop();
  }
}

void ClintWindow::deleteProjectionOverview() {
  if (m_projectionOverview != nullptr) {
    CLINT_ASSERT(m_graphicalInterface != m_projectionOverview,
                 "Deleting active projection overview");
    m_projectionOverview->setParent(nullptr);
    disconnect(m_projectionOverview, &ClintProjectionOverview::projectionSelected,
               this, &ClintWindow::projectionSelectedInOverview);
    delete m_projectionOverview;
  }
  m_projectionOverview = nullptr;
}

void ClintWindow::createProjections(ClintScop *vscop) {
  resetCentralWidget(nullptr);
  deleteProjectionOverview();
  deleteProjection();
  m_projectionOverview = new ClintProjectionOverview(vscop, this);
  connect(m_projectionOverview, &ClintProjectionOverview::projectionSelected, this, &ClintWindow::projectionSelectedInOverview);
  projectionSelectedAlone(VizProperties::NO_DIMENSION, VizProperties::NO_DIMENSION);
}

void ClintWindow::openFileByName(QString fileName) {
  char *cFileName = strdup(QFile::encodeName(fileName).constData());
  QString fileNameNoPath = QFileInfo(fileName).fileName();
  m_fileBasename = fileNameNoPath;
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

ClintScop *ClintWindow::regenerateScopWithSequence(osl_scop_p originalScop, const TransformationSequence &sequence) {
  ClintScop *oldscop = (*m_program)[0];

  ClintScop *newscop = new ClintScop(originalScop, m_parameterValue, nullptr, m_program); // FIXME: provide original code when coloring is done for it...
  (*m_program)[0] = newscop;

  createProjections(newscop);

  // Copy transformations.
  for (TransformationGroup g : sequence.groups) {
    newscop->transform(g);
  }
  try {
    newscop->executeTransformationSequence();
    connect(newscop, &ClintScop::transformExecuted, this, &ClintWindow::scopTransformed);
    disconnect(oldscop, &ClintScop::transformExecuted, this, &ClintWindow::scopTransformed);
    return newscop;
  } catch (std::logic_error e) {
    QMessageBox::critical(this, "Could not apply sequence", e.what(), QMessageBox::Ok, QMessageBox::Ok);
    return NULL;
  }
}

void ClintWindow::regenerateScop(osl_scop_p originalScop) {
  if (!m_program)
    return;
  ClintScop *oldscop = (*m_program)[0];

  if (originalScop == nullptr) {
    CLINT_ASSERT(oldscop != nullptr, "regenerating scop with no original provided or existing");
    originalScop = oldscop->scopPart();
  }
  ClintScop *newscop = regenerateScopWithSequence(originalScop, oldscop->transformationSequence());
  if (newscop == NULL)
    return;

  // Copy redo list after sequence execution since it cleans it.
  newscop->resetRedoSequence(oldscop->redoSequence());
  m_actionEditRedo->setEnabled(newscop->hasRedo());
  m_actionEditUndo->setEnabled(newscop->hasUndo());

  oldscop->setParent(nullptr);
  oldscop->deleteLater();
//  delete oldscop;
}

void ClintWindow::regenerateScop(const TransformationSequence &sequence) {
  if (!m_program)
    return;
  ClintScop *oldscop = (*m_program)[0];
  CLINT_ASSERT(oldscop != nullptr, "regenerationg scop with no original provided");

  regenerateScopWithSequence(oldscop->scopPart(), sequence);
  m_actionEditRedo->setEnabled(false);
  m_actionEditUndo->setEnabled(false);

  oldscop->setParent(nullptr);
  oldscop->deleteLater();
//  delete oldscop;
}

void ClintWindow::editUndo() {

  if (!m_program)
    return;
  ClintScop *vscop = (*m_program)[0];
  if (!vscop)
    return;
  CLINT_ASSERT(vscop->hasUndo(), "No undo possible, but the button is enabled");
  vscop->undoTransformation();
  regenerateScop();
}

void ClintWindow::editRedo() {

  if (!m_program)
    return;
  ClintScop *vscop = (*m_program)[0];
  if (!vscop)
    return;
  CLINT_ASSERT(vscop->hasRedo(), "No redo possible, but the button is enabled");
  vscop->redoTransformation();
  regenerateScop();
}

void ClintWindow::editVizProperties() {
  if (m_projection == nullptr && m_projectionOverview == nullptr)
    return;
  VizProperties *props = nullptr;
  if (m_projection)
    props = m_projection->vizProperties();
  else
    props = m_projectionOverview->vizProperties();
  if (props == nullptr)
    return;

  PropertiesDialog *dialog = new PropertiesDialog(props);
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
    projectionSelectedAlone(VizProperties::NO_DIMENSION, VizProperties::NO_DIMENSION);
  } else {
    projectionSelectedInOverview(0, 1); // FIXME: hardcoded values here...
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
    regenerateScop(scop);
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

  ClayParser parser;
  try {
    TransformationSequence newSequence = parser.parse(plainText.toStdString());
    regenerateScop(newSequence);
    updateCodeEditor();
  } catch (std::logic_error e) {
    QMessageBox::critical(this, "Could not parse transformation", e.what(),
                          QMessageBox::Ok, QMessageBox::Ok);
  } catch (...) {
    QMessageBox::critical(this, "Could not parse transformation", "Unidentified error",
                          QMessageBox::Ok, QMessageBox::Ok);
  }
}

void ClintWindow::scopTransformed() {
  if (!m_program || !m_graphicalInterface)
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

  if (m_projection && m_graphicalInterface == m_projection->widget()) {
    m_projection->updateProjection();
  }
  if (m_graphicalInterface == m_projectionOverview) {
    m_projectionOverview->updateAllProjections();
  }
}

void ClintWindow::deleteProjection() {
  if (m_projection) {
    CLINT_ASSERT(m_graphicalInterface != m_projection->widget(),
                 "Deleting active projection");
    disconnect(m_projection, &VizProjection::selected, this, &ClintWindow::projectionSelectedAlone);
    m_projection->setParent(nullptr);
    // FIXME: this should be deleted, but segfaults when doubleclicking on the projection (not from the menu or undo).
//    m_projection->widget()->setParent(nullptr);
//    m_projection->widget()->deleteLater();
    m_projection->deleteLater();
  }
  m_projection = nullptr;
}

void ClintWindow::projectionSelectedInOverview(int horizontal, int vertical) {
  deleteProjection();
  m_projection = new VizProjection(horizontal, vertical, this);
  m_projection->projectScop((* m_program)[0]);
  connect(m_projection, &VizProjection::selected, this, &ClintWindow::projectionSelectedAlone);
  resetCentralWidget(m_projection->widget());

  m_horizontalDimSelected = horizontal;
  m_verticalDimSelected = vertical;

  // Prevent setChecked from triggering another action.
  m_actionViewProjectionMatrix->setEnabled(false);
  m_actionViewProjectionMatrix->setChecked(false);
  m_actionViewProjectionMatrix->setEnabled(true);

  setWindowTitle(QString("%1 [projection %2x%3] - Clint").arg(m_fileBasename).arg(horizontal).arg(vertical));
}

void ClintWindow::projectionSelectedAlone(int horizontal, int vertical) {
  (void) horizontal;
  (void) vertical;

  m_horizontalDimSelected = VizProperties::NO_DIMENSION;
  m_verticalDimSelected = VizProperties::NO_DIMENSION;

  resetCentralWidget(m_projectionOverview);
  deleteProjection();
  m_projectionOverview->updateRowColumn(horizontal, vertical);
  m_projectionOverview->update();
  // Prevent setChecked from triggering another action.
  m_actionViewProjectionMatrix->setEnabled(false);
  m_actionViewProjectionMatrix->setChecked(true);
  m_actionViewProjectionMatrix->setEnabled(true);

  setWindowTitle(QString("%1 [overview] - Clint").arg(m_fileBasename));
}

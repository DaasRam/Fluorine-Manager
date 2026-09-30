#include "diagnosticreport.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBrowser>

#include <gtest/gtest.h>

TEST(DiagnosticReport, IncludesSuppliedSafeBuildLaunchAndPreparationDetails)
{
  DiagnosticReportContext context;
  context.version = QStringLiteral("0.4.2");
  context.channel = QStringLiteral("nightly");
  context.commit = QStringLiteral("abcdef012345");
  context.effective_backend = DiagnosticReportBackend::Usvfs;
  context.launch_mode = DiagnosticReportLaunchMode::Proton;
  context.proton_launcher = DiagnosticReportAvailability::Available;
  context.wine_prefix = DiagnosticReportAvailability::Available;
  context.steam_linux_runtime = DiagnosticReportAvailability::Missing;
  context.backend_prerequisite = DiagnosticReportAvailability::Available;

  DiagnosticPreparationSummary preparation;
  preparation.phase = DiagnosticPreparationPhase::Hashing;
  preparation.files_scanned = 1200;
  preparation.files_hashed = 4;
  preparation.bytes_hashed = 8192;
  preparation.index_reused = false;
  preparation.index_reuse_reason = VfsIndexReuseReason::ArchiveProofChanged;
  context.last_preparation = preparation;

  DiagnosticReportDialog dialog(context);
  const QString report = dialog.reportText();
  EXPECT_NE(report.indexOf(QStringLiteral("Version: 0.4.2")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("Channel: nightly")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("Commit: abcdef012345")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("Launch mode: Proton")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("Effective backend: USVFS")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("Steam Linux Runtime: Missing")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("Files checked: 1200")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("Files hashed: 4")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("Bytes hashed: 8192")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("archive-proof-changed")), -1);
}

TEST(DiagnosticReport, MarksVersionAndPreparationUnavailableWithoutInventingData)
{
  DiagnosticReportDialog dialog(DiagnosticReportContext{});
  const QString report = dialog.reportText();
  EXPECT_NE(report.indexOf(QStringLiteral("Version: unavailable")), -1);
  EXPECT_NE(report.indexOf(
                QStringLiteral("No recent preparation summary is available.")),
            -1);
  EXPECT_EQ(report.indexOf(QStringLiteral("Channel:")), -1);
  EXPECT_EQ(report.indexOf(QStringLiteral("Commit:")), -1);
  EXPECT_EQ(report.indexOf(QStringLiteral("Launch mode:")), -1);
  EXPECT_EQ(report.indexOf(QStringLiteral("Effective backend:")), -1);
  EXPECT_EQ(report.indexOf(QStringLiteral("Proton launcher:")), -1);
  EXPECT_EQ(report.indexOf(QStringLiteral("Files checked:")), -1);
}

TEST(DiagnosticReport, PreservesEditsAndSignalsExplicitActions)
{
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  DiagnosticReportDialog dialog(DiagnosticReportContext{});

  auto* problem = dialog.findChild<QPlainTextEdit*>(
      QStringLiteral("reportProblemField"));
  auto* reproduction = dialog.findChild<QPlainTextEdit*>(
      QStringLiteral("reportReproductionField"));
  auto* expected = dialog.findChild<QPlainTextEdit*>(
      QStringLiteral("reportExpectedField"));
  auto* category = dialog.findChild<QComboBox*>(
      QStringLiteral("reportCategoryCombo"));
  auto* preview = dialog.findChild<QTextBrowser*>(
      QStringLiteral("reportPreview"));
  ASSERT_NE(problem, nullptr);
  ASSERT_NE(reproduction, nullptr);
  ASSERT_NE(expected, nullptr);
  ASSERT_NE(category, nullptr);
  ASSERT_NE(preview, nullptr);

  problem->setPlainText(QStringLiteral("The warm scan is slow."));
  reproduction->setPlainText(QStringLiteral("1. Launch the selected game."));
  expected->setPlainText(QStringLiteral("No files need rehashing."));
  category->setCurrentIndex(1);

  const QString report = dialog.reportText();
  EXPECT_NE(report.indexOf(QStringLiteral("Category: Slow preparation or cache reuse")),
            -1);
  EXPECT_NE(report.indexOf(QStringLiteral("The warm scan is slow.")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("1. Launch the selected game.")), -1);
  EXPECT_NE(report.indexOf(QStringLiteral("No files need rehashing.")), -1);
  EXPECT_NE(preview->toPlainText().indexOf(QStringLiteral("The warm scan is slow.")),
            -1);

  QSignalSpy openIssueSpy(&dialog,
                          &DiagnosticReportDialog::openIssueFormsRequested);
  auto* openIssue = dialog.findChild<QPushButton*>(
      QStringLiteral("openIssueFormsButton"));
  ASSERT_NE(openIssue, nullptr);
  openIssue->click();
  EXPECT_EQ(openIssueSpy.count(), 1);
  EXPECT_NE(dialog.result(), QDialog::Accepted);

  auto* copy = dialog.findChild<QPushButton*>(
      QStringLiteral("copyReportButton"));
  ASSERT_NE(copy, nullptr);
  copy->click();
  ASSERT_NE(QApplication::clipboard(), nullptr);
  EXPECT_EQ(QApplication::clipboard()->text(), report);
}

TEST(DiagnosticReport, SavesCurrentEditableReport)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  DiagnosticReportDialog dialog(DiagnosticReportContext{});
  auto* problem = dialog.findChild<QPlainTextEdit*>(
      QStringLiteral("reportProblemField"));
  ASSERT_NE(problem, nullptr);
  problem->setPlainText(QStringLiteral("Saved draft content."));

  const QString path = temporary.filePath(QStringLiteral("report.md"));
  QString error;
  ASSERT_TRUE(dialog.saveReportToFile(path, &error)) << error.toStdString();

  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(QString::fromUtf8(file.readAll()), dialog.reportText());
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);
  if (app.arguments().size() == 3 && app.arguments().at(1) == "--render-review") {
    const QDir directory(app.arguments().at(2));
    if (!QDir().mkpath(directory.absolutePath())) return 1;
    DiagnosticReportContext context;
    context.version = "0.3.4-dev";
    context.launch_mode = DiagnosticReportLaunchMode::Proton;
    context.effective_backend = DiagnosticReportBackend::Fuse;
    DiagnosticPreparationSummary preparation;
    preparation.phase = DiagnosticPreparationPhase::Complete;
    preparation.files_scanned = 42000;
    preparation.index_reused = true;
    context.last_preparation = preparation;
    DiagnosticReportDialog dialog(context);
    dialog.findChild<QPlainTextEdit*>("reportProblemField")->setPlainText(
        "Preparing this modlist took longer after updating the app.");
    dialog.findChild<QPlainTextEdit*>("reportReproductionField")->setPlainText(
        "1. Open the existing instance.\n2. Run the same program twice.");
    dialog.findChild<QPlainTextEdit*>("reportExpectedField")->setPlainText(
        "Reuse unchanged files and explain which operation is taking time.");
    dialog.show();
    app.processEvents();
    if (!dialog.grab().save(directory.filePath("issue-report.png"))) return 1;
    QFont font = dialog.font();
    font.setPointSize(16);
    dialog.setFont(font);
    dialog.resize(850, 850);
    app.processEvents();
    return dialog.grab().save(directory.filePath("issue-report-large-text.png")) ? 0 : 1;
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

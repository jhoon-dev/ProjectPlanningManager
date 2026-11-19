#include "ui/NetworkPrintDialog.h"
#include "ui/SandboxAccess.h"
#include "reporting/NetworkPrint.h"
#include "formatting/QtDiagnosticText.h"
#include <QApplication>
#include <cmath>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QPainter>
#include <QPrinter>
#include <QPrintDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>
namespace ppm {
namespace {
class Canvas final : public QWidget {
    std::shared_ptr<const NetworkPrintLayout> layout_;
    int page_;

public:
    Canvas(std::shared_ptr<const NetworkPrintLayout> l, int page) : layout_(std::move(l)), page_(page) {
        const auto size = layout_->page.fullRect(QPageLayout::Point).size();
        setFixedSize(size.toSize());
        setObjectName("networkPrintCanvas");
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        paintNetworkPage(p, *layout_, page_);
    }
};
class Dialog final : public QDialog {
    Document &doc_;
    std::shared_ptr<const Project> source_;
    std::uint64_t session_;
    QPicture picture_;
    QRectF bounds_;
    QString provenance_;
    QStringList details_;
    QComboBox *paper_, *orientation_;
    QCheckBox *fit_;
    QSpinBox *scale_, *margin_, *page_;
    QLabel *message_;
    QScrollArea *preview_;
    QPushButton *pdf_, *print_, *save_;
    std::shared_ptr<const NetworkPrintLayout> layout_;
    std::shared_ptr<CalculationControl> control_;
    bool busy_ = false;
    bool current() const {
        return doc_.project() && doc_.sessionGeneration() == session_ &&
               doc_.project()->documentId == source_->documentId && doc_.project()->revision == source_->revision;
    }
    PrintPreferences prefs() const {
        auto s = source_->printPreferences.value_or(PrintPreferences{});
        s.paper = paper_->currentIndex();
        s.orientation = orientation_->currentIndex();
        s.margin = margin_->value();
        s.networkFit = fit_->isChecked();
        s.networkScale = scale_->value();
        return s;
    }
    void showPage() {
        if (layout_) {
            delete preview_->takeWidget();
            preview_->setWidget(new Canvas(layout_, page_->value() - 1));
        }
    }
    void prepare() {
        try {
            layout_ = std::make_shared<const NetworkPrintLayout>(
                buildNetworkPrint(picture_, bounds_, prefs(), QString::fromStdString(source_->name) + " · 네트워크",
                                  provenance_ + " · 출력 설정 rev " + QString::number(source_->revision), details_));
            page_->setRange(1, layout_->pageCount());
            showPage();
            message_->setText(QString("%1쪽 · 현재 표시망의 벡터 출력 · 전체 프로젝트를 재계산한 망의 표시 부분입니다.")
                                  .arg(layout_->pageCount()));
            pdf_->setEnabled(!busy_);
            print_->setEnabled(!busy_);
        } catch (const std::exception &e) {
            layout_.reset();
            pdf_->setEnabled(false);
            print_->setEnabled(false);
            message_->setText(qErrorText(e));
        }
    }
    void output(bool pdf) {
        try {
            if (busy_ || !layout_ || !current())
                throw std::runtime_error("문서가 변경되었습니다. 네트워크를 다시 준비하세요.");
            const auto layout = layout_;
            QString path;
            std::shared_ptr<QPrinter> printer;
            if (pdf) {
                path = QFileDialog::getSaveFileName(this, "네트워크 PDF · 새 파일명", "PPM_network.pdf", "PDF (*.pdf)",
                                                    nullptr, QFileDialog::DontConfirmOverwrite);
                if (path.isEmpty())
                    return;
                if (!path.endsWith(".pdf", Qt::CaseInsensitive))
                    path += ".pdf";
                if (!authorizeSandboxFile(this, path))
                    return;
            } else {
                printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
                printer->setPageLayout(layout->page);
                printer->setFullPage(true);
                QPrintDialog dialog(printer.get(), this);
                dialog.setMinMax(1, layout->pageCount());
                dialog.setOption(QAbstractPrintDialog::PrintPageRange);
                if (dialog.exec() != QDialog::Accepted)
                    return;
                const auto chosen = printer->pageLayout();
                const auto a = chosen.fullRect(QPageLayout::Point).size(),
                           b = layout->page.fullRect(QPageLayout::Point).size();
                if (std::abs(a.width() - b.width()) > 1 || std::abs(a.height() - b.height()) > 1)
                    throw std::runtime_error("프린터 용지/방향이 미리보기와 다릅니다. 설정을 맞추세요.");
                const auto minimum = chosen.minimumMargins(), wanted = layout->page.margins(chosen.units());
                if (wanted.left() < minimum.left() || wanted.top() < minimum.top() ||
                    wanted.right() < minimum.right() || wanted.bottom() < minimum.bottom())
                    throw std::runtime_error("프린터 최소 여백보다 작은 설정입니다.");
                if (printer->pageOrder() == QPrinter::LastPageFirst)
                    throw std::runtime_error("네트워크 인쇄는 정방향 페이지 순서를 지원합니다.");
                if (printer->copyCount() != 1)
                    throw std::runtime_error("네트워크 인쇄는 한 번에 1부를 지원합니다. 복사 수를 1로 설정하세요.");
            }
            if (!current())
                throw std::runtime_error("문서가 변경되어 출력을 차단했습니다.");
            control_ = std::make_shared<CalculationControl>();
            busy_ = true;
            pdf_->setEnabled(false);
            print_->setEnabled(false);
            save_->setEnabled(false);
            message_->setText("네트워크 출력 중…");
            auto *watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
                const auto result = watcher->result();
                watcher->deleteLater();
                busy_ = false;
                save_->setEnabled(!doc_.readOnly() && current());
                prepare();
                message_->setText(result);
            });
            watcher->setFuture(QtConcurrent::run([layout, path, printer, control = control_] {
                try {
                    if (printer) {
                        const auto first = std::max(1, printer->fromPage()), last = printer->toPage();
                        paintNetwork(*printer, *layout, first, last, control);
                    } else
                        exportNetworkPdf(*layout, path, control);
                    return QString("네트워크 출력을 완료했습니다.");
                } catch (const CalculationCancelled &) {
                    return QString("출력을 취소했습니다.");
                } catch (const std::exception &e) {
                    return qErrorText(e);
                }
            }));
        } catch (const std::exception &e) {
            message_->setText(qErrorText(e));
        }
    }

public:
    Dialog(Document &doc, QPicture pic, QRectF bounds, QString provenance, QWidget *parent, const QStringList &details)
        : QDialog(parent), doc_(doc), source_(doc.snapshot()), session_(doc.sessionGeneration()),
          picture_(std::move(pic)), bounds_(bounds), provenance_(std::move(provenance)), details_(details) {
        setObjectName("networkPrintDialog");
        setWindowTitle("네트워크 페이지 설정·미리보기·인쇄");
        resize(1050, 800);
        auto *body = new QVBoxLayout(this);
        auto *controls = new QHBoxLayout;
        body->addLayout(controls);
        const auto s = source_->printPreferences.value_or(PrintPreferences{});
        paper_ = new QComboBox(this);
        paper_->setObjectName("networkPrintPaper");
        paper_->addItems({"A4", "A3"});
        paper_->setCurrentIndex(s.paper);
        controls->addWidget(paper_);
        orientation_ = new QComboBox(this);
        orientation_->addItems({"가로", "세로"});
        orientation_->setCurrentIndex(s.orientation);
        controls->addWidget(orientation_);
        margin_ = new QSpinBox(this);
        margin_->setRange(5, 30);
        margin_->setSuffix(" mm 여백");
        margin_->setValue(s.margin);
        controls->addWidget(margin_);
        fit_ = new QCheckBox("표시망 한 장 맞춤", this);
        fit_->setObjectName("networkPrintFit");
        fit_->setChecked(s.networkFit);
        controls->addWidget(fit_);
        scale_ = new QSpinBox(this);
        scale_->setObjectName("networkPrintScale");
        scale_->setRange(25, 200);
        scale_->setSuffix("% (타일)");
        scale_->setValue(s.networkScale);
        controls->addWidget(scale_);
        page_ = new QSpinBox(this);
        page_->setObjectName("networkPrintPage");
        controls->addWidget(page_);
        message_ = new QLabel(this);
        message_->setWordWrap(true);
        message_->setTextFormat(Qt::PlainText);
        body->addWidget(message_);
        preview_ = new QScrollArea(this);
        body->addWidget(preview_, 1);
        auto *actions = new QHBoxLayout;
        body->addLayout(actions);
        save_ = new QPushButton("설정 문서에 저장", this);
        save_->setObjectName("saveNetworkPrintPreferences");
        save_->setEnabled(!doc_.readOnly());
        actions->addWidget(save_);
        pdf_ = new QPushButton("PDF 저장…", this);
        pdf_->setObjectName("exportNetworkPdf");
        actions->addWidget(pdf_);
        print_ = new QPushButton("인쇄…", this);
        actions->addWidget(print_);
        auto *cancel = new QPushButton("출력 취소", this);
        actions->addWidget(cancel);
        auto *close = new QPushButton("닫기", this);
        actions->addWidget(close);
        connect(close, &QPushButton::clicked, this, &Dialog::reject);
        connect(cancel, &QPushButton::clicked, this, [this] {
            if (control_)
                control_->cancel();
        });
        connect(pdf_, &QPushButton::clicked, this, [this] { output(true); });
        connect(print_, &QPushButton::clicked, this, [this] { output(false); });
        connect(save_, &QPushButton::clicked, this, [this] {
            try {
                if (!current() || busy_ || doc_.readOnly())
                    throw std::runtime_error("이전 문서의 설정은 저장할 수 없습니다.");
                doc_.setPrintPreferences(prefs());
                source_ = doc_.snapshot();
                prepare();
            } catch (const std::exception &e) {
                message_->setText(qErrorText(e));
            }
        });
        for (auto *c : {paper_, orientation_})
            connect(c, &QComboBox::currentIndexChanged, this, [this] { prepare(); });
        for (auto *s : {scale_, margin_})
            connect(s, &QSpinBox::valueChanged, this, [this] { prepare(); });
        connect(fit_, &QCheckBox::toggled, this, [this] { prepare(); });
        connect(page_, &QSpinBox::valueChanged, this, [this] { showPage(); });
        prepare();
    }
    ~Dialog() override {
        if (control_)
            control_->cancel();
    }
    void reject() override {
        if (control_)
            control_->cancel();
        QDialog::reject();
    }
};
} // namespace
void showNetworkPrint(Document &doc, QPicture picture, const QRectF &bounds, const QString &provenance, QWidget *parent,
                      const QStringList &details) {
    Dialog dialog(doc, std::move(picture), bounds, provenance, parent, details);
    dialog.exec();
}
} // namespace ppm
